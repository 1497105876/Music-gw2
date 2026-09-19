// AiClient.cpp：AI 请求层（WinHTTP + OpenAI 兼容格式）
//
// 只有一份请求代码：POST {base_url}/chat/completions。服务商之间的差别
// 全在 base_url / api_key / model 三个字段上，所以这里不做任何分支。
//
// 注意：本文件里所有函数都是同步阻塞的，只允许在工作线程里调用。

#include "stdafx.h"
#include "AiClient.h"
#include "Common.h"
#include <winhttp.h>
#include <memory>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

namespace
{
    using json = nlohmann::json;

    std::string ToUtf8(const std::wstring& s)
    {
        return CCommon::UnicodeToStr(s, CodeType::UTF8);
    }

    std::wstring FromUtf8(const std::string& s)
    {
        return CCommon::StrToUnicode(s, CodeType::UTF8);
    }

    struct UrlParts
    {
        bool ok{ false };
        bool https{ true };
        std::wstring host;
        int port{ 443 };
        std::wstring path;
    };

    UrlParts CrackUrl(const std::wstring& url)
    {
        UrlParts out;
        if (url.empty())
            return out;
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = (DWORD)-1;
        uc.dwHostNameLength = (DWORD)-1;
        uc.dwUrlPathLength = (DWORD)-1;
        uc.dwExtraInfoLength = (DWORD)-1;
        if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc))
            return out;
        out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
        out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
        out.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        out.port = out.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        // 去掉结尾的斜杠，后面拼 /chat/completions 才不会变成双斜杠
        while (!out.path.empty() && out.path.back() == L'/')
            out.path.pop_back();
        out.ok = !out.host.empty();
        return out;
    }

    // 请求头里不能有回车换行，key 是从 ini 读来的，防一手
    std::wstring SafeHeaderValue(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
        {
            if (c == L'\r' || c == L'\n' || c == L'\0')
                continue;
            out += c;
        }
        return out;
    }

    // 一次 HTTP 往返的原始结果
    struct RawResponse
    {
        bool ok{ false };
        AiErrorKind kind{ AiErrorKind::Network };
        int status{ 0 };
        int elapsed_ms{ 0 };
        std::string body;
        std::wstring detail;        // 出错时补一句更具体的话
    };

    // 发一次请求。stream 打开的话每读到一段就回调，让它能边收边显示。
    RawResponse DoRequest(const AiCallParams& params,
                          const std::wstring& verb,
                          const std::wstring& full_url,
                          const std::string& body,
                          bool stream,
                          std::function<void(const std::wstring&)> on_delta,
                          volatile bool* cancel)
    {
        RawResponse out;
        UrlParts url = CrackUrl(full_url);
        if (!url.ok)
        {
            out.kind = AiErrorKind::Network;
            out.detail = L"API 地址填得不对";
            return out;
        }

        DWORD access = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;   // 跟随系统
        std::wstring manual_proxy;
        const wchar_t* proxy_name = WINHTTP_NO_PROXY_NAME;
        if (params.proxy_mode == AiProxyMode::None)
        {
            access = WINHTTP_ACCESS_TYPE_NO_PROXY;
        }
        else if (params.proxy_mode == AiProxyMode::Manual && !params.proxy_url.empty())
        {
            access = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            manual_proxy = params.proxy_url;
            proxy_name = manual_proxy.c_str();
        }

        HINTERNET h_session = WinHttpOpen(L"MusicPlayer2", access, proxy_name, WINHTTP_NO_PROXY_BYPASS, 0);
        if (h_session == NULL)
        {
            out.detail = L"无法初始化网络组件";
            return out;
        }

        DWORD timeout_ms = static_cast<DWORD>(params.timeout_sec) * 1000;
        if (timeout_ms < 3000) timeout_ms = 3000;
        WinHttpSetTimeouts(h_session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

        HINTERNET h_connect = WinHttpConnect(h_session, url.host.c_str(), url.port, 0);
        if (h_connect == NULL)
        {
            out.detail = L"连不上服务器";
            WinHttpCloseHandle(h_session);
            return out;
        }

        HINTERNET h_request = WinHttpOpenRequest(h_connect, verb.c_str(), url.path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            url.https ? WINHTTP_FLAG_SECURE : 0);
        if (h_request == NULL)
        {
            out.detail = L"无法创建请求";
            WinHttpCloseHandle(h_connect);
            WinHttpCloseHandle(h_session);
            return out;
        }

        // 让服务端可以压；老系统上这个选项不存在，忽略失败即可
        DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
        WinHttpSetOption(h_request, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));

        std::wstring headers = L"Content-Type: application/json\r\n";
        headers += stream ? L"Accept: text/event-stream\r\n" : L"Accept: application/json\r\n";
        if (!params.api_key.empty())
            headers += L"Authorization: Bearer " + SafeHeaderValue(params.api_key) + L"\r\n";
        WinHttpAddRequestHeaders(h_request, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        DWORD tick_begin = ::GetTickCount();

        BOOL sent = body.empty()
            ? WinHttpSendRequest(h_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            : WinHttpSendRequest(h_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);

        if (!sent || !WinHttpReceiveResponse(h_request, NULL))
        {
            DWORD err = ::GetLastError();
            out.elapsed_ms = static_cast<int>(::GetTickCount() - tick_begin);
            WinHttpCloseHandle(h_request);
            WinHttpCloseHandle(h_connect);
            WinHttpCloseHandle(h_session);
            if (err == ERROR_WINHTTP_TIMEOUT || err == ERROR_INTERNET_TIMEOUT)
            {
                out.kind = AiErrorKind::Timeout;
                out.detail = L"等了 " + std::to_wstring(params.timeout_sec) + L" 秒还没回";
            }
            else if (err == ERROR_WINHTTP_NAME_NOT_RESOLVED || err == ERROR_WINHTTP_CANNOT_CONNECT ||
                     err == ERROR_WINHTTP_CONNECTION_ERROR || err == ERROR_INTERNET_CANNOT_CONNECT)
            {
                out.kind = AiErrorKind::Network;
                out.detail = L"连不上服务器，看看网络或代理设置";
            }
            else
            {
                out.kind = AiErrorKind::Network;
                out.detail = L"请求没发出去（Windows 错误码 " + std::to_wstring(err) + L"）";
            }
            return out;
        }

        DWORD status = 0;
        DWORD status_size = sizeof(status);
        WinHttpQueryHeaders(h_request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
        out.status = static_cast<int>(status);

        // 流式：边收边把新内容递出去；非流式：一口气读完
        std::string recv;
        std::wstring full;
        std::string carry;      // 上一次没凑成一行的零头
        char buf[8192];
        DWORD bytes_read = 0;
        while (WinHttpReadData(h_request, buf, sizeof(buf), &bytes_read) && bytes_read > 0)
        {
            if (cancel != nullptr && *cancel)
            {
                out.elapsed_ms = static_cast<int>(::GetTickCount() - tick_begin);
                WinHttpCloseHandle(h_request);
                WinHttpCloseHandle(h_connect);
                WinHttpCloseHandle(h_session);
                out.kind = AiErrorKind::Cancelled;
                out.ok = true;
                out.detail = L"已取消";
                return out;
            }

            if (stream && on_delta)
            {
                carry.append(buf, bytes_read);
                size_t pos = 0;
                while (pos < carry.size())
                {
                    size_t nl = carry.find('\n', pos);
                    if (nl == std::string::npos)
                        break;
                    std::string line = carry.substr(pos, nl - pos);
                    pos = nl + 1;
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.size() < 6 || line.compare(0, 6, "data: ") != 0)
                        continue;
                    std::string payload = line.substr(6);
                    if (payload == "[DONE]")
                        continue;
                    try
                    {
                        json j = json::parse(payload);
                        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty())
                        {
                            const json& ch = j["choices"][0];
                            if (ch.contains("delta") && ch["delta"].contains("content") && ch["delta"]["content"].is_string())
                            {
                                std::wstring delta = FromUtf8(ch["delta"]["content"].get<std::string>());
                                if (!delta.empty())
                                {
                                    full += delta;
                                    on_delta(delta);
                                }
                            }
                        }
                    }
                    catch (...)
                    {
                        // 半行或奇奇怪怪的片段，跳过就是了
                    }
                }
                if (pos < carry.size())
                    carry.erase(0, pos);
                else
                    carry.clear();
            }
            else
            {
                recv.append(buf, bytes_read);
            }
        }

        out.elapsed_ms = static_cast<int>(::GetTickCount() - tick_begin);
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);

        if (status >= 400)
        {
            std::wstring server_msg;
            try
            {
                json j = json::parse(recv);
                if (j.contains("error") && j["error"].contains("message") && j["error"]["message"].is_string())
                    server_msg = FromUtf8(j["error"]["message"].get<std::string>());
            }
            catch (...) {}

            if (status == 401 || status == 403)
            {
                out.kind = AiErrorKind::Auth;
                out.detail = L"API Key 多半填错了或已失效";
            }
            else if (status == 429)
            {
                out.kind = AiErrorKind::RateLimit;
                out.detail = L"请求太密了，等一会儿再试";
            }
            else if (status == 400 || status == 404 || status == 422)
            {
                out.kind = AiErrorKind::Protocol;
                out.detail = L"请求被拒绝，检查模型名对不对";
            }
            else if (status >= 500)
            {
                out.kind = AiErrorKind::Server;
                out.detail = L"服务商那边出问题了";
            }
            else
            {
                out.kind = AiErrorKind::Server;
                out.detail = L"请求没成功";
            }
            if (!server_msg.empty())
                out.detail += L"（" + server_msg + L"）";
            return out;
        }

        out.ok = true;
        out.kind = AiErrorKind::None;
        if (stream)
            out.body = ToUtf8(full);
        else
            out.body = recv;
        return out;
    }

    // 把一次 HTTP 结果翻成给用户看的一句话
    std::wstring ErrorText(AiErrorKind kind, int status, const std::wstring& detail)
    {
        std::wstring head;
        switch (kind)
        {
        case AiErrorKind::Auth:      head = L"鉴权失败"; break;
        case AiErrorKind::RateLimit: head = L"限流"; break;
        case AiErrorKind::Timeout:   head = L"超时"; break;
        case AiErrorKind::Server:    head = L"服务端错误"; break;
        case AiErrorKind::Protocol:  head = L"返回异常"; break;
        case AiErrorKind::Cancelled: head = L"已取消"; break;
        default:                     head = L"网络错误"; break;
        }
        std::wstring s = head;
        if (status > 0)
            s += L"（HTTP " + std::to_wstring(status) + L"）";
        if (!detail.empty())
            s += L"：" + detail;
        return s;
    }

    bool WorthRetry(AiErrorKind kind)
    {
        return kind == AiErrorKind::Timeout || kind == AiErrorKind::Network ||
               kind == AiErrorKind::Server || kind == AiErrorKind::RateLimit;
    }
}

// ---------------------------------------------------------------- 工作线程

namespace
{
    struct ChatJob
    {
        HWND hwnd{ NULL };
        int gen{ 0 };
        AiCallParams params;
        std::vector<AiChatMessage> messages;
        std::wstring source;
    };

    // 线程里要 PostMessage 增量，但 PostMessage 是同步送到界面的，
    // 所以回调里分配的字符串由界面负责 delete。
    UINT __cdecl ChatThreadProc(LPVOID pParam)
    {
        ChatJob* job = static_cast<ChatJob*>(pParam);
        if (job == nullptr)
            return 0;

        AiCallResult r = AiHttpClient::Chat(job->params, job->messages,
            [job](const std::wstring& delta) {
                if (job->hwnd != NULL && !delta.empty())
                {
                    if (!::PostMessage(job->hwnd, WM_AI_CHAT_DELTA, (WPARAM)job->gen,
                                       (LPARAM)new std::wstring(delta)))
                    {
                        // 窗口已经没了，自己收拾
                    }
                }
            }, nullptr);

        std::unique_ptr<AiChatDoneResult> done(new AiChatDoneResult);
        done->ok = r.ok;
        done->kind = r.error_kind;
        done->text = r.text;
        done->error = r.error;
        done->elapsed_ms = r.elapsed_ms;
        done->source = job->source;

        if (job->hwnd != NULL)
        {
            LPARAM lp = (LPARAM)done.release();
            if (!::PostMessage(job->hwnd, WM_AI_CHAT_DONE, (WPARAM)job->gen, lp))
                delete reinterpret_cast<AiChatDoneResult*>(lp);
        }

        delete job;
        return 0;
    }

    struct SimpleJob
    {
        HWND hwnd{ NULL };
        int gen{ 0 };
        AiCallParams params;
    };

    UINT __cdecl TestThreadProc(LPVOID pParam)
    {
        SimpleJob* job = static_cast<SimpleJob*>(pParam);
        if (job == nullptr)
            return 0;
        AiCallResult r = AiHttpClient::TestConnection(job->params);
        if (job->hwnd != NULL)
        {
            LPARAM lp = (LPARAM)new AiCallResult(r);
            if (!::PostMessage(job->hwnd, WM_AI_TEST_DONE, (WPARAM)job->gen, lp))
                delete reinterpret_cast<AiCallResult*>(lp);
        }
        delete job;
        return 0;
    }

    UINT __cdecl ModelsThreadProc(LPVOID pParam)
    {
        SimpleJob* job = static_cast<SimpleJob*>(pParam);
        if (job == nullptr)
            return 0;
        AiModelListResult out;
        std::wstring error;
        out.ok = AiHttpClient::FetchModels(job->params, out.models, error);
        out.error = error;
        if (job->hwnd != NULL)
        {
            LPARAM lp = (LPARAM)new AiModelListResult(out);
            if (!::PostMessage(job->hwnd, WM_AI_MODELS_DONE, (WPARAM)job->gen, lp))
                delete reinterpret_cast<AiModelListResult*>(lp);
        }
        delete job;
        return 0;
    }
}

void AiStartChatJob(HWND hwnd, int gen, const AiCallParams& params,
                    const std::vector<AiChatMessage>& messages, const std::wstring& source)
{
    ChatJob* job = new ChatJob;
    job->hwnd = hwnd;
    job->gen = gen;
    job->params = params;
    job->messages = messages;
    job->source = source;
    CWinThread* pThread = AfxBeginThread(ChatThreadProc, job, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
    if (pThread == nullptr)
    {
        delete job;
        return;
    }
    pThread->m_bAutoDelete = TRUE;
    pThread->ResumeThread();
}

void AiStartTestJob(HWND hwnd, int gen, const AiCallParams& params)
{
    SimpleJob* job = new SimpleJob;
    job->hwnd = hwnd;
    job->gen = gen;
    job->params = params;
    CWinThread* pThread = AfxBeginThread(TestThreadProc, job, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
    if (pThread == nullptr)
    {
        delete job;
        return;
    }
    pThread->m_bAutoDelete = TRUE;
    pThread->ResumeThread();
}

void AiStartFetchModelsJob(HWND hwnd, int gen, const AiCallParams& params)
{
    SimpleJob* job = new SimpleJob;
    job->hwnd = hwnd;
    job->gen = gen;
    job->params = params;
    CWinThread* pThread = AfxBeginThread(ModelsThreadProc, job, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
    if (pThread == nullptr)
    {
        delete job;
        return;
    }
    pThread->m_bAutoDelete = TRUE;
    pThread->ResumeThread();
}

// ---------------------------------------------------------------- 参数

AiCallParams AiCallParams::FromModel(const AiModelConfig& m, const AiRequestConfig& req)
{
    AiCallParams p;
    p.base_url = m.base_url;
    p.api_key = m.api_key;
    p.model = m.model;
    p.temperature = m.temperature;
    p.top_p = m.top_p;
    p.max_tokens = m.max_tokens;
    p.timeout_sec = m.timeout_sec;
    p.stream = req.stream;
    p.retry = req.retry;
    p.proxy_mode = req.proxy_mode;
    p.proxy_url = req.proxy_url;
    return p;
}

bool AiCallParams::Valid(std::wstring& why) const
{
    if (base_url.empty())
    {
        why = L"API 地址是空的";
        return false;
    }
    if (base_url.find(L"http://") != 0 && base_url.find(L"https://") != 0)
    {
        why = L"API 地址要以 http:// 或 https:// 开头";
        return false;
    }
    if (model.empty())
    {
        why = L"模型名是空的";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- 对话

AiCallResult AiHttpClient::Chat(const AiCallParams& params,
                                const std::vector<AiChatMessage>& messages,
                                std::function<void(const std::wstring&)> on_delta,
                                volatile bool* cancel)
{
    AiCallResult result;

    std::wstring why;
    if (!params.Valid(why))
    {
        result.SetError(AiErrorKind::NoConfig, why);
        return result;
    }

    // 请求体
    std::string body;
    try
    {
        json j;
        j["model"] = ToUtf8(params.model);
        j["messages"] = json::array();
        for (const auto& m : messages)
        {
            json item;
            item["role"] = ToUtf8(m.role);
            item["content"] = ToUtf8(m.content);
            j["messages"].push_back(item);
        }
        j["temperature"] = params.temperature;
        j["top_p"] = params.top_p;
        j["max_tokens"] = params.max_tokens;
        j["stream"] = params.stream;
        body = j.dump();
    }
    catch (...)
    {
        result.SetError(AiErrorKind::Protocol, L"拼请求体时出错");
        return result;
    }

    std::wstring url = params.base_url + L"/chat/completions";

    const int attempts = params.retry + 1;
    for (int i = 0; i < attempts; ++i)
    {
        if (cancel != nullptr && *cancel)
        {
            result.SetError(AiErrorKind::Cancelled, L"已取消");
            return result;
        }

        RawResponse raw = DoRequest(params, L"POST", url, body, params.stream, on_delta, cancel);
        result.http_status = raw.status;
        result.elapsed_ms = raw.elapsed_ms;

        if (raw.kind == AiErrorKind::Cancelled)
        {
            result.SetError(AiErrorKind::Cancelled, L"已取消");
            return result;
        }
        if (!raw.ok)
        {
            if (i + 1 < attempts && WorthRetry(raw.kind))
                continue;
            result.SetError(raw.kind, ErrorText(raw.kind, raw.status, raw.detail));
            return result;
        }

        // 流式的内容已经在回调里攒过了，这里直接用
        if (params.stream)
        {
            // raw.body 里放的是拼接好的全文（UTF-8）
            result.text = FromUtf8(raw.body);
            result.ok = true;
            result.error_kind = AiErrorKind::None;
            return result;
        }

        std::wstring text;
        try
        {
            json j = json::parse(raw.body);
            if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty())
            {
                const json& ch = j["choices"][0];
                if (ch.contains("message") && ch["message"].contains("content") && ch["message"]["content"].is_string())
                    text = FromUtf8(ch["message"]["content"].get<std::string>());
            }
            if (text.empty() && j.contains("error") && j["error"].contains("message"))
            {
                result.SetError(AiErrorKind::Protocol,
                    L"服务商返回了错误：" + FromUtf8(j["error"]["message"].get<std::string>()));
                return result;
            }
        }
        catch (...)
        {
            result.SetError(AiErrorKind::Protocol, L"返回的内容不是预期的 JSON");
            return result;
        }

        if (text.empty())
        {
            result.SetError(AiErrorKind::Protocol, L"服务商没返回内容（可能不支持这个模型名）");
            return result;
        }

        result.ok = true;
        result.text = text;
        result.error_kind = AiErrorKind::None;
        return result;
    }

    result.SetError(AiErrorKind::Network, L"重试几次都没成");
    return result;
}

// ---------------------------------------------------------------- 模型列表

bool AiHttpClient::FetchModels(const AiCallParams& params,
                               std::vector<std::wstring>& out_models,
                               std::wstring& error)
{
    out_models.clear();

    std::wstring why;
    if (!params.Valid(why))
    {
        error = why;
        return false;
    }

    std::wstring url = params.base_url + L"/models";
    RawResponse raw = DoRequest(params, L"GET", url, std::string(), false, nullptr, nullptr);
    if (!raw.ok)
    {
        error = ErrorText(raw.kind, raw.status, raw.detail);
        return false;
    }

    try
    {
        json j = json::parse(raw.body);
        if (!j.contains("data") || !j["data"].is_array())
        {
            error = L"这个服务商没返回模型列表，请手填模型名";
            return false;
        }
        for (const auto& item : j["data"])
        {
            if (item.is_string())
            {
                out_models.push_back(FromUtf8(item.get<std::string>()));
            }
            else if (item.is_object() && item.contains("id") && item["id"].is_string())
            {
                out_models.push_back(FromUtf8(item["id"].get<std::string>()));
            }
        }
    }
    catch (...)
    {
        error = L"模型列表解析失败，请手填模型名";
        return false;
    }

    if (out_models.empty())
    {
        error = L"没拉到任何模型，请手填模型名";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- 测试连接

AiCallResult AiHttpClient::TestConnection(const AiCallParams& params)
{
    AiCallParams p = params;
    p.stream = false;
    p.retry = 0;
    // 只求验证通不通，别让它真去写一篇小作文
    p.max_tokens = 16;
    if (p.timeout_sec > 20)
        p.timeout_sec = 20;

    std::vector<AiChatMessage> messages;
    messages.push_back({ L"user", L"hi" });

    return Chat(p, messages, nullptr, nullptr);
}

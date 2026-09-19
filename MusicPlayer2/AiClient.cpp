// AiClient.cpp：AI 请求层（WinHTTP + OpenAI 兼容格式）
//
// 只有一份请求代码：POST {base_url}/chat/completions。服务商之间的差别
// 全在 base_url / api_key / model 三个字段上，所以这里不做任何分支。
//
// 注意：本文件里所有函数都是同步阻塞的，只允许在工作线程里调用。

#include "stdafx.h"
#include "MusicPlayer2.h"       // 诊断日志要用 theApp.m_appdata_dir
#include "AiClient.h"
#include "Common.h"
#include <winhttp.h>
#include <cwctype>
#include <memory>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

namespace
{
    using json = nlohmann::json;

    std::string ToUtf8(const std::wstring& s)
    {
        // ⚠ 必须用 UTF8_NO_BOM。
        // CodeType::UTF8 会在结果最前面塞 3 字节 BOM（0xEF 0xBB 0xBF）—— 那是给
        // ini / 文本文件用的。而这里是往 JSON 里塞字符串，BOM 会实打实成为内容的一部分：
        // 模型名就变成 "\uFEFFgpt-oss:120b"，服务商一律回 "model not found"（404/503），
        // 而界面上和日志里因为 BOM 不可见，看起来完全正常，极难发现。
        return CCommon::UnicodeToStr(s, CodeType::UTF8_NO_BOM);
    }

    std::wstring FromUtf8(const std::string& s)
    {
        return CCommon::StrToUnicode(s, CodeType::UTF8);
    }

    // 诊断日志：把这次请求真正发出去的 URL / 头 / body、以及收到的状态码记下来。
    // 落 %APPDATA%\MusicPlayer2\ai_request.log（UTF-8，追加）。Key 会打码。
    // 存在的意义是「界面显示 A、发出去却是 B」这类问题 —— 光看代码和界面都看不出来，
    // 只有把字节摊开才能定案。
    void LogRequest(const std::string& utf8_line)
    {
        const std::wstring dir = theApp.m_appdata_dir;
        if (dir.empty())
            return;
        std::wstring path = dir;
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
            path += L'\\';
        path += L"ai_request.log";

        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"ab") != 0 || f == nullptr)
            return;
        fwrite(utf8_line.data(), 1, utf8_line.size(), f);
        fwrite("\r\n", 1, 2, f);
        fclose(f);
    }

    void LogRequest(const std::wstring& line)
    {
        LogRequest(ToUtf8(line));
    }

    // 去掉首尾空白。地址和 Key 常常是粘贴来的，前后带空格是很常见的事，
    // 不处理的话「 https://xxx 」会被判成「地址要以 http:// 开头」，看着莫名其妙。
    std::wstring Trim(const std::wstring& s)
    {
        size_t b = 0;
        size_t e = s.size();
        while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n'))
            ++b;
        while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n'))
            --e;
        return s.substr(b, e - b);
    }

    // 很多人会把「完整端点」直接填进 API 地址，比如
    //     https://ollama.com/v1/chat/completions
    // 这时候再拼一次 /chat/completions 就成了 .../chat/completions/chat/completions，
    // 服务端只会回一个 404。这里先把尾巴摘掉，只留 base。
    std::wstring StripEndpoint(const std::wstring& b)
    {
        static const wchar_t* kEnds[] = { L"/chat/completions", L"/completions", L"/responses" };
        std::wstring lower;
        lower.reserve(b.size());
        for (wchar_t c : b)
            lower += static_cast<wchar_t>(std::towlower(c));
        for (const wchar_t* e : kEnds)
        {
            const size_t n = std::wstring(e).size();
            if (lower.size() >= n && lower.compare(lower.size() - n, n, e) == 0)
                return b.substr(0, b.size() - n);
        }
        return b;
    }

    // 拼请求地址：base 允许写成
    //     https://a.com/v1                     -> https://a.com/v1/chat/completions
    //     https://a.com/v1/                    -> 同上（结尾斜杠去掉）
    //     https://a.com/v1/chat/completions    -> 同上（重复的端点尾巴先摘掉）
    std::wstring JoinUrl(const std::wstring& base, const wchar_t* tail)
    {
        std::wstring b = Trim(base);
        while (!b.empty() && (b.back() == L'/' || b.back() == L'\\'))
            b.pop_back();
        if (!b.empty())
            b = StripEndpoint(b);
        while (!b.empty() && (b.back() == L'/' || b.back() == L'\\'))
            b.pop_back();
        if (b.empty())
            return std::wstring(tail);
        return b + tail;
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

        // 端口：用 CrackUrl 给的真实端口，这样 http://localhost:11434/v1 这种也能连上
        out.port = (uc.nPort != 0) ? uc.nPort : INTERNET_DEFAULT_HTTP_PORT;

        // 是不是 https 只看地址前缀，不要拿 uc.nScheme 跟 INTERNET_SCHEME_HTTPS 比。
        // WinHTTP 自己那套编号是 http=1 / https=2，而 wininet.h 的枚举是
        // INTERNET_SCHEME_HTTP=3 / INTERNET_SCHEME_HTTPS=4 —— 两者不是一回事。
        // 之前就是在这里比错了：https 的地址被判成明文，端口退到 80、
        // 请求也不带 WINHTTP_FLAG_SECURE，于是所有 https 服务商都「连不上」。
        std::wstring head;
        head.reserve(url.size());
        for (wchar_t c : url)
            head += static_cast<wchar_t>(std::towlower(c));
        out.https = (head.rfind(L"https://", 0) == 0);
        if (out.port == INTERNET_DEFAULT_HTTP_PORT && out.https)
            out.port = INTERNET_DEFAULT_HTTPS_PORT;
        // 去掉结尾的斜杠，后面拼 /chat/completions 才不会变成双斜杠
        while (!out.path.empty() && out.path.back() == L'/')
            out.path.pop_back();
        // 地址只写到域名（比如 http://127.0.0.1:8080）时 CrackUrl 给的路径是空的，
        // 空路径交给 WinHttpOpenRequest 会请求失败，补一个根路径
        if (out.path.empty())
            out.path = L"/";
        out.ok = !out.host.empty();
        return out;
    }

    // 请求头里不能有回车换行，key 是从界面/ini 来的，顺手也把首尾空白去了
    std::wstring SafeHeaderValue(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : Trim(s))
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

    // 三个句柄打包，任何一条出错路径都能一口气收干净
    struct Handles
    {
        HINTERNET session{ NULL };
        HINTERNET connect{ NULL };
        HINTERNET request{ NULL };

        void Close()
        {
            if (request != NULL) { WinHttpCloseHandle(request); request = NULL; }
            if (connect != NULL) { WinHttpCloseHandle(connect); connect = NULL; }
            if (session != NULL) { WinHttpCloseHandle(session); session = NULL; }
        }
    };

    // Windows 错误码说人话，省得用户对着一个数字发呆
    std::wstring WinHttpErrorText(DWORD err)
    {
        switch (err)
        {
        case ERROR_WINHTTP_TIMEOUT:             return L"等太久没回音";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:   return L"地址解析不了，检查域名或网络";
        case ERROR_WINHTTP_CANNOT_CONNECT:      return L"连不上服务器";
        case ERROR_WINHTTP_CONNECTION_ERROR:    return L"连接被中途掐断";
        case ERROR_WINHTTP_INVALID_URL:         return L"API 地址格式不对";
        case 12045:                             return L"证书不受信任";         // ERROR_WINHTTP_INVALID_CA
        case ERROR_WINHTTP_SECURE_FAILURE:      return L"HTTPS 握手失败（证书或系统时间可能有问题）";
        case 12157:                             return L"安全通道出错";         // ERROR_WINHTTP_SECURE_CHANNEL_ERROR
        default:                                return L"请求没发出去";
        }
    }

    // 建好 session / connect / request。force_no_proxy 用来在「跟随系统」连不上时退回直连。
    bool OpenHandles(const AiCallParams& params, const UrlParts& url,
                     const std::wstring& verb, bool force_no_proxy, Handles& h)
    {
        DWORD access = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;   // 跟随系统
        std::wstring manual_proxy;
        const wchar_t* proxy_name = WINHTTP_NO_PROXY_NAME;
        if (params.proxy_mode == AiProxyMode::None || force_no_proxy)
        {
            access = WINHTTP_ACCESS_TYPE_NO_PROXY;
        }
        else if (params.proxy_mode == AiProxyMode::Manual && !params.proxy_url.empty())
        {
            access = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            manual_proxy = params.proxy_url;
            proxy_name = manual_proxy.c_str();
        }

        h.session = WinHttpOpen(L"MusicPlayer2", access, proxy_name, WINHTTP_NO_PROXY_BYPASS, 0);
        if (h.session == NULL)
            return false;

        DWORD timeout_ms = static_cast<DWORD>(params.timeout_sec) * 1000;
        if (timeout_ms < 3000) timeout_ms = 3000;
        WinHttpSetTimeouts(h.session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

        h.connect = WinHttpConnect(h.session, url.host.c_str(), url.port, 0);
        if (h.connect == NULL)
            return false;

        h.request = WinHttpOpenRequest(h.connect, verb.c_str(), url.path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            url.https ? WINHTTP_FLAG_SECURE : 0);
        if (h.request == NULL)
            return false;

        // 让服务端可以压；老系统上这个选项不存在，忽略失败即可
        DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
        WinHttpSetOption(h.request, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));
        return true;
    }

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
            out.detail = L"API 地址填得不对（" + full_url + L"）";
            return out;
        }

        Handles h;
        if (!OpenHandles(params, url, verb, false, h))
        {
            out.detail = L"无法建立网络请求（地址 " + full_url + L"）";
            h.Close();
            return out;
        }

        std::wstring headers = L"Content-Type: application/json\r\n";
        headers += stream ? L"Accept: text/event-stream\r\n" : L"Accept: application/json\r\n";
        if (!params.api_key.empty())
            headers += L"Authorization: Bearer " + SafeHeaderValue(params.api_key) + L"\r\n";
        if (!WinHttpAddRequestHeaders(h.request, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
        {
            // 头没加上就别发了，否则服务端只会回一句看不懂的 400
            out.detail = L"请求头没组装成功（Windows 错误码 " + std::to_wstring(::GetLastError()) + L"）";
            h.Close();
            return out;
        }

        // 真正发出去的东西落一份盘（Key 打码）。出问题时拿这个跟界面上的值逐字对比，
        // 「界面显示 A、发出去是 B」这种就再也藏不住了。
        {
            std::wstring masked = headers;
            const std::wstring tag = L"Authorization: Bearer ";
            const size_t p = masked.find(tag);
            if (p != std::wstring::npos)
            {
                const size_t s = p + tag.size();
                if (masked.size() > s + 8)
                    masked.replace(s, masked.size() - s, masked.substr(s, 8) + L"***");
            }
            LogRequest(L"====> " + verb + L" " + full_url);
            LogRequest(L"headers: " + masked);
            LogRequest(L"body(" + std::to_wstring(body.size()) + L"): " + FromUtf8(body));
        }

        DWORD tick_begin = ::GetTickCount();

        BOOL sent = body.empty()
            ? WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            : WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);

        // 「跟随系统」的代理（含 WPAD / 系统代理）要是不可用，表现就是一直连不上。
        // 这里不消耗用户的重试次数，直接偷偷退回直连再试一次。
        if (!sent && params.proxy_mode == AiProxyMode::System)
        {
            DWORD first_err = ::GetLastError();
            h.Close();
            if (OpenHandles(params, url, verb, true, h))
            {
                WinHttpAddRequestHeaders(h.request, headers.c_str(), (DWORD)-1,
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
                sent = body.empty()
                    ? WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                    : WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                        (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
            }
            if (!sent)
                ::SetLastError(first_err);      // 两次都不行，报第一次的原因
        }

        if (!sent || !WinHttpReceiveResponse(h.request, NULL))
        {
            DWORD err = ::GetLastError();
            out.elapsed_ms = static_cast<int>(::GetTickCount() - tick_begin);
            h.Close();
            if (err == ERROR_WINHTTP_TIMEOUT || err == ERROR_INTERNET_TIMEOUT)
                out.kind = AiErrorKind::Timeout;
            else
                out.kind = AiErrorKind::Network;
            out.detail = WinHttpErrorText(err)
                + L"（Windows 错误码 " + std::to_wstring(err)
                + L"，地址 " + full_url + L"）";
            LogRequest(L"<==== 传输失败 err=" + std::to_wstring(err) + L"  " + out.detail);
            return out;
        }

        DWORD status = 0;
        DWORD status_size = sizeof(status);
        WinHttpQueryHeaders(h.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
        out.status = static_cast<int>(status);
        LogRequest(L"<==== HTTP " + std::to_wstring(status));

        // 流式：边收边把新内容递出去；非流式：一口气读完
        std::string recv;
        std::wstring full;
        std::string carry;      // 上一次没凑成一行的零头
        char buf[8192];
        DWORD bytes_read = 0;
        while (WinHttpReadData(h.request, buf, sizeof(buf), &bytes_read) && bytes_read > 0)
        {
            if (cancel != nullptr && *cancel)
            {
                out.elapsed_ms = static_cast<int>(::GetTickCount() - tick_begin);
                h.Close();
                out.kind = AiErrorKind::Cancelled;
                out.ok = true;
                out.detail = L"已取消";
                return out;
            }

            // 流式：边收边把新内容攒起来（有没有回调都要攒，回调只是顺手递出去）
            if (stream)
            {
                if (status >= 400)
                    recv.append(buf, bytes_read);       // 出错时留一份原文，好在提示里带上服务端的话
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
                    // 规范写法是 "data: xxx"，但确实有服务商不带那个空格
                    if (line.size() < 5 || line.compare(0, 5, "data:") != 0)
                        continue;
                    std::string payload = line.substr(5);
                    if (!payload.empty() && payload[0] == ' ')
                        payload.erase(0, 1);
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
                                    if (on_delta)
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
        h.Close();

        if (status == 0)
        {
            // 连上了却读不到状态码 —— 不能当成功，否则界面上会显示一条空回答
            out.kind = AiErrorKind::Network;
            out.detail = L"服务器没返回 HTTP 状态码（地址 " + full_url + L"）";
            return out;
        }

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
            else if (status == 404)
            {
                out.kind = AiErrorKind::Protocol;
                out.detail = L"两种可能：① API 地址填错（要填服务商的 base 地址，"
                             L"形如 https://xxx/v1，别把 /chat/completions 也塞进去）；"
                             L"② 服务商那边没有这个模型（不少服务商对「没权限用的模型」"
                             L"也回 404，而不是 401）。对照下面的服务商原话看是哪一种";
            }
            else if (status == 400 || status == 422)
            {
                out.kind = AiErrorKind::Protocol;
                out.detail = L"请求被拒绝，多半是模型名不对，或者服务商不接受某个参数";
            }
            else if (status >= 500)
            {
                out.kind = AiErrorKind::Server;
                out.detail = L"服务商那边出错了。如果原话里提到 model / channel，"
                             L"是这个模型在你的账号下没有可用渠道，换一个模型试试；"
                             L"否则多半是对方过载或维护，等一会儿再试";
            }
            else
            {
                out.kind = AiErrorKind::Server;
                out.detail = L"请求没成功";
            }
            if (!server_msg.empty())
            {
                out.detail += L"\n服务商原话：" + server_msg;
            }
            if (!recv.empty())
            {
                // 原样再给一份 —— 解析出来的 message 有时候会漏掉关键信息
                std::wstring raw = FromUtf8(recv.substr(0, 300));
                for (auto& c : raw)
                    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
                out.detail += L"\n服务商原始返回：" + raw;
                LogRequest(L"<==== 服务商原始返回：" + FromUtf8(recv.substr(0, 500)));
            }

            // 把「真正装在请求体里发出去的那个 model」原样摘出来，连长度一起报。
            // 界面上显示的名字和发出去的字节理当一模一样，可一旦不一样
            // （夹了空格、多带了前缀、被截断），光看界面是绝对看不出来的 ——
            // 这一步就是为了让这种情况无处可藏。
            std::wstring model_in_body;
            try
            {
                json jb = json::parse(body);
                if (jb.contains("model") && jb["model"].is_string())
                    model_in_body = FromUtf8(jb["model"].get<std::string>());
            }
            catch (...) {}

            out.detail += L"\n实际请求：" + verb + L" " + full_url;
            out.detail += L"\n请求体里的 model=\"" + model_in_body + L"\"（"
                + std::to_wstring(model_in_body.size()) + L" 个字符）";
            if (model_in_body != params.model)
            {
                out.detail += L"\n⚠ 注意：跟配置里读出来的 \"" + params.model + L"\"（"
                    + std::to_wstring(params.model.size()) + L" 个字符）不一致！";
            }
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
    // 三个关键字段都是从界面/ini 来的，去一遍首尾空白：
    // 「 https://a.com/v1 」这种看着没毛病，不 trim 会被判成地址格式不对
    p.base_url = Trim(m.base_url);
    p.api_key = Trim(m.api_key);
    p.model = Trim(m.model);
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
    const std::wstring url = Trim(base_url);
    const std::wstring name = Trim(model);
    if (url.empty())
    {
        why = L"API 地址是空的";
        return false;
    }
    if (url.find(L"http://") != 0 && url.find(L"https://") != 0)
    {
        why = L"API 地址要以 http:// 或 https:// 开头";
        return false;
    }
    if (name.empty())
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

    std::wstring url = JoinUrl(params.base_url, L"/chat/completions");

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

    std::wstring url = JoinUrl(params.base_url, L"/models");
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

// AiProtocol.h —— OpenAI 兼容协议里「决定兼容性」的纯逻辑
//
// 为什么单独拆出来：
//   请求体长什么样、别人家的响应怎么解析 —— 这才是兼容性的全部所在，
//   而它们跟 MFC / WinHTTP / 界面统统无关。拆出来之后，就能用一个独立的命令行
//   测试程序真编译、真运行、连着模拟服务端把各家协议变体跑一遍 ——
//   而不是只靠读代码或者拿 Python 复刻一遍来"论证"。
//   测试程序：.scratch/test_ai_protocol.cpp（连同编译命令写在文件头）
//
// 依赖：STL + nlohmann/json + windows.h（只用宽窄字符转换）
//
// ⚠ 宽转窄一律走这里的 WideToUtf8（WideCharToMultiByte，绝不含 BOM）。
//   不要用 CCommon::UnicodeToStr(s, CodeType::UTF8) —— 那个会塞 3 字节 BOM，
//   塞进 JSON 里就变成模型名 "\uFEFFgpt-oss:120b"，服务商一律回 model not found。
#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <nlohmann/json.hpp>

namespace AiProtocol
{
    using json = nlohmann::json;

    // ------------------------------------------------------------------ 字符

    // 宽 -> UTF-8。WideCharToMultiByte(CP_UTF8, 0, ...) 天然不写 BOM。
    inline std::string WideToUtf8(const std::wstring& s)
    {
        if (s.empty())
            return std::string();
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                            nullptr, 0, nullptr, nullptr);
        if (n <= 0)
            return std::string();
        std::string out(static_cast<size_t>(n), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                              &out[0], n, nullptr, nullptr);
        return out;
    }

    // 去掉开头的 UTF-8 BOM。有些服务端返回的 JSON 前面真带着 BOM，
    // 直接丢给 json::parse 会抛异常 —— 表现就是「返回的内容不是预期的 JSON」。
    inline std::string StripBom(const std::string& s)
    {
        if (s.size() >= 3 &&
            static_cast<unsigned char>(s[0]) == 0xEF &&
            static_cast<unsigned char>(s[1]) == 0xBB &&
            static_cast<unsigned char>(s[2]) == 0xBF)
            return s.substr(3);
        return s;
    }

    // UTF-8 -> 宽。顺手吃掉可能存在的 BOM。
    inline std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty())
            return std::wstring();
        const char* p = s.data();
        size_t n = s.size();
        if (n >= 3 &&
            static_cast<unsigned char>(p[0]) == 0xEF &&
            static_cast<unsigned char>(p[1]) == 0xBB &&
            static_cast<unsigned char>(p[2]) == 0xBF)
        {
            p += 3;
            n -= 3;
        }
        if (n == 0)
            return std::wstring();
        const int w = ::MultiByteToWideChar(CP_UTF8, 0, p, static_cast<int>(n), nullptr, 0);
        if (w <= 0)
            return std::wstring();
        std::wstring out(static_cast<size_t>(w), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, p, static_cast<int>(n), &out[0], w);
        return out;
    }

    // 去掉首尾空白。地址和 Key 常常是粘贴来的，前后带空格很常见 ——
    // 不处理的话「 https://xxx 」会被判成「地址要以 http:// 开头」，看着莫名其妙。
    inline std::wstring Trim(const std::wstring& s)
    {
        size_t b = 0;
        size_t e = s.size();
        while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n'))
            ++b;
        while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n'))
            --e;
        return s.substr(b, e - b);
    }

    // ------------------------------------------------------------------ URL

    // 很多人会把「完整端点」直接填进 API 地址，比如
    //     https://ollama.com/v1/chat/completions
    // 这时再拼一次就成了 .../chat/completions/chat/completions，服务端只回 404。
    // 这里先把尾巴摘掉，只留 base。
    inline std::wstring StripEndpoint(const std::wstring& b)
    {
        static const wchar_t* kEnds[] = { L"/chat/completions", L"/completions", L"/responses" };
        std::wstring lower;
        lower.reserve(b.size());
        for (wchar_t c : b)
            lower += static_cast<wchar_t>(::towlower(c));

        for (const wchar_t* end : kEnds)
        {
            const size_t len = wcslen(end);
            if (lower.size() >= len && lower.compare(lower.size() - len, len, end) == 0)
                return b.substr(0, b.size() - len);
        }
        return b;
    }

    // 拼请求地址：去首尾空白、去结尾斜杠、摘掉误填的端点尾巴。
    // 「https://a.com/v1/」+「/models」必须得到 /v1/models 而不是 /v1//models。
    inline std::wstring JoinUrl(const std::wstring& base, const wchar_t* tail)
    {
        std::wstring b = StripEndpoint(Trim(base));
        while (!b.empty() && (b.back() == L'/' || b.back() == L'\\'))
            b.pop_back();
        if (b.empty())
            return std::wstring(tail);
        return b + tail;
    }

    // ------------------------------------------------------------------ 请求体

    struct ChatMessage
    {
        std::wstring role;
        std::wstring content;
    };

    struct ChatRequest
    {
        std::wstring model;
        std::vector<ChatMessage> messages;
        double temperature{ 0.7 };
        double top_p{ 1.0 };
        int max_tokens{ 1024 };
        bool stream{ false };
    };

    // 拼请求体。
    // slim = 保守参数：去掉 temperature / top_p，max_tokens 换成 max_completion_tokens。
    //   这是给「只认新参数」的服务商准备的（新版 OpenAI 的 o 系列 / gpt-5、Kimi 也把
    //   max_tokens 标成已弃用）。这几个字段本来就是可选的，不传等于用服务商默认值，
    //   对宽松的服务商没有任何副作用，所以拿来当「被 400 拒绝后的第二次尝试」很合适。
    inline std::string BuildChatBody(const ChatRequest& r, bool slim)
    {
        json j;
        j["model"] = WideToUtf8(r.model);
        j["messages"] = json::array();
        for (const auto& m : r.messages)
        {
            json item;
            item["role"] = WideToUtf8(m.role);
            item["content"] = WideToUtf8(m.content);
            j["messages"].push_back(item);
        }
        if (slim)
        {
            j["max_completion_tokens"] = r.max_tokens;
        }
        else
        {
            j["temperature"] = r.temperature;
            j["top_p"] = r.top_p;
            j["max_tokens"] = r.max_tokens;
        }
        j["stream"] = r.stream;
        return j.dump();
    }

    // ------------------------------------------------------------------ 响应解析

    // 取正文。used_reasoning 非空时会告诉调用方「这次拿到的其实是思考过程」——
    // 流式必须靠它把思考和正文**分开累积**。混在一起的话，用户看到的回答会是
    // 「一大段模型的英文自言自语 + 最后一句正经回答」（2026-09-19 踩过）。
    inline std::wstring PickContent(const json& obj, bool* used_reasoning = nullptr)
    {
        if (used_reasoning) *used_reasoning = false;
        if (!obj.is_object())
            return std::wstring();

        // ① 正文：content，以及旧版补全接口的 text
        static const char* kBody[] = { "content", "text" };
        for (const char* key : kBody)
        {
            auto it = obj.find(key);
            if (it == obj.end() || !it->is_string())
                continue;
            std::wstring s = Utf8ToWide(it->get<std::string>());
            if (!s.empty())
                return s;
        }

        // ② 兜底：思考过程只在正文完全没有时才拿出来顶上，并且打上标记
        static const char* kThink[] = { "reasoning_content", "reasoning", "thinking" };
        for (const char* key : kThink)
        {
            auto it = obj.find(key);
            if (it == obj.end() || !it->is_string())
                continue;
            std::wstring s = Utf8ToWide(it->get<std::string>());
            if (!s.empty())
            {
                if (used_reasoning) *used_reasoning = true;
                return s;
            }
        }
        return std::wstring();
    }

    // 各家出错时回的话长得都不一样，这里按出现频率从高到低挨个试。
    // 少认一种，用户看到的就是「服务商原始返回：一坨 JSON」而不是人话。
    //   {"error":{"message":"..."}}            OpenAI / DeepSeek / Kimi / Agnes（实测）
    //   {"error":{"code":"1001","message":...}} 智谱（实测，未带 Key 时就是这个）
    //   {"code":"...","message":"..."}         通义（部分接口 message 在顶层）
    //   {"detail":"..."} / {"msg":"..."} / {"error_msg":"..."}  各类网关
    //   {"error":"..."}                        error 直接是字符串
    inline std::wstring PickErrorMessage(const json& j)
    {
        auto as_text = [](const json& v) -> std::wstring {
            return v.is_string() ? Utf8ToWide(v.get<std::string>()) : std::wstring();
        };

        if (j.contains("error"))
        {
            const json& e = j["error"];
            if (e.is_string())
            {
                std::wstring s = Utf8ToWide(e.get<std::string>());
                if (!s.empty())
                    return s;
            }
            else if (e.is_object())
            {
                for (const char* key : { "message", "msg", "detail", "error_msg" })
                {
                    auto it = e.find(key);
                    if (it == e.end())
                        continue;
                    std::wstring s = as_text(*it);
                    if (!s.empty())
                        return s;
                }
            }
        }

        for (const char* key : { "message", "detail", "msg", "error_msg", "error_description" })
        {
            auto it = j.find(key);
            if (it == j.end())
                continue;
            std::wstring s = as_text(*it);
            if (!s.empty())
                return s;
        }
        return std::wstring();
    }

    struct ChatParseResult
    {
        bool ok{ false };            // 拿到了正文
        bool server_error{ false };  // 服务商明说了错误（而不是单纯空正文）
        bool text_is_reasoning{ false };    // 正文是空的，拿思考过程顶上了
        std::wstring text;
        std::wstring error;
    };

    // 解析非流式的 /chat/completions 响应。body 不是合法 JSON 时抛异常，调用方接着。
    inline ChatParseResult ParseChatResponse(const std::string& body)
    {
        ChatParseResult r;
        json j = json::parse(StripBom(body));

        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty())
        {
            const json& ch = j["choices"][0];
            bool is_reason = false;
            if (ch.contains("message"))
                r.text = PickContent(ch["message"], &is_reason);
            if (r.text.empty())
                r.text = PickContent(ch, &is_reason);       // 旧版补全风格：text 挂在 choice 上
            r.text_is_reasoning = (is_reason && !r.text.empty());
        }

        if (r.text.empty())
        {
            std::wstring e = PickErrorMessage(j);
            if (!e.empty())
            {
                r.server_error = true;
                r.error = e;
            }
        }
        r.ok = !r.text.empty();
        return r;
    }

    // 解析一行 SSE。true = 这行带来了内容增量。
    // done = 收到 [DONE] 结束标记。
    //
    // 兼容点：
    //   · 规范写 "data: xxx"，但确实有服务商不带那个空格 —— 直接 substr(5) 再吃掉可选空格
    //   · 流式里多数给 delta，个别服务商塞的是完整的 message
    //   · 思考型模型头几片 delta 里只有 reasoning，正文还没开始
    inline bool ParseSseLine(const std::string& line, std::wstring& delta_out, bool& done,
                             bool* from_reasoning = nullptr)
    {
        delta_out.clear();
        done = false;
        if (from_reasoning) *from_reasoning = false;

        if (line.size() < 5 || line.compare(0, 5, "data:") != 0)
            return false;                       // 空行 / event: / id: / :comment

        std::string payload = line.substr(5);
        if (!payload.empty() && payload[0] == ' ')
            payload.erase(0, 1);

        if (payload == "[DONE]")
        {
            done = true;
            return false;
        }

        try
        {
            json j = json::parse(StripBom(payload));
            if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty())
                return false;
            const json& ch = j["choices"][0];
            std::wstring d;
            bool is_reason = false;
            if (ch.contains("delta"))
                d = PickContent(ch["delta"], &is_reason);
            else if (ch.contains("message"))
                d = PickContent(ch["message"], &is_reason);
            if (d.empty())
                return false;
            delta_out = d;
            if (from_reasoning) *from_reasoning = is_reason;
            return true;
        }
        catch (...)
        {
            return false;                       // 半行或怪片段，跳过
        }
    }

    // 解析 GET /models 的响应。返回 false = 这个服务商没给可识别的列表。
    // body 不是合法 JSON 时抛异常，调用方接着。
    inline bool ParseModelList(const std::string& body, std::vector<std::wstring>& out)
    {
        out.clear();
        json j = json::parse(StripBom(body));

        // 标准是 {"data":[{"id":"..."}]}，但确实有网关用别的壳子：
        // {"models":[...]}、{"result":[...]}，或者顶层干脆就是个数组。
        const json* arr = nullptr;
        if (j.is_array())
        {
            arr = &j;
        }
        else if (j.is_object())
        {
            for (const char* key : { "data", "models", "result", "list" })
            {
                auto it = j.find(key);
                if (it != j.end() && it->is_array())
                {
                    arr = &*it;
                    break;
                }
            }
        }
        if (arr == nullptr)
            return false;

        for (const auto& item : *arr)
        {
            if (item.is_string())
            {
                out.push_back(Utf8ToWide(item.get<std::string>()));
                continue;
            }
            if (!item.is_object())
                continue;
            for (const char* key : { "id", "name", "model" })
            {
                auto it = item.find(key);
                if (it == item.end() || !it->is_string())
                    continue;
                std::wstring name = Utf8ToWide(it->get<std::string>());
                if (!name.empty())
                    out.push_back(name);
                break;
            }
        }
        return true;
    }
}

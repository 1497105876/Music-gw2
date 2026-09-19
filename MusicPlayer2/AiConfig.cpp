// AiConfig.cpp：AI 配置的默认值、读写（跟其它设置写在同一个 ini 里）
//
// 存法说明：ini 一行一个值，而系统提示词是带换行的，所以存之前先把
// 反斜杠和换行压成 \n，读的时候再还原。模型是变长的，用 [ai_model_xxx]
// 这种带前缀的 section 存，id 列表记在 [ai] 的 model_ids 里。

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "AiConfig.h"
#include "IniHelper.h"
#include "Common.h"

namespace
{
    // 服务商预置表。顺序即下拉里的顺序，「自定义」放最后。
    // 国内直连的排前面，境外可能连不上的排后面。
    const AiProviderPreset kPresets[] = {
        { L"zhipu",    L"智谱 GLM",       L"https://open.bigmodel.cn/api/paas/v4",              L"glm-4-flash" },
        { L"deepseek", L"DeepSeek",       L"https://api.deepseek.com/v1",                       L"deepseek-chat" },
        { L"kimi",     L"月之暗面 Kimi",  L"https://api.moonshot.cn/v1",                        L"moonshot-v1-8k" },
        { L"qwen",     L"通义千问",       L"https://dashscope.aliyuncs.com/compatible-mode/v1", L"qwen-plus" },
        { L"agnes",    L"Agnes AI",       L"https://apihub.agnes-ai.com/v1",                    L"agnes-2.5-flash" },
        { L"ollama",   L"Ollama",         L"https://ollama.com/v1",                             L"gpt-oss:20b" },
        { L"openai",   L"OpenAI",         L"https://api.openai.com/v1",                         L"gpt-4o-mini" },
        { L"custom",   L"自定义",         L"",                                                  L"" },
    };

    const wchar_t* const kSectionAi = L"ai";
    const wchar_t* const kSectionRequest = L"ai_request";
    const wchar_t* const kSectionPrompt = L"ai_prompt";
    const wchar_t* const kSectionPrivacy = L"ai_privacy";
    const wchar_t* const kModelPrefix = L"ai_model_";

    const int kMaxPromptHistory = 3;    // 提示词历史只留最近 3 版

    std::wstring& MutableDefaultPrompt()
    {
        static std::wstring s_prompt =
            L"你是一个懂音乐、也懂数据的朋友。回答时请做到：\n"
            L"1. 只依据我给出的统计数据说话，数据里没有的别编造；\n"
            L"2. 说话自然口语化，别用「根据数据分析」这种腔调；\n"
            L"3. 结论要落到具体数字上，别讲空话；\n"
            L"4. 没把握就直接说不知道。";
        return s_prompt;
    }
}

const AiProviderPreset* AiGetPresets(int& count)
{
    count = sizeof(kPresets) / sizeof(kPresets[0]);
    return kPresets;
}

const AiProviderPreset* AiFindPreset(const std::wstring& key)
{
    for (const auto& p : kPresets)
    {
        if (key == p.key)
            return &p;
    }
    // 找不到就当自定义
    return &kPresets[sizeof(kPresets) / sizeof(kPresets[0]) - 1];
}

std::wstring AiModelConfig::DisplayName() const
{
    if (!name.empty())
        return name;
    return ProviderName();
}

std::wstring AiModelConfig::ProviderName() const
{
    return AiFindPreset(provider)->name;
}

const AiModelConfig* AiSettings::CurrentModel() const
{
    int index = IndexOf(current_model_id);
    if (index < 0)
        return nullptr;
    return &models[index];
}

AiModelConfig* AiSettings::MutableCurrentModel()
{
    int index = IndexOf(current_model_id);
    if (index < 0)
        return nullptr;
    return &models[index];
}

int AiSettings::IndexOf(const std::wstring& id) const
{
    if (id.empty())
        return -1;
    for (size_t i = 0; i < models.size(); ++i)
    {
        if (models[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

std::wstring AiSettings::NewModelId() const
{
    int max_no = 0;
    for (const auto& m : models)
    {
        if (m.id.size() > 1 && m.id[0] == L'm')
        {
            int no = _wtoi(m.id.c_str() + 1);
            if (no > max_no) max_no = no;
        }
    }
    wchar_t buf[16];
    swprintf_s(buf, L"m%d", max_no + 1);
    std::wstring id{ buf };
    // 极端情况下（手改过 ini）撞号就继续往后找
    while (IndexOf(id) >= 0)
    {
        max_no++;
        swprintf_s(buf, L"m%d", max_no + 1);
        id = buf;
    }
    return id;
}

std::wstring AiSettings::ChatHistoryDir() const
{
    if (!privacy.chat_history_dir.empty())
        return privacy.chat_history_dir;
    return theApp.m_appdata_dir + L"chat\\";
}

// ───────────────────────── 存取 ─────────────────────────

namespace AiConfig
{
    AiSettings& Get()
    {
        static AiSettings s_settings;
        return s_settings;
    }

    const std::wstring& DefaultSystemPrompt()
    {
        return MutableDefaultPrompt();
    }

    std::wstring EscapeLine(const std::wstring& text)
    {
        std::wstring out;
        out.reserve(text.size() + 8);
        for (wchar_t c : text)
        {
            if (c == L'\\') out += L"\\\\";
            else if (c == L'\n') out += L"\\n";
            else if (c == L'\r') continue;
            else out += c;
        }
        return out;
    }

    std::wstring UnescapeLine(const std::wstring& text)
    {
        std::wstring out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\\' && i + 1 < text.size())
            {
                wchar_t n = text[i + 1];
                if (n == L'n') { out += L'\n'; ++i; continue; }
                if (n == L'\\') { out += L'\\'; ++i; continue; }
            }
            out += text[i];
        }
        return out;
    }

    void Load(CIniHelper& ini)
    {
        AiSettings& s = Get();

        s.enabled = ini.GetBool(kSectionAi, L"enabled", true);
        s.chat_mode = static_cast<AiChatMode>(ini.GetInt(kSectionAi, L"chat_mode", 0));
        if (static_cast<int>(s.chat_mode) < 0 || static_cast<int>(s.chat_mode) > 2)
            s.chat_mode = AiChatMode::Local;
        s.current_model_id = ini.GetString(kSectionAi, L"current_model", L"");

        // 模型：先读 id 列表，再逐个 section 读
        std::vector<std::wstring> ids;
        ini.GetStringList(kSectionAi, L"model_ids", ids, std::vector<std::wstring>());
        s.models.clear();
        for (const auto& id : ids)
        {
            std::wstring app = kModelPrefix + id;
            AiModelConfig m;
            m.id = id;
            m.name = ini.GetString(app.c_str(), L"name", L"");
            m.provider = ini.GetString(app.c_str(), L"provider", L"custom");
            m.base_url = ini.GetString(app.c_str(), L"base_url", L"");
            m.api_key = ini.GetString(app.c_str(), L"api_key", L"");
            m.model = ini.GetString(app.c_str(), L"model", L"");
            m.temperature = ini.GetDouble(app.c_str(), L"temperature", 0.7);
            m.top_p = ini.GetDouble(app.c_str(), L"top_p", 1.0);
            m.max_tokens = ini.GetInt(app.c_str(), L"max_tokens", 1024);
            m.timeout_sec = ini.GetInt(app.c_str(), L"timeout_sec", 30);
            if (m.temperature < 0) m.temperature = 0;
            if (m.temperature > 2) m.temperature = 2;
            if (m.top_p <= 0) m.top_p = 1.0;
            if (m.max_tokens < 16) m.max_tokens = 16;
            if (m.timeout_sec < 3) m.timeout_sec = 3;
            if (m.timeout_sec > 300) m.timeout_sec = 300;
            s.models.push_back(m);
        }
        // 当前模型指了个不存在的，就退回第一套
        if (s.IndexOf(s.current_model_id) < 0)
            s.current_model_id = s.models.empty() ? L"" : s.models.front().id;

        s.request.stream = ini.GetBool(kSectionRequest, L"stream", true);
        s.request.retry = ini.GetInt(kSectionRequest, L"retry", 2);
        if (s.request.retry < 0) s.request.retry = 0;
        if (s.request.retry > 5) s.request.retry = 5;
        int proxy_mode = ini.GetInt(kSectionRequest, L"proxy_mode", static_cast<int>(AiProxyMode::System));
        if (proxy_mode < 0 || proxy_mode > 2)
            proxy_mode = static_cast<int>(AiProxyMode::System);
        s.request.proxy_mode = static_cast<AiProxyMode>(proxy_mode);
        s.request.proxy_url = ini.GetString(kSectionRequest, L"proxy_url", L"");

        std::wstring sys = UnescapeLine(ini.GetString(kSectionPrompt, L"system", L""));
        s.prompt.system = sys.empty() ? DefaultSystemPrompt() : sys;
        int lang = ini.GetInt(kSectionPrompt, L"language", 0);
        if (lang < 0 || lang > 2) lang = 0;
        s.prompt.language = static_cast<AiAnswerLanguage>(lang);
        s.prompt.history.clear();
        for (int i = 0; i < kMaxPromptHistory; ++i)
        {
            wchar_t kt[32], kv[32];
            swprintf_s(kt, L"h%d_time", i);
            swprintf_s(kv, L"h%d_text", i);
            std::wstring time = ini.GetString(kSectionPrompt, kt, L"");
            if (time.empty())
                continue;
            AiPromptHistoryItem item;
            item.time = time;
            item.text = UnescapeLine(ini.GetString(kSectionPrompt, kv, L""));
            s.prompt.history.push_back(item);
        }

        s.privacy.allow_song_meta = ini.GetBool(kSectionPrivacy, L"allow_song_meta", true);
        s.privacy.save_chat_history = ini.GetBool(kSectionPrivacy, L"save_chat_history", false);
        s.privacy.chat_history_dir = ini.GetString(kSectionPrivacy, L"chat_history_dir", L"");
    }

    void Save(CIniHelper& ini)
    {
        const AiSettings& s = Get();

        ini.WriteBool(kSectionAi, L"enabled", s.enabled);
        ini.WriteInt(kSectionAi, L"chat_mode", static_cast<int>(s.chat_mode));
        ini.WriteString(kSectionAi, L"current_model", s.current_model_id);

        std::vector<std::wstring> ids;
        for (const auto& m : s.models)
            ids.push_back(m.id);
        ini.WriteStringList(kSectionAi, L"model_ids", ids);

        for (const auto& m : s.models)
        {
            std::wstring app = kModelPrefix + m.id;
            ini.WriteString(app.c_str(), L"name", m.name);
            ini.WriteString(app.c_str(), L"provider", m.provider);
            ini.WriteString(app.c_str(), L"base_url", m.base_url);
            ini.WriteString(app.c_str(), L"api_key", m.api_key);
            ini.WriteString(app.c_str(), L"model", m.model);
            ini.WriteDouble(app.c_str(), L"temperature", m.temperature);
            ini.WriteDouble(app.c_str(), L"top_p", m.top_p);
            ini.WriteInt(app.c_str(), L"max_tokens", m.max_tokens);
            ini.WriteInt(app.c_str(), L"timeout_sec", m.timeout_sec);
        }

        ini.WriteBool(kSectionRequest, L"stream", s.request.stream);
        ini.WriteInt(kSectionRequest, L"retry", s.request.retry);
        ini.WriteInt(kSectionRequest, L"proxy_mode", static_cast<int>(s.request.proxy_mode));
        ini.WriteString(kSectionRequest, L"proxy_url", s.request.proxy_url);

        ini.WriteString(kSectionPrompt, L"system", EscapeLine(s.prompt.system));
        ini.WriteInt(kSectionPrompt, L"language", static_cast<int>(s.prompt.language));
        for (int i = 0; i < kMaxPromptHistory; ++i)
        {
            wchar_t kt[32], kv[32];
            swprintf_s(kt, L"h%d_time", i);
            swprintf_s(kv, L"h%d_text", i);
            if (i < static_cast<int>(s.prompt.history.size()))
            {
                ini.WriteString(kSectionPrompt, kt, s.prompt.history[i].time);
                ini.WriteString(kSectionPrompt, kv, EscapeLine(s.prompt.history[i].text));
            }
            else
            {
                // 历史变短了，把上一轮残留的键清掉
                ini.WriteString(kSectionPrompt, kt, L"");
                ini.WriteString(kSectionPrompt, kv, L"");
            }
        }

        ini.WriteBool(kSectionPrivacy, L"allow_song_meta", s.privacy.allow_song_meta);
        ini.WriteBool(kSectionPrivacy, L"save_chat_history", s.privacy.save_chat_history);
        ini.WriteString(kSectionPrivacy, L"chat_history_dir", s.privacy.chat_history_dir);
    }
}

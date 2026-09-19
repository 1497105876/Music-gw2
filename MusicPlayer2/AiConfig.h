#pragma once
#include <string>
#include <vector>

// AI 功能的配置层：数据结构 + 落盘。
//
// 全盘只认一种协议 —— OpenAI 兼容格式（POST {base_url}/chat/completions）。
// 服务商之间只有 base_url / api_key / model 三个字段不同，所以「预置服务商」
// 只是把这几个字段帮你填好，请求代码自始至终只有一份。

class CIniHelper;

// ───────────────────────── 服务商预置 ─────────────────────────
struct AiProviderPreset
{
    const wchar_t* key;         // 存进配置的标识（"zhipu" / "custom" ...）
    const wchar_t* name;        // 界面上显示的名字
    const wchar_t* base_url;    // 形如 https://open.bigmodel.cn/api/paas/v4
    const wchar_t* model;       // 默认模型名
};

// 返回预置表（含末尾的「自定义」）
const AiProviderPreset* AiGetPresets(int& count);
// 按 key 找预置，找不到返回「自定义」
const AiProviderPreset* AiFindPreset(const std::wstring& key);

// ───────────────────────── 数据 ─────────────────────────

// 一套模型配置
struct AiModelConfig
{
    std::wstring id;            // 内部唯一标识（m1、m2 ...），不显示
    std::wstring name;          // 备注名，随便起
    std::wstring provider;      // AiProviderPreset::key
    std::wstring base_url;      // 不包含末尾斜杠，也不包含 /chat/completions
    std::wstring api_key;
    std::wstring model;

    double temperature{ 0.7 };
    double top_p{ 1.0 };
    int max_tokens{ 1024 };
    int timeout_sec{ 30 };

    std::wstring DisplayName() const;       // 备注名；没填就回退到服务商名
    std::wstring ProviderName() const;      // 服务商显示名
};

enum class AiProxyMode
{
    None = 0,       // 不使用代理
    System = 1,     // 跟随系统设置
    Manual = 2      // 手动填写
};

enum class AiAnswerLanguage
{
    Ui = 0,         // 跟随界面语言
    Chinese = 1,
    English = 2
};

enum class AiChatMode
{
    Local = 0,      // 本地规则引擎，零网络
    Model = 1,      // 带聚合值问模型
    Max = 2         // 聚合值 + 完整原始记录
};

struct AiRequestConfig
{
    bool stream{ true };                            // 边生成边显示
    int retry{ 2 };                                 // 失败重试次数
    AiProxyMode proxy_mode{ AiProxyMode::System };
    std::wstring proxy_url;                         // 仅 Manual 时生效
};

// 系统提示词的一次历史版本
struct AiPromptHistoryItem
{
    std::wstring time;      // "2026-09-19 17:30"
    std::wstring text;
};

struct AiPromptConfig
{
    std::wstring system;
    AiAnswerLanguage language{ AiAnswerLanguage::Ui };
    std::vector<AiPromptHistoryItem> history;       // 只留最近 3 条，更早的自动丢
};

struct AiPrivacyConfig
{
    // 「只发送聚合统计值」是强制项，没有开关 —— 文件路径任何情况下都不出本机
    bool allow_song_meta{ true };       // 允许一并发送曲目名和歌手
    bool save_chat_history{ false };    // 对话记录存到本地，默认关
    std::wstring chat_history_dir;      // 空则用默认目录
};

struct AiSettings
{
    bool enabled{ true };
    AiChatMode chat_mode{ AiChatMode::Local };      // 对话页上次的档位
    std::wstring current_model_id;

    std::vector<AiModelConfig> models;
    AiRequestConfig request;
    AiPromptConfig prompt;
    AiPrivacyConfig privacy;

    const AiModelConfig* CurrentModel() const;
    AiModelConfig* MutableCurrentModel();
    int IndexOf(const std::wstring& id) const;
    std::wstring NewModelId() const;                // 生成一个不撞车的 id
    std::wstring ChatHistoryDir() const;            // 空或没设置时给默认目录
};

namespace AiConfig
{
    // 进程内唯一的一份配置，界面改的就是它，保存时整体落盘
    AiSettings& Get();

    void Load(CIniHelper& ini);
    void Save(CIniHelper& ini);

    // 内置默认系统提示词，「重置为默认」按钮用它
    const std::wstring& DefaultSystemPrompt();

    // 系统提示词里带换行，ini 是一行一个值，所以存的时候把换行压成 \n
    std::wstring EscapeLine(const std::wstring& text);
    std::wstring UnescapeLine(const std::wstring& text);
}

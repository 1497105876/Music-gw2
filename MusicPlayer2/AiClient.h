#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "AiConfig.h"

// AI 请求层：只有一份代码，全部走 OpenAI 兼容格式。
//
//   对话   POST {base_url}/chat/completions
//   模型   GET  {base_url}/models
//
// 这里的函数都是**同步阻塞**的，设计上就只应该在**工作线程**里调用；
// 想中途放弃就把 cancel 指向的原子标志置 true。

// 一次调用的失败原因，界面据此决定说什么话
enum class AiErrorKind
{
    None,           // 成功
    Cancelled,      // 用户中途取消
    NoConfig,       // 压根没配模型
    Timeout,
    Network,        // 连不上 / 连接被重置
    Auth,           // 401 / 403：Key 多半无效
    RateLimit,      // 429
    Server,         // 5xx
    Protocol        // 返回了能读但不是我们要的形状
};

struct AiCallResult
{
    bool ok{ false };
    AiErrorKind error_kind{ AiErrorKind::None };
    int http_status{ 0 };
    int elapsed_ms{ 0 };
    std::wstring text;          // 成功的回答全文
    std::wstring error;         // 失败时给用户看的一句话（已含服务商名等上下文）

    // 失败原因里常要带上服务商名，这里统一拼
    void SetError(AiErrorKind kind, const std::wstring& msg)
    {
        ok = false;
        error_kind = kind;
        error = msg;
    }
};

// 一轮对话里的角色
struct AiChatMessage
{
    std::wstring role;      // system / user / assistant
    std::wstring content;
};

// 发一次请求要带上的东西，全部从一套模型配置里取
struct AiCallParams
{
    std::wstring base_url;
    std::wstring api_key;
    std::wstring model;
    double temperature{ 0.7 };
    double top_p{ 1.0 };
    int max_tokens{ 1024 };
    int timeout_sec{ 30 };
    bool stream{ true };
    int retry{ 2 };                 // 失败重试次数（只对「值得重试」的错重试）
    AiProxyMode proxy_mode{ AiProxyMode::System };
    std::wstring proxy_url;

    // 从一套模型配置填好字段（请求层的通用项从全局设置补）
    static AiCallParams FromModel(const AiModelConfig& m, const AiRequestConfig& req);
    bool Valid(std::wstring& why) const;    // 关键字段在不在
};

// 工作线程干完活后 PostMessage 回界面用的消息。
// wParam 一律是「代号」(gen)：界面在切换/关闭时把 gen 加一，就可以安全忽略迟到的结果。
#define WM_AI_CHAT_DELTA   (WM_APP + 212)   // lParam = new std::wstring(本次增量)，界面用完 delete
#define WM_AI_CHAT_DONE    (WM_APP + 213)   // lParam = AiChatDoneResult*，界面用完 delete
#define WM_AI_TEST_DONE    (WM_APP + 214)   // lParam = AiCallResult*，界面用完 delete
#define WM_AI_MODELS_DONE  (WM_APP + 215)   // lParam = AiModelListResult*，界面用完 delete

struct AiChatDoneResult
{
    bool ok{ false };
    AiErrorKind kind{ AiErrorKind::None };
    std::wstring text;
    std::wstring error;
    int elapsed_ms{ 0 };
    std::wstring source;        // 气泡底部那行「依据：…」
};

struct AiModelListResult
{
    bool ok{ false };
    std::wstring error;
    std::vector<std::wstring> models;
};

// 起工作线程的三个入口。线程自己持有参数副本，界面可以放心先销毁。
//
// cancel 是可选的「中断开关」：界面点「停止生成」时把它置 true，工作线程
// 下一次读数据时就会收手（延迟很短）。用 shared_ptr 而不是裸指针是为了生命周期安全 ——
// 界面（窗口）先销毁时它那份引用就没了，但线程手里还握着一份，绝不会变成野指针。
void AiStartChatJob(HWND hwnd, int gen, const AiCallParams& params,
                    const std::vector<AiChatMessage>& messages, const std::wstring& source,
                    std::shared_ptr<std::atomic<bool>> cancel = nullptr);
void AiStartTestJob(HWND hwnd, int gen, const AiCallParams& params);
void AiStartFetchModelsJob(HWND hwnd, int gen, const AiCallParams& params);

class AiHttpClient
{
public:
    // 对话。stream 打开时每收到一小段就回调一次 on_delta（回调发生在调用线程里）。
    // cancel 非空且被置为 true 时尽快收手，返回 Cancelled。
    static AiCallResult Chat(const AiCallParams& params,
                             const std::vector<AiChatMessage>& messages,
                             std::function<void(const std::wstring&)> on_delta = nullptr,
                             const std::atomic<bool>* cancel = nullptr);

    // 拉服务商的模型列表。失败不算致命 —— 让调用方退回手填就行。
    static bool FetchModels(const AiCallParams& params,
                            std::vector<std::wstring>& out_models,
                            std::wstring& error);

    // 「测试连接」：发一条最小请求，只为验证 Key 和地址通不通
    static AiCallResult TestConnection(const AiCallParams& params);
};

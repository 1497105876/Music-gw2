#pragma once
#include <string>
#include <vector>
#include "StatCommon.h"

// AI 对话的「喂料层」：把已经算好的统计值拼成一段给模型看的文字。
//
// 这一层存在的意义是**不让原始记录直接出门**：
//   · 模型档 —— 只有聚合值（总览 / 时段 / Top 榜 / 行为分解 / 遗珠）
//   · Max 档 —— 聚合值 + 逐条原始记录（时间 / 标题 / 歌手 / 时长 / 是否完播）
//   · 文件路径、歌词、封面路径 —— 任何档位都不出现，这是用户故事里承诺过的
//
// 另外「本地」档不走网络，由这里的规则引擎直接拼一句人话回答。

// 一次性把当前数据快照交进来（指针，不拷贝）
struct AiStatSnapshot
{
    const std::vector<PlayRecord>* all_records{ nullptr };  // 全量（AI 的上下文用「全部记录」）
    const std::vector<PlayRecord>* filtered{ nullptr };     // 当前筛选
    const StatSummary* summary{ nullptr };
    const FinishBreakdown* finish{ nullptr };
    const std::vector<ArtistRankItem>* artists{ nullptr };
    const std::vector<AlbumRankItem>* albums{ nullptr };
    const std::vector<SongRankItem>* songs{ nullptr };
    const int* hour_hist{ nullptr };                        // 24 小时分布
    int first_ymd{ 0 };
    int last_ymd{ 0 };

    bool Valid() const { return summary != nullptr && all_records != nullptr && finish != nullptr; }
};

namespace AiStatContext
{
    // 「模型」档：一段 600~900 token 的聚合值文字
    std::wstring BuildSummaryText(const AiStatSnapshot& s, bool allow_song_meta);

    // 「Max」档：在聚合值后面追加逐条原始记录
    std::wstring BuildRawRecordsText(const AiStatSnapshot& s, int max_rows, bool allow_song_meta);

    // 「本地」档：规则引擎直接给答案（零网络）
    // 本地档用：一条「已经写成一句人话」的事实（带数字、带简单判断）
    struct LocalFact
    {
        std::wstring text;                  // 现成的一句话
        std::vector<std::wstring> keys;     // 问题里出现这些词就算命中
        int weight{ 0 };                    // 一条都没命中时按它挑最重要的
        bool exclusive{ false };            // 命中就独占回答，不再拼别的事实
    };
    // 只针对传入的这一批记录构造事实（调用方可能已把范围缩到「上周」）
    std::vector<LocalFact> BuildLocalFacts(const std::vector<PlayRecord>& recs,
        bool allow_song_meta, const std::wstring& question);
    std::wstring BuildLocalAnswer(const AiStatSnapshot& s, const std::wstring& question, bool allow_song_meta);

    // 这次回答用到了哪些数字（气泡底部那行「依据：…」）
    std::wstring BuildSourceText(const AiStatSnapshot& s, const std::wstring& question);


    // ── 本地档的「菜单式问答」──
    // 纯代码引擎理解不了自由提问，所以把**能答准的问题**做成菜单让用户挑：
    // 点一下直接发，答案由专用生成器算，不再出现「问第三名答第一名」这类事。
    struct LocalQa
    {
        std::wstring              id;
        std::wstring              question;
        std::vector<std::wstring> next;      // 追问（存 id），形成探索路径
    };
    struct LocalQaGroup
    {
        std::wstring              name;
        std::vector<std::wstring> ids;
    };

    const std::vector<LocalQa>&      LocalQaCatalog();
    const std::vector<LocalQaGroup>& LocalQaMenu();
    const LocalQa* FindLocalQaById(const std::wstring& id);
    const LocalQa* FindLocalQaByText(const std::wstring& question);

    // 按 id 生成一段有把握的答案；id 不认识就返回空串
    std::wstring BuildQaAnswer(const AiStatSnapshot& s, const std::wstring& id, bool allow_song_meta);

    // 快捷提问池，界面每次进来随机挑几条
    const std::vector<std::wstring>& QuickQuestionPool();
}

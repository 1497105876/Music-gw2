#pragma once
#include "PlayStatistics.h"
#include "StatAnalysis.h"
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 共享类型定义（主对话框与全部统计子页共用）
// 说明：本文件是统计功能的“共享数据契约”，所有时间范围/粒度/聚合结果类型均在此定义。
//      具体聚合算法在 CStatAnalysis（StatAnalysis.h/.cpp）中实现。
// ─────────────────────────────────────────────────────────────────────────────

// 自定义消息：CPlayStatistics 写入一条播放记录后，通知已打开的统计对话框刷新。
// 使用 WM_USER 段（< 0x8000），可用 ON_MESSAGE 映射；选 +150 避开工程内已用值（最大 WM_USER+143）。
#define WM_STAT_RECORD_APPENDED (WM_USER + 150)

// 聚合粒度
enum class Grain { Day = 0, Week = 1, Month = 2, Year = 3 };

// 时间范围预设
enum class RangePreset { Last7, Last30, Last90, ThisYear, LastYear, All, Custom };

// 全局统计过滤器（时间范围均用 YYYYMMDD 整数表示，含首尾；0 表示无界）
struct StatFilter
{
    int         from_ymd{ 0 };                          // 含；0 = 无下界
    int         to_ymd{ 0 };                            // 含；0 = 无上界
    Grain       grain{ Grain::Day };                    // 粒度
    RangePreset preset{ RangePreset::Last30 };          // 当前预设
};

// ── 聚合结果类型（供各子页与 AI 洞察共用，字段与架构文档 A4 类图一致）──

// 按粒度聚合的分桶结果
struct PeriodBucket
{
    int          key{ 0 };              // 桶键：天=YYYYMMDD / 周=YYYYWW / 月=YYYYMM / 年=YYYY
    std::wstring label;                 // 显示标签：MM-DD / Www / YYYY-MM / YYYY
    int          count{ 0 };            // 播放次数
    int          duration_sec{ 0 };     // 播放时长（秒）
    int          completed_count{ 0 };  // 完播次数
    int          skipped_count{ 0 };    // 跳过次数
};

// 热力图单元格（一天一格）
struct HeatCell
{
    int ymd{ 0 };               // YYYYMMDD
    int count{ 0 };             // 播放次数
    int duration_sec{ 0 };      // 播放时长（秒）
};

// 跳过位置分桶
struct SkipBucket
{
    int          index{ 0 };        // 桶序号 0~3
    std::wstring label;             // 桶标签，如 "0~25%"
    int          count{ 0 };        // 次数
    double       percent{ 0.0 };    // 占全部跳过记录百分比
};

// 周期对比（同比/环比）
struct PeriodComparison
{
    PeriodBucket current;                                   // 当前周期
    PeriodBucket previous;                                  // 上一周期
    PeriodBucket last_year;                                 // 去年同期
    int          count_delta{ 0 };                          // 与上期的次数差
    double       count_delta_percent{ 0.0 };                // 与上期的次数变化百分比
    bool         has_previous{ false };                     // 上期是否有数据
    bool         has_last_year{ false };                    // 去年同期是否有数据
    bool         same_as_previous{ false };                 // 去年同期与上一周期是否为同一周期（年粒度下二者重合）
};

// 专辑排行项
struct AlbumRankItem
{
    std::wstring album;             // 专辑名（空归入“未知专辑”）
    int          duration_sec{ 0 }; // 累计时长（秒）
    int          count{ 0 };        // 播放次数
};

// 遗珠（反复听却从未完播）
struct RetiredGem
{
    std::wstring file_path;
    std::wstring title;
    std::wstring artist;
    int          count{ 0 };        // 播放次数
};

// 歌单/来源贡献
struct PlaylistContribution
{
    std::wstring source;            // playlist_source
    int          count{ 0 };        // 播放次数
    int          duration_sec{ 0 }; // 播放时长（秒）
    double       percent{ 0.0 };    // 时长占比（0~100）
};

// 五维雷达（0~100）
struct RadarScore
{
    double explore{ 0.0 };          // 探索度
    double focus{ 0.0 };            // 专注度
    double night{ 0.0 };            // 夜行度
    double loyalty{ 0.0 };          // 专一度
    double fresh{ 0.0 };            // 新鲜度
};

// 按年归档回顾
struct YearReview
{
    int          year{ 0 };
    int          count{ 0 };
    int          duration_sec{ 0 };
    std::wstring top_artist;
    std::wstring top_song;
};

// 全局统计上下文：唯一数据源，主对话框构造一次，子页只读。
// records 指向主对话框持有的过滤后记录集（不拷贝，避免 2 万条记录被复制 9 份）。
struct StatContext
{
    const std::vector<PlayRecord>* records{ nullptr };  // 已按时间范围过滤（唯一时间过滤点）
    StatFilter  filter;
    StatSummary summary;                                // 主对话框统一算一次（15 秒口径已统一）
    bool        valid{ false };
};

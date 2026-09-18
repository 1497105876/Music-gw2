#pragma once
#include "PlayStatistics.h"
#include <string>
#include <vector>

// 前向声明共享类型（完整定义见 StatCommon.h），供下方接口签名使用。
// StatCommon.h 反向包含本文件以取得 StatSummary，故此处不能包含 StatCommon.h。
enum class Grain;
struct PeriodBucket;
struct HeatCell;
struct SkipBucket;
struct PeriodComparison;
struct FinishBreakdown;
struct AlbumRankItem;
struct ArtistRankItem;
struct SongRankItem;
struct RetiredGem;
struct PlaylistContribution;
struct RadarScore;
struct YearReview;
struct StatFilter;

// 统计聚合结果，供概览页显示和 AI 洞察使用
struct StatSummary
{
    // 口径版本号：口径调整（含过滤规则变化）时必须递增
    int schema_version = 2;         // v2：统一 15 秒口径，时段统计纳入 <15 秒过滤

    // 基本统计
    int total_count = 0;            // 累计播放次数
    int total_duration_sec = 0;     // 累计播放时长（秒）
    int today_count = 0;            // 今日播放次数
    int week_count = 0;             // 本周播放次数
    int month_count = 0;            // 本月播放次数
    int active_days = 0;            // 有播放记录的天数
    int today_active_hour = 0;      // 今日活跃时段（小时，-1 表示无记录）

    // 完整收听
    int completed_count = 0;        // 完整听完全曲的次数
    double completed_rate = 0.0;    // 完整收听率（0~100）
    double skip_rate = 0.0;         // 跳过率（0~100）

    // 深度统计
    int avg_plays_per_day = 0;      // 日均播放次数
    int longest_streak = 0;         // 最长连续听歌天数
    int current_streak = 0;         // 当前连续听歌天数
    int first_hour = -1;            // 最常听歌时段起始（小时）
    int last_hour = -1;             // 最常听歌时段结束（小时）
    int night_owl_sec = 0;          // 0点-6点夜间收听总时长（秒）
    int night_owl_percent = 0;      // 夜间收听时长占总时长百分比
    int weekend_percent = 0;        // 周末收听占比（按次数）
    int one_hit_wonders = 0;        // 只听过1次的歌曲数
    int repeat_depth = 0;           // 听 5 次以上的歌曲数
    int new_songs_month = 0;        // 本月第一次听的新歌曲数
    int total_songs = 0;            // 听过的不同歌曲总数
    double avg_completion = 0.0;    // 平均完成度 = 播放时长/歌曲长度（0~100）
    int explore_percent = 0;        // 探索型百分比（只听1次的歌曲占曲库比例）
    int inflation_percent = 0;      // 收听集中度：Top10 歌曲播放次数占比

    // Top 榜
    std::wstring top_artist;        // 最爱歌手
    int top_artist_sec = 0;         // 最爱歌手累计时长（秒）
    std::wstring top_song;          // 最爱歌曲（标题）
    std::wstring top_song_artist;   // 最爱歌曲的歌手
    int top_song_count = 0;         // 最爱歌曲播放次数

    // 档案徽章（key 用于图标/存档，text 是直接显示的短句）
    struct Badge
    {
        std::wstring key;           // 徽章标识（如 listener_night）
        std::wstring title;         // 徽章名（如 夜猫子）
        std::wstring text;          // 一句话说明（如 0点到6点听了 x 小时）
    };
    std::vector<Badge> badges;
};

// 播放统计聚合：所有计算只依赖 PlayRecord，无 UI 依赖，可被界面和 AI 共用
class CStatAnalysis
{
public:
    // ── 口径判定（15 秒过滤的唯一入口，所有聚合必须调用） ──
    // 记录是否计入统计：实际播放时长 >= 15 秒
    static bool IsCounted(const PlayRecord& r);

    // ── 时间解析（统一口径，"YYYY-MM-DDTHH:MM:SS"，禁止第二套解析） ──
    static int YmdOf(const std::wstring& played_at);            // -> YYYYMMDD；非法返回 0
    static int HourOf(const std::wstring& played_at);           // -> 小时；非法返回 -1
    static std::wstring FormatYmd(int ymd, wchar_t sep = L'-'); // 20260101 -> "2026-01-01"
    static std::wstring FormatBucketLabel(int key, Grain g);    // MM-DD / Www / YYYY-MM / YYYY

    // 把秒数格式化成 "X小时X分X秒"
    static std::wstring FormatDuration(int seconds);

    // ── 聚合接口（本批实现 ComputeSummary/ComputeBuckets/ComputeHourHistogram） ──
    // 从播放记录计算全部统计指标（内部统一走 IsCounted）
    static StatSummary ComputeSummary(const std::vector<PlayRecord>& records);

    // 按粒度聚合分桶（天/周/月/年），按 key 升序
    static std::vector<PeriodBucket> ComputeBuckets(const std::vector<PlayRecord>& records, Grain grain);

    // 24 小时时段直方图，写入 out_hour[24]，返回计入的总次数
    static int ComputeHourHistogram(const std::vector<PlayRecord>& records, int out_hour[24]);

    // ── 以下接口在后续批次实现（本批仅声明，供子页/洞察统一签名） ──
    static std::vector<HeatCell>      ComputeHeatmapGrid(const std::vector<PlayRecord>& records);
    static std::vector<SkipBucket>    ComputeSkipDistribution(const std::vector<PlayRecord>& records);
    static PeriodComparison           ComputePeriodComparison(const std::vector<PlayRecord>& records, const StatFilter& filter);
    static std::vector<AlbumRankItem> ComputeAlbumRank(const std::vector<PlayRecord>& records, int top_n = 20);
    // 结束状态分解与平均完成度（概览页用）
    static FinishBreakdown ComputeFinishBreakdown(const std::vector<PlayRecord>& records);

    // 歌手排行：按累计时长降序
    static std::vector<ArtistRankItem> ComputeArtistRank(const std::vector<PlayRecord>& records, int top_n = 200);
    // 曲目排行：按播放次数降序
    static std::vector<SongRankItem> ComputeSongRank(const std::vector<PlayRecord>& records, int top_n = 200);
    static std::vector<PeriodBucket>  ComputeNewSongTrend(const std::vector<PlayRecord>& records);
    static std::vector<RetiredGem>    ComputeRetiredGems(const std::vector<PlayRecord>& records, int min_count = 3);
    static std::vector<PlaylistContribution> ComputePlaylistContribution(const std::vector<PlayRecord>& records);
    static int                        ComputeStreakMiss(const std::vector<PlayRecord>& records);
    static RadarScore                 ComputeRadar(const StatSummary& summary);
    static std::vector<YearReview>    ComputeYearlyReviews(const std::vector<PlayRecord>& records);
};

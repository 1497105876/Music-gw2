#pragma once
#include "PlayStatistics.h"
#include <string>
#include <vector>

// 统计聚合结果，供概览页显示和 AI 洞察使用
struct StatSummary
{
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
    std::wstring top_genre;         // 最爱流派
    int top_genre_count = 0;        // 最爱流派播放次数

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
    // 从播放记录计算全部统计指标（传入全部记录，内部统一过滤 <15 秒的试听）
    static StatSummary ComputeSummary(const std::vector<PlayRecord>& records);

    // 把秒数格式化成 "X小时X分X秒"
    static std::wstring FormatDuration(int seconds);
};

#include "stdafx.h"
#include "StatAiInsight.h"
#include "StatAnalysis.h"
#include "PlayStatistics.h"
#include <algorithm>

// 本地统计洞察引擎：
// 不调用任何在线模型，用规则模板从聚合数据里挑出最有信息量的结论，
// 输出自然语言的"AI 式"解读。开销：纯内存字符串拼接，微秒级，无常驻资源。
std::vector<std::wstring> CStatAiInsight::GenerateInsights(const StatSummary& s)
{
    std::vector<std::wstring> out;

    if (s.total_count <= 0)
    {
        out.push_back(L"还没有播放记录，听几首歌再来看看你的音乐报告吧。");
        return out;
    }

    // ── 1. 总量概览 ──
    {
        std::wstring line = L"这阵子你一共听了 " + std::to_wstring(s.total_count) +
            L" 首歌，累计 " + CStatAnalysis::FormatDuration(s.total_duration_sec);
        if (s.active_days > 0)
        {
            line += L"，横跨 " + std::to_wstring(s.active_days) + L" 天";
            if (s.avg_plays_per_day > 0)
                line += L"，日均 " + std::to_wstring(s.avg_plays_per_day) + L" 首";
            line += L"。";
        }
        out.push_back(line);
    }

    // ── 2. 口味核心：最爱歌手 + 流派 ──
    {
        if (!s.top_artist.empty())
        {
            std::wstring line = L"你的心头好是「" + s.top_artist + L"」，一个人就占了 " +
                CStatAnalysis::FormatDuration(s.top_artist_sec) + L" 的播放时长";
            if (!s.top_genre.empty())
                line += L"，最近你偏爱「" + s.top_genre + L"」风格";
            line += L"。";
            out.push_back(line);
        }
        else if (!s.top_genre.empty())
        {
            out.push_back(L"最近你偏爱「" + s.top_genre + L"」风格的音乐。");
        }
    }

    // ── 3. 完整收听率评价 ──
    {
        wchar_t buf[64];
        swprintf_s(buf, L"你的完整收听率是 %.0f%%", s.completed_rate);
        std::wstring line = buf;
        if (s.completed_rate >= 70)
            line += L"，说明你很会挑歌，基本都能听到最后。";
        else if (s.completed_rate >= 40)
            line += L"，口味比较随性，对歌单有一定的挑选。";
        else
            line += L"，切歌比较频繁——也许该整理一下歌单，把爱听的留下。";
        out.push_back(line);
    }

    // ── 4. 行为亮点（有徽章就展开说） ──
    for (const auto& badge : s.badges)
    {
        out.push_back(L"【" + badge.title + L"】" + badge.text + L"。");
        if (out.size() >= 5) break;      // 徽章最多展开 3~4 条，防止刷屏
    }

    // ── 5. 时段洞察 ──
    if (s.first_hour >= 0)
    {
        wchar_t buf[64];
        if (s.first_hour >= 0 && s.first_hour < 6)
            swprintf_s(buf, L"你最常在凌晨 %d 点前后听歌，深夜的音乐时光虽然美好，也别太熬夜哦。", s.first_hour);
        else if (s.first_hour < 12)
            swprintf_s(buf, L"你的黄金听歌时段在上午 %d 点，一天从音乐开始。", s.first_hour);
        else if (s.first_hour < 18)
            swprintf_s(buf, L"你的黄金听歌时段在下午 %d 点，音乐是你的午后伴侣。", s.first_hour);
        else
            swprintf_s(buf, L"你的黄金听歌时段在晚上 %d 点，夜深人静时最懂音乐。", s.first_hour);
        out.push_back(buf);
    }

    // ── 6. 新鲜度洞察 ──
    if (s.total_songs > 0)
    {
        int familiar = 100 - s.explore_percent;
        if (s.explore_percent >= 50)
        {
            out.push_back(L"你是探索型听众——" + std::to_wstring(s.explore_percent) +
                L"% 的歌只听过一次，一直在发现新音乐。");
        }
        else if (s.explore_percent <= 20)
        {
            out.push_back(L"你是深情型听众——" + std::to_wstring(familiar) +
                L"% 的歌你会反复听，专情于熟悉的旋律。");
        }
    }

    // ── 7. 连续天数激励 ──
    if (s.current_streak >= 3)
    {
        out.push_back(L"你已经连续 " + std::to_wstring(s.current_streak) +
            L" 天听歌了，保持这份节奏，音乐一直都在。");
    }

    // ── 8. 本月新歌 ──
    if (s.new_songs_month > 0)
    {
        out.push_back(L"这个月你解锁了 " + std::to_wstring(s.new_songs_month) +
            L" 首新歌，好奇心在线。");
    }

    // ── 9. 一条可执行的建议（挑最值得说的说） ──
    if (s.skip_rate >= 50 && s.total_count >= 20)
    {
        out.push_back(L"小建议：你的跳过率到了 " + std::to_wstring((int)(s.skip_rate + 0.5)) +
            L"%，可以试试把不常听的歌移出歌单，让每首歌都值得播放。");
    }
    else if (s.top_song_count >= 10 && !s.top_song.empty())
    {
        std::wstring line = L"小建议：你已经把「" + s.top_song;
        if (!s.top_song_artist.empty())
            line += L" - " + s.top_song_artist;
        line += L"」听了 " + std::to_wstring(s.top_song_count) +
            L" 遍，不妨基于它找找同风格的宝藏歌曲。";
        out.push_back(line);
    }
    else if (s.avg_completion > 0 && s.avg_completion < 50)
    {
        out.push_back(L"小建议：你的平均完成度只有 " + std::to_wstring((int)(s.avg_completion + 0.5)) +
            L"%，很多歌只听了一小段，试试给自己一首歌的完整时间。");
    }

    return out;
}

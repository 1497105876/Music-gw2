// AiStatContext.cpp：AI 对话的喂料层

#include "stdafx.h"
#include "AiStatContext.h"
#include "StatAnalysis.h"
#include <algorithm>

namespace
{
    std::wstring Num(int v)
    {
        return std::to_wstring(v);
    }

    std::wstring Pct(double v)
    {
        wchar_t b[32];
        swprintf_s(b, L"%.1f%%", v);
        return b;
    }

    std::wstring Duration(int sec)
    {
        return CStatAnalysis::FormatDuration(sec);
    }

    std::wstring DateText(int ymd)
    {
        return ymd > 0 ? CStatAnalysis::FormatYmd(ymd) : L"未知";
    }

    // 曲目标题（关掉「发送歌名」时只给一个不带名字的说法）
    std::wstring SongLabel(const std::wstring& title, bool allow_meta)
    {
        if (allow_meta && !title.empty())
            return title;
        return L"（曲名已隐藏）";
    }

    std::wstring ArtistLabel(const std::wstring& artist, bool allow_meta)
    {
        if (!allow_meta)
            return L"未知歌手";
        return artist.empty() ? L"未知歌手" : artist;
    }

    const wchar_t* ReasonText(PlayRecord::FinishReason r)
    {
        switch (r)
        {
        case PlayRecord::FinishReason::COMPLETED:  return L"播完";
        case PlayRecord::FinishReason::SKIPPED:    return L"跳过";
        case PlayRecord::FinishReason::STOPPED:    return L"停止";
        default:                                   return L"出错";
        }
    }

    // 模型档要的聚合值，一律按「全部记录」现算一遍 —— 新写进来的播放记录要能跟上
    void EnsureAggregates(const AiStatSnapshot& s, StatSummary& sum, FinishBreakdown& fin, int hour[24])
    {
        if (s.all_records != nullptr && !s.all_records->empty())
        {
            sum = CStatAnalysis::ComputeSummary(*s.all_records);
            fin = CStatAnalysis::ComputeFinishBreakdown(*s.all_records);
            CStatAnalysis::ComputeHourHistogram(*s.all_records, hour);
        }
        else
        {
            if (s.summary != nullptr) sum = *s.summary;
            if (s.finish != nullptr) fin = *s.finish;
            for (int i = 0; i < 24; ++i)
                hour[i] = (s.hour_hist != nullptr) ? s.hour_hist[i] : 0;
        }
    }
}

namespace AiStatContext
{
    std::wstring BuildSummaryText(const AiStatSnapshot& s, bool allow_song_meta)
    {
        if (!s.Valid() || s.all_records == nullptr || s.all_records->empty())
            return L"（这段时间里没有可统计的播放记录）";

        StatSummary sum;
        FinishBreakdown fin;
        int hour[24]{};
        EnsureAggregates(s, sum, fin, hour);

        std::wstring t;
        auto Line = [&t](const std::wstring& line) { t += L"- " + line + L"\n"; };

        Line(L"数据范围：全部记录，从 " + DateText(s.first_ymd) + L" 到 " + DateText(s.last_ymd));
        Line(L"计入统计：" + Num(fin.total) + L" 次播放 / " + Duration(sum.total_duration_sec) +
             L" / " + Num(sum.total_songs) + L" 首曲目 / 活跃 " + Num(sum.active_days) + L" 天");
        if (fin.total > 0)
        {
            Line(L"完播率 " + Pct(sum.completed_rate) + L"，跳过率 " + Pct(sum.skip_rate) +
                 L"，平均单次 " + Duration(fin.total > 0 ? sum.total_duration_sec / fin.total : 0));
        }

        int peak_hour = -1, peak_cnt = 0;
        for (int h = 0; h < 24; ++h)
        {
            if (hour[h] > peak_cnt) { peak_cnt = hour[h]; peak_hour = h; }
        }
        if (peak_hour >= 0)
        {
            Line(L"最活跃时段 " + Num(peak_hour) + L":00-" + Num(peak_hour) + L":59（" + Num(peak_cnt) + L" 次）");
            std::wstring dist;
            for (int h = 0; h < 24; ++h)
            {
                if (hour[h] == 0) continue;
                dist += Num(h) + L"点" + Num(hour[h]) + L" ";
                if (dist.size() > 200) break;
            }
            Line(L"时段分布：" + dist);
        }
        if (sum.night_owl_percent > 0)
            Line(L"深夜（0-6 点）占 " + Num(sum.night_owl_percent) + L"%，周末占 " + Num(sum.weekend_percent) + L"%");
        Line(L"连续听歌：当前 " + Num(sum.current_streak) + L" 天 / 最长 " + Num(sum.longest_streak) + L" 天");

        // Top 榜
        std::vector<ArtistRankItem> artists;
        std::vector<AlbumRankItem> albums;
        std::vector<SongRankItem> songs;
        if (s.all_records != nullptr)
        {
            artists = CStatAnalysis::ComputeArtistRank(*s.all_records, 5);
            albums = CStatAnalysis::ComputeAlbumRank(*s.all_records, 5);
            songs = CStatAnalysis::ComputeSongRank(*s.all_records, 5);
        }
        if (!artists.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < artists.size() && i < 5; ++i)
            {
                if (i > 0) line += L" / ";
                line += ArtistLabel(artists[i].artist, allow_song_meta) + L" " + Duration(artists[i].duration_sec);
            }
            Line(L"Top5 歌手：" + line);
        }
        if (!albums.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < albums.size() && i < 5; ++i)
            {
                if (i > 0) line += L" / ";
                line += (allow_song_meta ? albums[i].album : L"（专辑名已隐藏）") + L" " + Duration(albums[i].duration_sec);
            }
            Line(L"Top5 专辑：" + line);
        }
        if (!songs.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < songs.size() && i < 5; ++i)
            {
                if (i > 0) line += L" / ";
                line += SongLabel(songs[i].title, allow_song_meta) + L"（" + Num(songs[i].count) + L" 次）";
            }
            Line(L"Top5 曲目：" + line);
        }

        Line(L"行为分解：播完 " + Num(fin.completed) + L" / 跳过 " + Num(fin.skipped) +
             L" / 停止 " + Num(fin.stopped) + L" / 出错 " + Num(fin.errored));

        std::vector<RetiredGem> gems = CStatAnalysis::ComputeRetiredGems(*s.all_records, 3);
        if (!gems.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < gems.size() && i < 5; ++i)
            {
                if (i > 0) line += L" / ";
                line += SongLabel(gems[i].title, allow_song_meta) + L"（听了 " + Num(gems[i].count) + L" 遍）";
            }
            Line(L"遗珠（反复听却没听完）：" + line);
        }

        Line(L"只听过 1 次的曲子 " + Num(sum.one_hit_wonders) + L" 首，听过 5 次以上的 " + Num(sum.repeat_depth) + L" 首");

        return t;
    }

    std::wstring BuildRawRecordsText(const AiStatSnapshot& s, int max_rows, bool allow_song_meta)
    {
        if (s.all_records == nullptr || s.all_records->empty())
            return L"";

        std::wstring t;
        t += L"## 原始播放记录（最多 " + Num(max_rows) + L" 条，时间倒序；文件路径已剔除）\n";
        t += L"格式：时间 | 标题 | 歌手 | 本次播放 | 结果\n";

        int shown = 0;
        for (const auto& r : *s.all_records)
        {
            if (shown >= max_rows)
                break;
            std::wstring played = r.played_at.size() >= 16 ? r.played_at.substr(0, 16) : r.played_at;
            std::replace(played.begin(), played.end(), L'T', L' ');
            t += played + L" | " + SongLabel(r.title, allow_song_meta) +
                 L" | " + ArtistLabel(r.artist, allow_song_meta) +
                 L" | " + Duration(r.play_duration_sec) +
                 L" | " + ReasonText(r.finish_reason) + L"\n";
            ++shown;
        }
        const int total = static_cast<int>(s.all_records->size());
        if (total > shown)
            t += L"（还有 " + Num(total - shown) + L" 条未列出）\n";
        return t;
    }

    std::wstring BuildSourceText(const AiStatSnapshot& s, const std::wstring& question)
    {
        std::wstring src;
        bool hit_artist = question.find(L"歌手") != std::wstring::npos || question.find(L"谁") != std::wstring::npos;
        bool hit_time = question.find(L"时段") != std::wstring::npos || question.find(L"几点") != std::wstring::npos ||
                        question.find(L"什么时候") != std::wstring::npos || question.find(L"晚上") != std::wstring::npos ||
                        question.find(L"深夜") != std::wstring::npos;
        bool hit_behavior = question.find(L"跳过") != std::wstring::npos || question.find(L"切歌") != std::wstring::npos ||
                            question.find(L"完播") != std::wstring::npos || question.find(L"没听完") != std::wstring::npos ||
                            question.find(L"反复") != std::wstring::npos;
        bool hit_change = question.find(L"变化") != std::wstring::npos || question.find(L"比") != std::wstring::npos ||
                          question.find(L"趋势") != std::wstring::npos;

        if (hit_artist) src += L"Top5 歌手";
        if (hit_time) src += (src.empty() ? L"" : L" / ") + std::wstring(L"时段分布");
        if (hit_behavior) src += (src.empty() ? L"" : L" / ") + std::wstring(L"行为分解 / 遗珠");
        if (hit_change) src += (src.empty() ? L"" : L" / ") + std::wstring(L"周期对比");
        if (src.empty()) src = L"总览指标 / Top 榜";
        return src;
    }

    std::wstring BuildLocalAnswer(const AiStatSnapshot& s, const std::wstring& question, bool allow_song_meta)
    {
        if (!s.Valid() || s.all_records == nullptr || s.all_records->empty())
            return L"这段时间里还没有可统计的播放记录。先去听几首歌，回来再问。";

        StatSummary sum;
        FinishBreakdown fin;
        int hour[24]{};
        EnsureAggregates(s, sum, fin, hour);

        auto Has = [&question](const wchar_t* k) { return question.find(k) != std::wstring::npos; };

        std::wstring a;

        // ── 听得最多的歌手 / 专辑 / 曲目 ──
        if (Has(L"歌手") || Has(L"谁") || (Has(L"最爱") && !Has(L"专辑") && !Has(L"曲目")))
        {
            std::vector<ArtistRankItem> rank = CStatAnalysis::ComputeArtistRank(*s.all_records, 3);
            if (!rank.empty())
            {
                a = L"听得最多的歌手是 " + ArtistLabel(rank[0].artist, allow_song_meta) +
                    L"，累计 " + Duration(rank[0].duration_sec) + L"，" + Num(rank[0].count) + L" 次。";
                if (rank.size() > 1)
                    a += L"\n\n第二名是 " + ArtistLabel(rank[1].artist, allow_song_meta) +
                         L"（" + Duration(rank[1].duration_sec) + L"）。";
            }
            if (!a.empty()) return a;
        }
        if (Has(L"专辑"))
        {
            std::vector<AlbumRankItem> rank = CStatAnalysis::ComputeAlbumRank(*s.all_records, 3);
            if (!rank.empty())
            {
                a = L"听得最多的是专辑《" + (allow_song_meta ? rank[0].album : L"已隐藏") +
                    L"》，" + Duration(rank[0].duration_sec) + L"，" + Num(rank[0].count) + L" 次。";
                return a;
            }
        }
        if (Has(L"曲目") || Has(L"哪首") || Has(L"歌名") || (Has(L"最多") && Has(L"首")))
        {
            std::vector<SongRankItem> rank = CStatAnalysis::ComputeSongRank(*s.all_records, 3);
            if (!rank.empty())
            {
                a = L"播得最多的是《" + SongLabel(rank[0].title, allow_song_meta) +
                    L"》，" + Num(rank[0].count) + L" 次，累计 " + Duration(rank[0].duration_sec) + L"。";
                return a;
            }
        }

        // ── 时段 ──
        if (Has(L"时段") || Has(L"几点") || Has(L"什么时候") || Has(L"晚上") || Has(L"深夜") || Has(L"凌晨"))
        {
            int peak = -1, peak_cnt = 0;
            for (int h = 0; h < 24; ++h)
            {
                if (hour[h] > peak_cnt) { peak_cnt = hour[h]; peak = h; }
            }
            if (peak >= 0)
            {
                a = L"你最常在 " + Num(peak) + L":00-" + Num(peak) + L":59 听歌，这段时间一共 " + Num(peak_cnt) + L" 次。";
                if (sum.night_owl_percent > 0)
                    a += L"\n\n深夜（0-6 点）占了全部时长的 " + Num(sum.night_owl_percent) + L"%。";
                if (sum.weekend_percent > 0)
                    a += L"\n\n周末贡献了 " + Num(sum.weekend_percent) + L"% 的播放次数。";
                return a;
            }
        }

        // ── 完播 / 跳过 / 遗珠 ──
        if (Has(L"完播") || Has(L"跳过") || Has(L"切歌") || Has(L"没听完") || Has(L"反复") || Has(L"遗珠"))
        {
            if (fin.total > 0)
            {
                a = Num(fin.completed) + L" 次完整听完（" + Pct(fin.completed * 100.0 / fin.total) + L"），" +
                    Num(fin.skipped) + L" 次中途切走（" + Pct(fin.skipped * 100.0 / fin.total) + L"）。";
            }
            std::vector<RetiredGem> gems = CStatAnalysis::ComputeRetiredGems(*s.all_records, 3);
            if (!gems.empty())
            {
                a += L"\n\n这几首你反复点开，却一次都没听完：";
                for (size_t i = 0; i < gems.size() && i < 5; ++i)
                {
                    a += L"\n· " + SongLabel(gems[i].title, allow_song_meta) +
                         L"（听了 " + Num(gems[i].count) + L" 遍）";
                }
            }
            if (!a.empty()) return a;
        }

        // ── 连续 / 纪录 ──
        if (Has(L"连续") || Has(L"纪录") || Has(L"记录") || Has(L"天数"))
        {
            a = L"已经连续 " + Num(sum.current_streak) + L" 天听歌，最长纪录是 " + Num(sum.longest_streak) + L" 天。";
            const int miss = CStatAnalysis::ComputeStreakMiss(*s.all_records);
            if (miss > 0)
                a += L"\n\n历史上最长的一段是连续 " + Num(miss) + L" 天，后来断掉了。";
            return a;
        }

        // ── 变化 / 对比 ──
        if (Has(L"变化") || Has(L"比") || Has(L"趋势") || Has(L"新歌"))
        {
            StatFilter all_filter;
            all_filter.preset = RangePreset::All;
            PeriodComparison pc = CStatAnalysis::ComputePeriodComparison(*s.all_records, all_filter);
            if (pc.has_previous)
            {
                const int delta = pc.count_delta;
                if (delta > 0)
                    a = L"比上一个周期多听了 " + Num(delta) + L" 次（" + Pct(pc.count_delta_percent) + L"）。";
                else if (delta < 0)
                    a = L"比上一个周期少听了 " + Num(-delta) + L" 次（" + Pct(pc.count_delta_percent) + L"）。";
                else
                    a = L"跟上一个周期听得一样多。";
            }
            else
            {
                a = L"上一个周期还没有记录，暂时没得比。";
            }
            std::vector<PeriodBucket> news = CStatAnalysis::ComputeNewSongTrend(*s.all_records);
            if (!news.empty())
            {
                const PeriodBucket* best = &news.front();
                for (const auto& b : news)
                {
                    if (b.count > best->count) best = &b;
                }
                if (best->count > 0)
                    a += L"\n\n新歌发现最多的是 " + best->label + L"，那个月新听了 " + Num(best->count) + L" 首。";
            }
            return a;
        }

        // ── 兜底：一句话都没对上任何维度 ──
        // 以前这里直接甩一段总览，用户问的是 A、回答的是 B，观感就是「答非所问」。
        // 改成先**承认没对上**，再说清能问什么 —— 人和人说话就是这么处理的。
        a = L"这句话我没找到能对上的数据维度。";
        if (fin.total > 0)
        {
            a += L"\n\n顺手报个总量：一共 " + Num(fin.total) + L" 次播放、" +
                 Duration(sum.total_duration_sec) + L"，涉及 " + Num(sum.total_songs) + L" 首曲子。";
        }
        a += L"\n\n我能回答这些方面：\n"
             L"· 听得最多的歌手 / 专辑 / 曲目\n"
             L"· 常听的时段，深夜和周末各占多少\n"
             L"· 完播率、跳过率，有没有反复点开却没听完的歌\n"
             L"· 连续听了多少天、最长纪录\n"
             L"· 跟上一个周期比有什么变化、哪个月新歌最多\n"
             L"\n换个说法再问，或者直接点下面的快捷提问。";
        return a;
    }

    const std::vector<std::wstring>& QuickQuestionPool()
    {
        static const std::vector<std::wstring> pool = {
            L"我最近听得怎么样？",
            L"听得最多的歌手是谁",
            L"有什么歌我反复听却没听完",
            L"我的听歌习惯有什么变化",
            L"哪个时段我最爱听歌",
            L"这个月比上个月听得多吗",
            L"我有没有连续听歌的纪录",
            L"新歌我听了多少首",
            L"我听得最久的一天是哪天",
            L"我的完播率算高还是低",
            L"听得最多的专辑是哪张",
            L"我周末听歌多吗"
        };
        return pool;
    }
}

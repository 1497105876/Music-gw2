// AiStatContext.cpp：AI 对话的喂料层

#include "stdafx.h"
#include "AiStatContext.h"
#include "StatAnalysis.h"
#include <algorithm>
#include <map>
#include <set>

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
    // 对任意一批记录现算聚合值
    void AggregateOf(const std::vector<PlayRecord>& recs, StatSummary& sum,
        FinishBreakdown& fin, int hour[24])
    {
        sum = CStatAnalysis::ComputeSummary(recs);
        fin = CStatAnalysis::ComputeFinishBreakdown(recs);
        CStatAnalysis::ComputeHourHistogram(recs, hour);
    }

    void EnsureAggregates(const AiStatSnapshot& s, StatSummary& sum, FinishBreakdown& fin, int hour[24])
    {
        if (s.all_records != nullptr && !s.all_records->empty())
        {
            AggregateOf(*s.all_records, sum, fin, hour);
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


    // ═══════ 日期运算 ═══════
    // YYYYMMDD 与「距 1970-01-01 的天数」互转，用 Howard Hinnant 的 days-from-civil。
    // 纯整数运算，不碰 tm / time_t —— 没有时区与夏令时的坑，也方便在离线脚本里一比一复刻。
    long DaysFromYmd(int ymd)
    {
        int y = ymd / 10000;
        int m = (ymd / 100) % 100;
        int d = ymd % 100;
        y -= (m <= 2) ? 1 : 0;
        const long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u +
            static_cast<unsigned>(d) - 1u;
        const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
        return era * 146097L + static_cast<long>(doe) - 719468L;
    }

    int YmdFromDays(long z)
    {
        z += 719468L;
        const long era = (z >= 0 ? z : z - 146096L) / 146097L;
        const unsigned doe = static_cast<unsigned>(z - era * 146097L);
        const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
        const int y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
        const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
        const unsigned mp = (5u * doy + 2u) / 153u;
        const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
        const unsigned m = (mp < 10u) ? (mp + 3u) : (mp - 9u);
        return (y + ((m <= 2u) ? 1 : 0)) * 10000 + static_cast<int>(m) * 100 +
            static_cast<int>(d);
    }

    int AddDays(int ymd, int delta)
    {
        return YmdFromDays(DaysFromYmd(ymd) + static_cast<long>(delta));
    }

    // 0 = 周日, 1 = 周一 … 6 = 周六（1970-01-01 是周四）
    int WeekdayOf(int ymd)
    {
        int w = static_cast<int>((DaysFromYmd(ymd) + 4L) % 7L);
        if (w < 0) w += 7;
        return w;
    }

    int TodayYmdLocal()
    {
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        return static_cast<int>(st.wYear) * 10000 + static_cast<int>(st.wMonth) * 100 +
            static_cast<int>(st.wDay);
    }

    int MonthFirstDay(int ymd)
    {
        return (ymd / 10000) * 10000 + ((ymd / 100) % 100) * 100 + 1;
    }

    int MonthLastDay(int ymd)
    {
        const int y = ymd / 10000;
        const int m = (ymd / 100) % 100;
        const int ny = (m == 12) ? y + 1 : y;
        const int nm = (m == 12) ? 1 : m + 1;
        return AddDays(ny * 10000 + nm * 100 + 1, -1);
    }

    // 一段时间的「起止」用 MM-DD 表示，回答里标范围用
    std::wstring ShortDate(int ymd)
    {
        if (ymd <= 0) return L"未知";
        wchar_t b[16]{};
        swprintf_s(b, L"%02d-%02d", (ymd / 100) % 100, ymd % 100);
        return b;
    }

    // 问题里出现的时间范围
    struct TimeScope
    {
        bool         active{ false };
        int          from_ymd{ 0 };
        int          to_ymd{ 0 };
        int          span{ 0 };         // 含首尾的天数
        std::wstring label;             // 「上周」「最近 7 天」
    };

    // 问题里有没有明确的时间指代？有就翻成日期区间。
    //
    // 候选词按**长度降序**匹配：「最近一个月」（5 字）必须比「最近」（2 字）先命中，
    // 「上个星期」（4 字）也必须先于「上星期」「上周」，否则会被从中间截断。
    TimeScope ParseTimeScope(const std::wstring& q)
    {
        TimeScope none;

        const int today = TodayYmdLocal();
        if (today <= 0) return none;

        const int wd = WeekdayOf(today);                       // 0 = 周日
        const int this_monday = AddDays(today, -((wd + 6) % 7));   // 本周一
        const int month_first = MonthFirstDay(today);
        const int last_month_last = AddDays(month_first, -1);
        const int last_month_first = MonthFirstDay(last_month_last);
        const int year_first = (today / 10000) * 10000 + 101;
        const int last_year_first = (today / 10000 - 1) * 10000 + 101;
        const int last_year_last = year_first - 1;

        struct Cand
        {
            std::wstring key;
            TimeScope    scope;
        };
        std::vector<Cand> cands;

        auto push = [&](const wchar_t* key, int from, int to, const wchar_t* label)
        {
            TimeScope s;
            s.active = true;
            s.from_ymd = from;
            s.to_ymd = to;
            s.span = static_cast<int>(DaysFromYmd(to) - DaysFromYmd(from)) + 1;
            s.label = label;
            cands.push_back({ key, s });
        };

        push(L"最近一个月", AddDays(today, -29), today, L"最近 30 天");
        push(L"近一个月",   AddDays(today, -29), today, L"最近 30 天");
        push(L"最近两周",   AddDays(today, -13), today, L"最近 14 天");
        push(L"最近14天",   AddDays(today, -13), today, L"最近 14 天");
        push(L"最近一周",   AddDays(today, -6),  today, L"最近 7 天");
        push(L"最近7天",    AddDays(today, -6),  today, L"最近 7 天");
        push(L"最近三天",   AddDays(today, -2),  today, L"最近 3 天");
        push(L"最近3天",    AddDays(today, -2),  today, L"最近 3 天");
        push(L"这几天",     AddDays(today, -6),  today, L"最近 7 天");
        push(L"最近",       AddDays(today, -6),  today, L"最近 7 天");
        push(L"大前天",     AddDays(today, -3),  AddDays(today, -3), L"大前天");
        push(L"前天",       AddDays(today, -2),  AddDays(today, -2), L"前天");
        push(L"昨天",       AddDays(today, -1),  AddDays(today, -1), L"昨天");
        push(L"昨日",       AddDays(today, -1),  AddDays(today, -1), L"昨天");
        push(L"今天",       today, today, L"今天");
        push(L"今日",       today, today, L"今天");
        push(L"上上星期",   AddDays(this_monday, -14), AddDays(this_monday, -8), L"上上周");
        push(L"上上周",     AddDays(this_monday, -14), AddDays(this_monday, -8), L"上上周");
        push(L"上个星期",   AddDays(this_monday, -7),  AddDays(this_monday, -1), L"上周");
        push(L"上星期",     AddDays(this_monday, -7),  AddDays(this_monday, -1), L"上周");
        push(L"上周",       AddDays(this_monday, -7),  AddDays(this_monday, -1), L"上周");
        push(L"这个星期",   this_monday, today, L"本周");
        push(L"本星期",     this_monday, today, L"本周");
        push(L"这周",       this_monday, today, L"本周");
        push(L"本周",       this_monday, today, L"本周");
        push(L"上个月",     last_month_first, last_month_last, L"上个月");
        push(L"上月",       last_month_first, last_month_last, L"上个月");
        push(L"这个月",     month_first, today, L"本月");
        push(L"本月",       month_first, today, L"本月");
        push(L"去年",       last_year_first, last_year_last, L"去年");
        push(L"上年",       last_year_first, last_year_last, L"去年");
        push(L"今年",       year_first, today, L"今年");
        push(L"本年",       year_first, today, L"今年");

        std::stable_sort(cands.begin(), cands.end(),
            [](const Cand& a, const Cand& b) { return a.key.size() > b.key.size(); });

        for (const auto& c : cands)
        {
            if (q.find(c.key) != std::wstring::npos)
                return c.scope;
        }
        return none;
    }

    // 按日期区间切出记录子集（含首尾；0 表示该侧无界）
    std::vector<PlayRecord> SliceByDate(const std::vector<PlayRecord>& all, int from_ymd, int to_ymd)
    {
        std::vector<PlayRecord> out;
        out.reserve(all.size());
        for (const auto& r : all)
        {
            const int y = CStatAnalysis::YmdOf(r.played_at);
            if (y == 0) continue;
            if (from_ymd > 0 && y < from_ymd) continue;
            if (to_ymd > 0 && y > to_ymd) continue;
            out.push_back(r);
        }
        return out;
    }


    // 时段词 → [起始小时, 结束小时)。长词排前面，免得「一大早」被「早上」截走。
    bool ParseHourRange(const std::wstring& q, int& h1, int& h2)
    {
        struct R
        {
            const wchar_t* key;
            int a;
            int b;
        };
        static const R kRanges[] = {
            { L"一大早", 5, 9 },
            { L"凌晨",   0, 6 },
            { L"半夜",   0, 6 },
            { L"深夜",   0, 6 },
            { L"清晨",   5, 8 },
            { L"早晨",   6, 9 },
            { L"早上",   6, 9 },
            { L"上午",   9, 12 },
            { L"中午",   11, 14 },
            { L"下午",   13, 18 },
            { L"傍晚",   17, 19 },
            { L"晚上",   18, 24 },
            { L"夜晚",   18, 24 },
            { L"夜里",   20, 24 },
            { L"白天",   9, 18 },
        };
        for (const auto& r : kRanges)
        {
            if (q.find(r.key) != std::wstring::npos)
            {
                h1 = r.a;
                h2 = r.b;
                return true;
            }
        }
        return false;
    }

    // 星期词 → 掩码（bit0=周一 … bit6=周日）；0 表示问题里没提星期
    int ParseWeekdayMask(const std::wstring& q)
    {
        int mask = 0;
        struct R
        {
            const wchar_t* key;
            int bit;
        };
        static const R kDays[] = {
            { L"周一", 0 }, { L"星期一", 0 }, { L"礼拜一", 0 },
            { L"周二", 1 }, { L"星期二", 1 }, { L"礼拜二", 1 },
            { L"周三", 2 }, { L"星期三", 2 }, { L"礼拜三", 2 },
            { L"周四", 3 }, { L"星期四", 3 }, { L"礼拜四", 3 },
            { L"周五", 4 }, { L"星期五", 4 }, { L"礼拜五", 4 },
            { L"周六", 5 }, { L"星期六", 5 }, { L"礼拜六", 5 },
            { L"周日", 6 }, { L"周天", 6 }, { L"星期日", 6 },
            { L"星期天", 6 }, { L"礼拜日", 6 },
        };
        for (const auto& d : kDays)
        {
            if (q.find(d.key) != std::wstring::npos)
                mask |= (1 << d.bit);
        }
        if (q.find(L"周末") != std::wstring::npos || q.find(L"双休") != std::wstring::npos)
            mask |= (1 << 5) | (1 << 6);
        if (q.find(L"工作日") != std::wstring::npos)
            mask |= 0x1F;
        return mask;
    }

    // WeekdayOf 的 0=周日 转成掩码位
    int WeekdayBit(int wd)
    {
        return (wd == 0) ? 6 : (wd - 1);
    }

namespace AiStatContext
{
    std::wstring BuildSummaryText(const AiStatSnapshot& s, bool allow_song_meta)
    {
        if (!s.Valid() || s.all_records == nullptr || s.all_records->empty())
            return L"（这段时间里没有可统计的播放记录）";

        const std::vector<PlayRecord>& recs = *s.all_records;

        StatSummary sum;
        FinishBreakdown fin;
        int hour[24]{};
        EnsureAggregates(s, sum, fin, hour);

        std::wstring t;
        auto Line = [&t](const std::wstring& line) { t += L"- " + line + L"\n"; };

        // ⚠ 先交代「现在是什么时候、数据记到哪一天」。
        // 模型不知道今天几号的话，用户问「上周」「昨天」「最近」它根本无从定位 ——
        // 之前它只能对着一个总账本瞎猜。
        {
            static const wchar_t* kWeek[] = { L"周日", L"周一", L"周二", L"周三",
                                              L"周四", L"周五", L"周六" };
            const int today = TodayYmdLocal();
            std::wstring head = L"今天 " + CStatAnalysis::FormatYmd(today) +
                L"（" + kWeek[WeekdayOf(today)] + L"）";
            if (s.last_ymd > 0)
                head += L"，数据记到 " + CStatAnalysis::FormatYmd(s.last_ymd);
            Line(head);
            Line(L"用户问「上周」「昨天」「这个月」时，请按上面的日期自己推算区间，"
                 L"下面的逐日/逐周数字可以对照");
        }

        // ⚠ 口径必须写出来，否则模型会按自己的理解解释这些比率
        Line(L"口径：单次播放≥15 秒才计入；完播率＝「播完」占计入次数的比例；"
             L"跳过＝用户主动切走，停止＝播放器停止，出错＝解码失败");

        Line(L"统计范围：全部记录 " + DateText(s.first_ymd) + L" ~ " + DateText(s.last_ymd));
        Line(L"合计：" + Num(fin.total) + L" 次 / " + Duration(sum.total_duration_sec) +
             L" / " + Num(sum.total_songs) + L" 首曲目 / 活跃 " + Num(sum.active_days) + L" 天");
        if (fin.total > 0)
        {
            Line(L"完播率 " + Pct(sum.completed_rate) + L"，跳过率 " + Pct(sum.skip_rate) +
                 L"，平均单次 " + Duration(sum.total_duration_sec / fin.total));
        }

        int peak_hour = -1, peak_cnt = 0;
        for (int h = 0; h < 24; ++h)
        {
            if (hour[h] > peak_cnt) { peak_cnt = hour[h]; peak_hour = h; }
        }
        if (peak_hour >= 0)
        {
            Line(L"最活跃时段 " + Num(peak_hour) + L":00-" + Num(peak_hour) + L":59（" +
                 Num(peak_cnt) + L" 次）");
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
            Line(L"深夜（0-6 点）占 " + Num(sum.night_owl_percent) + L"%，周末占 " +
                 Num(sum.weekend_percent) + L"%");
        Line(L"连续听歌：当前 " + Num(sum.current_streak) + L" 天 / 最长 " +
             Num(sum.longest_streak) + L" 天");

        // ⚠ 时间线 —— 「上周我听了多久」「昨天听了什么」「哪天听得最多」全靠它。
        //    只给周粒度就答不了「昨天」，只给日粒度则几十天太长，所以两者都给：
        //    最近 14 天逐日（精确）+ 逐周概览（看长期起伏）。
        {
            std::vector<PeriodBucket> days = CStatAnalysis::ComputeBuckets(recs, Grain::Day);

            const int today = TodayYmdLocal();
            const int from = AddDays(today, -13);
            std::wstring dl;
            for (const auto& b : days)
            {
                if (b.key < from) continue;
                if (!dl.empty()) dl += L" / ";
                dl += b.label + L" " + Num(b.count) + L" 次 " + Duration(b.duration_sec);
            }
            if (!dl.empty())
                Line(L"最近 14 天逐日：" + dl);

            // 周聚合按「周一起算」，标签写成日期区间 —— 比 "2026-W36" 好读，
            // 模型也不用再去换算那是几月几号
            struct WeekAgg
            {
                int first_ymd{ 0 };
                int last_ymd{ 0 };
                int count{ 0 };
                int duration_sec{ 0 };
            };
            std::vector<WeekAgg> weeks;
            for (const auto& b : days)
            {
                const int monday = AddDays(b.key, -((WeekdayOf(b.key) + 6) % 7));
                if (weeks.empty() || weeks.back().first_ymd != monday)
                {
                    WeekAgg w;
                    w.first_ymd = monday;
                    weeks.push_back(w);
                }
                WeekAgg& w = weeks.back();
                w.last_ymd = b.key;
                w.count += b.count;
                w.duration_sec += b.duration_sec;
            }
            if (!weeks.empty())
            {
                const size_t skip = (weeks.size() > 14) ? (weeks.size() - 14) : 0;
                std::wstring wl;
                for (size_t i = skip; i < weeks.size(); ++i)
                {
                    if (!wl.empty()) wl += L" / ";
                    wl += ShortDate(weeks[i].first_ymd) + L"~" + ShortDate(weeks[i].last_ymd) +
                        L" " + Num(weeks[i].count) + L" 次";
                }
                Line(L"逐周（近 14 周，周一起算）：" + wl);
            }
        }

        // ⚠ 榜单给到 10 名（原来只给 5 名，问「第 8 名是谁」「还有谁」就没得答）。
        //    标题里写明排序依据：歌手/专辑按时长、曲目按次数。
        //    不写的话模型会默认都按次数，据此算出来的「占比」就错了。
        std::vector<ArtistRankItem> artists = CStatAnalysis::ComputeArtistRank(recs, 10);
        std::vector<AlbumRankItem> albums = CStatAnalysis::ComputeAlbumRank(recs, 10);
        std::vector<SongRankItem> songs = CStatAnalysis::ComputeSongRank(recs, 10);

        if (!artists.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < artists.size() && i < 10; ++i)
            {
                if (i > 0) line += L" / ";
                line += ArtistLabel(artists[i].artist, allow_song_meta) + L" " +
                    Duration(artists[i].duration_sec) + L"（" + Num(artists[i].count) + L" 次）";
            }
            Line(L"歌手排行（按时长）：" + line);
        }
        if (!albums.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < albums.size() && i < 10; ++i)
            {
                if (i > 0) line += L" / ";
                line += (allow_song_meta ? albums[i].album : L"（专辑名已隐藏）") + L" " +
                    Duration(albums[i].duration_sec);
            }
            Line(L"专辑排行（按时长）：" + line);
        }
        if (!songs.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < songs.size() && i < 10; ++i)
            {
                if (i > 0) line += L" / ";
                line += SongLabel(songs[i].title, allow_song_meta) + L"（" +
                    Num(songs[i].count) + L" 次）";
            }
            Line(L"曲目排行（按次数）：" + line);
        }

        Line(L"行为分解：播完 " + Num(fin.completed) + L" / 跳过 " + Num(fin.skipped) +
             L" / 停止 " + Num(fin.stopped) + L" / 出错 " + Num(fin.errored));

        std::vector<RetiredGem> gems = CStatAnalysis::ComputeRetiredGems(recs, 3);
        if (!gems.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < gems.size() && i < 5; ++i)
            {
                if (i > 0) line += L" / ";
                line += SongLabel(gems[i].title, allow_song_meta) + L"（听了 " +
                    Num(gems[i].count) + L" 遍）";
            }
            Line(L"遗珠（反复点开却没听完）：" + line);
        }

        Line(L"只听过 1 次的曲子 " + Num(sum.one_hit_wonders) + L" 首，听过 5 次以上的 " +
             Num(sum.repeat_depth) + L" 首");

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

    // ── 本地档的「事实清单」 ──
    //
    // 以前本地档是「关键词命中就返回某一条、一条都没命中就甩总览」，所以动不动就答非所问。
    // 现在改成：**先把数据算成一堆现成的人话**（每条都带具体数字，有的还带一句判断），
    // 再按问题挑最相关的几条拼起来。好处是问什么都能答上，也不会拿无关内容硬凑。
    // 只针对 recs 这一批记录构造事实 —— 调用方可能已经把范围缩到「上周」了。
    std::vector<LocalFact> BuildLocalFacts(const std::vector<PlayRecord>& recs, bool allow_song_meta,
        const std::wstring& question)
    {
        std::vector<LocalFact> f;
        if (recs.empty())
            return f;

        StatSummary sum;
        FinishBreakdown fin;
        int hour[24]{};
        AggregateOf(recs, sum, fin, hour);

        // 总览
        {
            std::wstring t = L"这段时间一共听了 " + Num(fin.total) + L" 次、" +
                Duration(sum.total_duration_sec) + L"，涉及 " + Num(sum.total_songs) +
                L" 首曲子，活跃 " + Num(sum.active_days) + L" 天。";
            f.push_back({ t, { L"总", L"概", L"多少", L"统计", L"数据", L"次数", L"时长", L"整体" }, 60 });
        }

        // 歌手
        {
            std::vector<ArtistRankItem> r = CStatAnalysis::ComputeArtistRank(recs, 3);
            if (!r.empty())
            {
                std::wstring t = L"听得最多的歌手是 " + ArtistLabel(r[0].artist, allow_song_meta) +
                    L"，" + Num(r[0].count) + L" 次、" + Duration(r[0].duration_sec) + L"。";
                if (r.size() > 1 && r[1].count > 0)
                {
                    if (r[0].count >= r[1].count * 2)
                        t += L"比第二名「" + ArtistLabel(r[1].artist, allow_song_meta) +
                             L"」多出一倍还多，听得相当集中。";
                    else
                        t += L"第二名是 " + ArtistLabel(r[1].artist, allow_song_meta) +
                             L"（" + Num(r[1].count) + L" 次），咬得挺紧。";
                }
                f.push_back({ t, { L"歌手", L"谁", L"最爱", L"喜欢", L"唱" }, 85 });
            }
        }

        // 专辑
        {
            std::vector<AlbumRankItem> r = CStatAnalysis::ComputeAlbumRank(recs, 3);
            if (!r.empty())
            {
                std::wstring t = L"听得最多的专辑是《" + (allow_song_meta ? r[0].album : L"已隐藏") +
                    L"》，" + Num(r[0].count) + L" 次、" + Duration(r[0].duration_sec) + L"。";
                f.push_back({ t, { L"专辑", L"唱片" }, 70 });
            }
        }

        // 曲目
        {
            std::vector<SongRankItem> r = CStatAnalysis::ComputeSongRank(recs, 3);
            if (!r.empty())
            {
                std::wstring t = L"播得最多的是《" + SongLabel(r[0].title, allow_song_meta) +
                    L"》，" + Num(r[0].count) + L" 次、" + Duration(r[0].duration_sec) + L"。";
                if (r.size() > 1)
                    t += L"紧随其后的是《" + SongLabel(r[1].title, allow_song_meta) +
                         L"》（" + Num(r[1].count) + L" 次）。";
                f.push_back({ t, { L"曲目", L"哪首", L"歌名", L"单曲", L"最常听", L"最多" }, 80 });
            }
        }

        // 时段
        {
            int peak = -1, peak_cnt = 0;
            for (int h = 0; h < 24; ++h)
                if (hour[h] > peak_cnt) { peak_cnt = hour[h]; peak = h; }
            if (peak >= 0 && peak_cnt > 0)
            {
                std::wstring t = L"最常在 " + Num(peak) + L":00-" + Num(peak) + L":59 听，这一段有 " +
                    Num(peak_cnt) + L" 次。";
                if (sum.night_owl_percent > 0)
                    t += L"深夜（0-6 点）占了全部时长的 " + Pct(sum.night_owl_percent) + L"。";
                if (sum.weekend_percent > 0)
                    t += L"周末贡献了 " + Pct(sum.weekend_percent) + L" 的播放次数。";
                f.push_back({ t, { L"时段", L"几点", L"什么时候", L"晚上", L"深夜", L"凌晨", L"周末", L"白天" }, 75 });
            }
        }

        // 完播 / 跳过
        {
            if (fin.total > 0)
            {
                std::wstring t = L"完整听完 " + Num(fin.completed) + L" 次（" +
                    Pct(fin.completed * 100.0 / fin.total) + L"），中途切走 " + Num(fin.skipped) +
                    L" 次（" + Pct(fin.skipped * 100.0 / fin.total) + L"）。";
                if (sum.completed_rate >= 70.0)
                    t += L"完播率挺高，听歌不怎么跳。";
                else if (sum.skip_rate >= 40.0)
                    t += L"跳过偏多，歌单里可能有几首不太合口味。";
                f.push_back({ t, { L"完播", L"跳过", L"切歌", L"没听完", L"听完整", L"听完" }, 70 });
            }
        }

        // 遗珠
        {
            std::vector<RetiredGem> g = CStatAnalysis::ComputeRetiredGems(recs, 3);
            if (!g.empty())
            {
                std::wstring t = L"有 " + Num(static_cast<int>(g.size())) +
                    L" 首是反复点开、却一次都没听完的：";
                for (size_t i = 0; i < g.size() && i < 5; ++i)
                    t += L"\n· " + SongLabel(g[i].title, allow_song_meta) + L"（点开 " +
                         Num(g[i].count) + L" 遍）";
                f.push_back({ t, { L"遗珠", L"没听完", L"反复", L"可惜", L"浪费" }, 65 });
            }
        }

        // 连续天数
        {
            std::wstring t = L"已经连续 " + Num(sum.current_streak) + L" 天听歌，最长纪录 " +
                Num(sum.longest_streak) + L" 天。";
            const int miss = CStatAnalysis::ComputeStreakMiss(recs);
            if (miss > 0)
                t += L"中间断过最长的一段是 " + Num(miss) + L" 天。";
            f.push_back({ t, { L"连续", L"纪录", L"记录", L"天数", L"坚持", L"断了" }, 65 });
        }

        // 周期对比
        {
            StatFilter all_filter;
            all_filter.preset = RangePreset::All;
            PeriodComparison pc = CStatAnalysis::ComputePeriodComparison(recs, all_filter);
            if (pc.has_previous)
            {
                std::wstring t;
                if (pc.count_delta > 0)
                    t = L"比上一个周期多听了 " + Num(pc.count_delta) + L" 次（" +
                        Pct(pc.count_delta_percent) + L"）。";
                else if (pc.count_delta < 0)
                    t = L"比上一个周期少听了 " + Num(-pc.count_delta) + L" 次（" +
                        Pct(pc.count_delta_percent) + L"）。";
                else
                    t = L"跟上一个周期听得一样多。";
                f.push_back({ t, { L"变化", L"比", L"趋势", L"多了", L"少了", L"对比", L"最近" }, 60 });
            }
        }

        // 新歌发现
        {
            std::vector<PeriodBucket> news = CStatAnalysis::ComputeNewSongTrend(recs);
            if (!news.empty())
            {
                const PeriodBucket* best = &news.front();
                for (const auto& b : news)
                    if (b.count > best->count) best = &b;
                if (best->count > 0)
                {
                    std::wstring t = L"新歌听得最多的是 " + best->label + L"，那个月新听了 " +
                        Num(best->count) + L" 首。";
                    f.push_back({ t, { L"新歌", L"新听", L"刚听", L"发现" }, 55 });
                }
            }
        }

        // ───────── 以下三类都要看问题才知道要不要生成 ─────────

        // 【交叉】星期 × 时段。
        // 「周末深夜听歌多吗」只报峰值小时是答不了问题的 —— 必须把两个维度交叉起来，
        // 而且得跟对照组（工作日／其余几天）比一下，才谈得上「多」还是「不多」。
        {
            int h1 = 0, h2 = 0;
            const int mask = ParseWeekdayMask(question);
            if (mask != 0 && ParseHourRange(question, h1, h2))
            {
                int other_mask = (~mask) & 0x7F;
                if (mask == ((1 << 5) | (1 << 6)))
                    other_mask = 0x1F;                          // 周末 → 工作日
                else if (mask == 0x1F)
                    other_mask = (1 << 5) | (1 << 6);           // 工作日 → 周末

                static const wchar_t* kNames[] = { L"周一", L"周二", L"周三", L"周四",
                                                   L"周五", L"周六", L"周日" };
                // ⚠ 周末的掩码是 (1<<5)|(1<<6)，并不等于任何单独一位，
                // 所以这两种组合必须先判掉，否则会落到「这几天」的兜底上，
                // 输出成「这几天的 0:00-5:59 …对照工作日」，主语都丢了。
                std::wstring a_name = L"这几天";
                if (mask == ((1 << 5) | (1 << 6)))
                {
                    a_name = L"周末";
                }
                else if (mask == 0x1F)
                {
                    a_name = L"工作日";
                }
                else
                {
                    for (int b = 0; b < 7; ++b)
                    {
                        if (mask == (1 << b))
                            a_name = kNames[b];
                    }
                }
                std::wstring b_name = L"其余几天";
                if (other_mask == 0x1F)                b_name = L"工作日";
                else if (other_mask == ((1 << 5) | (1 << 6))) b_name = L"周末";

                int a_cnt = 0, a_sec = 0, a_base = 0;
                int b_cnt = 0, b_sec = 0, b_base = 0;
                for (const auto& r : recs)
                {
                    const int ymd = CStatAnalysis::YmdOf(r.played_at);
                    const int hh = CStatAnalysis::HourOf(r.played_at);
                    if (ymd == 0 || hh < 0) continue;
                    const int bit = 1 << WeekdayBit(WeekdayOf(ymd));
                    const bool in_range = (hh >= h1 && hh < h2);
                    if (mask & bit)
                    {
                        a_base += r.play_duration_sec;
                        if (in_range) { a_cnt++; a_sec += r.play_duration_sec; }
                    }
                    else if (other_mask & bit)
                    {
                        b_base += r.play_duration_sec;
                        if (in_range) { b_cnt++; b_sec += r.play_duration_sec; }
                    }
                }
                if (a_base > 0 && b_base > 0)
                {
                    const double ap = a_sec * 100.0 / a_base;
                    const double bp = b_sec * 100.0 / b_base;
                    std::wstring verdict;
                    if (bp > 0.0 && ap > bp * 1.5)
                        verdict = a_name + L"这个时段确实听得多不少。";
                    else if (ap > 0.0 && bp > ap * 1.5)
                        verdict = L"反而是" + b_name + L"在同一时段听得多些。";
                    else
                        verdict = L"两边差别不大。";

                    std::wstring t = a_name + L"的 " + Num(h1) + L":00-" + Num(h2 - 1) +
                        L":59 一共 " + Num(a_cnt) + L" 次、" + Duration(a_sec) + L"，占" +
                        a_name + L"总时长的 " + Pct(ap) + L"；" + b_name + L"在同一时段 " +
                        Num(b_cnt) + L" 次、占 " + Pct(bp) + L" —— " + verdict;
                    f.push_back({ t, { L"周末", L"工作日", L"平时", L"深夜", L"凌晨",
                                       L"早上", L"上午", L"中午", L"下午", L"晚上",
                                       L"白天", L"周一", L"周二", L"周三", L"周四",
                                       L"周五", L"周六", L"周日" }, 95, true });
                }
            }
        }

        // 【否定】问「不喜欢谁 / 很少听谁」时，给的是**听得最少**的几位。
        //
        // 以前一律返回「听得最多」，问「不太喜欢听谁」答「你最爱薛之谦」，
        // 语义正好反了 —— 这是最扎眼的一种答非所问。
        {
            // 注意「不太喜欢」：中间隔了个「太」，字面上并不包含「不喜欢」，
            // 所以必须单列 —— 这是复验时真踩到的。
            const bool negate =
                question.find(L"不喜欢") != std::wstring::npos ||
                question.find(L"不太喜欢") != std::wstring::npos ||
                question.find(L"不爱听") != std::wstring::npos ||
                question.find(L"不太爱") != std::wstring::npos ||
                question.find(L"不爱") != std::wstring::npos ||
                question.find(L"讨厌") != std::wstring::npos ||
                question.find(L"最少") != std::wstring::npos ||
                question.find(L"不怎么听") != std::wstring::npos ||
                question.find(L"没怎么听") != std::wstring::npos ||
                question.find(L"不常") != std::wstring::npos ||
                question.find(L"很少") != std::wstring::npos;
            if (negate)
            {
                std::vector<ArtistRankItem> all_art =
                    CStatAnalysis::ComputeArtistRank(recs, 100000);
                if (all_art.size() >= 3)
                {
                    std::wstring names;
                    for (size_t k = 0; k < 3; ++k)
                    {
                        const ArtistRankItem& a = all_art[all_art.size() - 1 - k];
                        if (!names.empty()) names += L"、";
                        names += ArtistLabel(a.artist, allow_song_meta) + L"（" +
                            Num(a.count) + L" 次）";
                    }
                    std::wstring t = L"在「你听过的」范围里，听得最少的是 " + names +
                        L" —— 基本点开就切了。（数据里只有你播放过的歌手，"
                        L"完全没听过的歌手不会出现）";
                    f.push_back({ t, { L"不喜欢", L"不太喜欢", L"不爱听", L"不太爱",
                                       L"不爱", L"讨厌", L"最少", L"不怎么听",
                                       L"没怎么听", L"不常", L"很少", L"不想听" }, 95, true });
                }
            }
        }

        // 【排除】「除了 A 我还听谁」→ 先把 A 从数据里摘掉，再看谁排第一
        {
            if (question.find(L"除了") != std::wstring::npos ||
                question.find(L"除开") != std::wstring::npos ||
                question.find(L"不算") != std::wstring::npos)
            {
                std::vector<ArtistRankItem> all_art =
                    CStatAnalysis::ComputeArtistRank(recs, 300);
                std::wstring excluded;
                std::vector<PlayRecord> rest;
                for (const auto& a : all_art)
                {
                    if (a.artist.empty()) continue;
                    if (question.find(a.artist) == std::wstring::npos) continue;
                    excluded = ArtistLabel(a.artist, allow_song_meta);
                    for (const auto& r : recs)
                    {
                        if (r.artist != a.artist)
                            rest.push_back(r);
                    }
                    break;                          // 只处理第一个匹配上的
                }
                // 问题里未必写了名字，也可能说「除了听最多的那个」——
                // 这时候直接排除榜首。复验时「除了听最多的那个歌手」就是这样漏掉的。
                if (excluded.empty() && !all_art.empty())
                {
                    const bool by_ref =
                        question.find(L"最多") != std::wstring::npos ||
                        question.find(L"第一") != std::wstring::npos ||
                        question.find(L"榜首") != std::wstring::npos ||
                        question.find(L"最爱") != std::wstring::npos ||
                        question.find(L"最喜欢") != std::wstring::npos;
                    if (by_ref)
                    {
                        excluded = ArtistLabel(all_art[0].artist, allow_song_meta);
                        for (const auto& r : recs)
                        {
                            if (r.artist != all_art[0].artist)
                                rest.push_back(r);
                        }
                    }
                }

                if (!excluded.empty() && !rest.empty())
                {
                    std::vector<ArtistRankItem> r2 = CStatAnalysis::ComputeArtistRank(rest, 3);
                    if (!r2.empty())
                    {
                        std::wstring names;
                        for (size_t k = 0; k < r2.size() && k < 3; ++k)
                        {
                            if (!names.empty()) names += L"、";
                            names += ArtistLabel(r2[k].artist, allow_song_meta) + L"（" +
                                Duration(r2[k].duration_sec) + L"）";
                        }
                        std::wstring t = L"把「" + excluded + L"」去掉之后，你听得最多的是 " +
                            names + L"。";
                        f.push_back({ t, { L"除了", L"除开", L"不算", L"还有谁",
                                           L"别的", L"其它", L"其他" }, 95, true });
                    }
                }
            }
        }

        // 【常备】单日之最 —— 快捷提问里就有「我听得最久的一天是哪天」，
        // 原来事实清单里根本没有对应项，问它必然兜底。
        {
            std::vector<PeriodBucket> days = CStatAnalysis::ComputeBuckets(recs, Grain::Day);
            if (!days.empty())
            {
                const PeriodBucket* by_count = &days.front();
                const PeriodBucket* by_time = &days.front();
                for (const auto& b : days)
                {
                    if (b.count > by_count->count) by_count = &b;
                    if (b.duration_sec > by_time->duration_sec) by_time = &b;
                }
                std::wstring t = L"听得最久的一天是 " + by_time->label + L"（" +
                    Duration(by_time->duration_sec) + L"、" + Num(by_time->count) + L" 次）";
                if (by_count != by_time)
                    t += L"；次数最多的是 " + by_count->label + L"（" +
                         Num(by_count->count) + L" 次），不是同一天";
                t += L"。";
                t += L"平均每个听歌的日子听 " +
                     Duration(sum.total_duration_sec / static_cast<int>(days.size())) + L"。";
                f.push_back({ t, { L"哪天", L"最久", L"最多的一天", L"单日",
                                   L"最长", L"一天", L"最高" }, 72 });
            }
        }

        // 【常备】本月 vs 上月 —— 快捷提问里的「这个月比上个月听得多吗」靠它
        {
            const int today = TodayYmdLocal();
            const int mf = MonthFirstDay(today);
            const int lm_last = AddDays(mf, -1);
            const int lm_first = MonthFirstDay(lm_last);

            int c1 = 0, s1 = 0, c2 = 0, s2 = 0;
            for (const auto& r : recs)
            {
                const int y = CStatAnalysis::YmdOf(r.played_at);
                if (y >= mf && y <= today)          { c1++; s1 += r.play_duration_sec; }
                else if (y >= lm_first && y <= lm_last) { c2++; s2 += r.play_duration_sec; }
            }
            if (c1 > 0 || c2 > 0)
            {
                std::wstring t = L"本月到目前 " + Num(c1) + L" 次（" + Duration(s1) +
                    L"），上个月整月 " + Num(c2) + L" 次（" + Duration(s2) + L"）。";
                if (c2 > 0)
                {
                    if (c1 > c2)
                        t += L"本月已经超过上月了。";
                    else if (c1 * 2 < c2)
                        t += L"按目前的节奏，本月大概比不上上月。";
                    else
                        t += L"跟上月差不多。";
                }
                f.push_back({ t, { L"这个月", L"本月", L"上个月", L"上月", L"比上",
                                   L"月度", L"这月" }, 78 });
            }
        }

        // 【完播】「有哪些歌我从来不跳过」—— 要的是「每次点开都听完」的曲子清单，
        // 原来会命中一条泛泛的完播率事实，等于没回答。
        {
            const bool never_skip =
                question.find(L"从不") != std::wstring::npos ||
                question.find(L"从没") != std::wstring::npos ||
                question.find(L"从来不跳") != std::wstring::npos ||
                question.find(L"一直听完") != std::wstring::npos ||
                question.find(L"每次都听完") != std::wstring::npos;
            if (never_skip)
            {
                struct Agg
                {
                    std::wstring title;
                    int total{ 0 };
                    int done{ 0 };
                };
                std::map<std::wstring, Agg> m;
                for (const auto& r : recs)
                {
                    if (r.title.empty()) continue;
                    Agg& a = m[r.title];
                    a.title = r.title;
                    a.total++;
                    if (r.finish_reason == PlayRecord::FinishReason::COMPLETED)
                        a.done++;
                }
                std::vector<Agg> full;
                for (const auto& kv : m)
                {
                    if (kv.second.total >= 3 && kv.second.total == kv.second.done)
                        full.push_back(kv.second);
                }
                std::stable_sort(full.begin(), full.end(),
                    [](const Agg& x, const Agg& y) { return x.total > y.total; });

                const std::vector<std::wstring> keys = { L"从不", L"从没", L"从来不跳",
                                                         L"每次都听完", L"一次都没跳" };
                if (full.empty())
                {
                    f.push_back({ L"没有「每次点开都听完」的曲子 —— 听 3 次以上的里面，"
                                  L"每一条都至少跳过一次。", keys, 92, true });
                }
                else
                {
                    std::wstring t = L"听 3 次以上、每次都完整听完的有 " +
                        Num(static_cast<int>(full.size())) + L" 首，最常听的是：";
                    for (size_t i = 0; i < full.size() && i < 5; ++i)
                    {
                        t += L"\n· " + SongLabel(full[i].title, allow_song_meta) +
                             L"（" + Num(full[i].total) + L" 次全听完）";
                    }
                    f.push_back({ t, keys, 92, true });
                }
            }
        }

        // 【常备】工作日 vs 周末。
        // 直接比总量是不公平的（一周里工作日 5 天、周末才 2 天），所以按**活跃日平均**比。
        {
            int wd_cnt = 0, wd_sec = 0, we_cnt = 0, we_sec = 0;
            std::set<int> wd_days, we_days;
            for (const auto& r : recs)
            {
                const int y = CStatAnalysis::YmdOf(r.played_at);
                if (y == 0) continue;
                const int wd = WeekdayOf(y);
                if (wd == 0 || wd == 6)
                {
                    we_cnt++;
                    we_sec += r.play_duration_sec;
                    we_days.insert(y);
                }
                else
                {
                    wd_cnt++;
                    wd_sec += r.play_duration_sec;
                    wd_days.insert(y);
                }
            }
            if (wd_cnt > 0 && we_cnt > 0 && !wd_days.empty() && !we_days.empty())
            {
                const double wd_avg = wd_sec / static_cast<double>(wd_days.size());
                const double we_avg = we_sec / static_cast<double>(we_days.size());
                std::wstring t = L"工作日合计 " + Num(wd_cnt) + L" 次、" + Duration(wd_sec) +
                    L"；周末 " + Num(we_cnt) + L" 次、" + Duration(we_sec) + L"。";
                t += L"按活跃日平均：工作日每天 " + Duration(static_cast<int>(wd_avg)) +
                     L"，周末每天 " + Duration(static_cast<int>(we_avg));
                if (we_avg > wd_avg * 1.2)
                    t += L" —— 周末明显听得多。";
                else if (wd_avg > we_avg * 1.2)
                    t += L" —— 工作日反而听得多。";
                else
                    t += L" —— 两者差不多。";
                f.push_back({ t, { L"周末", L"工作日", L"平时", L"上班", L"双休" }, 76, true });
            }
        }

        return f;
    }

    std::wstring BuildLocalAnswer(const AiStatSnapshot& s, const std::wstring& question, bool allow_song_meta)
    {
        if (!s.Valid() || s.all_records == nullptr || s.all_records->empty())
            return L"这段时间里还没有可统计的播放记录。先去听几首歌，回来再问。";

        // ① 先看问题有没有指定时间范围（「上周」「昨天」「最近」…）。
        //
        // 以前不管问什么都拿全部数据作答，所以「上周我听了多久」会得到三个月的总数 ——
        // 这是本地档最容易被察觉的答非所问。现在先在范围内算，并在回答开头写明范围。
        const TimeScope scope = ParseTimeScope(question);
        std::vector<PlayRecord> sliced;
        const std::vector<PlayRecord>* src = s.all_records;
        bool scoped = false;                    // 是否真的按这个范围作答
        if (scope.active)
        {
            sliced = SliceByDate(*s.all_records, scope.from_ymd, scope.to_ymd);
            if (sliced.empty())
            {
                // 这一段确实没听。但先确认问题**本来是问数据**——
                // 「我今天心情不太好」这种闲聊也会命中「今天」，
                // 直接回「今天没有播放记录」就变成答非所问了。
                const bool stat_intent =
                    question.find(L"听") != std::wstring::npos ||
                    question.find(L"歌") != std::wstring::npos ||
                    question.find(L"播放") != std::wstring::npos ||
                    question.find(L"唱") != std::wstring::npos ||
                    question.find(L"统计") != std::wstring::npos ||
                    question.find(L"次数") != std::wstring::npos ||
                    question.find(L"时长") != std::wstring::npos ||
                    question.find(L"专辑") != std::wstring::npos ||
                    question.find(L"歌手") != std::wstring::npos ||
                    question.find(L"完播") != std::wstring::npos ||
                    question.find(L"跳过") != std::wstring::npos;

                if (stat_intent)
                {
                    // 别拿别的时间段硬凑，说清楚，并告诉他最近一次是什么时候
                    int last = 0;
                    for (const auto& r : *s.all_records)
                    {
                        const int y = CStatAnalysis::YmdOf(r.played_at);
                        if (y > last) last = y;
                    }
                    std::wstring a = scope.label + L"（" + ShortDate(scope.from_ymd) + L" ~ " +
                        ShortDate(scope.to_ymd) + L"）这段时间没有播放记录。";
                    if (last > 0)
                        a += L"\n你最后一次听歌是 " + ShortDate(last) + L"。";
                    a += L"\n想看整体情况的话，把时间词去掉再问一次就行。";
                    return a;
                }
                // 没有统计意图 —— 忽略这次时间指代，落回全量照常作答
            }
            else
            {
                src = &sliced;
                scoped = true;
            }
        }

        const std::vector<LocalFact> facts = BuildLocalFacts(*src, allow_song_meta, question);
        if (facts.empty())
            return L"数据还太少，暂时没什么可说的。多听几首再来问。";

        // ② 按问题里的关键词打分：命中词越多分越高，同分时重要的排前面
        struct Scored
        {
            const LocalFact* f{ nullptr };
            int score{ 0 };
        };
        std::vector<Scored> hits;
        hits.reserve(facts.size());
        for (const auto& x : facts)
        {
            int sc = 0;
            for (const auto& k : x.keys)
            {
                if (question.find(k) != std::wstring::npos)
                    sc += 10;
            }
            if (sc > 0)
                sc += x.weight / 10;
            hits.push_back({ &x, sc });
        }
        std::stable_sort(hits.begin(), hits.end(),
            [](const Scored& a, const Scored& b) { return a.score > b.score; });

        // 指定了范围就先把这个说清楚，免得用户以为答的还是全部
        std::wstring head;
        if (scoped)
            head = L"（范围：" + scope.label + L" " + ShortDate(scope.from_ymd) + L" ~ " +
                   ShortDate(scope.to_ymd) + L"）\n\n";

        if (hits[0].score > 0)
        {
            // 交叉 / 否定 / 排除这类事实本身就是一个完整答案，拼上别的事实只会把话题带偏。
            //
            // ⚠ 必须**遍历**命中列表，不能只看 hits[0]：hits 是按分数降序的，而泛化事实
            // （比如「歌手」）的关键词命中数往往更多 —— 问题里出现「歌手」「谁」都能加 10 分，
            // 于是它排在「排除」前面，只看首位就永远轮不到 exclusive。
            // 就是靠复验里「除了听最多的那个歌手」那题才发现的。
            for (const auto& h : hits)
            {
                if (h.score <= 0)
                    break;
                if (h.f->exclusive)
                    return head + h.f->text;
            }

            std::wstring a = head;
            bool first = true;
            for (size_t i = 0; i < hits.size() && i < 3; ++i)
            {
                if (hits[i].score <= 0)
                    break;
                if (!first)
                    a += L"\n\n";
                a += hits[i].f->text;
                first = false;
            }
            return a;
        }

        // 一条都没命中：别硬塞无关内容，先说清没对上，再给最有价值的几条
        std::wstring a = head + L"这句话我没找到能对上的角度，先把最要紧的几条给你：";
        {
            std::vector<const LocalFact*> by_weight;
            by_weight.reserve(facts.size());
            for (const auto& x : facts)
                by_weight.push_back(&x);
            std::stable_sort(by_weight.begin(), by_weight.end(),
                [](const LocalFact* x, const LocalFact* y) { return x->weight > y->weight; });
            for (size_t i = 0; i < by_weight.size() && i < 3; ++i)
                a += L"\n· " + by_weight[i]->text;
        }
        a += L"\n\n换个说法再问也行，或者直接点下面的快捷提问。";
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
            L"我周末听歌多吗",
            // 下面几条是为了把本轮新加的能力「亮出来」—— 用户不知道能这么问，
            // 再好的路由也没机会生效
            L"上周我听了多久",
            L"我工作日和周末听歌有区别吗",
            L"除了听得最多的歌手，我还听谁",
            L"我很少听谁的歌"
        };
        return pool;
    }
}

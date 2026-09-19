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
    // 问题里有没有明确的时间指代？有就翻成日期区间。
    //
    // 候选词按**长度降序**匹配：「最近一个月」（5 字）必须比「最近」（2 字）先命中，
    // 「上个星期」（4 字）也必须先于「上星期」「上周」，否则会被从中间截断。
    // 「9月15号」「09月15日」→ 月、日。「号 / 日」都认
    bool ParseMonthDay(const std::wstring& q, int& m, int& d)
    {
        const size_t p = q.find(L"月");
        if (p == std::wstring::npos || p == 0) return false;
        size_t i = p;
        while (i > 0 && q[i - 1] >= L'0' && q[i - 1] <= L'9') --i;
        if (i == p) return false;
        const int mm = _wtoi(q.substr(i, p - i).c_str());
        size_t j = p + 1;
        while (j < q.size() && q[j] != L'号' && q[j] != L'日' &&
               (q[j] < L'0' || q[j] > L'9')) ++j;
        if (j >= q.size() || q[j] == L'月') return false;
        const int dd = _wtoi(q.c_str() + j);
        if (mm < 1 || mm > 12 || dd < 1 || dd > 31) return false;
        m = mm;
        d = dd;
        return true;
    }

    // 「15号」「15日」→ 日（前面紧挨着数字才算，免得「星期日」误触）
    bool ParseDayOnly(const std::wstring& q, int& d)
    {
        for (size_t k = 1; k < q.size(); ++k)
        {
            if ((q[k] == L'号' || q[k] == L'日') &&
                q[k - 1] >= L'0' && q[k - 1] <= L'9')
            {
                size_t i = k;
                while (i > 0 && q[i - 1] >= L'0' && q[i - 1] <= L'9') --i;
                const int dd = _wtoi(q.substr(i, k - i).c_str());
                if (dd >= 1 && dd <= 31) { d = dd; return true; }
                return false;
            }
        }
        return false;
    }

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

        // ── 具体日期：「9月15号」「15号都听过什么歌」 ──
        //
        // 以前完全解析不了这类问法，直接掉进兜底，用户看到的就是一句没头没脑的短回答。
        // 「N号」= 最近过去的那个 N 号（当月还没到就算上个月的）；
        // 「X月N号」先算今年，还没到的算去年。
        {
            int m = 0, d = 0;
            if (ParseMonthDay(q, m, d))
            {
                const int y = today / 10000;
                int ymd = y * 10000 + m * 100 + d;
                if (ymd > today)
                    ymd = (y - 1) * 10000 + m * 100 + d;
                push(L"__specific_md", ymd, ymd, ShortDate(ymd).c_str());
            }
            else if (ParseDayOnly(q, d))
            {
                int yy = today / 10000;
                int mm = (today / 100) % 100;
                int ymd = yy * 10000 + mm * 100 + d;
                if (ymd > today)
                {
                    if (--mm < 1) { mm = 12; --yy; }
                    ymd = yy * 10000 + mm * 100 + d;
                }
                push(L"__specific_d", ymd, ymd, ShortDate(ymd).c_str());
            }
        }

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

            // ⚠ 别让模型自己推算日期 —— 实测它会推错（把「昨天」算成四天前）。
            // 常用区间直接给对照表，它照抄就行。
            const int wd = WeekdayOf(today);
            const int monday = AddDays(today, -((wd + 6) % 7));
            Line(L"日期对照：昨天 " + CStatAnalysis::FormatYmd(AddDays(today, -1)) +
                 L"，前天 " + CStatAnalysis::FormatYmd(AddDays(today, -2)) +
                 L"，本周一 " + CStatAnalysis::FormatYmd(monday) +
                 L"，上周 " + CStatAnalysis::FormatYmd(monday - 7) +
                 L" ~ " + CStatAnalysis::FormatYmd(monday - 1));
            if (s.last_ymd > 0 && s.last_ymd < today)
                Line(L"注意：数据只记到 " + CStatAnalysis::FormatYmd(s.last_ymd) +
                     L"，之后的日期还没有记录，被问到就直说，别拿别的日子硬凑");
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
            // 24 小时全给（含 0 次的），不再截断 ——
            // 以前这里超过 200 字符就 break，后面的小时直接丢了，模型看到的是**残缺分布**，
            // 于是问「下午听得多吗」「凌晨呢」这种就只能瞎猜。
            std::wstring dist;
            for (int h = 0; h < 24; ++h)
            {
                if (h > 0) dist += L" ";
                dist += Num(h) + L"点" + Num(hour[h]);
            }
            Line(L"时段分布（24 小时全量）：" + dist);
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

        // 下面这几个字段以前一直闲置（算了没用），本地档已经启用，模型档也得给 ——
        // 不给的话「我听得专一吗」「我是不是喜新厌旧」这类问题模型只能编。
        if (sum.inflation_percent > 0)
        {
            Line(L"收听集中度：最常听的 10 首占全部播放 " + Num(sum.inflation_percent) +
                 L"%（越高＝越专一，越低＝越杂食）");
        }
        if (sum.explore_percent > 0)
            Line(L"探索型占比 " + Num(sum.explore_percent) + L"%（只听过一次的曲目占比）");
        if (sum.new_songs_month > 0)
            Line(L"本月第一次听的新曲 " + Num(sum.new_songs_month) + L" 首");

        return t;
    }

    std::wstring BuildRawRecordsText(const AiStatSnapshot& s, int max_rows, bool allow_song_meta,
        int from_ymd, int to_ymd)
    {
        if (s.all_records == nullptr || s.all_records->empty() || max_rows <= 0)
            return L"";

        // 先按日期筛，再从**最新**往回取 ——
        // 以前是从头（最早）正着取，于是问「昨天」「15号」时，
        // 最新的那批记录反而被 max_rows 截在门外，模型只能对着一堆老数据瞎答。
        std::vector<const PlayRecord*> picked;
        for (auto it = s.all_records->rbegin(); it != s.all_records->rend(); ++it)
        {
            const PlayRecord& r = *it;
            const int y = CStatAnalysis::YmdOf(r.played_at);
            if (y == 0) continue;
            if (from_ymd > 0 && y < from_ymd) continue;
            if (to_ymd > 0 && y > to_ymd) continue;
            picked.push_back(&r);
            if (static_cast<int>(picked.size()) >= max_rows)
                break;
        }
        if (picked.empty())
            return L"\n## 原始播放记录\n（这个时间范围内没有记录）\n";

        std::wstring t;
        t += L"\n## 原始播放记录（" + Num(static_cast<int>(picked.size())) +
             L" 条，时间倒序；文件路径已剔除）\n";
        t += L"格式：时间 | 标题 | 歌手 | 本次播放 | 结果\n";
        // ⚠ 不给这句的话，模型很容易把这批数据当成「要接着往下写的东西」，
        //    照格式续写出一堆不存在的播放记录 —— 用户看到的就是「发疯」。
        t += L"（以上只是供你查证的数据，不要照它的格式续写，只回答最后那个问题）\n";
        for (const PlayRecord* p : picked)
        {
            const PlayRecord& r = *p;
            std::wstring played = r.played_at.size() >= 16 ? r.played_at.substr(0, 16) : r.played_at;
            std::replace(played.begin(), played.end(), L'T', L' ');
            t += played + L" | " + SongLabel(r.title, allow_song_meta) +
                 L" | " + ArtistLabel(r.artist, allow_song_meta) +
                 L" | " + Duration(r.play_duration_sec) +
                 L" | " + ReasonText(r.finish_reason) + L"\n";
        }
        if (from_ymd == 0 && to_ymd == 0 &&
            static_cast<int>(s.all_records->size()) > static_cast<int>(picked.size()))
        {
            t += L"（只列了最近的 " + Num(static_cast<int>(picked.size())) + L" 条，更早的未列出）\n";
        }
        return t;
    }

    // 前置声明：定义在后面「事实清单」那节，带口语同义词匹配
    bool KeyHit(const std::wstring& question, const std::wstring& key);

    std::wstring BuildSourceText(const AiStatSnapshot& s, const std::wstring& question)
    {
        std::wstring src;
        // 走 KeyHit 而不是裸 find —— 否则「夜猫子」「切歌」这类口语说法识别不出来，
        // 依据那行就会漏掉真正用到的数据
        bool hit_artist = KeyHit(question, L"歌手") || KeyHit(question, L"谁");
        bool hit_time = KeyHit(question, L"时段") || KeyHit(question, L"深夜");
        bool hit_behavior = KeyHit(question, L"跳过") || KeyHit(question, L"完播") ||
                            KeyHit(question, L"遗珠");
        bool hit_change = KeyHit(question, L"变化");

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

    // ── 口语说法 → 标准关键词 ──
    //
    // 事实库里的 key 是「曲目 / 完播 / 深夜」这种书面词，可用户不会照着问 ——
    // 他会说「听歌多吗」「我是夜猫子吗」「老切歌」。以前裸用 find(key) 匹配，
    // 于是这些再正常不过的问法**一条都命中不了**，直接掉进兜底话术，显得很笨。
    // 这里给每个标准词配一批人话说法，匹配时一并认。
    const std::map<std::wstring, std::vector<std::wstring>>& SynonymMap()
    {
        static const std::map<std::wstring, std::vector<std::wstring>> m = {
            { L"多少", { L"多吗", L"量大", L"量大不大", L"多少次", L"多久", L"几首", L"几次", L"总共", L"一共", L"听了多少" } },
            { L"曲目", { L"歌", L"曲子", L"哪首", L"哪支", L"首歌", L"听歌", L"单曲" } },
            { L"歌手", { L"谁", L"哪个歌手", L"演唱", L"什么歌手", L"艺人" } },
            { L"专辑", { L"唱片", L"大碟", L"哪张专辑" } },
            { L"时段", { L"几点", L"什么时候", L"哪个时间", L"习惯几点", L"一般几点" } },
            { L"深夜", { L"夜猫子", L"熬夜", L"半夜", L"通宵", L"凌晨", L"晚上不睡", L"晚睡" } },
            { L"周末", { L"双休", L"周六", L"周日", L"休息日", L"不上班" } },
            { L"完播", { L"听完", L"完整", L"没听完", L"听一半", L"完整度" } },
            { L"跳过", { L"切歌", L"切掉", L"跳掉", L"不听了", L"换歌" } },
            { L"连续", { L"连着", L"坚持", L"断了", L"天数", L"连续听" } },
            { L"变化", { L"比", L"趋势", L"多了", L"少了", L"涨", L"降", L"有没有变" } },
            { L"新歌", { L"新听", L"刚听", L"最近发现", L"新发现", L"第一次听" } },
            { L"遗珠", { L"反复", L"可惜", L"浪费", L"老是没听完" } },
        };
        return m;
    }

    // 问题里有没有提到这个关键词（含它的口语说法）
    bool KeyHit(const std::wstring& question, const std::wstring& key)
    {
        if (question.find(key) != std::wstring::npos)
            return true;
        const auto& all = SynonymMap();
        const auto it = all.find(key);
        if (it == all.end())
            return false;
        for (const auto& syn : it->second)
        {
            if (question.find(syn) != std::wstring::npos)
                return true;
        }
        return false;
    }

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

        // 听得专一还是杂食
        //
        // 用的是 inflation_percent（Top10 曲目占了多少次）和 one_hit_wonders ——
        // 这两个字段早就算好了却一直没人用，正好派上「我听得专一吗 / 喜新厌旧吗」这类问法。
        if (sum.inflation_percent > 0)
        {
            std::wstring judge;
            if (sum.inflation_percent >= 60)      judge = L"你听得相当专一，主力就是那几首。";
            else if (sum.inflation_percent >= 35) judge = L"算均衡，既有常听的，也在不断换新的。";
            else                                  judge = L"你挺杂食的，听得很散，没什么固定主力。";
            std::wstring t = L"最常听的 10 首占了全部播放的 " + Num(sum.inflation_percent) + L"%。" + judge;
            if (sum.one_hit_wonders > 0)
                t += L"另外有 " + Num(sum.one_hit_wonders) + L" 首只听过一次就再没碰过。";
            f.push_back({ t, { L"专一", L"杂食", L"集中", L"重复", L"固定", L"喜新厌旧", L"口味", L"常听" }, 58 });
        }

        // 平均一次听多久 / 一天听几次
        if (fin.total > 0)
        {
            const int avg_sec = sum.total_duration_sec / fin.total;
            std::wstring t = L"平均每次听 " + Duration(avg_sec);
            if (sum.active_days > 0)
                t += L"，有听歌的日子里平均一天 " + Num(fin.total / sum.active_days) + L" 次";
            t += L"。";
            f.push_back({ t, { L"平均", L"每次", L"一天", L"单次", L"一般", L"通常" }, 52 });
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

        // 【点名】问题里提到了具体曲名 —— 「搁浅是谁唱的」「演员听了多少次」。
        //
        // 这类问法以前会被泛化的「歌手」事实接走（问题里那个「唱」字命中了它），
        // 于是问「搁浅是谁唱的」，得到的是「你听得最多的歌手是薛之谦」—— 纯答非所问。
        // 这里把问到的曲名认出来，直接给这首歌的歌手和次数。
        {
            struct TitleAgg
            {
                std::wstring artist;
                int count{ 0 };
                int duration_sec{ 0 };
            };
            std::map<std::wstring, TitleAgg> by_title;
            for (const auto& r : recs)
            {
                if (r.title.empty()) continue;
                TitleAgg& a = by_title[r.title];
                if (a.artist.empty() && !r.artist.empty()) a.artist = r.artist;
                a.count++;
                a.duration_sec += r.play_duration_sec;
            }

            // 取最长匹配：免得「演员」这种短曲名把「演员的自我修养」抢走
            const std::wstring* hit = nullptr;
            for (const auto& kv : by_title)
            {
                if (kv.first.size() < 2) continue;      // 单字曲名太容易误触发
                if (question.find(kv.first) == std::wstring::npos) continue;
                if (hit == nullptr || kv.first.size() > hit->size())
                    hit = &kv.first;
            }
            if (hit != nullptr)
            {
                const TitleAgg& a = by_title[*hit];
                const std::wstring artist = a.artist.empty() ? L"未知歌手" : a.artist;
                std::wstring t = L"《" + *hit + L"》是 " + ArtistLabel(artist, allow_song_meta) +
                    L" 的，记录里放了 " + Num(a.count) + L" 次、共 " + Duration(a.duration_sec) + L"。";
                // 曲名本身当 key：问题里提到就必然命中；exclusive 独占回答，不再拼别的事实
                f.push_back({ t, { *hit }, 110, true });
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
                // 这段有记录，但可能一条满 15 秒的都没有 —— 那样后面的事实全是 0，
                // 用户看到一排「0 次(0 秒)」比看到解释更懵，先说清楚。
                int counted = 0;
                for (const auto& r : sliced)
                {
                    if (CStatAnalysis::IsCounted(r)) ++counted;
                }
                if (counted == 0)
                {
                    std::wstring a = scope.label + L"（" + ShortDate(scope.from_ymd) + L" ~ " +
                        ShortDate(scope.to_ymd) + L"）有 " + Num(static_cast<int>(sliced.size())) +
                        L" 条播放记录，但没有一条满 15 秒，所以都没计入统计。";
                    a += L"\n可能是开了播放器没真正听，也可能是记录没写全。";
                    int last = 0;
                    for (const auto& r : *s.all_records)
                    {
                        if (!CStatAnalysis::IsCounted(r)) continue;
                        const int y = CStatAnalysis::YmdOf(r.played_at);
                        if (y > last) last = y;
                    }
                    if (last > 0)
                        a += L"\n有有效时长的记录最晚到 " + ShortDate(last) + L"。";
                    return a;
                }
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
                if (KeyHit(question, k))        // 认口语说法，不再裸匹配
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
        a += L"\n\n（本地档只是照着数据回答，不会聊天 —— 想聊得随意点可以切到「模型」档。）";
        a += L"\n我能答的方向：听了多少 / 最爱谁 / 哪首最常听 / 几点听歌 / 是不是夜猫子 / "
              L"听得专一还是杂食 / 有没有连续在听 / 哪些反复听却没听完。";
        return a;
    }

    // ═══════ 本地档的「菜单式问答」 ═══════
    //
    // 纯代码的规则引擎没法理解自由提问 —— 让用户随便打字，就必然出现
    // 「问第三名答第一名」这类答非所问。与其装作听得懂，不如**把能答准的问题摆出来**：
    // 用户从菜单里挑，答案由专用生成器算，准确率 100%，也不用猜。
    //
    // 每条 = 一个能问的问题 + 问完之后推荐什么（next 存 id，形成探索路径）。
    const std::vector<LocalQa>& LocalQaCatalog()
    {
        static const std::vector<LocalQa> kQa = {
            // ── 总览 ──
            { L"overview",  L"我总共听了多少",           { L"recent7", L"month_now", L"top_artist", L"hour_peak" } },
            { L"recent7",   L"我最近一周听得怎么样",      { L"top_artist", L"hour_peak", L"finish_rate", L"trend" } },
            { L"month_now", L"我这个月听了多少",          { L"month_cmp", L"recent7", L"new_song" } },
            { L"day_avg",   L"我平均每天听多久",          { L"day_best", L"streak", L"overview" } },

            // ── 歌手 / 专辑 / 曲目 ──
            { L"top_artist",    L"听得最多的歌手是谁",      { L"artist_2", L"artist_3", L"artist_all", L"top_album" } },
            { L"artist_2",      L"听得第二多的歌手是谁",     { L"artist_3", L"artist_4", L"artist_all" } },
            { L"artist_3",      L"听得第三多的歌手是谁",     { L"artist_4", L"artist_5", L"artist_all" } },
            { L"artist_4",      L"听得第四多的歌手是谁",     { L"artist_5", L"artist_all", L"top_album" } },
            { L"artist_5",      L"听得第五多的歌手是谁",     { L"artist_all", L"top_album", L"top_song" } },
            { L"artist_all",    L"把歌手前十名都列出来",     { L"least_artist", L"top_album", L"album_all" } },
            { L"least_artist",  L"我听得最少的歌手是谁",     { L"artist_all", L"top_artist" } },
            { L"artist_except", L"除了最常听的歌手，我还听谁", { L"artist_2", L"artist_3", L"artist_all" } },
            { L"top_album",     L"听得最多的专辑是哪张",     { L"album_all", L"top_song", L"song_all" } },
            { L"album_all",     L"把专辑前十名都列出来",     { L"top_song", L"song_all" } },
            { L"top_song",      L"听得最多的歌是哪首",       { L"song_all", L"never_skip", L"retire_gem" } },
            { L"song_all",      L"把曲目前十名都列出来",     { L"never_skip", L"retire_gem", L"top_artist" } },

            // ── 时段 / 习惯 ──
            { L"hour_peak",   L"我一般在什么时段听歌",      { L"hour_all", L"hour_deep", L"weekend_cmp" } },
            { L"hour_all",    L"我一天的听歌分布",          { L"hour_peak", L"hour_deep" } },
            { L"hour_deep",   L"我深夜听歌多吗",            { L"hour_peak", L"weekend_cmp", L"day_best" } },
            { L"weekend_cmp", L"周末和平时听歌有区别吗",     { L"hour_peak", L"hour_deep", L"day_avg" } },
            { L"day_best",    L"我听得最久的一天是哪天",     { L"streak", L"day_avg", L"overview" } },
            { L"streak",      L"我连续听歌多少天了",         { L"day_best", L"day_avg" } },

            // ── 听歌行为 ──
            { L"finish_rate", L"我的完播率高吗",            { L"never_skip", L"retire_gem", L"skip_most" } },
            { L"never_skip",  L"有哪些歌我每次都听完",       { L"top_song", L"retire_gem" } },
            { L"retire_gem",  L"有哪些歌我反复听却没听完",    { L"skip_most", L"finish_rate" } },
            { L"skip_most",   L"我最常跳过的歌是哪首",       { L"finish_rate", L"retire_gem" } },

            // ── 对比 / 变化 ──
            { L"month_cmp", L"这个月比上个月听得多吗",        { L"month_now", L"trend", L"new_song" } },
            { L"week_last", L"上周我听了多久",              { L"recent7", L"month_now", L"trend" } },
            { L"trend",     L"我最近听歌变多了还是变少了",     { L"recent7", L"month_cmp", L"week_last" } },
            { L"new_song",  L"我听了多少首新歌",             { L"top_song", L"recent7" } },
        };
        return kQa;
    }

    const std::vector<LocalQaGroup>& LocalQaMenu()
    {
        static const std::vector<LocalQaGroup> kMenu = {
            { L"总览",        { L"overview", L"recent7", L"month_now", L"day_avg" } },
            { L"歌手 专辑 曲目", { L"top_artist", L"artist_2", L"artist_3", L"artist_all",
                                L"least_artist", L"artist_except", L"top_album",
                                L"top_song", L"song_all" } },
            { L"时段 习惯",    { L"hour_peak", L"hour_all", L"hour_deep", L"weekend_cmp",
                                L"day_best", L"streak" } },
            { L"听歌行为",     { L"finish_rate", L"never_skip", L"retire_gem", L"skip_most" } },
            { L"对比 变化",    { L"month_cmp", L"week_last", L"trend", L"new_song" } },
        };
        return kMenu;
    }

    const LocalQa* FindLocalQaById(const std::wstring& id)
    {
        const std::vector<LocalQa>& all = LocalQaCatalog();
        for (const auto& q : all)
        {
            if (q.id == id)
                return &q;
        }
        return nullptr;
    }

    // 用户手打时也可能正好问在目录里 —— 去掉空白和常见标点后比一比，
    // 能对上就用专用答案，比走关键词匹配准得多。
    const LocalQa* FindLocalQaByText(const std::wstring& q)
    {
        std::wstring key;
        for (wchar_t c : q)
        {
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n')
                continue;
            if (c == L'?' || c == L'？' || c == L'!' || c == L'！' ||
                c == L'.' || c == L'。' || c == L',' || c == L'，')
                continue;
            key += c;
        }
        const std::vector<LocalQa>& all = LocalQaCatalog();
        for (const auto& item : all)
        {
            std::wstring k2;
            for (wchar_t c : item.question)
            {
                if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n')
                    continue;
                if (c == L'?' || c == L'？' || c == L'!' || c == L'！' ||
                    c == L'.' || c == L'。' || c == L',' || c == L'，')
                    continue;
                k2 += c;
            }
            if (key == k2)
                return &item;
        }
        return nullptr;
    }

    std::wstring BuildQaAnswer(const AiStatSnapshot& s, const std::wstring& id, bool allow_song_meta)
    {
        if (!s.Valid() || s.all_records == nullptr || s.all_records->empty())
            return L"还没有可统计的播放记录。先去听几首歌，回来再问。";

        const std::vector<PlayRecord>& all = *s.all_records;
        const int today = TodayYmdLocal();

        StatSummary sum0;
        FinishBreakdown fin0;
        int hour0[24]{};
        AggregateOf(all, sum0, fin0, hour0);

        // 「某段区间」类问题共用（最近一周 / 上周 / 本月…）：报总量，并跟上一段比一比。
        // 有对比才叫回答 —— 单说「485 次」用户也不知道这是多还是少。
        auto range_answer = [&](int from, int to, const wchar_t* label,
                                const std::vector<PlayRecord>& prev) -> std::wstring
        {
            const std::vector<PlayRecord> r = SliceByDate(all, from, to);
            if (r.empty())
                return std::wstring(label) + L"（" + ShortDate(from) + L" ~ " + ShortDate(to) +
                    L"）没有播放记录。";
            StatSummary sum;
            FinishBreakdown fin;
            int hour[24]{};
            AggregateOf(r, sum, fin, hour);

            std::wstring t = std::wstring(label) + L"（" + ShortDate(from) + L" ~ " +
                ShortDate(to) + L"）听了 " + Num(fin.total) + L" 次、" +
                Duration(sum.total_duration_sec) + L"，涉及 " + Num(sum.total_songs) +
                L" 首曲目，活跃 " + Num(sum.active_days) + L" 天。";
            const int span = static_cast<int>(DaysFromYmd(to) - DaysFromYmd(from)) + 1;
            if (span > 0)
                t += L"平均每天 " + Duration(sum.total_duration_sec / span) + L"。";
            if (!prev.empty())
            {
                const FinishBreakdown pf = CStatAnalysis::ComputeFinishBreakdown(prev);
                if (pf.total > 0)
                {
                    const int d = fin.total - pf.total;
                    if (d > 0)
                        t += L"比上一段多 " + Num(d) + L" 次。";
                    else if (d < 0)
                        t += L"比上一段少 " + Num(-d) + L" 次。";
                    else
                        t += L"跟上一段次数持平。";
                }
            }
            return t;
        };

        // ── 总览 ──
        if (id == L"overview")
        {
            std::wstring t = L"从 " + DateText(s.first_ymd) + L" 到 " + DateText(s.last_ymd) +
                L"，你一共听了 " + Num(fin0.total) + L" 次、" +
                Duration(sum0.total_duration_sec) + L"，涉及 " + Num(sum0.total_songs) +
                L" 首曲子，活跃 " + Num(sum0.active_days) + L" 天。";
            if (fin0.total > 0)
                t += L"完播率 " + Pct(sum0.completed_rate) + L"，平均每次听 " +
                     Duration(sum0.total_duration_sec / fin0.total) + L"。";
            return t;
        }
        if (id == L"recent7")
            return range_answer(AddDays(today, -6), today, L"最近 7 天",
                SliceByDate(all, AddDays(today, -13), AddDays(today, -7)));
        if (id == L"month_now")
            return range_answer(MonthFirstDay(today), today, L"本月", std::vector<PlayRecord>());
        if (id == L"day_avg")
        {
            const int span = static_cast<int>(DaysFromYmd(s.last_ymd) - DaysFromYmd(s.first_ymd)) + 1;
            const int active = (sum0.active_days > 0) ? sum0.active_days : 1;
            std::wstring t = L"从 " + DateText(s.first_ymd) + L" 到现在一共 " + Num(span) +
                L" 天，其中 " + Num(sum0.active_days) + L" 天听过歌。";
            t += L"按活跃天算，平均每天 " + Duration(sum0.total_duration_sec / active) + L"；";
            t += L"摊到每一天是 " +
                 Duration(sum0.total_duration_sec / (span > 0 ? span : 1)) + L"。";
            return t;
        }

        // ── 歌手（含任意名次 —— 用户就是问「第三名」被答成榜首才要求改的）──
        if (id == L"top_artist" || id == L"artist_2" || id == L"artist_3" ||
            id == L"artist_4" || id == L"artist_5")
        {
            const int n = (id == L"top_artist") ? 1
                : (id == L"artist_2") ? 2
                : (id == L"artist_3") ? 3
                : (id == L"artist_4") ? 4 : 5;
            static const wchar_t* kOrd[] = { L"第一", L"第二", L"第三", L"第四", L"第五" };

            const std::vector<ArtistRankItem> r = CStatAnalysis::ComputeArtistRank(all, 10);
            if (static_cast<int>(r.size()) < n)
                return L"你听过的歌手不到 " + Num(n) + L" 个，没有第 " + Num(n) + L" 名。";

            const ArtistRankItem& a = r[n - 1];
            std::wstring t = L"听得";
            t += kOrd[n - 1];
            t += L"多的是 " + ArtistLabel(a.artist, allow_song_meta) + L"，" +
                 Num(a.count) + L" 次、" + Duration(a.duration_sec);
            if (a.song_count > 0)
                t += L"（" + Num(a.song_count) + L" 首曲目）";
            t += L"。";
            if (n == 1 && r.size() > 1 && r[1].count > 0)
            {
                if (a.count >= r[1].count * 2)
                    t += L"比第二名「" + ArtistLabel(r[1].artist, allow_song_meta) +
                         L"」多出一倍还多，听得相当集中。";
                else
                    t += L"第二名是 " + ArtistLabel(r[1].artist, allow_song_meta) +
                         L"（" + Num(r[1].count) + L" 次），咬得挺紧。";
            }
            else if (n > 1)
            {
                const ArtistRankItem& p = r[n - 2];
                const int d = p.count - a.count;
                if (d > 0)
                    t += L"比第 " + Num(n - 1) + L" 名「" +
                         ArtistLabel(p.artist, allow_song_meta) + L"」（" + Num(p.count) +
                         L" 次）少 " + Num(d) + L" 次。";
            }
            return t;
        }
        if (id == L"artist_all")
        {
            const std::vector<ArtistRankItem> r = CStatAnalysis::ComputeArtistRank(all, 10);
            if (r.empty())
                return L"还没有歌手数据。";
            std::wstring t = L"歌手前十（按时长排）：";
            for (size_t i = 0; i < r.size(); ++i)
            {
                t += L"\n" + Num(static_cast<int>(i + 1)) + L". " +
                     ArtistLabel(r[i].artist, allow_song_meta) + L" " +
                     Duration(r[i].duration_sec) + L"（" + Num(r[i].count) + L" 次）";
            }
            return t;
        }
        if (id == L"least_artist")
        {
            const std::vector<ArtistRankItem> r = CStatAnalysis::ComputeArtistRank(all, 0);
            if (r.size() < 3)
                return L"你听过的歌手太少，还排不出「最少」的。";
            std::wstring t = L"在「你听过的」范围里，听得最少的是 ";
            for (size_t k = 0; k < 3; ++k)
            {
                const ArtistRankItem& a = r[r.size() - 1 - k];
                if (k > 0) t += L"、";
                t += ArtistLabel(a.artist, allow_song_meta) + L"（" + Num(a.count) + L" 次）";
            }
            t += L" —— 基本点开就切了。";
            t += L"\n（数据里只有你播放过的歌手，完全没听过的不会出现）";
            return t;
        }
        if (id == L"artist_except")
        {
            const std::vector<ArtistRankItem> r = CStatAnalysis::ComputeArtistRank(all, 1);
            if (r.empty())
                return L"还没有歌手数据。";
            std::vector<PlayRecord> rest;
            for (const auto& x : all)
            {
                if (x.artist != r[0].artist)
                    rest.push_back(x);
            }
            if (rest.empty())
                return L"你只听过 " + ArtistLabel(r[0].artist, allow_song_meta) +
                    L" 一个歌手，没有别人了。";
            const std::vector<ArtistRankItem> r2 = CStatAnalysis::ComputeArtistRank(rest, 3);
            std::wstring t = L"把听得最多的「" + ArtistLabel(r[0].artist, allow_song_meta) +
                L"」去掉之后，排在最前面的是：";
            for (size_t i = 0; i < r2.size(); ++i)
            {
                t += L"\n" + Num(static_cast<int>(i + 1)) + L". " +
                     ArtistLabel(r2[i].artist, allow_song_meta) + L" " +
                     Duration(r2[i].duration_sec) + L"（" + Num(r2[i].count) + L" 次）";
            }
            return t;
        }

        // ── 专辑 / 曲目 ──
        if (id == L"top_album" || id == L"album_all")
        {
            const std::vector<AlbumRankItem> r = CStatAnalysis::ComputeAlbumRank(all, 10);
            if (r.empty())
                return L"这些记录里没有专辑标签，统计不了。";
            const std::wstring hide = L"（专辑名已隐藏）";
            if (id == L"top_album")
            {
                std::wstring t = L"听得最多的专辑是《" +
                    (allow_song_meta ? r[0].album : hide) + L"》，" + Duration(r[0].duration_sec) +
                    L"（" + Num(r[0].count) + L" 次）";
                if (r.size() > 1)
                    t += L"；第二名是《" + (allow_song_meta ? r[1].album : hide) + L"》（" +
                         Num(r[1].count) + L" 次）";
                t += L"。";
                return t;
            }
            std::wstring t = L"专辑前十（按时长）：";
            for (size_t i = 0; i < r.size(); ++i)
            {
                t += L"\n" + Num(static_cast<int>(i + 1)) + L". " +
                     (allow_song_meta ? r[i].album : hide) + L" " +
                     Duration(r[i].duration_sec);
            }
            return t;
        }
        if (id == L"top_song" || id == L"song_all")
        {
            const std::vector<SongRankItem> r = CStatAnalysis::ComputeSongRank(all, 10);
            if (r.empty())
                return L"还没有曲目数据。";
            if (id == L"top_song")
            {
                std::wstring t = L"听得最多的是《" + SongLabel(r[0].title, allow_song_meta) +
                    L"》，" + Num(r[0].count) + L" 次、" + Duration(r[0].duration_sec);
                if (r.size() > 1)
                    t += L"；紧随其后的是《" + SongLabel(r[1].title, allow_song_meta) + L"》（" +
                         Num(r[1].count) + L" 次）";
                t += L"。";
                return t;
            }
            std::wstring t = L"曲目前十（按次数）：";
            for (size_t i = 0; i < r.size(); ++i)
            {
                t += L"\n" + Num(static_cast<int>(i + 1)) + L". " +
                     SongLabel(r[i].title, allow_song_meta) + L"（" + Num(r[i].count) + L" 次）";
            }
            return t;
        }

        // ── 时段 / 习惯 ──
        if (id == L"hour_peak")
        {
            int peak = -1, cnt = 0;
            for (int h = 0; h < 24; ++h)
            {
                if (hour0[h] > cnt) { cnt = hour0[h]; peak = h; }
            }
            if (peak < 0 || cnt == 0)
                return L"还没有足够的时段数据。";
            const double pct = (fin0.total > 0) ? cnt * 100.0 / fin0.total : 0.0;
            std::wstring t = L"最常在 " + Num(peak) + L":00-" + Num(peak) + L":59 听，这个小时 " +
                Num(cnt) + L" 次，占全部的 " + Pct(pct) + L"。";
            t += L"前后各一小时分别是 " + Num(hour0[(peak + 23) % 24]) + L" 次和 " +
                 Num(hour0[(peak + 1) % 24]) + L" 次。";
            if (sum0.night_owl_percent > 0)
                t += L"深夜（0-6 点）占全部时长的 " + Pct(sum0.night_owl_percent) + L"。";
            return t;
        }
        if (id == L"hour_all")
        {
            std::wstring t = L"一天 24 小时的听歌次数：";
            for (int h = 0; h < 24; ++h)
            {
                if (hour0[h] == 0) continue;
                t += L"\n" + Num(h) + L" 点：" + Num(hour0[h]) + L" 次";
            }
            return t;
        }
        if (id == L"hour_deep")
        {
            int cnt = 0, sec = 0;
            for (const auto& r : all)
            {
                const int h = CStatAnalysis::HourOf(r.played_at);
                if (h >= 0 && h < 6)
                {
                    cnt++;
                    sec += r.play_duration_sec;
                }
            }
            std::wstring t = L"深夜 0-6 点一共 " + Num(cnt) + L" 次、" + Duration(sec);
            if (sum0.total_duration_sec > 0)
                t += L"，占全部听歌时长的 " + Pct(sec * 100.0 / sum0.total_duration_sec);
            t += L"。";
            if (sum0.active_days > 0)
            {
                wchar_t buf[64]{};
                swprintf_s(buf, L"每个听歌的日子平均有 %.1f 次。", cnt / (double)sum0.active_days);
                t += buf;
            }
            if (sum0.night_owl_percent >= 20.0)
                t += L"深夜听的比例不低，注意作息。";
            else
                t += L"占比不算高。";
            return t;
        }
        if (id == L"weekend_cmp")
        {
            int wd_cnt = 0, wd_sec = 0, we_cnt = 0, we_sec = 0;
            std::set<int> wd_days, we_days;
            for (const auto& r : all)
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
            if (wd_cnt == 0 || we_cnt == 0 || wd_days.empty() || we_days.empty())
                return L"工作日或周末缺一边的数据，比不了。";
            const double wa = wd_sec / static_cast<double>(wd_days.size());
            const double ea = we_sec / static_cast<double>(we_days.size());
            std::wstring t = L"工作日合计 " + Num(wd_cnt) + L" 次、" + Duration(wd_sec) +
                L"；周末 " + Num(we_cnt) + L" 次、" + Duration(we_sec) + L"。";
            t += L"按活跃日平均：工作日每天 " + Duration(static_cast<int>(wa)) + L"，周末每天 " +
                 Duration(static_cast<int>(ea));
            if (ea > wa * 1.2)
                t += L" —— 周末明显听得多。";
            else if (wa > ea * 1.2)
                t += L" —— 工作日反而听得多。";
            else
                t += L" —— 两者差不多。";
            t += L"（比日均才公平：一周里工作日有 5 天、周末只有 2 天）";
            return t;
        }
        if (id == L"day_best")
        {
            const std::vector<PeriodBucket> days = CStatAnalysis::ComputeBuckets(all, Grain::Day);
            if (days.empty())
                return L"还没有足够的日期数据。";
            const PeriodBucket* bt = &days.front();
            const PeriodBucket* bc = &days.front();
            for (const auto& b : days)
            {
                if (b.duration_sec > bt->duration_sec) bt = &b;
                if (b.count > bc->count) bc = &b;
            }
            std::wstring t = L"听得最久的一天是 " + bt->label + L"，" +
                Duration(bt->duration_sec) + L"（" + Num(bt->count) + L" 次）";
            if (bc != bt)
                t += L"；次数最多的是 " + bc->label + L"（" + Num(bc->count) + L" 次），不是同一天";
            t += L"。";
            t += L"平均每个听歌的日子 " +
                 Duration(sum0.total_duration_sec / static_cast<int>(days.size())) + L"。";
            return t;
        }
        if (id == L"streak")
        {
            std::wstring t = L"当前连续听了 " + Num(sum0.current_streak) + L" 天，最长纪录 " +
                Num(sum0.longest_streak) + L" 天。";
            const int miss = CStatAnalysis::ComputeStreakMiss(all);
            if (miss > 0)
                t += L"中间断过最长的一段是 " + Num(miss) + L" 天。";
            return t;
        }

        // ── 听歌行为 ──
        if (id == L"finish_rate")
        {
            if (fin0.total <= 0)
                return L"还没有足够的播放记录。";
            std::wstring t = L"一共 " + Num(fin0.total) + L" 次，完整听完 " +
                Num(fin0.completed) + L" 次（" + Pct(sum0.completed_rate) + L"），中途切走 " +
                Num(fin0.skipped) + L" 次（" + Pct(sum0.skip_rate) + L"）。";
            if (fin0.avg_completion > 0.0)
                t += L"平均完成度 " + Pct(fin0.avg_completion) + L"。";
            if (sum0.completed_rate >= 80.0)
                t += L"完播率很高，你听歌基本从头到尾，不怎么跳。";
            else if (sum0.completed_rate >= 60.0)
                t += L"算正常水平。";
            else
                t += L"跳过偏多，歌单里可能有些不太合口味的。";
            return t;
        }
        if (id == L"never_skip")
        {
            std::map<std::wstring, int> total, done;
            for (const auto& r : all)
            {
                if (r.title.empty()) continue;
                total[r.title]++;
                if (r.finish_reason == PlayRecord::FinishReason::COMPLETED)
                    done[r.title]++;
            }
            std::vector<std::pair<std::wstring, int>> full;
            for (const auto& kv : total)
            {
                if (kv.second >= 3 && kv.second == done[kv.first])
                    full.push_back(std::make_pair(kv.first, kv.second));
            }
            std::stable_sort(full.begin(), full.end(),
                [](const std::pair<std::wstring, int>& x, const std::pair<std::wstring, int>& y)
                { return x.second > y.second; });
            if (full.empty())
                return L"没有「每次点开都听完」的曲子 —— 听 3 次以上的里面，每条都至少跳过一次。";
            std::wstring t = L"听 3 次以上、每次都完整听完的有 " +
                Num(static_cast<int>(full.size())) + L" 首：";
            for (size_t i = 0; i < full.size() && i < 8; ++i)
            {
                t += L"\n· " + SongLabel(full[i].first, allow_song_meta) + L"（" +
                     Num(full[i].second) + L" 次全听完）";
            }
            return t;
        }
        if (id == L"retire_gem")
        {
            const std::vector<RetiredGem> g = CStatAnalysis::ComputeRetiredGems(all, 8);
            if (g.empty())
                return L"没找到「反复点开却没听完」的曲子 —— 你听歌挺有始有终的。";
            std::wstring t = L"有 " + Num(static_cast<int>(g.size())) +
                L" 首是反复点开、却一次都没听完的：";
            for (size_t i = 0; i < g.size() && i < 8; ++i)
            {
                t += L"\n· " + SongLabel(g[i].title, allow_song_meta) + L"（点开 " +
                     Num(g[i].count) + L" 遍）";
            }
            return t;
        }
        if (id == L"skip_most")
        {
            std::map<std::wstring, int> sk;
            for (const auto& r : all)
            {
                if (r.title.empty()) continue;
                if (r.finish_reason == PlayRecord::FinishReason::SKIPPED)
                    sk[r.title]++;
            }
            if (sk.empty())
                return L"你一次都没主动切过歌 —— 没有「最常跳过」的曲子。";
            std::vector<std::pair<std::wstring, int>> v(sk.begin(), sk.end());
            std::stable_sort(v.begin(), v.end(),
                [](const std::pair<std::wstring, int>& x, const std::pair<std::wstring, int>& y)
                { return x.second > y.second; });
            std::wstring t = L"被你切掉最多的曲子：";
            for (size_t i = 0; i < v.size() && i < 5; ++i)
            {
                t += L"\n· " + SongLabel(v[i].first, allow_song_meta) + L"（切掉 " +
                     Num(v[i].second) + L" 次）";
            }
            return t;
        }

        // ── 对比 / 变化 ──
        if (id == L"month_cmp")
        {
            const int mf = MonthFirstDay(today);
            const int lm_last = AddDays(mf, -1);
            const int lm_first = MonthFirstDay(lm_last);
            const std::vector<PlayRecord> cur = SliceByDate(all, mf, today);
            const std::vector<PlayRecord> prv = SliceByDate(all, lm_first, lm_last);
            if (cur.empty() && prv.empty())
                return L"本月和上月都没有播放记录。";
            const FinishBreakdown fc = CStatAnalysis::ComputeFinishBreakdown(cur);
            const FinishBreakdown fp = CStatAnalysis::ComputeFinishBreakdown(prv);
            std::wstring t = L"本月到目前 " + Num(fc.total) + L" 次，上个月整月 " +
                Num(fp.total) + L" 次。";
            if (fp.total > 0)
            {
                const double r = (fc.total - fp.total) * 100.0 / fp.total;
                wchar_t buf[80]{};
                if (r > 10.0)
                {
                    swprintf_s(buf, L"本月已经比上月多 %.0f%%。", r);
                    t += buf;
                }
                else if (r < -10.0)
                {
                    swprintf_s(buf, L"目前比上月少 %.0f%%。", -r);
                    t += buf;
                }
                else
                {
                    t += L"跟上月差不多。";
                }
                if (today < MonthLastDay(today))
                    t += L"（本月还没过完）";
            }
            return t;
        }
        if (id == L"week_last")
        {
            const int wd = WeekdayOf(today);
            const int mon = AddDays(today, -((wd + 6) % 7));
            return range_answer(AddDays(mon, -7), AddDays(mon, -1), L"上周",
                SliceByDate(all, AddDays(mon, -14), AddDays(mon, -8)));
        }
        if (id == L"trend")
        {
            const std::vector<PlayRecord> a7 = SliceByDate(all, AddDays(today, -6), today);
            const std::vector<PlayRecord> b7 = SliceByDate(all, AddDays(today, -13),
                AddDays(today, -7));
            const FinishBreakdown fa = CStatAnalysis::ComputeFinishBreakdown(a7);
            const FinishBreakdown fb = CStatAnalysis::ComputeFinishBreakdown(b7);
            if (fa.total == 0 && fb.total == 0)
                return L"最近两周都没有播放记录，看不出趋势。";
            std::wstring t = L"最近 7 天 " + Num(fa.total) + L" 次，再往前 7 天 " +
                Num(fb.total) + L" 次";
            if (fb.total > 0)
            {
                const double r = (fa.total - fb.total) * 100.0 / fb.total;
                if (r > 15.0)
                    t += L"，多了 " + Pct(r);
                else if (r < -15.0)
                    t += L"，少了 " + Pct(-r);
                else
                    t += L"，基本持平";
            }
            t += L"。";
            return t;
        }
        if (id == L"new_song")
        {
            std::map<std::wstring, int> first_ymd;
            for (const auto& r : all)
            {
                if (r.title.empty()) continue;
                if (!CStatAnalysis::IsCounted(r)) continue;
                const int y = CStatAnalysis::YmdOf(r.played_at);
                if (y == 0) continue;
                auto it = first_ymd.find(r.title);
                if (it == first_ymd.end() || y < it->second)
                    first_ymd[r.title] = y;
            }
            if (first_ymd.empty())
                return L"还没有曲目数据。";
            const int mf = MonthFirstDay(today);
            const int last30 = AddDays(today, -29);
            int n_month = 0, n_30 = 0;
            for (const auto& kv : first_ymd)
            {
                if (kv.second >= mf) n_month++;
                if (kv.second >= last30) n_30++;
            }
            std::wstring t = L"记录里一共出现过 " + Num(static_cast<int>(first_ymd.size())) +
                L" 首不同的曲子。";
            t += L"本月第一次听的有 " + Num(n_month) + L" 首；最近 30 天里有 " +
                 Num(n_30) + L" 首。";
            return t;
        }

        return std::wstring();
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

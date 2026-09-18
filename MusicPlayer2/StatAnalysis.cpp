#include "stdafx.h"
#include "StatAnalysis.h"
#include "StatCommon.h"
#include "StatMeta.h"
#include "PlayStatistics.h"
#include <map>
#include <set>
#include <algorithm>
#include <ctime>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 口径判定
// ─────────────────────────────────────────────────────────────────────────────

// 记录是否计入统计：实际播放时长 >= 15 秒（15 秒过滤的唯一判定入口）
bool CStatAnalysis::IsCounted(const PlayRecord& r)
{
    return r.play_duration_sec >= 15;
}

// ─────────────────────────────────────────────────────────────────────────────
// 时间解析（统一口径，"YYYY-MM-DDTHH:MM:SS"，禁止第二套解析）
// ─────────────────────────────────────────────────────────────────────────────

int CStatAnalysis::YmdOf(const std::wstring& played_at)
{
    if (played_at.size() < 10) return 0;
    if (played_at[4] != L'-' || played_at[7] != L'-') return 0;
    int year = _wtoi(played_at.substr(0, 4).c_str());
    int month = _wtoi(played_at.substr(5, 2).c_str());
    int day = _wtoi(played_at.substr(8, 2).c_str());
    if (year <= 0 || month <= 0 || month > 12 || day <= 0 || day > 31) return 0;
    return year * 10000 + month * 100 + day;
}

int CStatAnalysis::HourOf(const std::wstring& played_at)
{
    // 完整格式固定为 "YYYY-MM-DDTHH:MM:SS"（19 字符）。
    // 收紧校验：长度不足 19 或任一分隔符位置不合法一律返回 -1，
    // 避免把 "2026-01-05T1" 这类被截断的字符串误解析出小时数。
    if (played_at.size() < 19) return -1;
    if (played_at[4] != L'-' || played_at[7] != L'-' || played_at[10] != L'T' ||
        played_at[13] != L':' || played_at[16] != L':') return -1;
    // 小时两位必须是数字
    if (played_at[11] < L'0' || played_at[11] > L'9' ||
        played_at[12] < L'0' || played_at[12] > L'9') return -1;
    int hour = _wtoi(played_at.substr(11, 2).c_str());
    if (hour < 0 || hour > 23) return -1;
    return hour;
}

std::wstring CStatAnalysis::FormatYmd(int ymd, wchar_t sep)
{
    if (ymd <= 0) return std::wstring();
    wchar_t buf[8];
    std::wstring s = L"";
    swprintf_s(buf, L"%04d", ymd / 10000);
    s += buf;
    s += sep;
    swprintf_s(buf, L"%02d", (ymd / 100) % 100);
    s += buf;
    s += sep;
    swprintf_s(buf, L"%02d", ymd % 100);
    s += buf;
    return s;
}

std::wstring CStatAnalysis::FormatBucketLabel(int key, Grain g)
{
    if (key <= 0) return std::wstring();
    wchar_t buf[24];
    switch (g)
    {
    case Grain::Day:
        swprintf_s(buf, L"%02d-%02d", (key / 100) % 100, key % 100);
        return buf;
    case Grain::Week:
        // 周键 = 周一所在年份 * 100 + 年内第几周；标签带年份，避免跨年时出现两个 "W01"
        swprintf_s(buf, L"%04d-W%02d", key / 100, key % 100);
        return buf;
    case Grain::Month:
        swprintf_s(buf, L"%04d-%02d", key / 100, key % 100);
        return buf;
    case Grain::Year:
        swprintf_s(buf, L"%04d", key);
        return buf;
    }
    return std::wstring();
}

// 计算某日所在周的“周键”：周一所在年份 * 100 + 该年内第几周
static int WeekKeyOfYmd(int ymd)
{
    struct tm tmv = {};
    tmv.tm_year = (ymd / 10000) - 1900;
    tmv.tm_mon = ((ymd / 100) % 100) - 1;
    tmv.tm_mday = ymd % 100;
    tmv.tm_hour = 12;
    time_t t = mktime(&tmv);
    if (t == static_cast<time_t>(-1)) return ymd / 10000 * 100;

    struct tm wd = {};
    localtime_s(&wd, &t);
    int wday = wd.tm_wday;                          // 0=周日 .. 6=周六
    int delta = (wday == 0) ? -6 : (1 - wday);      // 回到本周周一
    time_t mon = t + static_cast<time_t>(delta) * 86400;

    struct tm tm_mon = {};
    localtime_s(&tm_mon, &mon);
    int wy = tm_mon.tm_year + 1900;
    int week = tm_mon.tm_yday / 7 + 1;              // 该年内的第几周（以周一计）
    if (week < 1) week = 1;
    if (week > 53) week = 53;
    return wy * 100 + week;
}

// 日期键（YYYYMMDD）加/减天数
static int YmdAddDaysLocal(int ymd, int days)
{
    if (ymd <= 0) return ymd;
    struct tm tv = {};
    tv.tm_year = ymd / 10000 - 1900;
    tv.tm_mon = (ymd / 100) % 100 - 1;
    tv.tm_mday = ymd % 100;
    tv.tm_hour = 12;
    time_t t = mktime(&tv);
    if (t == static_cast<time_t>(-1)) return ymd;
    t += static_cast<time_t>(days) * 86400;
    struct tm out = {};
    localtime_s(&out, &t);
    return (out.tm_year + 1900) * 10000 + (out.tm_mon + 1) * 100 + out.tm_mday;
}

// 某月第一天（YYYYMMDD）
static int FirstDayOfMonth(int ymd)
{
    if (ymd <= 0) return ymd;
    return (ymd / 100) * 100 + 1;
}

// 粒度分桶键
static int BucketKeyOfYmd(int ymd, Grain g)
{
    switch (g)
    {
    case Grain::Day:   return ymd;
    case Grain::Week:  return WeekKeyOfYmd(ymd);
    case Grain::Month: return ymd / 100;
    case Grain::Year:  return ymd / 10000;
    }
    return ymd;
}

// 把秒数格式化成 "X小时X分X秒" / "X分X秒" / "X秒"
std::wstring CStatAnalysis::FormatDuration(int seconds)
{
    int hours = seconds / 3600;
    int mins = (seconds % 3600) / 60;
    int secs = seconds % 60;
    wchar_t buf[64];
    if (hours > 0)
        swprintf_s(buf, L"%d小时%d分%d秒", hours, mins, secs);
    else if (mins > 0)
        swprintf_s(buf, L"%d分%d秒", mins, secs);
    else
        swprintf_s(buf, L"%d秒", secs);
    return buf;
}

StatSummary CStatAnalysis::ComputeSummary(const std::vector<PlayRecord>& records)
{
    StatSummary s;
    s.schema_version = CStatMeta::GetSchemaVersion();

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    int today_year = tm_now.tm_year + 1900;
    int today_month = tm_now.tm_mon + 1;
    int today_day = tm_now.tm_mday;

    // 本周一的 0 点时间戳
    int weekday = tm_now.tm_wday;
    if (weekday == 0) weekday = 7;
    time_t week_start = now - (weekday - 1) * 86400;

    std::map<std::wstring, int> song_count;         // 每首歌播放次数（按路径）
    std::map<std::wstring, int> song_first_ym;      // 每首歌第一次播放的年月
    std::map<std::wstring, int> artist_time;        // 每个歌手累计时长
    std::map<int, int> hour_count;                  // 各时段播放次数
    std::set<std::wstring> active_dates;            // 有播放的日期（yyyy-MM-dd）
    std::set<std::wstring> all_dates;               // 出现过的日期（不过滤，用于连续天数）
    std::set<std::wstring> month_new_songs;         // 本月第一次听的歌

    int today_duration = 0;
    int total_duration = 0;
    int completed_count = 0, skipped_count = 0;
    int night_sec = 0;

    for (const auto& r : records)
    {
        // 统一走 YmdOf：无效日期（或长度不足）直接跳过
        int ymd = YmdOf(r.played_at);
        if (ymd == 0) continue;

        int year = ymd / 10000;
        int month = (ymd / 100) % 100;
        int day = ymd % 100;
        std::wstring date_key = r.played_at.substr(0, 10);
        all_dates.insert(date_key);

        // 15 秒口径唯一入口
        bool valid = IsCounted(r);
        if (valid)
        {
            s.total_count++;
            total_duration += r.play_duration_sec;
            active_dates.insert(date_key);
            song_count[r.file_path] += 1;
            if (song_first_ym.find(r.file_path) == song_first_ym.end())
                song_first_ym[r.file_path] = year * 100 + month;
            if (!r.artist.empty())
                artist_time[r.artist] += r.play_duration_sec;

            switch (r.finish_reason)
            {
            case PlayRecord::FinishReason::COMPLETED: completed_count++; break;
            case PlayRecord::FinishReason::SKIPPED:   skipped_count++; break;
            }

            // 歌曲完成度：播了多长 / 歌曲全长
            if (r.song_length_sec > 0)
            {
                double ratio = min(1.0, (double)r.play_duration_sec / r.song_length_sec);
                s.avg_completion += ratio;
            }

            if (year == today_year && month == today_month && day == today_day)
            {
                s.today_count++;
                today_duration += r.play_duration_sec;
            }
            if (year == today_year && month == today_month)
                s.month_count++;

            struct tm tm_r = {};
            tm_r.tm_year = year - 1900;
            tm_r.tm_mon = month - 1;
            tm_r.tm_mday = day;
            tm_r.tm_hour = 12;
            time_t record_time = mktime(&tm_r);
            if (record_time >= week_start)
                s.week_count++;

            // 时段统计（v2 起统一口径：仅统计 >=15 秒的记录，与其余指标一致）
            int hour = HourOf(r.played_at);
            if (hour >= 0)
            {
                hour_count[hour] += 1;
                if (hour < 6)
                    night_sec += r.play_duration_sec;
            }
        }
    }

    s.total_duration_sec = total_duration;

    // 今日活跃时段：记录已按时间倒序，第一条今天的就是最近一次
    {
        wchar_t today_key[16];
        swprintf_s(today_key, L"%04d-%02d-%02d", today_year, today_month, today_day);
        for (const auto& r : records)
        {
            if (r.played_at.size() >= 10 && r.played_at.compare(0, 10, today_key) == 0)
            {
                int hour = HourOf(r.played_at);
                if (hour >= 0) s.today_active_hour = hour;
                break;
            }
        }
    }

    s.total_songs = (int)song_count.size();
    s.completed_count = completed_count;
    if (s.total_count > 0)
    {
        s.completed_rate = (double)completed_count / s.total_count * 100;
        s.skip_rate = (double)skipped_count / s.total_count * 100;
        s.avg_completion = s.avg_completion / s.total_count * 100;
    }

    // 日均播放 = 总次数 / 有记录的天数
    if (!active_dates.empty())
        s.avg_plays_per_day = s.total_count / (int)active_dates.size();
    s.active_days = (int)active_dates.size();

    // 连续听歌天数：从今天（或昨天）往前数最长连续有记录的天数
    {
        auto is_active = [&](const std::wstring& ymd) -> bool {
            return active_dates.find(ymd) != active_dates.end();
            };
        auto make_key = [](time_t t) -> std::wstring {
            struct tm tm_b;
            localtime_s(&tm_b, &t);
            wchar_t buf[16];
            swprintf_s(buf, L"%04d-%02d-%02d", tm_b.tm_year + 1900, tm_b.tm_mon + 1, tm_b.tm_mday);
            return buf;
            };
        // 起点：今天听了从今天算，今天没听从昨天算（连续天数不因今天还没听而归零）
        time_t cursor = now;
        if (!is_active(make_key(cursor)))
            cursor -= 86400;
        if (is_active(make_key(cursor)))
        {
            while (is_active(make_key(cursor)))
            {
                s.current_streak++;
                cursor -= 86400;
            }
        }
        // 最长连续：遍历所有活跃日期，向上回溯
        for (const auto& d : active_dates)
        {
            struct tm tm_d = {};
            int y = _wtoi(d.substr(0, 4).c_str());
            int m = _wtoi(d.substr(5, 2).c_str());
            int dd = _wtoi(d.substr(8, 2).c_str());
            tm_d.tm_year = y - 1900; tm_d.tm_mon = m - 1; tm_d.tm_mday = dd; tm_d.tm_hour = 12;
            time_t t = mktime(&tm_d);
            // 只从"前一天不活跃"的日期开始数，避免重复计数
            time_t prev = t - 86400;
            if (is_active(make_key(prev)))
                continue;
            int streak = 0;
            time_t c = t;
            while (is_active(make_key(c)))
            {
                streak++;
                c += 86400;
            }
            s.longest_streak = max(s.longest_streak, streak);
        }
    }

    // 最常听歌时段：找出播放次数最多的小时，输出 2 小时区间
    int best_hour = -1, best_count = 0;
    for (const auto& [h, c] : hour_count)
    {
        if (c > best_count)
        {
            best_count = c;
            best_hour = h;
        }
    }
    if (best_hour >= 0)
    {
        s.first_hour = best_hour;
        s.last_hour = (best_hour + 1) % 24;
    }

    s.night_owl_sec = night_sec;
    if (total_duration > 0)
        s.night_owl_percent = (int)(night_sec * 100LL / total_duration);

    // 周末占比：周六周日播放次数占比
    {
        int weekend_count = 0;
        for (const auto& r : records)
        {
            if (!IsCounted(r)) continue;
            int ymd = YmdOf(r.played_at);
            if (ymd == 0) continue;
            struct tm tm_r = {};
            tm_r.tm_year = (ymd / 10000) - 1900;
            tm_r.tm_mon = ((ymd / 100) % 100) - 1;
            tm_r.tm_mday = ymd % 100;
            tm_r.tm_hour = 12;
            time_t t = mktime(&tm_r);
            struct tm tm_out;
            localtime_s(&tm_out, &t);
            if (tm_out.tm_wday == 0 || tm_out.tm_wday == 6)
                weekend_count++;
        }
        if (s.total_count > 0)
            s.weekend_percent = weekend_count * 100 / s.total_count;
    }

    // 单曲深度：只听1次的、听5次以上的
    for (const auto& [path, cnt] : song_count)
    {
        if (cnt <= 1) s.one_hit_wonders++;
        if (cnt >= 5) s.repeat_depth++;
        // 本月新歌：第一次播放发生在这个月
        int first_ym = song_first_ym[path];
        if (first_ym == today_year * 100 + today_month)
            month_new_songs.insert(path);
    }
    s.new_songs_month = (int)month_new_songs.size();

    if (s.total_songs > 0)
        s.explore_percent = s.one_hit_wonders * 100 / s.total_songs;

    // 收听集中度：Top10 歌曲播放次数占总次数比例
    {
        std::vector<int> counts;
        counts.reserve(song_count.size());
        long long sum = 0;
        for (const auto& [path, cnt] : song_count)
        {
            counts.push_back(cnt);
            sum += cnt;
        }
        std::sort(counts.begin(), counts.end(), std::greater<int>());
        long long top10 = 0;
        for (size_t i = 0; i < counts.size() && i < 10; i++)
            top10 += counts[i];
        if (sum > 0)
            s.inflation_percent = (int)(top10 * 100 / sum);
    }

    // Top 歌手 / 歌曲
    for (const auto& [name, dur] : artist_time)
    {
        if (dur > s.top_artist_sec)
        {
            s.top_artist_sec = dur;
            s.top_artist = name;
        }
    }
    {
        std::map<std::wstring, std::wstring> song_title;
        std::map<std::wstring, std::wstring> song_artist;
        for (const auto& r : records)
        {
            if (!IsCounted(r)) continue;
            if (!r.title.empty())
                song_title[r.file_path] = r.title;
            if (!r.artist.empty())
                song_artist[r.file_path] = r.artist;
        }
        for (const auto& [path, cnt] : song_count)
        {
            if (cnt > s.top_song_count)
            {
                s.top_song_count = cnt;
                s.top_song = song_title.count(path) ? song_title[path] : path;
                s.top_song_artist = song_artist.count(path) ? song_artist[path] : L"";
            }
        }
    }
    // ── 听歌档案徽章 ──
    if (s.total_count >= 10)
    {
        if (s.night_owl_sec >= 3600)
        {
            StatSummary::Badge b;
            b.key = L"night";
            b.title = L"夜猫子";
            b.text = L"深夜时段听了 " + FormatDuration(s.night_owl_sec);
            s.badges.push_back(std::move(b));
        }
        if (s.current_streak >= 3)
        {
            StatSummary::Badge b;
            b.key = L"streak";
            b.title = L"连听达人";
            wchar_t buf[16];
            swprintf_s(buf, L"已连续 %d 天听歌", s.current_streak);
            b.text = buf;
            s.badges.push_back(std::move(b));
        }
        if (s.completed_rate >= 70.0)
        {
            StatSummary::Badge b;
            b.key = L"listener";
            b.title = L"完整聆听者";
            wchar_t buf[16];
            swprintf_s(buf, L"完整收听率 %.0f%%", s.completed_rate);
            b.text = buf;
            s.badges.push_back(std::move(b));
        }
        if (s.repeat_depth >= 10)
        {
            StatSummary::Badge b;
            b.key = L"repeat";
            b.title = L"循环狂魔";
            wchar_t buf[32];
            swprintf_s(buf, L"%d 首歌听了 5 遍以上", s.repeat_depth);
            b.text = buf;
            s.badges.push_back(std::move(b));
        }
        if (s.explore_percent >= 50)
        {
            StatSummary::Badge b;
            b.key = L"explore";
            b.title = L"探索先锋";
            wchar_t buf[32];
            swprintf_s(buf, L"%d%% 的歌只听过一次", s.explore_percent);
            b.text = buf;
            s.badges.push_back(std::move(b));
        }
    }

    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// 聚合基座
// ─────────────────────────────────────────────────────────────────────────────

std::vector<PeriodBucket> CStatAnalysis::ComputeBuckets(const std::vector<PlayRecord>& records, Grain grain)
{
    std::map<int, PeriodBucket> buckets;

    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;            // 15 秒口径唯一入口
        int ymd = YmdOf(r.played_at);
        if (ymd == 0) continue;

        int key = 0;
        switch (grain)
        {
        case Grain::Day:   key = ymd;              break;
        case Grain::Week:  key = WeekKeyOfYmd(ymd); break;
        case Grain::Month: key = ymd / 100;         break;
        case Grain::Year:  key = ymd / 10000;       break;
        }

        PeriodBucket& b = buckets[key];
        b.key = key;
        b.count++;
        b.duration_sec += r.play_duration_sec;
        if (r.finish_reason == PlayRecord::FinishReason::COMPLETED) b.completed_count++;
        else if (r.finish_reason == PlayRecord::FinishReason::SKIPPED) b.skipped_count++;
    }

    // std::map 已按 key 升序，逐个生成标签
    std::vector<PeriodBucket> result;
    result.reserve(buckets.size());
    for (auto& [key, b] : buckets)
    {
        b.label = FormatBucketLabel(key, grain);
        result.push_back(std::move(b));
    }
    return result;
}

int CStatAnalysis::ComputeHourHistogram(const std::vector<PlayRecord>& records, int out_hour[24])
{
    for (int i = 0; i < 24; i++)
        out_hour[i] = 0;

    int total = 0;
    for (const auto& r : records)
    {
        // 与 ComputeSummary / ComputeBuckets 完全相同的口径：
        // 先做 15 秒过滤，再要求时间戳合法（与 ComputeSummary 的 YmdOf==0 跳过一致）
        if (!IsCounted(r)) continue;
        if (YmdOf(r.played_at) == 0) continue;
        int hour = HourOf(r.played_at);
        if (hour < 0 || hour > 23) continue;
        out_hour[hour] += 1;
        total++;
    }
    return total;
}

// ─────────────────────────────────────────────────────────────────────────────
// 批次 2 新增聚合接口
// ─────────────────────────────────────────────────────────────────────────────

// 热力图单元格（一天一格，按日期升序）
std::vector<HeatCell> CStatAnalysis::ComputeHeatmapGrid(const std::vector<PlayRecord>& records)
{
    std::map<int, HeatCell> grid;
    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        int ymd = YmdOf(r.played_at);
        if (ymd == 0) continue;
        HeatCell& c = grid[ymd];
        c.ymd = ymd;
        c.count++;
        c.duration_sec += r.play_duration_sec;
    }
    std::vector<HeatCell> result;
    result.reserve(grid.size());
    for (auto& [ymd, c] : grid)
        result.push_back(c);
    return result;
}

// 跳过位置分桶（仅 SKIPPED；完成度 4 桶：0~25 / 25~50 / 50~75 / 75~100%）
std::vector<SkipBucket> CStatAnalysis::ComputeSkipDistribution(const std::vector<PlayRecord>& records)
{
    std::vector<SkipBucket> buckets(4);
    for (int i = 0; i < 4; i++)
    {
        buckets[i].index = i;
        buckets[i].count = 0;
        buckets[i].percent = 0.0;
    }
    buckets[0].label = L"0~25%";
    buckets[1].label = L"25~50%";
    buckets[2].label = L"50~75%";
    buckets[3].label = L"75~100%";

    int total = 0;
    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;                                    // 与其它指标一致的口径
        if (r.finish_reason != PlayRecord::FinishReason::SKIPPED) continue;
        if (r.song_length_sec <= 0) continue;
        double ratio = (double)r.play_duration_sec / (double)r.song_length_sec;
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;
        int idx = (ratio < 0.25) ? 0 : (ratio < 0.50) ? 1 : (ratio < 0.75) ? 2 : 3;
        buckets[idx].count++;
        total++;
    }
    if (total > 0)
    {
        for (auto& b : buckets)
            b.percent = (double)b.count * 100.0 / (double)total;
    }
    return buckets;
}

// 同比 / 环比
PeriodComparison CStatAnalysis::ComputePeriodComparison(const std::vector<PlayRecord>& records, const StatFilter& filter)
{
    PeriodComparison pc;
    Grain g = filter.grain;

    // 参考日期：优先 filter.to_ymd，否则取记录中的最大日期
    int rep = filter.to_ymd;
    if (rep == 0)
    {
        for (const auto& r : records)
        {
            int y = YmdOf(r.played_at);
            if (y > rep) rep = y;
        }
    }
    if (rep == 0) return pc;                    // 无数据

    int cur_key = BucketKeyOfYmd(rep, g);

    // 上一周期参考日
    int prev_rep = rep;
    switch (g)
    {
    case Grain::Day:   prev_rep = YmdAddDaysLocal(rep, -1); break;
    case Grain::Week:  prev_rep = YmdAddDaysLocal(rep, -7); break;
    case Grain::Month: prev_rep = YmdAddDaysLocal(FirstDayOfMonth(rep), -1); break;
    case Grain::Year:  prev_rep = YmdAddDaysLocal(rep, -365); break;
    }
    // 去年同期参考日（年 -1，月日不变）
    int ly_rep = (rep / 10000 - 1) * 10000 + ((rep / 100) % 100) * 100 + (rep % 100);

    int prev_key = BucketKeyOfYmd(prev_rep, g);
    int ly_key = BucketKeyOfYmd(ly_rep, g);

    pc.current.key = cur_key;
    pc.current.label = FormatBucketLabel(cur_key, g);
    pc.previous.key = prev_key;
    pc.previous.label = FormatBucketLabel(prev_key, g);
    pc.last_year.key = ly_key;
    pc.last_year.label = FormatBucketLabel(ly_key, g);

    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        int ymd = YmdOf(r.played_at);
        if (ymd == 0) continue;
        int k = BucketKeyOfYmd(ymd, g);

        PeriodBucket* b = nullptr;
        if (k == cur_key) b = &pc.current;
        else if (k == prev_key) b = &pc.previous;
        else if (k == ly_key) b = &pc.last_year;
        if (b == nullptr) continue;

        b->count++;
        b->duration_sec += r.play_duration_sec;
        if (r.finish_reason == PlayRecord::FinishReason::COMPLETED) b->completed_count++;
        else if (r.finish_reason == PlayRecord::FinishReason::SKIPPED) b->skipped_count++;
    }

    pc.has_previous = pc.previous.count > 0;
    pc.count_delta = pc.current.count - pc.previous.count;
    if (pc.previous.count > 0)
        pc.count_delta_percent = (double)pc.count_delta / (double)pc.previous.count * 100.0;

    // 同比与环比是否指向同一周期：
    //   年粒度下“上一周期”即上一年，与“去年同期”语义重合（ly_key == prev_key）。
    //   此时不做“碰巧算空”处理，而是显式让 last_year 镜像 previous，使数据层如实
    //   表达“去年同期 == 上一周期”，并置 same_as_previous 供显示层去重（避免重复/空行）。
    pc.same_as_previous = (ly_key == prev_key);
    if (pc.same_as_previous)
    {
        pc.last_year = pc.previous;
        pc.has_last_year = pc.has_previous;
    }
    else
    {
        pc.has_last_year = pc.last_year.count > 0;
    }

    return pc;
}

// 专辑排行（按累计时长降序；空专辑名归“未知专辑”）
std::vector<AlbumRankItem> CStatAnalysis::ComputeAlbumRank(const std::vector<PlayRecord>& records, int top_n)
{
    std::map<std::wstring, AlbumRankItem> agg;
    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        std::wstring album = r.album.empty() ? std::wstring(L"未知专辑") : r.album;
        AlbumRankItem& it = agg[album];
        it.album = album;
        it.count++;
        it.duration_sec += r.play_duration_sec;
    }

    std::vector<AlbumRankItem> v;
    v.reserve(agg.size());
    for (auto& [k, it] : agg)
        v.push_back(it);
    std::sort(v.begin(), v.end(),
        [](const AlbumRankItem& a, const AlbumRankItem& b) { return a.duration_sec > b.duration_sec; });

    if (top_n > 0 && (int)v.size() > top_n)
        v.resize(top_n);
    return v;
}

// 结束状态分解：四态计数 + 平均完成度（曲目总长 <= 0 的记录排除）
FinishBreakdown CStatAnalysis::ComputeFinishBreakdown(const std::vector<PlayRecord>& records)
{
    FinishBreakdown b;
    double sum_completion = 0.0;
    int completion_cnt = 0;
    double sum_skip = 0.0;
    int skip_cnt = 0;
    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        b.total++;
        switch (r.finish_reason)
        {
        case PlayRecord::FinishReason::COMPLETED: b.completed++; break;
        case PlayRecord::FinishReason::SKIPPED:   b.skipped++;   break;
        case PlayRecord::FinishReason::STOPPED:   b.stopped++;   break;
        default:                                  b.errored++;   break;
        }
        double length_sec = static_cast<double>(r.song_length_sec) / 1000.0;
        if (length_sec > 0.5)
        {
            double c = static_cast<double>(r.play_duration_sec) / length_sec;
            if (c > 1.0) c = 1.0;
            sum_completion += c;
            completion_cnt++;
            if (r.finish_reason == PlayRecord::FinishReason::SKIPPED)
            {
                sum_skip += c;
                skip_cnt++;
            }
        }
    }
    if (completion_cnt > 0) b.avg_completion = sum_completion / completion_cnt * 100.0;
    b.avg_skip_completion = (skip_cnt > 0) ? (sum_skip / skip_cnt * 100.0) : -1.0;
    return b;
}

// 歌手排行：按累计播放时长降序（同值按名称字典序，保证顺序稳定）
std::vector<ArtistRankItem> CStatAnalysis::ComputeArtistRank(const std::vector<PlayRecord>& records, int top_n)
{
    std::map<std::wstring, ArtistRankItem> agg;
    std::map<std::wstring, std::set<std::wstring>> songs;
    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        std::wstring artist = r.artist.empty() ? std::wstring(L"未知歌手") : r.artist;
        ArtistRankItem& it = agg[artist];
        it.artist = artist;
        it.count++;
        it.duration_sec += r.play_duration_sec;
        songs[artist].insert(r.file_path);
    }

    std::vector<ArtistRankItem> v;
    v.reserve(agg.size());
    for (auto& [k, it] : agg)
    {
        it.song_count = static_cast<int>(songs[k].size());
        v.push_back(it);
    }
    std::sort(v.begin(), v.end(), [](const ArtistRankItem& a, const ArtistRankItem& b)
        {
            if (a.duration_sec != b.duration_sec) return a.duration_sec > b.duration_sec;
            return a.artist < b.artist;
        });

    if (top_n > 0 && (int)v.size() > top_n)
        v.resize(top_n);
    return v;
}

// 曲目排行：按播放次数降序（同次数按累计时长降序，再按路径保证稳定）
std::vector<SongRankItem> CStatAnalysis::ComputeSongRank(const std::vector<PlayRecord>& records, int top_n)
{
    std::map<std::wstring, SongRankItem> agg;
    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        SongRankItem& it = agg[r.file_path];
        if (it.file_path.empty())
        {
            it.file_path = r.file_path;
            it.title = r.title;
            it.artist = r.artist;
        }
        it.count++;
        it.duration_sec += r.play_duration_sec;
        int ymd = YmdOf(r.played_at);
        if (ymd > it.last_ymd) it.last_ymd = ymd;
    }

    std::vector<SongRankItem> v;
    v.reserve(agg.size());
    for (auto& [k, it] : agg)
        v.push_back(it);
    std::sort(v.begin(), v.end(), [](const SongRankItem& a, const SongRankItem& b)
        {
            if (a.count != b.count) return a.count > b.count;
            if (a.duration_sec != b.duration_sec) return a.duration_sec > b.duration_sec;
            return a.file_path < b.file_path;
        });

    if (top_n > 0 && (int)v.size() > top_n)
        v.resize(top_n);
    return v;
}

// 新发现趋势：每月“第一次听”的歌曲数（按月份升序）
std::vector<PeriodBucket> CStatAnalysis::ComputeNewSongTrend(const std::vector<PlayRecord>& records)
{
    std::map<std::wstring, int> first_ym;       // 路径 -> 最早的 YYYYMM
    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        int ymd = YmdOf(r.played_at);
        if (ymd == 0) continue;
        int ym = ymd / 100;
        auto it = first_ym.find(r.file_path);
        if (it == first_ym.end() || ym < it->second)
            first_ym[r.file_path] = ym;
    }

    std::map<int, int> month_new;               // YYYYMM -> 新歌数
    for (const auto& [path, ym] : first_ym)
        month_new[ym]++;

    std::vector<PeriodBucket> out;
    out.reserve(month_new.size());
    for (const auto& [ym, c] : month_new)
    {
        PeriodBucket b;
        b.key = ym;
        b.count = c;
        b.label = FormatBucketLabel(ym, Grain::Month);
        out.push_back(std::move(b));
    }
    return out;
}

// 遗珠挖掘：反复听（count>=min_count）却从未完整听完（COMPLETED==0）
std::vector<RetiredGem> CStatAnalysis::ComputeRetiredGems(const std::vector<PlayRecord>& records, int min_count)
{
    struct Agg { int count = 0; int completed = 0; std::wstring title; std::wstring artist; };
    std::map<std::wstring, Agg> agg;

    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        Agg& a = agg[r.file_path];
        a.count++;
        if (r.finish_reason == PlayRecord::FinishReason::COMPLETED) a.completed++;
        if (!r.title.empty()) a.title = r.title;
        if (!r.artist.empty()) a.artist = r.artist;
    }

    std::vector<RetiredGem> v;
    for (const auto& [path, a] : agg)
    {
        if (a.count >= min_count && a.completed == 0)
        {
            RetiredGem g;
            g.file_path = path;
            g.title = a.title.empty() ? path : a.title;
            g.artist = a.artist;
            g.count = a.count;
            v.push_back(std::move(g));
        }
    }
    std::sort(v.begin(), v.end(),
        [](const RetiredGem& x, const RetiredGem& y) { return x.count > y.count; });
    return v;
}

// 歌单/来源贡献（按 playlist_source 聚合时长占比）
std::vector<PlaylistContribution> CStatAnalysis::ComputePlaylistContribution(const std::vector<PlayRecord>& records)
{
    std::map<std::wstring, int> dur;
    std::map<std::wstring, int> cnt;
    long long total = 0;

    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        std::wstring src = r.playlist_source.empty() ? std::wstring(L"未知来源") : r.playlist_source;
        dur[src] += r.play_duration_sec;
        cnt[src] += 1;
        total += r.play_duration_sec;
    }

    std::vector<PlaylistContribution> v;
    v.reserve(dur.size());
    for (const auto& [src, d] : dur)
    {
        PlaylistContribution c;
        c.source = src;
        c.count = cnt[src];
        c.duration_sec = d;
        c.percent = (total > 0) ? (double)d * 100.0 / (double)total : 0.0;
        v.push_back(std::move(c));
    }
    std::sort(v.begin(), v.end(),
        [](const PlaylistContribution& a, const PlaylistContribution& b) { return a.duration_sec > b.duration_sec; });
    return v;
}

// “差点就连续 x 天”：返回最近一次“已中断”的连续听歌天数（0 = 没有中断的连续段）
int CStatAnalysis::ComputeStreakMiss(const std::vector<PlayRecord>& records)
{
    std::set<int> active;
    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        int ymd = YmdOf(r.played_at);
        if (ymd == 0) continue;
        active.insert(ymd);
    }
    if (active.empty()) return 0;

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    int today = (tm_now.tm_year + 1900) * 10000 + (tm_now.tm_mon + 1) * 100 + tm_now.tm_mday;

    int best = 0;
    for (int ymd : active)
    {
        // 段落起点：前一天不活跃
        int prev = YmdAddDaysLocal(ymd, -1);
        if (active.count(prev)) continue;

        int len = 0;
        int cur = ymd;
        while (active.count(cur))
        {
            len++;
            cur = YmdAddDaysLocal(cur, 1);
        }
        // cur 为段落结束后的第一个不活跃日；cur <= today 表示这段已经中断（不在进行中）
        if (cur <= today && len > best)
            best = len;
    }
    return best;
}

// 五维雷达（0~100）：探索 / 专注 / 夜行 / 专一 / 新鲜
RadarScore CStatAnalysis::ComputeRadar(const StatSummary& s)
{
    auto clamp100 = [](double v) -> double {
        if (v < 0.0) return 0.0;
        if (v > 100.0) return 100.0;
        return v;
        };

    RadarScore r;
    r.explore = clamp100((double)s.explore_percent);
    r.focus = clamp100(100.0 - s.skip_rate);
    r.night = clamp100((double)s.night_owl_percent);
    r.loyalty = clamp100(100.0 - s.explore_percent);
    r.fresh = clamp100(100.0 - s.inflation_percent);
    return r;
}

// 按年归档回顾
std::vector<YearReview> CStatAnalysis::ComputeYearlyReviews(const std::vector<PlayRecord>& records)
{
    struct Agg
    {
        int count = 0;
        int duration = 0;
        std::map<std::wstring, int> artist_time;
        std::map<std::wstring, int> song_count;
        std::map<std::wstring, std::wstring> song_title;
    };
    std::map<int, Agg> years;

    for (const auto& r : records)
    {
        if (!IsCounted(r)) continue;
        int ymd = YmdOf(r.played_at);
        if (ymd == 0) continue;
        int year = ymd / 10000;
        Agg& a = years[year];
        a.count++;
        a.duration += r.play_duration_sec;
        if (!r.artist.empty()) a.artist_time[r.artist] += r.play_duration_sec;
        a.song_count[r.file_path] += 1;
        if (!r.title.empty()) a.song_title[r.file_path] = r.title;
    }

    std::vector<YearReview> result;
    for (auto& [year, a] : years)
    {
        YearReview y;
        y.year = year;
        y.count = a.count;
        y.duration_sec = a.duration;

        int best = 0;
        for (const auto& [name, d] : a.artist_time)
            if (d > best) { best = d; y.top_artist = name; }

        best = 0;
        std::wstring top_path;
        for (const auto& [path, c] : a.song_count)
            if (c > best) { best = c; top_path = path; }
        if (!top_path.empty())
            y.top_song = a.song_title.count(top_path) ? a.song_title[top_path] : top_path;

        result.push_back(std::move(y));
    }
    // 年份降序
    std::sort(result.begin(), result.end(),
        [](const YearReview& x, const YearReview& y) { return x.year > y.year; });
    return result;
}

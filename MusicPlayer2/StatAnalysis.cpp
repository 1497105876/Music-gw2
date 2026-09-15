#include "stdafx.h"
#include "StatAnalysis.h"
#include "PlayStatistics.h"
#include <map>
#include <set>
#include <algorithm>
#include <ctime>

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
    std::map<std::wstring, int> genre_count;        // 每个流派播放次数
    std::map<int, int> hour_count;                  // 各时段播放次数
    std::set<std::wstring> active_dates;            // 有播放的日期（yyyyMMdd）
    std::set<std::wstring> all_dates;               // 出现过的日期（不过滤，用于连续天数）
    std::set<std::wstring> month_new_songs;         // 本月第一次听的歌

    int today_duration = 0;
    int total_duration = 0;
    int completed_count = 0, skipped_count = 0;
    int night_sec = 0;

    for (const auto& r : records)
    {
        if (r.played_at.size() < 10) continue;

        int year = _wtoi(r.played_at.substr(0, 4).c_str());
        int month = _wtoi(r.played_at.substr(5, 2).c_str());
        int day = _wtoi(r.played_at.substr(8, 2).c_str());
        std::wstring date_key = r.played_at.substr(0, 10);
        all_dates.insert(date_key);

        bool valid = (r.play_duration_sec >= 15);   // 和其他统计页一致：不足15秒不计入
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
            if (!r.genre.empty())
                genre_count[r.genre] += 1;

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
        }

        // 时段统计（不过滤15秒，保持和概览页时段分布口径一致）
        if (r.played_at.size() >= 13)
        {
            int hour = _wtoi(r.played_at.substr(11, 2).c_str());
            hour_count[hour] += 1;
            if (valid)
            {
                if (hour >= 0 && hour < 6)
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
            if (r.played_at.size() >= 13 && r.played_at.compare(0, 10, today_key) == 0)
            {
                s.today_active_hour = _wtoi(r.played_at.substr(11, 2).c_str());
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
            if (r.play_duration_sec < 15) continue;
            if (r.played_at.size() < 10) continue;
            int y = _wtoi(r.played_at.substr(0, 4).c_str());
            int m = _wtoi(r.played_at.substr(5, 2).c_str());
            int dd = _wtoi(r.played_at.substr(8, 2).c_str());
            struct tm tm_r = {};
            tm_r.tm_year = y - 1900; tm_r.tm_mon = m - 1; tm_r.tm_mday = dd; tm_r.tm_hour = 12;
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

    // Top 歌手 / 歌曲 / 流派
    for (const auto& [name, dur] : artist_time)
    {
        if (dur > s.top_artist_sec)
        {
            s.top_artist_sec = dur;
            s.top_artist = name;
        }
    }
    {
        std::map<std::wstring, std::pair<int, int>> song_first;  // path -> (次数, 是否有标题)
        std::map<std::wstring, std::wstring> song_title;
        std::map<std::wstring, std::wstring> song_artist;
        for (const auto& r : records)
        {
            if (r.play_duration_sec < 15) continue;
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
    for (const auto& [g, c] : genre_count)
    {
        if (c > s.top_genre_count)
        {
            s.top_genre_count = c;
            s.top_genre = g;
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
        if (s.top_genre_count > 0 && s.top_genre_count * 100 / s.total_count >= 40)
        {
            StatSummary::Badge b;
            b.key = L"genre";
            b.title = L"专一取向";
            b.text = L"偏爱「" + s.top_genre + L"」风格";
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

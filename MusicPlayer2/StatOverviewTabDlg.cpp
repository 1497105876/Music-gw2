#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatOverviewTabDlg.h"
#include "StatAnalysis.h"
#include <map>

IMPLEMENT_DYNAMIC(CStatOverviewTabDlg, CStatTabDlg)

CStatOverviewTabDlg::CStatOverviewTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_OVERVIEW_DLG, pParent)
{
}

CStatOverviewTabDlg::~CStatOverviewTabDlg()
{
}

void CStatOverviewTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_OVERVIEW_LIST2, m_list);
}

BEGIN_MESSAGE_MAP(CStatOverviewTabDlg, CStatTabDlg)
END_MESSAGE_MAP()

BOOL CStatOverviewTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_list.InsertColumn(COL_ITEM, L"统计项", LVCFMT_LEFT, theApp.DPI(200));
    m_list.InsertColumn(COL_VALUE, L"数值", LVCFMT_LEFT, theApp.DPI(300));

    return TRUE;
}

// 从全局上下文聚合（口径统一：仅统计 >=15 秒的记录）
void CStatOverviewTabDlg::Refresh()
{
    m_list.DeleteAllItems();
    m_dirty = false;

    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr)
        return;
    const std::vector<PlayRecord>& records = *m_stat_ctx->records;

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    int today_year = tm_now.tm_year + 1900;
    int today_month = tm_now.tm_mon + 1;
    int today_day = tm_now.tm_mday;

    int weekday = tm_now.tm_wday;
    if (weekday == 0) weekday = 7;
    time_t week_start = now - (weekday - 1) * 86400;

    int today_count = 0, week_count = 0, month_count = 0;
    int today_duration = 0, week_duration = 0, total_duration = 0;
    int completed_count = 0, skipped_count = 0, stopped_count = 0, error_count = 0;
    int total_count = 0;

    std::map<std::wstring, int> song_play_time;
    std::map<std::wstring, int> artist_play_time;
    std::map<std::wstring, int> album_play_time;
    std::map<std::wstring, int> genre_play_count;
    std::map<int, int> hour_distribution;

    for (const auto& r : records)
    {
        if (!CStatAnalysis::IsCounted(r)) continue;     // 15 秒口径唯一入口

        int ymd = CStatAnalysis::YmdOf(r.played_at);
        if (ymd == 0) continue;

        int year = ymd / 10000;
        int month = (ymd / 100) % 100;
        int day = ymd % 100;
        total_count++;

        if (year == today_year && month == today_month && day == today_day)
        {
            today_count++;
            today_duration += r.play_duration_sec;
        }

        struct tm tm_record = {};
        tm_record.tm_year = year - 1900;
        tm_record.tm_mon = month - 1;
        tm_record.tm_mday = day;
        tm_record.tm_hour = 12;
        time_t record_time = mktime(&tm_record);
        if (record_time >= week_start)
        {
            week_count++;
            week_duration += r.play_duration_sec;
        }

        if (year == today_year && month == today_month)
            month_count++;

        int hour = CStatAnalysis::HourOf(r.played_at);
        if (hour >= 0)
            hour_distribution[hour]++;

        total_duration += r.play_duration_sec;

        switch (r.finish_reason)
        {
        case PlayRecord::FinishReason::COMPLETED: completed_count++; break;
        case PlayRecord::FinishReason::SKIPPED:   skipped_count++; break;
        case PlayRecord::FinishReason::STOPPED:   stopped_count++; break;
        case PlayRecord::FinishReason::PLAY_ERROR: error_count++; break;
        }

        song_play_time[r.file_path] += r.play_duration_sec;
        if (!r.artist.empty())
            artist_play_time[r.artist] += r.play_duration_sec;
        if (!r.album.empty())
            album_play_time[r.album] += r.play_duration_sec;
        if (!r.genre.empty())
            genre_play_count[r.genre] += 1;
    }

    auto format_time = [](int seconds) -> std::wstring {
        return CStatAnalysis::FormatDuration(seconds);
    };

    int row = 0;
    auto add_row = [&](const std::wstring& item, const std::wstring& value) {
        m_list.InsertItem(row, item.c_str());
        m_list.SetItemText(row, 1, value.c_str());
        row++;
    };

    add_row(L"── 基本统计 ──", L"");
    add_row(L"今日播放歌曲数", std::to_wstring(today_count) + L" 首");
    add_row(L"今日播放总时长", format_time(today_duration));
    add_row(L"本周播放歌曲数", std::to_wstring(week_count) + L" 首");
    add_row(L"本周播放总时长", format_time(week_duration));
    add_row(L"本月播放歌曲数", std::to_wstring(month_count) + L" 首");
    add_row(L"累计播放歌曲数", std::to_wstring(total_count) + L" 首");
    add_row(L"累计播放总时长", format_time(total_duration));

    // 平均播放时长：总时长 / 总次数，反映单次听歌的平均深度
    if (total_count > 0)
        add_row(L"平均单曲播放时长", format_time(total_duration / total_count));

    // Top 歌手/专辑/流派（口味速览）
    auto top_of = [](const std::map<std::wstring, int>& m) -> std::pair<std::wstring, int> {
        std::pair<std::wstring, int> best{ L"", 0 };
        for (const auto& [k, v] : m)
        {
            if (v > best.second)
                best = { k, v };
        }
        return best;
    };
    if (!artist_play_time.empty())
    {
        auto best = top_of(artist_play_time);
        add_row(L"最常听歌手", best.first + L"（" + format_time(best.second) + L"）");
    }
    if (!album_play_time.empty())
    {
        auto best = top_of(album_play_time);
        add_row(L"最常听专辑", best.first + L"（" + format_time(best.second) + L"）");
    }
    if (!genre_play_count.empty())
    {
        auto best = top_of(genre_play_count);
        add_row(L"最常听流派", best.first + L"（" + std::to_wstring(best.second) + L" 次）");
    }

    // 时段分布峰值
    int best_hour = -1, best_hour_count = 0;
    for (const auto& [h, c] : hour_distribution)
    {
        if (c > best_hour_count)
        {
            best_hour_count = c;
            best_hour = h;
        }
    }
    if (best_hour >= 0)
    {
        wchar_t buf[24];
        swprintf_s(buf, L"%02d:00-%02d:00", best_hour, (best_hour + 1) % 24);
        add_row(L"最活跃时段", std::wstring(buf) + L"（" + std::to_wstring(best_hour_count) + L" 次）");
    }

    add_row(L"", L"");
    add_row(L"── 播放结果 ──", L"");
    add_row(L"完整收听", std::to_wstring(completed_count) + L" 次");
    add_row(L"跳过", std::to_wstring(skipped_count) + L" 次");
    add_row(L"停止", std::to_wstring(stopped_count) + L" 次");
    add_row(L"出错", std::to_wstring(error_count) + L" 次");
    if (total_count > 0)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f%%", (double)completed_count / total_count * 100);
        add_row(L"完整收听率", buf);
        swprintf_s(buf, L"%.1f%%", (double)skipped_count / total_count * 100);
        add_row(L"跳过率", buf);
    }

    add_row(L"", L"");
    add_row(L"── 听歌时段分布 ──", L"");
    for (int h = 0; h < 24; h++)
    {
        int count = 0;
        auto it = hour_distribution.find(h);
        if (it != hour_distribution.end()) count = it->second;
        if (count > 0)
        {
            wchar_t buf[16];
            swprintf_s(buf, L"%02d:00-%02d:00", h, h + 1);
            add_row(buf, std::to_wstring(count) + L" 首");
        }
    }
}

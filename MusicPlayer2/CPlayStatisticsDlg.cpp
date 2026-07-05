// CPlayStatisticsDlg.cpp: 播放统计对话框实现
//

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "CPlayStatisticsDlg.h"
#include "PlayStatistics.h"
#include <cmath>
#include "Common.h"
#include "AudioCommon.h"
#include <sstream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

IMPLEMENT_DYNAMIC(CPlayStatisticsDlg, CBaseDialog)

CPlayStatisticsDlg::CPlayStatisticsDlg(CWnd* pParent /*=nullptr*/)
    : CBaseDialog(IDD_PLAY_STATISTICS_DIALOG, pParent)
{
}

CPlayStatisticsDlg::~CPlayStatisticsDlg()
{
}

CString CPlayStatisticsDlg::GetDialogName() const
{
    return L"PlayStatisticsDlg";
}

bool CPlayStatisticsDlg::InitializeControls()
{
    SetWindowTextW(L"播放统计");

    SetDlgItemTextW(IDC_EXPORT_CSV_BTN, L"导出CSV");
    SetDlgItemTextW(IDC_EXPORT_JSON_BTN, L"导出JSON");
    SetDlgItemTextW(IDCANCEL, L"关闭");

    RepositionTextBasedControls({
        { CtrlTextInfo::L2, IDC_EXPORT_CSV_BTN, CtrlTextInfo::W32 },
        { CtrlTextInfo::L1, IDC_EXPORT_JSON_BTN, CtrlTextInfo::W32 },
        { CtrlTextInfo::R1, IDCANCEL, CtrlTextInfo::W32 }
        });
    return true;
}

void CPlayStatisticsDlg::DoDataExchange(CDataExchange* pDX)
{
    CBaseDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_TAB, m_tab);
    DDX_Control(pDX, IDC_STAT_DETAIL_LIST, m_detail_list);
    DDX_Control(pDX, IDC_STAT_OVERVIEW_LIST, m_overview_list);
    DDX_Control(pDX, IDC_STAT_CHART_STATIC, m_chart_static);
    DDX_Control(pDX, IDC_STAT_TIME_FILTER, m_time_filter);
    DDX_Control(pDX, IDC_STAT_VIEW_BTN1, m_view_btns[0]);
    DDX_Control(pDX, IDC_STAT_VIEW_BTN2, m_view_btns[1]);
    DDX_Control(pDX, IDC_STAT_VIEW_BTN3, m_view_btns[2]);
    DDX_Control(pDX, IDC_STAT_VIEW_BTN4, m_view_btns[3]);
    DDX_Control(pDX, IDC_STAT_VIEW_BTN5, m_view_btns[4]);
}

BEGIN_MESSAGE_MAP(CPlayStatisticsDlg, CBaseDialog)
    ON_NOTIFY(TCN_SELCHANGE, IDC_STAT_TAB, &CPlayStatisticsDlg::OnTcnSelChangeTab)
    ON_BN_CLICKED(IDC_EXPORT_CSV_BTN, &CPlayStatisticsDlg::OnBnClickedExportCsvButton)
    ON_BN_CLICKED(IDC_EXPORT_JSON_BTN, &CPlayStatisticsDlg::OnBnClickedExportJsonButton)
    ON_CBN_SELCHANGE(IDC_STAT_TIME_FILTER, &CPlayStatisticsDlg::OnCbnSelChangeTimeFilter)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_DRAWITEM()
    ON_CONTROL_RANGE(BN_CLICKED, IDC_STAT_VIEW_BTN1, IDC_STAT_VIEW_BTN5, OnBnClickedViewBtn)
END_MESSAGE_MAP()

// CPlayStatisticsDlg 消息处理程序

BOOL CPlayStatisticsDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    // 初始化 Tab 控件
    m_tab.InsertItem(0, L"总览");
    m_tab.InsertItem(1, L"详细记录");
    m_tab.InsertItem(2, L"图形分析");

    // 初始化详细记录列表
    m_detail_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_detail_list.InsertColumn(DCOL_INDEX, L"序号", LVCFMT_LEFT, 50);
    m_detail_list.InsertColumn(DCOL_TIME, L"播放时间", LVCFMT_LEFT, 140);
    m_detail_list.InsertColumn(DCOL_TITLE, L"标题", LVCFMT_LEFT, 200);
    m_detail_list.InsertColumn(DCOL_ARTIST, L"艺术家", LVCFMT_LEFT, 120);
    m_detail_list.InsertColumn(DCOL_ALBUM, L"专辑", LVCFMT_LEFT, 120);
    m_detail_list.InsertColumn(DCOL_PLAY_DUR, L"播放时长", LVCFMT_RIGHT, 70);
    m_detail_list.InsertColumn(DCOL_SONG_LEN, L"歌曲长度", LVCFMT_RIGHT, 70);
    m_detail_list.InsertColumn(DCOL_RESULT, L"结果", LVCFMT_CENTER, 60);
    m_detail_list.InsertColumn(DCOL_SOURCE, L"来源", LVCFMT_LEFT, 80);

    // 初始化总览列表
    m_overview_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_overview_list.InsertColumn(OCOL_ITEM, L"统计项", LVCFMT_LEFT, 200);
    m_overview_list.InsertColumn(OCOL_VALUE, L"数值", LVCFMT_LEFT, 300);

    // 初始化时间筛选下拉框
    InitTimeFilter();

    // 将图表区域改为 OWNERDRAW
    ::SetWindowLongPtr(m_chart_static.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart_static.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW);

    LoadRecords();
    ShowOverview();

    // 默认显示总览
    m_tab.SetCurSel(0);
    m_overview_list.ShowWindow(SW_SHOW);
    m_detail_list.ShowWindow(SW_HIDE);
    m_chart_static.ShowWindow(SW_HIDE);
    // 隐藏视图按钮（只在图形分析Tab显示）
    for (int i = 0; i < 5; i++)
        m_view_btns[i].ShowWindow(SW_HIDE);

    UpdateViewButtons();

    return TRUE;
}

// 初始化时间筛选下拉框
void CPlayStatisticsDlg::InitTimeFilter()
{
    m_time_filter.AddString(L"全部时间");
    m_time_filter.AddString(L"最近7天");
    m_time_filter.AddString(L"最近30天");
    m_time_filter.AddString(L"最近90天");
    m_time_filter.SetCurSel(0);
    m_time_filter_days = 0;
}

// 时间筛选切换
void CPlayStatisticsDlg::OnCbnSelChangeTimeFilter()
{
    int sel = m_time_filter.GetCurSel();
    switch (sel)
    {
    case 0: m_time_filter_days = 0;  break;
    case 1: m_time_filter_days = 7;  break;
    case 2: m_time_filter_days = 30; break;
    case 3: m_time_filter_days = 90; break;
    }
    m_chart_static.Invalidate();
}

// 按时间筛选记录
std::vector<PlayRecord> CPlayStatisticsDlg::GetFilteredRecords() const
{
    if (m_time_filter_days == 0)
        return m_records;

    time_t now = time(nullptr);
    time_t cutoff = now - (time_t)m_time_filter_days * 86400;

    std::vector<PlayRecord> result;
    for (const auto& r : m_records)
    {
        if (r.played_at.size() >= 19)
        {
            struct tm tm_buf = {};
            tm_buf.tm_year = _wtoi(r.played_at.substr(0, 4).c_str()) - 1900;
            tm_buf.tm_mon  = _wtoi(r.played_at.substr(5, 2).c_str()) - 1;
            tm_buf.tm_mday = _wtoi(r.played_at.substr(8, 2).c_str());
            tm_buf.tm_hour = _wtoi(r.played_at.substr(11, 2).c_str());
            tm_buf.tm_min  = _wtoi(r.played_at.substr(14, 2).c_str());
            tm_buf.tm_sec  = _wtoi(r.played_at.substr(17, 2).c_str());
            time_t t = _mkgmtime(&tm_buf);
            if (t >= cutoff)
                result.push_back(r);
        }
    }
    return result;
}

// 一次性聚合统计
CPlayStatisticsDlg::StatsAggregate CPlayStatisticsDlg::AggregateStats(
    const std::vector<PlayRecord>& records) const
{
    StatsAggregate s;
    for (const auto& r : records)
    {
        s.total_plays++;
        s.total_duration_sec += r.play_duration_sec;

        switch (r.finish_reason)
        {
        case PlayRecord::FinishReason::COMPLETED:  s.completed++; break;
        case PlayRecord::FinishReason::SKIPPED:    s.skipped++;   break;
        case PlayRecord::FinishReason::STOPPED:    s.stopped++;   break;
        case PlayRecord::FinishReason::PLAY_ERROR:  s.errored++;  break;
        }

        if (r.played_at.size() >= 13)
        {
            int hr = _wtoi(r.played_at.substr(11, 2).c_str());
            if (hr >= 0 && hr < 24)
                s.hour_count[hr]++;
        }

        if (r.played_at.size() >= 10)
        {
            std::wstring date = r.played_at.substr(0, 10);
            s.daily_count[date]++;
        }

        if (!r.artist.empty())
            s.artist_time[r.artist] += r.play_duration_sec;
        if (!r.album.empty())
            s.album_time[r.album] += r.play_duration_sec;
        if (!r.genre.empty())
            s.genre_time[r.genre] += r.play_duration_sec;

        s.song_count[r.file_path]++;
    }
    return s;
}

// 加载所有播放记录
void CPlayStatisticsDlg::LoadRecords()
{
    m_records.clear();

    std::wstring stats_dir = theApp.m_config_dir + L"statistics\\";
    std::wstring search_pattern = stats_dir + L"playlog_*.jsonl";

    WIN32_FIND_DATA find_data;
    HANDLE hFind = FindFirstFile(search_pattern.c_str(), &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            std::wstring file_path = stats_dir + find_data.cFileName;
            std::ifstream ifs(file_path, std::ios::binary);
            if (ifs.is_open())
            {
                std::string line;
                while (std::getline(ifs, line))
                {
                    if (line.empty()) continue;

                    PlayRecord record;
                    // 简单 JSON 解析
                    auto extract_string = [&line](const std::string& key, std::wstring& out) {
                        std::string search = "\"" + key + "\":\"";
                        size_t pos = line.find(search);
                        if (pos == std::string::npos) return;
                        pos += search.size();
                        size_t end = pos;
                        while (end < line.size())
                        {
                            if (line[end] == '\\' && end + 1 < line.size()) { end += 2; continue; }
                            if (line[end] == '"') break;
                            end++;
                        }
                        std::string utf8_val = line.substr(pos, end - pos);
                        // 反转义
                        std::string unescaped;
                        for (size_t i = 0; i < utf8_val.size(); i++)
                        {
                            if (utf8_val[i] == '\\' && i + 1 < utf8_val.size())
                            {
                                char next = utf8_val[i + 1];
                                if (next == '"') unescaped += '"';
                                else if (next == '\\') unescaped += '\\';
                                else if (next == 'n') unescaped += '\n';
                                else if (next == 'r') unescaped += '\r';
                                else if (next == 't') unescaped += '\t';
                                else unescaped += utf8_val[i];
                                i++;
                            }
                            else
                            {
                                unescaped += utf8_val[i];
                            }
                        }
                        // UTF-8 → wstring
                        int len = ::MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, nullptr, 0);
                        if (len > 0)
                        {
                            out.resize(len - 1);
                            ::MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, &out[0], len);
                        }
                    };

                    auto extract_int = [&line](const std::string& key, int& out) {
                        std::string search = "\"" + key + "\":";
                        size_t pos = line.find(search);
                        if (pos == std::string::npos) return;
                        pos += search.size();
                        if (pos >= line.size()) return;
                        out = atoi(line.c_str() + pos);
                    };

                    auto extract_bool = [&line](const std::string& key, bool& out) {
                        std::string search = "\"" + key + "\":";
                        size_t pos = line.find(search);
                        if (pos == std::string::npos) return;
                        pos += search.size();
                        if (pos >= line.size()) return;
                        out = (line[pos] == 't');
                    };

                    extract_string("file_path", record.file_path);
                    extract_string("title", record.title);
                    extract_string("artist", record.artist);
                    extract_string("album", record.album);
                    extract_string("genre", record.genre);
                    extract_string("played_at", record.played_at);
                    extract_int("play_duration_sec", record.play_duration_sec);
                    extract_int("song_length_sec", record.song_length_sec);
                    int reason = 0;
                    extract_int("finish_reason", reason);
                    record.finish_reason = static_cast<PlayRecord::FinishReason>(reason);
                    extract_int("volume", record.volume);
                    extract_bool("was_shuffled", record.was_shuffled);
                    extract_string("playlist_source", record.playlist_source);
                    extract_int("bitrate", record.bitrate);
                    extract_int("sample_rate", record.sample_rate);
                    extract_int("channels", record.channels);

                    m_records.push_back(std::move(record));
                }
                ifs.close();
            }
        }
    } while (FindNextFile(hFind, &find_data));

    FindClose(hFind);

    // 按时间倒序排列
    std::sort(m_records.begin(), m_records.end(), [](const PlayRecord& a, const PlayRecord& b) {
        return a.played_at > b.played_at;
    });
}

// 显示总览数据
void CPlayStatisticsDlg::ShowOverview()
{
    m_overview_list.DeleteAllItems();

    // 获取当前时间
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    int today_year = tm_now.tm_year + 1900;
    int today_month = tm_now.tm_mon + 1;
    int today_day = tm_now.tm_mday;

    // 计算本周起始（周一为起始）
    int weekday = tm_now.tm_wday; // 0=周日
    if (weekday == 0) weekday = 7;
    time_t week_start = now - (weekday - 1) * 86400;
    struct tm tm_week;
    localtime_s(&tm_week, &week_start);
    int week_year = tm_week.tm_year + 1900;
    int week_month = tm_week.tm_mon + 1;
    int week_day = tm_week.tm_mday;

    int today_count = 0, week_count = 0, month_count = 0;
    int today_duration = 0, week_duration = 0, total_duration = 0;
    int completed_count = 0, skipped_count = 0, stopped_count = 0, error_count = 0;

    // 按歌曲/艺术家/专辑分组统计
    std::map<std::wstring, int> song_play_time;
    std::map<std::wstring, int> artist_play_time;
    std::map<std::wstring, int> album_play_time;
    std::map<int, int> hour_distribution; // 时段分布

    for (const auto& r : m_records)
    {
        // 解析 played_at 中的日期部分
        if (r.played_at.size() >= 10)
        {
            int year = _wtoi(r.played_at.substr(0, 4).c_str());
            int month = _wtoi(r.played_at.substr(5, 2).c_str());
            int day = _wtoi(r.played_at.substr(8, 2).c_str());

            if (year == today_year && month == today_month && day == today_day)
            {
                today_count++;
                today_duration += r.play_duration_sec;
            }

            // 本周
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
            {
                month_count++;
            }

            // 时段分布
            if (r.played_at.size() >= 13)
            {
                int hour = _wtoi(r.played_at.substr(11, 2).c_str());
                hour_distribution[hour]++;
            }
        }

        total_duration += r.play_duration_sec;

        // 播放结果统计
        switch (r.finish_reason)
        {
        case PlayRecord::FinishReason::COMPLETED: completed_count++; break;
        case PlayRecord::FinishReason::SKIPPED:   skipped_count++; break;
        case PlayRecord::FinishReason::STOPPED:   stopped_count++; break;
        case PlayRecord::FinishReason::PLAY_ERROR:  error_count++; break;
        }

        // 分组统计
        song_play_time[r.file_path] += r.play_duration_sec;
        if (!r.artist.empty())
            artist_play_time[r.artist] += r.play_duration_sec;
        if (!r.album.empty())
            album_play_time[r.album] += r.play_duration_sec;
    }

    int total_count = static_cast<int>(m_records.size());

    // 格式化时间字符串
    auto format_time = [](int seconds) -> std::wstring {
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
    };

    int row = 0;
    auto add_row = [&](const std::wstring& item, const std::wstring& value) {
        m_overview_list.InsertItem(row, item.c_str());
        m_overview_list.SetItemText(row, 1, value.c_str());
        row++;
    };

    // 基本统计
    add_row(L"── 基本统计 ──", L"");
    add_row(L"今日播放歌曲数", std::to_wstring(today_count) + L" 首");
    add_row(L"今日播放总时长", format_time(today_duration));
    add_row(L"本周播放歌曲数", std::to_wstring(week_count) + L" 首");
    add_row(L"本周播放总时长", format_time(week_duration));
    add_row(L"本月播放歌曲数", std::to_wstring(month_count) + L" 首");
    add_row(L"累计播放歌曲数", std::to_wstring(total_count) + L" 首");
    add_row(L"累计播放总时长", format_time(total_duration));

    // 播放结果统计
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

    // 最常听歌曲 Top 20
    add_row(L"", L"");
    add_row(L"── 最常听歌曲 Top 20 ──", L"");
    std::vector<std::pair<std::wstring, int>> sorted_songs(song_play_time.begin(), song_play_time.end());
    std::sort(sorted_songs.begin(), sorted_songs.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    int top = 0;
    for (const auto& [path, dur] : sorted_songs)
    {
        if (top >= 20) break;
        // 从文件路径中提取文件名
        std::wstring name = path;
        size_t pos = name.find_last_of(L"\\/");
        if (pos != std::wstring::npos) name = name.substr(pos + 1);
        add_row(std::to_wstring(top + 1) + L". " + name, format_time(dur));
        top++;
    }

    // 最常听艺术家 Top 10
    add_row(L"", L"");
    add_row(L"── 最常听艺术家 Top 10 ──", L"");
    std::vector<std::pair<std::wstring, int>> sorted_artists(artist_play_time.begin(), artist_play_time.end());
    std::sort(sorted_artists.begin(), sorted_artists.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    top = 0;
    for (const auto& [artist, dur] : sorted_artists)
    {
        if (top >= 10) break;
        add_row(std::to_wstring(top + 1) + L". " + artist, format_time(dur));
        top++;
    }

    // 最常听专辑 Top 10
    add_row(L"", L"");
    add_row(L"── 最常听专辑 Top 10 ──", L"");
    std::vector<std::pair<std::wstring, int>> sorted_albums(album_play_time.begin(), album_play_time.end());
    std::sort(sorted_albums.begin(), sorted_albums.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    top = 0;
    for (const auto& [album, dur] : sorted_albums)
    {
        if (top >= 10) break;
        add_row(std::to_wstring(top + 1) + L". " + album, format_time(dur));
        top++;
    }

    // 听歌时段分布
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

// 显示详细记录
void CPlayStatisticsDlg::ShowDetail()
{
    m_detail_list.DeleteAllItems();

    auto format_dur = [](int sec) -> std::wstring {
        int m = sec / 60;
        int s = sec % 60;
        wchar_t buf[16];
        swprintf_s(buf, L"%d:%02d", m, s);
        return buf;
    };

    auto reason_str = [](PlayRecord::FinishReason r) -> std::wstring {
        switch (r)
        {
        case PlayRecord::FinishReason::COMPLETED:  return L"播完";
        case PlayRecord::FinishReason::SKIPPED:    return L"跳过";
        case PlayRecord::FinishReason::STOPPED:    return L"停止";
        case PlayRecord::FinishReason::PLAY_ERROR:   return L"出错";
        }
        return L"";
    };

    int row = 0;
    for (const auto& r : m_records)
    {
        m_detail_list.InsertItem(row, std::to_wstring(row + 1).c_str());
        m_detail_list.SetItemText(row, DCOL_TIME, r.played_at.c_str());
        m_detail_list.SetItemText(row, DCOL_TITLE, r.title.c_str());
        m_detail_list.SetItemText(row, DCOL_ARTIST, r.artist.c_str());
        m_detail_list.SetItemText(row, DCOL_ALBUM, r.album.c_str());
        m_detail_list.SetItemText(row, DCOL_PLAY_DUR, format_dur(r.play_duration_sec).c_str());
        // song_length_sec 实际存的是毫秒，需 ÷1000
        m_detail_list.SetItemText(row, DCOL_SONG_LEN, format_dur(r.song_length_sec / 1000).c_str());
        m_detail_list.SetItemText(row, DCOL_RESULT, reason_str(r.finish_reason).c_str());
        m_detail_list.SetItemText(row, DCOL_SOURCE, r.playlist_source.c_str());
        row++;
    }
}

// Tab 切换
void CPlayStatisticsDlg::OnTcnSelChangeTab(NMHDR* pNMHDR, LRESULT* pResult)
{
    int sel = m_tab.GetCurSel();
    if (sel == 0)
    {
        // 总览
        m_overview_list.ShowWindow(SW_SHOW);
        m_detail_list.ShowWindow(SW_HIDE);
        m_chart_static.ShowWindow(SW_HIDE);
        for (int i = 0; i < 5; i++)
            m_view_btns[i].ShowWindow(SW_HIDE);
    }
    else if (sel == 1)
    {
        // 详细记录 - 首次切换时加载数据
        if (m_detail_list.GetItemCount() == 0 && !m_records.empty())
            ShowDetail();
        m_overview_list.ShowWindow(SW_HIDE);
        m_detail_list.ShowWindow(SW_SHOW);
        m_chart_static.ShowWindow(SW_HIDE);
        for (int i = 0; i < 5; i++)
            m_view_btns[i].ShowWindow(SW_HIDE);
    }
    else if (sel == 2)
    {
        // 图形分析
        m_overview_list.ShowWindow(SW_HIDE);
        m_detail_list.ShowWindow(SW_HIDE);
        m_chart_static.ShowWindow(SW_SHOW);
        for (int i = 0; i < 5; i++)
            m_view_btns[i].ShowWindow(SW_SHOW);
        m_chart_static.Invalidate();
    }
    *pResult = 0;
}

// 导出 CSV
void CPlayStatisticsDlg::OnBnClickedExportCsvButton()
{
    // 弹出文件保存对话框
    wchar_t file_path[MAX_PATH] = { 0 };
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = GetSafeHwnd();
    ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"csv";
    ofn.lpstrTitle = L"导出播放记录 (CSV)";

    if (!GetSaveFileName(&ofn))
        return;

    std::wofstream ofs(file_path);
    if (!ofs.is_open())
    {
        AfxMessageBox(L"无法创建文件", MB_ICONERROR);
        return;
    }

    // BOM
    ofs << L'\xFEFF';

    // 表头
    ofs << L"序号,播放时间,标题,艺术家,专辑,播放时长(秒),歌曲长度(秒),结果,来源\n";

    int idx = 1;
    for (const auto& r : m_records)
    {
        std::wstring reason;
        switch (r.finish_reason)
        {
        case PlayRecord::FinishReason::COMPLETED:  reason = L"播完"; break;
        case PlayRecord::FinishReason::SKIPPED:    reason = L"跳过"; break;
        case PlayRecord::FinishReason::STOPPED:    reason = L"停止"; break;
        case PlayRecord::FinishReason::PLAY_ERROR:   reason = L"出错"; break;
        }

        // CSV 转义：含逗号的字段用双引号包裹
        auto csv_escape = [](const std::wstring& s) -> std::wstring {
            if (s.find(L',') != std::wstring::npos || s.find(L'"') != std::wstring::npos)
            {
                std::wstring escaped = s;
                size_t pos = 0;
                while ((pos = escaped.find(L'"', pos)) != std::wstring::npos)
                {
                    escaped.insert(pos, 1, L'"');
                    pos += 2;
                }
                return L"\"" + escaped + L"\"";
            }
            return s;
        };

        ofs << idx << L','
            << csv_escape(r.played_at) << L','
            << csv_escape(r.title) << L','
            << csv_escape(r.artist) << L','
            << csv_escape(r.album) << L','
            << r.play_duration_sec << L','
            << r.song_length_sec << L','
            << reason << L','
            << csv_escape(r.playlist_source) << L'\n';
        idx++;
    }

    ofs.close();
    AfxMessageBox(L"导出完成", MB_ICONINFORMATION);
}

// 导出 JSON
void CPlayStatisticsDlg::OnBnClickedExportJsonButton()
{
    wchar_t file_path[MAX_PATH] = { 0 };
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = GetSafeHwnd();
    ofn.lpstrFilter = L"JSON 文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"json";
    ofn.lpstrTitle = L"导出播放记录 (JSON)";

    if (!GetSaveFileName(&ofn))
        return;

    std::ofstream ofs(file_path, std::ios::binary);
    if (!ofs.is_open())
    {
        AfxMessageBox(L"无法创建文件", MB_ICONERROR);
        return;
    }

    ofs << "[\n";
    for (size_t i = 0; i < m_records.size(); i++)
    {
        std::string json = m_records[i].ToJson();
        ofs << "  " << json;
        if (i + 1 < m_records.size())
            ofs << ",";
        ofs << "\n";
    }
    ofs << "]\n";
    ofs.flush();
    ofs.close();

    AfxMessageBox(L"导出完成", MB_ICONINFORMATION);
}

// ── 图形分析 ──

void CPlayStatisticsDlg::DrawAnalysisChart()
{
    CRect rect;
    m_chart_static.GetClientRect(&rect);
    m_chart_static.MapWindowPoints(this, &rect);
    if (rect.Width() < 80 || rect.Height() < 80) return;

    CDC* pDC = GetDC();
    if (!pDC) return;

    pDC->FillSolidRect(rect, RGB(252, 252, 255));
    pDC->SetBkMode(TRANSPARENT);

    auto filtered = GetFilteredRecords();
    auto stats = AggregateStats(filtered);

    switch (m_chart_type)
    {
    case 0: DrawOverviewPage(pDC, rect, stats); break;
    case 1: DrawRankingPage(pDC, rect, stats);  break;
    case 2: DrawGenrePage(pDC, rect, stats);    break;
    case 3: DrawTrendChart(pDC, rect, stats);   break;
    case 4: DrawHourChart(pDC, rect, stats);    break;
    }

    ReleaseDC(pDC);
}

// ── 辅助绘图函数 ──

void CPlayStatisticsDlg::DrawSeparator(CDC* pDC, int x1, int x2, int y)
{
    CPen pen(PS_SOLID, 1, RGB(215, 215, 220));
    CPen* old = pDC->SelectObject(&pen);
    pDC->MoveTo(x1, y);
    pDC->LineTo(x2, y);
    pDC->SelectObject(old);
}

void CPlayStatisticsDlg::DrawHourBars(CDC* pDC, const CRect& rect,
    const int hour_count[24], bool show_labels)
{
    int max_h = 1;
    for (int i = 0; i < 24; i++)
        if (hour_count[i] > max_h) max_h = hour_count[i];

    int cL = rect.left + 28, cR = rect.right;
    int cT = rect.top, cB = rect.bottom;
    int bw = (cR - cL) / 24;
    if (bw < 3) bw = 3;

    CPen penAxis(PS_SOLID, 1, RGB(180, 180, 180));
    pDC->SelectObject(&penAxis);
    pDC->MoveTo(cL, cB); pDC->LineTo(cR, cB);

    CFont small_font;
    small_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
    pDC->SelectObject(&small_font);

    for (int i = 0; i < 24; i++)
    {
        int bh = hour_count[i] > 0
            ? max(2, (int)((double)hour_count[i] / max_h * (cB - cT - 2)))
            : 0;
        int bx = cL + i * bw;
        COLORREF clr = i < 6 ? RGB(70, 100, 180)
                      : i < 12 ? RGB(100, 170, 230)
                      : i < 18 ? RGB(130, 190, 180)
                      : RGB(160, 130, 200);
        if (bh > 0)
        {
            CBrush br(clr);
            pDC->FillRect(CRect(bx + 1, cB - bh, bx + max(1, bw - 2), cB), &br);
        }
        if (show_labels && i % 6 == 0)
        {
            pDC->SetTextColor(RGB(100, 100, 100));
            wchar_t buf[8];
            swprintf_s(buf, L"%02d", i);
            pDC->TextOutW(bx, cB + 3, buf, 2);
        }
    }
    pDC->SelectStockObject(BLACK_PEN);
}

void CPlayStatisticsDlg::DrawResultBar(CDC* pDC, const CRect& rect,
    const StatsAggregate& stats)
{
    int total = stats.total_plays;
    if (total == 0) return;

    struct { int cnt; COLORREF clr; } segments[] = {
        { stats.completed, RGB(90, 175, 90) },
        { stats.skipped,   RGB(225, 145, 70) },
        { stats.stopped,   RGB(110, 150, 210) },
        { stats.errored,   RGB(215, 90, 90) },
    };

    int barL = rect.left;
    int barH = rect.Height();
    for (auto& seg : segments)
    {
        if (seg.cnt == 0) continue;
        int segW = max(seg.cnt > 0 ? 2 : 0,
            (int)((double)seg.cnt / total * rect.Width()));
        CBrush br(seg.clr);
        pDC->FillRect(CRect(barL, rect.top, barL + segW, rect.top + barH), &br);
        barL += segW;
    }
}

void CPlayStatisticsDlg::DrawTopBars(CDC* pDC, const CRect& rect,
    const std::vector<std::pair<std::wstring, int>>& sorted,
    int top_n, COLORREF colors[], int name_w)
{
    CFont fText;
    fText.CreatePointFont(85, L"Microsoft YaHei", pDC);
    pDC->SelectObject(&fText);
    pDC->SetBkMode(TRANSPARENT);

    int n = min(top_n, (int)sorted.size());
    if (n == 0) return;

    int max_val = 1;
    for (int i = 0; i < n; i++)
        if (sorted[i].second > max_val) max_val = sorted[i].second;

    int barMaxW = rect.Width() - name_w - 60;
    if (barMaxW < 20) barMaxW = 20;
    int rowH = rect.Height() / top_n;
    if (rowH < 14) rowH = 14;

    for (int i = 0; i < n; i++)
    {
        int rowY = rect.top + i * rowH;

        pDC->SetTextColor(RGB(50, 50, 50));
        std::wstring lbl = std::to_wstring(i + 1) + L". " + sorted[i].first;
        CRect rcName(rect.left, rowY, rect.left + name_w - 8, rowY + rowH);
        pDC->DrawTextW(lbl.c_str(), (int)lbl.size(), &rcName,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);

        int bw2 = max(3, (int)((double)sorted[i].second / max_val * barMaxW));
        CBrush br(colors[i % top_n]);
        pDC->FillRect(CRect(rect.left + name_w, rowY + 3,
            rect.left + name_w + bw2, rowY + rowH - 3), &br);
    }
}

// ── 视图0：概览页 ──

void CPlayStatisticsDlg::DrawOverviewPage(CDC* pDC, const CRect& rect,
    const StatsAggregate& stats)
{
    CFont fTitle, fSection, fText;
    fTitle.CreatePointFont(140, L"Microsoft YaHei", pDC);
    fSection.CreatePointFont(100, L"Microsoft YaHei", pDC);
    fText.CreatePointFont(85, L"Microsoft YaHei", pDC);

    pDC->SetBkMode(TRANSPARENT);

    int L = rect.left + 20, R = rect.right - 20;
    int y = rect.top + 8;
    wchar_t buf[256];

    // 标题
    pDC->SelectObject(&fTitle);
    pDC->SetTextColor(RGB(30, 30, 30));
    pDC->TextOutW(L, y, L"播放概览", 4);
    y += 28;

    // 基本统计
    int dur = stats.total_duration_sec;
    int completion_rate = stats.total_plays > 0
        ? stats.completed * 100 / stats.total_plays : 0;
    swprintf_s(buf,
        L"总播放 %d 次  |  时长 %d时%02d分%02d秒  |  完整率 %d%%",
        stats.total_plays, dur / 3600, (dur % 3600) / 60, dur % 60,
        completion_rate);
    pDC->SelectObject(&fText);
    pDC->SetTextColor(RGB(60, 60, 60));
    pDC->TextOutW(L, y, buf, (int)wcslen(buf));
    y += 28;

    DrawSeparator(pDC, L, R, y);
    y += 12;

    // 听歌时段
    pDC->SelectObject(&fSection);
    pDC->SetTextColor(RGB(40, 40, 40));
    pDC->TextOutW(L, y, L"听歌时段分布", 6);
    y += 18;

    {
        CRect hour_rect(L, y, R, y + 50);
        DrawHourBars(pDC, hour_rect, stats.hour_count, true);
        y = hour_rect.bottom + 16;
    }

    DrawSeparator(pDC, L, R, y);
    y += 12;

    // 播放结果
    pDC->SelectObject(&fSection);
    pDC->SetTextColor(RGB(40, 40, 40));
    pDC->TextOutW(L, y, L"播放结果", 4);
    y += 18;

    {
        CRect result_rect(L, y, R, y + 18);
        DrawResultBar(pDC, result_rect, stats);
        y = result_rect.bottom + 6;

        pDC->SelectObject(&fText);
        pDC->SetTextColor(RGB(60, 60, 60));
        int lx = L;
        struct { const wchar_t* lbl; int cnt; COLORREF clr; } pie[] = {
            { L"完整", stats.completed, RGB(90, 175, 90) },
            { L"跳过", stats.skipped,   RGB(225, 145, 70) },
            { L"停止", stats.stopped,   RGB(110, 150, 210) },
            { L"出错", stats.errored,   RGB(215, 90, 90) },
        };
        for (auto& it : pie)
        {
            CBrush br(it.clr);
            pDC->FillRect(CRect(lx, y, lx + 12, y + 12), &br);
            int pct = stats.total_plays > 0
                ? (int)((double)it.cnt / stats.total_plays * 100) : 0;
            swprintf_s(buf, L"%s %d(%d%%)", it.lbl, it.cnt, pct);
            pDC->TextOutW(lx + 16, y - 1, buf, (int)wcslen(buf));
            lx += 110;
        }
        y += 24;
    }

    DrawSeparator(pDC, L, R, y);
    y += 12;

    // 30天趋势缩略图
    pDC->SelectObject(&fSection);
    pDC->SetTextColor(RGB(40, 40, 40));
    pDC->TextOutW(L, y, L"近30天趋势", 6);
    y += 18;

    CRect trend_rect(L, y, R, rect.bottom - 8);
    DrawTrendChart(pDC, trend_rect, stats);
}

// ── 视图1：排行榜页 ──

void CPlayStatisticsDlg::DrawRankingPage(CDC* pDC, const CRect& rect,
    const StatsAggregate& stats)
{
    CFont fTitle, fSection, fText;
    fTitle.CreatePointFont(140, L"Microsoft YaHei", pDC);
    fSection.CreatePointFont(100, L"Microsoft YaHei", pDC);
    fText.CreatePointFont(85, L"Microsoft YaHei", pDC);

    pDC->SetBkMode(TRANSPARENT);

    int L = rect.left + 20, R = rect.right - 20;
    int y = rect.top + 8;
    wchar_t buf[256];

    pDC->SelectObject(&fTitle);
    pDC->SetTextColor(RGB(30, 30, 30));
    pDC->TextOutW(L, y, L"排行榜", 3);
    y += 30;

    int avail_h = rect.bottom - y - 10;
    int section_h = avail_h / 3;
    int nameW = 130;

    // ── Top 5 艺术家 ──
    pDC->SelectObject(&fSection);
    pDC->SetTextColor(RGB(40, 40, 40));
    pDC->TextOutW(L, y, L"Top 5 艺术家（按播放时长）", 14);
    y += 18;

    {
        std::vector<std::pair<std::wstring, int>> sorted(
            stats.artist_time.begin(), stats.artist_time.end());
        std::sort(sorted.begin(), sorted.end(),
            [](auto& a, auto& b) { return a.second > b.second; });

        COLORREF colors[] = {
            RGB(100,170,230), RGB(100,200,200), RGB(220,120,160),
            RGB(180,220,100), RGB(130,190,130)
        };
        CRect bar_rect(L, y, R, y + section_h - 30);
        DrawTopBars(pDC, bar_rect, sorted, 5, colors, nameW);
        pDC->SelectObject(&fText);
        pDC->SetTextColor(RGB(110, 110, 110));
        for (int i = 0; i < min(5, (int)sorted.size()); i++)
        {
            int dur2 = sorted[i].second;
            swprintf_s(buf, L"%d:%02d", dur2 / 60, dur2 % 60);
            int rowH = bar_rect.Height() / 5;
            int rowY = bar_rect.top + i * rowH;
            pDC->TextOutW(R - 48, rowY + 2, buf, (int)wcslen(buf));
        }
    }
    y = rect.top + 8 + 30 + section_h;

    // ── Top 5 专辑 ──
    pDC->SelectObject(&fSection);
    pDC->SetTextColor(RGB(40, 40, 40));
    pDC->TextOutW(L, y, L"Top 5 专辑（按播放时长）", 12);
    y += 18;

    {
        std::vector<std::pair<std::wstring, int>> sorted(
            stats.album_time.begin(), stats.album_time.end());
        std::sort(sorted.begin(), sorted.end(),
            [](auto& a, auto& b) { return a.second > b.second; });

        COLORREF colors[] = {
            RGB(230,130,100), RGB(240,200,100), RGB(230,170,130),
            RGB(200,160,220), RGB(170,150,230)
        };
        CRect bar_rect(L, y, R, y + section_h - 30);
        DrawTopBars(pDC, bar_rect, sorted, 5, colors, nameW);
        pDC->SelectObject(&fText);
        pDC->SetTextColor(RGB(110, 110, 110));
        for (int i = 0; i < min(5, (int)sorted.size()); i++)
        {
            int dur2 = sorted[i].second;
            swprintf_s(buf, L"%d:%02d", dur2 / 60, dur2 % 60);
            int rowH = bar_rect.Height() / 5;
            int rowY = bar_rect.top + i * rowH;
            pDC->TextOutW(R - 48, rowY + 2, buf, (int)wcslen(buf));
        }
    }
    y = rect.top + 8 + 30 + section_h * 2;

    // ── Top 5 歌曲 ──
    pDC->SelectObject(&fSection);
    pDC->SetTextColor(RGB(40, 40, 40));
    pDC->TextOutW(L, y, L"Top 5 歌曲（按播放次数）", 12);
    y += 18;

    {
        std::vector<std::pair<std::wstring, int>> sorted(
            stats.song_count.begin(), stats.song_count.end());
        std::sort(sorted.begin(), sorted.end(),
            [](auto& a, auto& b) { return a.second > b.second; });

        std::vector<std::pair<std::wstring, int>> named;
        for (const auto& [path, cnt] : sorted)
        {
            std::wstring name = path;
            size_t pos = name.find_last_of(L"\\/");
            if (pos != std::wstring::npos) name = name.substr(pos + 1);
            size_t dot = name.find_last_of(L'.');
            if (dot != std::wstring::npos) name = name.substr(0, dot);
            named.emplace_back(name, cnt);
        }

        COLORREF colors[] = {
            RGB(130,190,130), RGB(200,160,220), RGB(170,150,230),
            RGB(230,170,130), RGB(240,200,100)
        };
        CRect bar_rect(L, y, R, y + section_h - 30);
        DrawTopBars(pDC, bar_rect, named, 5, colors, nameW);
        pDC->SelectObject(&fText);
        pDC->SetTextColor(RGB(110, 110, 110));
        for (int i = 0; i < min(5, (int)named.size()); i++)
        {
            swprintf_s(buf, L"%d次", named[i].second);
            int rowH = bar_rect.Height() / 5;
            int rowY = bar_rect.top + i * rowH;
            pDC->TextOutW(R - 48, rowY + 2, buf, (int)wcslen(buf));
        }
    }
}

// ── 视图2：流派分布页 ──

void CPlayStatisticsDlg::DrawGenrePage(CDC* pDC, const CRect& rect,
    const StatsAggregate& stats)
{
    CFont fTitle, fSection, fText;
    fTitle.CreatePointFont(140, L"Microsoft YaHei", pDC);
    fSection.CreatePointFont(100, L"Microsoft YaHei", pDC);
    fText.CreatePointFont(85, L"Microsoft YaHei", pDC);

    pDC->SetBkMode(TRANSPARENT);

    int L = rect.left + 20, R = rect.right - 20;
    int y = rect.top + 8;

    pDC->SelectObject(&fTitle);
    pDC->SetTextColor(RGB(30, 30, 30));
    pDC->TextOutW(L, y, L"流派分布", 4);
    y += 30;

    std::vector<std::pair<std::wstring, int>> sorted(
        stats.genre_time.begin(), stats.genre_time.end());
    std::sort(sorted.begin(), sorted.end(),
        [](auto& a, auto& b) { return a.second > b.second; });

    std::vector<std::pair<std::wstring, int>> display;
    int other_time = 0;
    for (int i = 0; i < (int)sorted.size(); i++)
    {
        if (i < 5) display.push_back(sorted[i]);
        else other_time += sorted[i].second;
    }
    if (other_time > 0) display.emplace_back(L"其他", other_time);

    if (display.empty())
    {
        pDC->SelectObject(&fText);
        pDC->SetTextColor(RGB(120, 120, 120));
        pDC->TextOutW(L, y + 40, L"暂无流派数据", 6);
        return;
    }

    int total = 0;
    for (const auto& [name, t] : display) total += t;
    if (total == 0) return;

    COLORREF colors[] = {
        RGB(100,170,230), RGB(230,130,100), RGB(130,190,130),
        RGB(200,160,220), RGB(240,200,100), RGB(180,180,180)
    };

    // 环形图尺寸 —— 限制最大值，避免太大
    int avail_w = R - L;
    int avail_h = rect.bottom - y - 10;
    int pie_size = min(avail_w - 200, avail_h - 20);
    pie_size = min(pie_size, 220);  // 上限220
    if (pie_size < 80) pie_size = 80;
    int pie_x = L + 10;
    int pie_y = y + (avail_h - pie_size) / 2;
    int pie_r = pie_size / 2;
    CPoint center(pie_x + pie_r, pie_y + pie_r);
    int inner_r = pie_r * 2 / 3;

    // 绘制环形图
    double start_angle = -90.0;
    for (int i = 0; i < (int)display.size(); i++)
    {
        double sweep = (double)display[i].second / total * 360.0;
        double end_angle = start_angle + sweep;

        double rad_start = start_angle * 3.14159265 / 180.0;
        double rad_end = end_angle * 3.14159265 / 180.0;

        CPoint start_pt(
            center.x + (LONG)(pie_r * cos(rad_start)),
            center.y + (LONG)(pie_r * sin(rad_start)));
        CPoint end_pt(
            center.x + (LONG)(pie_r * cos(rad_end)),
            center.y + (LONG)(pie_r * sin(rad_end)));

        CBrush brush(colors[i % 6]);
        pDC->SelectObject(&brush);
        CPen white_pen(PS_SOLID, 1, RGB(255, 255, 255));
        pDC->SelectObject(&white_pen);

        pDC->Pie(
            center.x - pie_r, center.y - pie_r,
            center.x + pie_r, center.y + pie_r,
            start_pt.x, start_pt.y,
            end_pt.x, end_pt.y);

        start_angle = end_angle;
    }

    // 内圆遮盖
    CBrush bg_brush(RGB(252, 252, 255));
    pDC->SelectObject(&bg_brush);
    pDC->SelectStockObject(NULL_PEN);
    pDC->Ellipse(
        center.x - inner_r, center.y - inner_r,
        center.x + inner_r, center.y + inner_r);

    // 中心文字
    pDC->SelectObject(&fSection);
    pDC->SetTextColor(RGB(50, 50, 50));
    {
        wchar_t buf2[16];
        swprintf_s(buf2, L"%d种", (int)display.size());
        CRect center_rect(center.x - inner_r, center.y - 10,
                          center.x + inner_r, center.y + 10);
        pDC->DrawTextW(buf2, -1, &center_rect,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    // 右侧图例
    pDC->SelectObject(&fText);
    int legend_x = pie_x + pie_size + 20;
    int legend_y = y + 10;
    int line_h = 24;

    for (int i = 0; i < (int)display.size(); i++)
    {
        CBrush brush(colors[i % 6]);
        pDC->SelectObject(&brush);
        pDC->FillRect(CRect(legend_x, legend_y + i * line_h,
            legend_x + 14, legend_y + i * line_h + 14), &brush);

        int pct = (int)((double)display[i].second / total * 100);
        wchar_t buf3[128];
        swprintf_s(buf3, L"%s (%d%%)", display[i].first.c_str(), pct);
        pDC->SetTextColor(RGB(60, 60, 60));
        pDC->TextOutW(legend_x + 18, legend_y + i * line_h - 1,
            buf3, (int)wcslen(buf3));
    }
}

// ── 视图3：30天趋势大图 ──

void CPlayStatisticsDlg::DrawTrendChart(CDC* pDC, const CRect& rect,
    const StatsAggregate& stats)
{
    time_t now = time(nullptr);
    std::vector<std::wstring> dates;
    std::vector<int> counts;

    for (int i = 29; i >= 0; i--)
    {
        time_t t = now - i * 86400;
        struct tm tm_buf;
        localtime_s(&tm_buf, &t);
        wchar_t buf[16];
        swprintf_s(buf, L"%04d-%02d-%02d",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
        std::wstring date(buf);
        dates.push_back(date);

        auto it = stats.daily_count.find(date);
        counts.push_back(it != stats.daily_count.end() ? it->second : 0);
    }

    int max_count = 1;
    for (int c : counts) if (c > max_count) max_count = c;

    int margin_left = 45, margin_right = 15, margin_top, margin_bottom = 30;

    // 如果是概览页调用（rect.height < 100），不留标题空间
    if (rect.Height() > 100)
    {
        margin_top = 30;
        CFont fTitle;
        fTitle.CreatePointFont(140, L"Microsoft YaHei", pDC);
        pDC->SelectObject(&fTitle);
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(RGB(30, 30, 30));
        pDC->TextOutW(rect.left + margin_left, rect.top + 5, L"30天播放趋势", 6);
    }
    else
    {
        margin_top = 5;
    }

    int chart_w = rect.Width() - margin_left - margin_right;
    int chart_h = rect.Height() - margin_top - margin_bottom;
    if (chart_w <= 0 || chart_h <= 0) return;

    // 坐标轴
    CPen axis_pen(PS_SOLID, 1, RGB(180, 180, 180));
    CPen* old_pen = pDC->SelectObject(&axis_pen);
    pDC->MoveTo(rect.left + margin_left, rect.top + margin_top);
    pDC->LineTo(rect.left + margin_left, rect.top + margin_top + chart_h);
    pDC->LineTo(rect.left + margin_left + chart_w, rect.top + margin_top + chart_h);
    pDC->SelectObject(old_pen);

    CFont small_font;
    small_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
    CFont* pOldFont = pDC->SelectObject(&small_font);

    // Y轴刻度
    pDC->SetTextColor(RGB(120, 120, 120));
    wchar_t buf[16];
    swprintf_s(buf, L"%d", max_count);
    pDC->TextOutW(rect.left + 10, rect.top + margin_top - 5, buf);
    pDC->TextOutW(rect.left + 15, rect.top + margin_top + chart_h - 5, L"0");

    // 柱+折线
    int bar_w = chart_w / 30;
    if (bar_w < 2) bar_w = 2;
    CPen line_pen(PS_SOLID, 2, RGB(80, 140, 220));
    CBrush bar_brush(RGB(100, 170, 230));

    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < 30; i++)
    {
        int x = rect.left + margin_left + chart_w * i / 30 + bar_w / 2;
        int bar_h = (int)((double)counts[i] / max_count * chart_h);
        int y_pos = rect.top + margin_top + chart_h - bar_h;

        pDC->SelectObject(&bar_brush);
        pDC->Rectangle(x - bar_w / 2, y_pos,
            x + bar_w / 2, rect.top + margin_top + chart_h);

        if (prev_x >= 0)
        {
            pDC->SelectObject(&line_pen);
            pDC->MoveTo(prev_x, prev_y);
            pDC->LineTo(x, y_pos);
        }
        prev_x = x;
        prev_y = y_pos;
    }

    // X轴标签
    pDC->SetTextColor(RGB(100, 100, 100));
    for (int i = 0; i < 30; i += 5)
    {
        int x = rect.left + margin_left + chart_w * i / 30;
        std::wstring label = dates[i].substr(5);
        pDC->TextOutW(x, rect.top + margin_top + chart_h + 5,
            label.c_str(), (int)label.size());
    }

    pDC->SelectObject(pOldFont);
    pDC->SelectObject(old_pen);
}

// ── 视图4：时段分布大图 ──

void CPlayStatisticsDlg::DrawHourChart(CDC* pDC, const CRect& rect,
    const StatsAggregate& stats)
{
    int max_count = 1;
    for (int i = 0; i < 24; i++)
        if (stats.hour_count[i] > max_count) max_count = stats.hour_count[i];

    int margin_left = 45, margin_right = 15, margin_top = 30, margin_bottom = 40;
    int chart_w = rect.Width() - margin_left - margin_right;
    int chart_h = rect.Height() - margin_top - margin_bottom;
    if (chart_w <= 0 || chart_h <= 0) return;

    // 标题
    CFont fTitle;
    fTitle.CreatePointFont(140, L"Microsoft YaHei", pDC);
    pDC->SelectObject(&fTitle);
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(RGB(30, 30, 30));
    pDC->TextOutW(rect.left + margin_left, rect.top + 5, L"时段分布", 4);

    // 坐标轴
    CPen axis_pen(PS_SOLID, 1, RGB(180, 180, 180));
    CPen* old_pen = pDC->SelectObject(&axis_pen);
    pDC->MoveTo(rect.left + margin_left, rect.top + margin_top);
    pDC->LineTo(rect.left + margin_left, rect.top + margin_top + chart_h);
    pDC->LineTo(rect.left + margin_left + chart_w, rect.top + margin_top + chart_h);
    pDC->SelectObject(old_pen);

    CFont small_font;
    small_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
    CFont* pOldFont = pDC->SelectObject(&small_font);

    // Y轴刻度
    pDC->SetTextColor(RGB(120, 120, 120));
    wchar_t buf[16];
    swprintf_s(buf, L"%d", max_count);
    pDC->TextOutW(rect.left + 10, rect.top + margin_top - 5, buf);
    pDC->TextOutW(rect.left + 15, rect.top + margin_top + chart_h - 5, L"0");

    // 24根柱子
    int bar_w = chart_w / 24;
    if (bar_w < 4) bar_w = 4;

    for (int i = 0; i < 24; i++)
    {
        int x = rect.left + margin_left + chart_w * i / 24;
        int bar_h = (int)((double)stats.hour_count[i] / max_count * chart_h);
        int y_pos = rect.top + margin_top + chart_h - bar_h;

        COLORREF bar_color;
        if (i < 6)       bar_color = RGB(70, 100, 180);
        else if (i < 12) bar_color = RGB(100, 170, 230);
        else if (i < 18) bar_color = RGB(130, 190, 180);
        else             bar_color = RGB(160, 130, 200);

        CBrush color_brush(bar_color);
        pDC->SelectObject(&color_brush);
        pDC->Rectangle(x, y_pos, x + bar_w - 1,
            rect.top + margin_top + chart_h);

        if (i % 3 == 0)
        {
            pDC->SetTextColor(RGB(100, 100, 100));
            swprintf_s(buf, L"%d", i);
            pDC->TextOutW(x, rect.top + margin_top + chart_h + 5,
                buf, (int)wcslen(buf));
        }
    }

    // 时段标签
    const wchar_t* period_labels[] = { L"凌晨", L"清晨", L"上午", L"下午", L"傍晚", L"夜晚" };
    int label_positions[] = { 0, 4, 8, 12, 16, 20 };
    pDC->SetTextColor(RGB(110, 110, 110));
    for (int i = 0; i < 6; i++)
    {
        int x = rect.left + margin_left + chart_w * label_positions[i] / 24;
        pDC->TextOutW(x, rect.top + margin_top + chart_h + 20,
            period_labels[i], (int)wcslen(period_labels[i]));
    }

    pDC->SelectObject(pOldFont);
    pDC->SelectObject(old_pen);
}

// ── 消息处理 ──

BOOL CPlayStatisticsDlg::OnEraseBkgnd(CDC* pDC)
{
    return CBaseDialog::OnEraseBkgnd(pDC);
}

void CPlayStatisticsDlg::OnPaint()
{
    CPaintDC dc(this);
}

void CPlayStatisticsDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_CHART_STATIC && m_tab.GetCurSel() == 2)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        CRect rect(lpDrawItemStruct->rcItem);
        if (rect.Width() < 80 || rect.Height() < 80) return;

        auto filtered = GetFilteredRecords();
        auto stats = AggregateStats(filtered);

        pDC->FillSolidRect(rect, RGB(252, 252, 255));
        pDC->SetBkMode(TRANSPARENT);

        switch (m_chart_type)
        {
        case 0: DrawOverviewPage(pDC, rect, stats); break;
        case 1: DrawRankingPage(pDC, rect, stats);  break;
        case 2: DrawGenrePage(pDC, rect, stats);    break;
        case 3: DrawTrendChart(pDC, rect, stats);   break;
        case 4: DrawHourChart(pDC, rect, stats);    break;
        }
    }
    else
    {
        Default();
    }
}

// 视图按钮点击
void CPlayStatisticsDlg::OnBnClickedViewBtn(UINT nID)
{
    int idx = nID - IDC_STAT_VIEW_BTN1;
    if (idx >= 0 && idx < 5)
    {
        m_chart_type = idx;
        UpdateViewButtons();
        m_chart_static.Invalidate();
    }
}

// 更新按钮高亮
void CPlayStatisticsDlg::UpdateViewButtons()
{
    static CFont fontBold, fontNormal;
    static bool bInit = false;
    if (!bInit)
    {
        LOGFONT lf = {};
        CFont* pFont = m_view_btns[0].GetFont();
        if (pFont) pFont->GetLogFont(&lf);
        lf.lfWeight = FW_BOLD;
        fontBold.CreateFontIndirectW(&lf);
        lf.lfWeight = FW_NORMAL;
        fontNormal.CreateFontIndirectW(&lf);
        bInit = true;
    }
    for (int i = 0; i < 5; i++)
    {
        m_view_btns[i].SetFont(i == m_chart_type ? &fontBold : &fontNormal);
        m_view_btns[i].Invalidate();
    }
}

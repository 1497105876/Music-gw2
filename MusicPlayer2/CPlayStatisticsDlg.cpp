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
}

BEGIN_MESSAGE_MAP(CPlayStatisticsDlg, CBaseDialog)
    ON_NOTIFY(TCN_SELCHANGE, IDC_STAT_TAB, &CPlayStatisticsDlg::OnTcnSelChangeTab)
    ON_BN_CLICKED(IDC_EXPORT_CSV_BTN, &CPlayStatisticsDlg::OnBnClickedExportCsvButton)
    ON_BN_CLICKED(IDC_EXPORT_JSON_BTN, &CPlayStatisticsDlg::OnBnClickedExportJsonButton)
    ON_WM_PAINT()
    ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()

// CPlayStatisticsDlg 消息处理程序

BOOL CPlayStatisticsDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    // 初始化 Tab 控件
    m_tab.InsertItem(0, L"总览");
    m_tab.InsertItem(1, L"详细记录");
    m_tab.InsertItem(2, L"图表");

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

    // 加载数据
    LoadRecords();
    ShowOverview();
    ShowDetail();

    // 默认显示总览
    m_tab.SetCurSel(0);
    m_overview_list.ShowWindow(SW_SHOW);
    m_detail_list.ShowWindow(SW_HIDE);
    m_chart_static.ShowWindow(SW_HIDE);

    return TRUE;
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
    m_detail_list.SetRedraw(FALSE);
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
        m_detail_list.SetItemText(row, DCOL_SONG_LEN, format_dur(r.song_length_sec).c_str());
        m_detail_list.SetItemText(row, DCOL_RESULT, reason_str(r.finish_reason).c_str());
        m_detail_list.SetItemText(row, DCOL_SOURCE, r.playlist_source.c_str());
        row++;
    }

    m_detail_list.SetRedraw(TRUE);
    m_detail_list.Invalidate();
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
    }
    else if (sel == 1)
    {
        // 详细记录
        m_overview_list.ShowWindow(SW_HIDE);
        m_detail_list.ShowWindow(SW_SHOW);
        m_chart_static.ShowWindow(SW_HIDE);
    }
    else
    {
        // 图表
        m_overview_list.ShowWindow(SW_HIDE);
        m_detail_list.ShowWindow(SW_HIDE);
        m_chart_static.ShowWindow(SW_SHOW);
        Invalidate();
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

// ── 图表绘制 ──

void CPlayStatisticsDlg::OnPaint()
{
    CPaintDC dc(this);

    if (m_tab.GetCurSel() != 2)
        return;

    CRect rect;
    m_chart_static.GetClientRect(&rect);
    m_chart_static.MapWindowPoints(this, &rect);

    dc.FillSolidRect(rect, RGB(255, 255, 255));

    switch (m_chart_type)
    {
    case 0: DrawTrendChart(&dc, rect); break;
    case 1: DrawHourChart(&dc, rect); break;
    case 2: DrawPieChart(&dc, rect); break;
    }
}

void CPlayStatisticsDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
    CRect rect;
    m_chart_static.GetClientRect(&rect);
    m_chart_static.MapWindowPoints(this, &rect);

    if (rect.PtInRect(point))
    {
        m_chart_type = (m_chart_type + 1) % 3;
        Invalidate();
    }

    CBaseDialog::OnLButtonDown(nFlags, point);
}

// 播放趋势折线图：最近30天每天的播放次数
void CPlayStatisticsDlg::DrawTrendChart(CDC* pDC, const CRect& rect)
{
    std::map<std::wstring, int> daily_count;
    time_t now = time(nullptr);

    for (const auto& r : m_records)
    {
        if (r.played_at.size() >= 10)
        {
            std::wstring date = r.played_at.substr(0, 10);
            daily_count[date]++;
        }
    }

    std::vector<std::wstring> dates;
    for (int i = 29; i >= 0; i--)
    {
        time_t t = now - i * 86400;
        struct tm tm_buf;
        localtime_s(&tm_buf, &t);
        wchar_t buf[16];
        swprintf_s(buf, L"%04d-%02d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
        dates.push_back(buf);
    }

    int max_count = 1;
    for (const auto& d : dates)
    {
        auto it = daily_count.find(d);
        if (it != daily_count.end() && it->second > max_count)
            max_count = it->second;
    }

    int margin_left = 50, margin_right = 20, margin_top = 30, margin_bottom = 40;
    int chart_w = rect.Width() - margin_left - margin_right;
    int chart_h = rect.Height() - margin_top - margin_bottom;
    if (chart_w <= 0 || chart_h <= 0) return;

    pDC->SetTextColor(RGB(60, 60, 60));
    pDC->TextOutW(rect.left + margin_left, rect.top + 5, L"最近30天播放趋势");

    CPen axis_pen(PS_SOLID, 1, RGB(180, 180, 180));
    CPen* old_pen = pDC->SelectObject(&axis_pen);
    pDC->MoveTo(rect.left + margin_left, rect.top + margin_top);
    pDC->LineTo(rect.left + margin_left, rect.top + margin_top + chart_h);
    pDC->LineTo(rect.left + margin_left + chart_w, rect.top + margin_top + chart_h);
    pDC->SelectObject(old_pen);

    CFont small_font;
    small_font.CreatePointFont(80, L"微软雅黑", pDC);
    CFont* pOldFont = pDC->SelectObject(&small_font);

    pDC->SetTextColor(RGB(120, 120, 120));
    wchar_t buf[16];
    swprintf_s(buf, L"%d", max_count);
    pDC->TextOutW(rect.left + 10, rect.top + margin_top - 5, buf);
    pDC->TextOutW(rect.left + 15, rect.top + margin_top + chart_h - 5, L"0");

    int bar_w = chart_w / 30;
    if (bar_w < 2) bar_w = 2;
    CPen line_pen(PS_SOLID, 2, RGB(80, 140, 220));
    CBrush bar_brush(RGB(100, 170, 230));

    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < 30; i++)
    {
        int x = rect.left + margin_left + chart_w * i / 30 + bar_w / 2;
        int count = 0;
        auto it = daily_count.find(dates[i]);
        if (it != daily_count.end()) count = it->second;
        int bar_h = (int)((double)count / max_count * chart_h);
        int y = rect.top + margin_top + chart_h - bar_h;

        pDC->SelectObject(&bar_brush);
        pDC->Rectangle(x - bar_w / 2, y, x + bar_w / 2, rect.top + margin_top + chart_h);

        if (prev_x >= 0)
        {
            pDC->SelectObject(&line_pen);
            pDC->MoveTo(prev_x, prev_y);
            pDC->LineTo(x, y);
        }
        prev_x = x;
        prev_y = y;
    }

    pDC->SetTextColor(RGB(100, 100, 100));
    for (int i = 0; i < 30; i += 5)
    {
        int x = rect.left + margin_left + chart_w * i / 30;
        std::wstring label = dates[i].substr(5);
        pDC->TextOutW(x, rect.top + margin_top + chart_h + 5, label.c_str());
    }

    pDC->SelectObject(pOldFont);
    pDC->SelectObject(old_pen);
}

// 听歌时段柱状图：24小时分布
void CPlayStatisticsDlg::DrawHourChart(CDC* pDC, const CRect& rect)
{
    int hour_count[24] = { 0 };
    for (const auto& r : m_records)
    {
        if (r.played_at.size() >= 13)
        {
            int hour = _wtoi(r.played_at.substr(11, 2).c_str());
            if (hour >= 0 && hour < 24)
                hour_count[hour]++;
        }
    }

    int max_count = 1;
    for (int i = 0; i < 24; i++)
        if (hour_count[i] > max_count) max_count = hour_count[i];

    int margin_left = 50, margin_right = 20, margin_top = 30, margin_bottom = 40;
    int chart_w = rect.Width() - margin_left - margin_right;
    int chart_h = rect.Height() - margin_top - margin_bottom;
    if (chart_w <= 0 || chart_h <= 0) return;

    pDC->SetTextColor(RGB(60, 60, 60));
    pDC->TextOutW(rect.left + margin_left, rect.top + 5, L"听歌时段分布（24小时）");

    CPen axis_pen(PS_SOLID, 1, RGB(180, 180, 180));
    CPen* old_pen = pDC->SelectObject(&axis_pen);
    pDC->MoveTo(rect.left + margin_left, rect.top + margin_top);
    pDC->LineTo(rect.left + margin_left, rect.top + margin_top + chart_h);
    pDC->LineTo(rect.left + margin_left + chart_w, rect.top + margin_top + chart_h);
    pDC->SelectObject(old_pen);

    CFont small_font;
    small_font.CreatePointFont(80, L"微软雅黑", pDC);
    CFont* pOldFont = pDC->SelectObject(&small_font);

    pDC->SetTextColor(RGB(120, 120, 120));
    wchar_t buf[16];
    swprintf_s(buf, L"%d", max_count);
    pDC->TextOutW(rect.left + 10, rect.top + margin_top - 5, buf);
    pDC->TextOutW(rect.left + 15, rect.top + margin_top + chart_h - 5, L"0");

    int bar_w = chart_w / 24;
    if (bar_w < 4) bar_w = 4;

    for (int i = 0; i < 24; i++)
    {
        int x = rect.left + margin_left + chart_w * i / 24;
        int bar_h = (int)((double)hour_count[i] / max_count * chart_h);
        int y = rect.top + margin_top + chart_h - bar_h;

        COLORREF bar_color;
        if (i < 6)      bar_color = RGB(70, 100, 180);    // 凌晨
        else if (i < 12) bar_color = RGB(100, 170, 230);  // 上午
        else if (i < 18) bar_color = RGB(130, 190, 180);  // 下午
        else             bar_color = RGB(160, 130, 200);  // 晚上
        CBrush color_brush(bar_color);
        pDC->SelectObject(&color_brush);
        pDC->Rectangle(x, y, x + bar_w - 1, rect.top + margin_top + chart_h);

        if (i % 3 == 0)
        {
            pDC->SetTextColor(RGB(100, 100, 100));
            swprintf_s(buf, L"%d", i);
            pDC->TextOutW(x, rect.top + margin_top + chart_h + 5, buf);
        }
    }

    pDC->SelectObject(pOldFont);
    pDC->SelectObject(old_pen);
}

// 艺术家占比饼图：Top 10
void CPlayStatisticsDlg::DrawPieChart(CDC* pDC, const CRect& rect)
{
    std::map<std::wstring, int> artist_time;
    for (const auto& r : m_records)
    {
        if (!r.artist.empty())
            artist_time[r.artist] += r.play_duration_sec;
    }

    std::vector<std::pair<std::wstring, int>> sorted_artists(artist_time.begin(), artist_time.end());
    std::sort(sorted_artists.begin(), sorted_artists.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    int top_n = (int)sorted_artists.size();
    if (top_n > 10) top_n = 10;
    if (top_n == 0)
    {
        pDC->SetTextColor(RGB(120, 120, 120));
        pDC->TextOutW(rect.left + 20, rect.top + 20, L"暂无数据");
        return;
    }

    int total = 0;
    for (const auto& [name, t] : artist_time)
        total += t;
    if (total == 0) return;

    COLORREF colors[] = {
        RGB(100, 170, 230), RGB(230, 130, 100), RGB(130, 190, 130),
        RGB(200, 160, 220), RGB(240, 200, 100), RGB(100, 200, 200),
        RGB(230, 170, 130), RGB(170, 150, 230), RGB(180, 220, 100),
        RGB(220, 120, 160)
    };

    pDC->SetTextColor(RGB(60, 60, 60));
    pDC->TextOutW(rect.left + 20, rect.top + 5, L"艺术家播放时长占比（Top 10）");

    int pie_size = min(rect.Width(), rect.Height()) - 100;
    if (pie_size < 100) pie_size = 100;
    int pie_x = rect.left + 30;
    int pie_y = rect.top + 30;
    int pie_r = pie_size / 2;
    CPoint center(pie_x + pie_r, pie_y + pie_r);

    double start_angle = -90.0;
    for (int i = 0; i < top_n; i++)
    {
        double sweep = (double)sorted_artists[i].second / total * 360.0;
        double end_angle = start_angle + sweep;

        double rad_start = start_angle * 3.14159265 / 180.0;
        double rad_end = end_angle * 3.14159265 / 180.0;

        CPoint start_pt(
            center.x + (LONG)(pie_r * cos(rad_start)),
            center.y + (LONG)(pie_r * sin(rad_start)));
        CPoint end_pt(
            center.x + (LONG)(pie_r * cos(rad_end)),
            center.y + (LONG)(pie_r * sin(rad_end)));

        CBrush brush(colors[i % 10]);
        CBrush* old_brush = pDC->SelectObject(&brush);
        CPen white_pen(PS_SOLID, 1, RGB(255, 255, 255));
        CPen* old_pen2 = pDC->SelectObject(&white_pen);

        pDC->Pie(
            center.x - pie_r, center.y - pie_r,
            center.x + pie_r, center.y + pie_r,
            start_pt.x, start_pt.y,
            end_pt.x, end_pt.y);

        pDC->SelectObject(old_brush);
        pDC->SelectObject(old_pen2);

        start_angle = end_angle;
    }

    // 图例
    CFont small_font;
    small_font.CreatePointFont(90, L"微软雅黑", pDC);
    CFont* pOldFont = pDC->SelectObject(&small_font);

    int legend_x = pie_x + pie_size + 10;
    int legend_y = pie_y + 10;
    int line_h = 18;

    for (int i = 0; i < top_n; i++)
    {
        CBrush brush(colors[i % 10]);
        pDC->SelectObject(&brush);
        pDC->Rectangle(legend_x, legend_y + i * line_h, legend_x + 12, legend_y + i * line_h + 12);

        int pct = (int)((double)sorted_artists[i].second / total * 100);
        std::wstring label = sorted_artists[i].first + L" (" + std::to_wstring(pct) + L"%)";
        pDC->SetTextColor(RGB(60, 60, 60));
        pDC->TextOutW(legend_x + 16, legend_y + i * line_h - 1, label.c_str());
    }

    pDC->SelectObject(pOldFont);
}

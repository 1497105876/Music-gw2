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

    LoadRecords();
    ShowOverview();

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
        // 详细记录 - 首次切换时加载数据
        if (m_detail_list.GetItemCount() == 0 && !m_records.empty())
            ShowDetail();
        m_overview_list.ShowWindow(SW_HIDE);
        m_detail_list.ShowWindow(SW_SHOW);
        m_chart_static.ShowWindow(SW_HIDE);
    }
    else if (sel == 2)
    {
        // 图形分析
        m_overview_list.ShowWindow(SW_HIDE);
        m_detail_list.ShowWindow(SW_HIDE);
        m_chart_static.ShowWindow(SW_HIDE);
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

// ── 图形分析（可工作的版本） ──

void CPlayStatisticsDlg::DrawAnalysisChart(CDC* pDC)
{
    if (!pDC || !pDC->GetSafeHdc()) return;

    // 在 Tab 下方的对话框客户区绘制
    CRect rect;
    GetClientRect(&rect);
    // Tab 高度约 28，上方留点空间
    rect.top += 36;
    rect.bottom -= 40; // 底部按钮区域
    int w = rect.Width(), h = rect.Height();
    if (w < 80 || h < 80) return;

    // 统计数据
    int total = (int)m_records.size();
    int total_dur = 0, completed = 0, skipped = 0, stopped = 0, errored = 0;
    int hour_count[24] = {};
    std::map<std::wstring, int> artist_time;

    for (const auto& r : m_records)
    {
        total_dur += r.play_duration_sec;
        switch (r.finish_reason)
        {
        case PlayRecord::FinishReason::COMPLETED: completed++; break;
        case PlayRecord::FinishReason::SKIPPED:   skipped++; break;
        case PlayRecord::FinishReason::STOPPED:   stopped++; break;
        case PlayRecord::FinishReason::PLAY_ERROR: errored++; break;
        }
        if (r.played_at.size() >= 13)
        {
            int hr = _wtoi(r.played_at.substr(11, 2).c_str());
            if (hr >= 0 && hr < 24) hour_count[hr]++;
        }
        if (!r.artist.empty()) artist_time[r.artist] += r.play_duration_sec;
    }

    // CDC* pDC = GetDC();
    // if (!pDC) return;

    pDC->FillSolidRect(rect, RGB(248, 248, 252));

    int margin = 24;
    int L = rect.left + margin, R = rect.right - margin;
    int y = rect.top + 12;
    wchar_t buf[256];

    // 字体
    CFont fTitle, fSection, fText, fTextBold;
    fTitle.CreatePointFont(160, L"Microsoft YaHei", pDC);
    fSection.CreatePointFont(105, L"Microsoft YaHei", pDC);
    fText.CreatePointFont(85, L"Microsoft YaHei", pDC);
    fTextBold.CreatePointFont(85, L"Microsoft YaHei", pDC);
    LOGFONT lf;
    fTextBold.GetLogFont(&lf);
    lf.lfWeight = FW_BOLD;
    fTextBold.DeleteObject();
    fTextBold.CreateFontIndirect(&lf);

    pDC->SetBkMode(TRANSPARENT);

    // 辅助：绘制文字并返回高度
    auto drawText = [&](int x, int yPos, CFont* font, COLORREF color, const wchar_t* text, int maxW = 0) -> int {
        pDC->SelectObject(font);
        pDC->SetTextColor(color);
        CRect rc(x, yPos, maxW > 0 ? x + maxW : R, yPos + 200);
        pDC->DrawTextW(text, -1, &rc, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
        pDC->DrawTextW(text, -1, &rc, DT_LEFT | DT_WORDBREAK);
        return rc.Height();
    };

    // ── 标题 ──
    y += drawText(L, y, &fTitle, RGB(30, 30, 30), L"\u56fe\u5f62\u5206\u6790") + 8;

    // ── 基本统计 ──
    swprintf_s(buf, L"\u603b\u64ad\u653e: %d \u6b21  \u2502  \u65f6\u957f: %d\u65f0%02d\u5206%02d\u79d2  \u2502  \u5b8c\u6574\u7387: %d%%",
        total, total_dur / 3600, (total_dur % 3600) / 60, total_dur % 60,
        total > 0 ? completed * 100 / total : 0);
    {
        CRect rc(L, y, R, y + 60);
        pDC->SelectObject(&fText);
        pDC->SetTextColor(RGB(70, 70, 70));
        pDC->DrawTextW(buf, -1, &rc, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
        pDC->DrawTextW(buf, -1, &rc, DT_LEFT | DT_WORDBREAK);
        y = rc.bottom + 10;
    }

    // 分隔线
    CPen penLine(PS_SOLID, 1, RGB(210, 210, 215));
    pDC->SelectObject(&penLine);
    pDC->MoveTo(L, y); pDC->LineTo(R, y);
    y += 10;

    // ── 时段柱状图 ──
    y += drawText(L, y, &fSection, RGB(40, 40, 40), L"\u542c\u6b4c\u65f6\u6bb5") + 8;

    int max_h = 1;
    for (int i = 0; i < 24; i++) if (hour_count[i] > max_h) max_h = hour_count[i];

    int cL = L + 28, cR = R - 8;
    int cT = y, cB = y + 90;
    int bw = (cR - cL) / 24;
    if (bw < 3) bw = 3;

    CPen penAxis(PS_SOLID, 1, RGB(180, 180, 180));
    pDC->SelectObject(&penAxis);
    pDC->MoveTo(cL, cB); pDC->LineTo(cR, cB);

    for (int i = 0; i < 24; i++)
    {
        int bh = hour_count[i] > 0 ? max(2, (int)((double)hour_count[i] / max_h * (cB - cT - 2))) : 0;
        int bx = cL + i * bw;
        COLORREF clr = i < 6 ? RGB(80, 110, 190) : i < 12 ? RGB(100, 170, 230) : i < 18 ? RGB(110, 185, 170) : RGB(150, 120, 195);
        CBrush br(clr);
        pDC->FillRect(CRect(bx + 1, cB - bh, bx + max(1, bw - 2), cB), &br);
        if (i % 4 == 0)
        {
            pDC->SelectObject(&fText);
            pDC->SetTextColor(RGB(100, 100, 100));
            swprintf_s(buf, L"%02d", i);
            pDC->TextOutW(bx, cB + 3, buf, 2);
        }
    }
    y = cB + 36;

    // 分隔线
    pDC->SelectObject(&penLine);
    pDC->MoveTo(L, y); pDC->LineTo(R, y);
    y += 10;

    // ── 播放结果 ──
    y += drawText(L, y, &fSection, RGB(40, 40, 40), L"\u64ad\u653e\u7ed3\u679c") + 8;

    struct { const wchar_t* lbl; int cnt; COLORREF clr; } pie[] = {
        { L"\u5b8c\u6574", completed, RGB(90, 175, 90) },
        { L"\u8df3\u8fc7", skipped,   RGB(225, 145, 70) },
        { L"\u505c\u6b62", stopped,   RGB(110, 150, 210) },
        { L"\u51fa\u9519", errored,   RGB(215, 90, 90) },
    };

    int barL = L, barH = 20;
    for (auto& it : pie)
    {
        int segW = total > 0 ? max(it.cnt > 0 ? 2 : 0, (int)((double)it.cnt / total * (R - L))) : 0;
        if (segW > 0)
        {
            CBrush br(it.clr);
            pDC->FillRect(CRect(barL, y, barL + segW, y + barH), &br);
            barL += segW;
        }
    }
    y += barH + 8;

    // 图例
    int lx = L;
    pDC->SelectObject(&fText);
    for (auto& it : pie)
    {
        CBrush br(it.clr);
        pDC->FillRect(CRect(lx, y, lx + 12, y + 12), &br);
        pDC->SetTextColor(RGB(60, 60, 60));
        int pct = total > 0 ? (int)((double)it.cnt / total * 100) : 0;
        swprintf_s(buf, L"%s %d (%d%%)", it.lbl, it.cnt, pct);
        pDC->TextOutW(lx + 16, y - 1, buf, (int)wcslen(buf));
        lx += 160;
        if (lx > R - 140) { lx = L; y += 20; }
    }
    y += 32;

    // 分隔线
    pDC->SelectObject(&penLine);
    pDC->MoveTo(L, y); pDC->LineTo(R, y);
    y += 16;

    // ── Top 10 艺术家 ──
    y += drawText(L, y, &fSection, RGB(40, 40, 40), L"Top 10 \u827a\u672f\u5bb6") + 8;

    std::vector<std::pair<std::wstring, int>> sorted(artist_time.begin(), artist_time.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
    int topN = min((int)sorted.size(), 10);
    int max_at = 1;
    for (int i = 0; i < topN; i++) if (sorted[i].second > max_at) max_at = sorted[i].second;

    COLORREF ac[] = { RGB(100,170,230), RGB(230,130,100), RGB(130,190,130), RGB(200,160,220), RGB(240,200,100), RGB(100,200,200), RGB(230,170,130), RGB(170,150,230), RGB(180,220,100), RGB(220,120,160) };
    int nameW = 160, barMaxW = R - L - nameW - 70;
    int rowH = 22;

    for (int i = 0; i < topN && y + rowH < rect.bottom - 5; i++)
    {
        pDC->SelectObject(&fText);
        pDC->SetTextColor(RGB(50, 50, 50));
        std::wstring lbl = std::to_wstring(i + 1) + L". " + sorted[i].first;
        CRect rcName(L, y, L + nameW - 8, y + rowH);
        pDC->DrawTextW(lbl.c_str(), (int)lbl.size(), &rcName, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        int bw2 = max(3, (int)((double)sorted[i].second / max_at * barMaxW));
        CBrush br(ac[i % 10]);
        pDC->FillRect(CRect(L + nameW, y + 3, L + nameW + bw2, y + rowH - 3), &br);

        pDC->SetTextColor(RGB(110, 110, 110));
        int dur = sorted[i].second;
        swprintf_s(buf, L"%d:%02d", dur / 60, dur % 60);
        pDC->TextOutW(L + nameW + bw2 + 10, y + 2, buf, (int)wcslen(buf));
        y += rowH;
    }

    pDC->SelectStockObject(BLACK_PEN);
    pDC->SelectStockObject(NULL_BRUSH);
    // ReleaseDC(pDC);
}

// ── 原有图表绘制 ──

void CPlayStatisticsDlg::OnPaint()
{
    CPaintDC dc(this);

    if (m_tab.GetCurSel() != 2)
        return;

    DrawAnalysisChart(&dc);
}


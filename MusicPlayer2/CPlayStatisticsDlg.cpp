#include "stdafx.h"
#include "MusicPlayer2.h"
#include "CPlayStatisticsDlg.h"
#include "PlayStatistics.h"
#include "StatHelpDlg.h"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <afxdtctl.h>

namespace
{
    // 60s 兜底刷新定时器
    const UINT_PTR TIMER_ID_REFRESH = 1;

    // 日期键（YYYYMMDD）加/减天数
    int YmdAddDays(int ymd, int days)
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

    // 日期键（YYYYMMDD）与显示文本（yyyy-MM-dd）互转；日期用 CEditEx 输入，不再用原生日期选择器
    CString YmdToText(int ymd)
    {
        if (ymd <= 0) return CString();
        CString s;
        s.Format(L"%04d-%02d-%02d", ymd / 10000, (ymd / 100) % 100, ymd % 100);
        return s;
    }

    // 解析 yyyy-MM-dd（也容忍 / 与 . 作分隔符、前后空格）；失败返回 -1
    int TextToYmd(const CString& text_in)
    {
        CString text{ text_in };
        text.Trim();
        if (text.GetLength() < 8) return -1;

        // 统一分隔符为 '-'
        text.Replace(L'/', L'-');
        text.Replace(L'.', L'-');

        int year = 0, month = 0, day = 0;
        if (swscanf_s(text.GetString(), L"%d-%d-%d", &year, &month, &day) != 3) return -1;
        if (year < 1970 || year > 9999) return -1;
        if (month < 1 || month > 12) return -1;
        if (day < 1 || day > 31) return -1;

        // 用 mktime 做真实日期校验（能识别 2 月 30 日这类非法值）
        struct tm tv = {};
        tv.tm_year = year - 1900;
        tv.tm_mon = month - 1;
        tv.tm_mday = day;
        tv.tm_hour = 12;
        time_t t = mktime(&tv);
        if (t == static_cast<time_t>(-1)) return -1;
        struct tm out = {};
        localtime_s(&out, &t);
        if (out.tm_year + 1900 != year || out.tm_mon + 1 != month || out.tm_mday != day)
            return -1;

        return year * 10000 + month * 100 + day;
    }
}

IMPLEMENT_DYNAMIC(CPlayStatisticsDlg, CBaseDialog)

CPlayStatisticsDlg::CPlayStatisticsDlg(CWnd* pParent)
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
    SetDlgItemTextW(IDC_STAT_EXPORT_AGG_BTN, L"导出聚合CSV");
    SetDlgItemTextW(IDC_STAT_REPORT_BTN, L"生成报告");
    SetDlgItemTextW(IDCANCEL, L"关闭");

    // 主对话框最小尺寸（PRD 520x340）
    SetMinSize(theApp.DPI(480), theApp.DPI(300));

    RepositionTextBasedControls({
        { CtrlTextInfo::L4, IDC_EXPORT_CSV_BTN, CtrlTextInfo::W32 },
        { CtrlTextInfo::L3, IDC_EXPORT_JSON_BTN, CtrlTextInfo::W32 },
        { CtrlTextInfo::L2, IDC_STAT_EXPORT_AGG_BTN, CtrlTextInfo::W32 },
        { CtrlTextInfo::L1, IDC_STAT_REPORT_BTN, CtrlTextInfo::W32 },
        { CtrlTextInfo::R1, IDCANCEL, CtrlTextInfo::W32 }
        });
    return true;
}

void CPlayStatisticsDlg::DoDataExchange(CDataExchange* pDX)
{
    CBaseDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_TAB, m_tab);
    DDX_Control(pDX, IDC_STAT_RANGE_PRESET, m_preset_combo);
    DDX_Control(pDX, IDC_STAT_DATE_FROM, m_date_from);
    DDX_Control(pDX, IDC_STAT_DATE_TO, m_date_to);
}

BEGIN_MESSAGE_MAP(CPlayStatisticsDlg, CBaseDialog)
    ON_BN_CLICKED(IDC_EXPORT_CSV_BTN, &CPlayStatisticsDlg::OnBnClickedExportCsvButton)
    ON_BN_CLICKED(IDC_EXPORT_JSON_BTN, &CPlayStatisticsDlg::OnBnClickedExportJsonButton)
    ON_BN_CLICKED(IDC_STAT_EXPORT_AGG_BTN, &CPlayStatisticsDlg::OnBnClickedExportAggButton)
    ON_BN_CLICKED(IDC_STAT_REPORT_BTN, &CPlayStatisticsDlg::OnBnClickedReportButton)
    ON_CBN_SELCHANGE(IDC_STAT_RANGE_PRESET, &CPlayStatisticsDlg::OnCbnSelchangeRangePreset)
    ON_EN_KILLFOCUS(IDC_STAT_DATE_FROM, &CPlayStatisticsDlg::OnEnKillfocusDateFrom)
    ON_EN_KILLFOCUS(IDC_STAT_DATE_TO, &CPlayStatisticsDlg::OnEnKillfocusDateTo)
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_MESSAGE(WM_STAT_RECORD_APPENDED, &CPlayStatisticsDlg::OnStatRecordAppended)
END_MESSAGE_MAP()

// ─────────────────────────────────────────────────────────────────────────────
// 过滤条初始化与联动
// ─────────────────────────────────────────────────────────────────────────────

void CPlayStatisticsDlg::FillPresetCombo()
{
    CComboBox* pCombo = &m_preset_combo;
    if (pCombo == nullptr) return;
    pCombo->ResetContent();
    pCombo->AddString(L"近7天");      // Last7
    pCombo->AddString(L"近30天");     // Last30
    pCombo->AddString(L"近90天");     // Last90
    pCombo->AddString(L"今年");       // ThisYear
    pCombo->AddString(L"去年");       // LastYear
    pCombo->AddString(L"全部");       // All
    pCombo->AddString(L"自定义");     // Custom
}

void CPlayStatisticsDlg::ApplyPresetToFilter(RangePreset preset)
{
    m_filter.preset = preset;

    if (preset == RangePreset::Custom)
    {
        // 自定义：保留现有 from/to，仅切换预设标记
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    int today = st.wYear * 10000 + st.wMonth * 100 + st.wDay;

    switch (preset)
    {
    case RangePreset::Last7:
        m_filter.from_ymd = YmdAddDays(today, -6);
        m_filter.to_ymd = today;
        break;
    case RangePreset::Last30:
        m_filter.from_ymd = YmdAddDays(today, -29);
        m_filter.to_ymd = today;
        break;
    case RangePreset::Last90:
        m_filter.from_ymd = YmdAddDays(today, -89);
        m_filter.to_ymd = today;
        break;
    case RangePreset::ThisYear:
        m_filter.from_ymd = st.wYear * 10000 + 101;
        m_filter.to_ymd = today;
        break;
    case RangePreset::LastYear:
        m_filter.from_ymd = (st.wYear - 1) * 10000 + 101;
        m_filter.to_ymd = (st.wYear - 1) * 10000 + 1231;
        break;
    case RangePreset::All:
    default:
        m_filter.from_ymd = 0;
        m_filter.to_ymd = 0;
        break;
    }
}

void CPlayStatisticsDlg::SyncDatePickersFromFilter()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    int today = st.wYear * 10000 + st.wMonth * 100 + st.wDay;

    int from_disp = (m_filter.from_ymd != 0) ? m_filter.from_ymd : today;
    int to_disp = (m_filter.to_ymd != 0) ? m_filter.to_ymd : today;

    m_date_from.SetWindowText(YmdToText(from_disp));
    m_date_to.SetWindowText(YmdToText(to_disp));
}

void CPlayStatisticsDlg::SyncPresetComboToFilter()
{
    CComboBox* pCombo = &m_preset_combo;
    if (pCombo != nullptr)
        pCombo->SetCurSel(static_cast<int>(m_filter.preset));
}

void CPlayStatisticsDlg::InitFilterControls()
{
    FillPresetCombo();

    // 日期用项目自带的 CEditEx 输入（yyyy-MM-dd），全项目没有日期选择器，
    // 用原生 SysDateTimePick32 会显得突兀；这里只限制长度并在失焦时校验。
    m_date_from.SetLimitText(10);
    m_date_to.SetLimitText(10);

    // 默认：近 30 天，粒度 天
    m_filter = StatFilter{};
    m_filter.grain = Grain::Day;
    ApplyPresetToFilter(RangePreset::Last30);

    SyncDatePickersFromFilter();
    SyncPresetComboToFilter();
}

// ─────────────────────────────────────────────────────────────────────────────
// 数据加载 / 过滤 / 广播
// ─────────────────────────────────────────────────────────────────────────────

void CPlayStatisticsDlg::ApplyFilter()
{
    m_filtered_records.clear();
    m_filtered_records.reserve(m_all_records.size());

    for (const auto& r : m_all_records)
    {
        int ymd = CStatAnalysis::YmdOf(r.played_at);
        if (ymd == 0) continue;     // 无法定位时间的记录不参与统计
        if (m_filter.from_ymd != 0 && ymd < m_filter.from_ymd) continue;
        if (m_filter.to_ymd != 0 && ymd > m_filter.to_ymd) continue;
        m_filtered_records.push_back(r);
    }

    m_context.records = &m_filtered_records;
    m_context.filter = m_filter;
    m_context.summary = CStatAnalysis::ComputeSummary(m_filtered_records);
    m_context.valid = true;
}

void CPlayStatisticsDlg::BroadcastContext()
{
    m_overview_dlg.SetContext(&m_context);
    m_trend_dlg.SetContext(&m_context);
    m_artist_rank_dlg.SetContext(&m_context);
    m_album_rank_dlg.SetContext(&m_context);
    m_song_rank_dlg.SetContext(&m_context);
    m_genre_dlg.SetContext(&m_context);
    m_songs_dlg.SetContext(&m_context);
    m_profile_dlg.SetContext(&m_context);
}

void CPlayStatisticsDlg::RefreshAllViews()
{
    LoadRecords();
    ApplyFilter();
    BroadcastContext();
}

// ─────────────────────────────────────────────────────────────────────────────
// 初始化
// ─────────────────────────────────────────────────────────────────────────────

BOOL CPlayStatisticsDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    // 过滤条（默认近 30 天）
    InitFilterControls();

    // 解析全量记录（打开期只解析一次）
    LoadRecords();

    // 创建子对话框
    m_overview_dlg.Create(IDD_STAT_OVERVIEW_DLG, &m_tab);
    m_trend_dlg.Create(IDD_STAT_TREND_DLG, &m_tab);
    m_artist_rank_dlg.Create(IDD_STAT_ARTIST_RANK_DLG, &m_tab);
    m_album_rank_dlg.Create(IDD_STAT_ALBUM_RANK_DLG, &m_tab);
    m_song_rank_dlg.Create(IDD_STAT_SONG_RANK_DLG, &m_tab);
    m_genre_dlg.Create(IDD_STAT_GENRE_DLG, &m_tab);
    m_songs_dlg.Create(IDD_STAT_SONGS_DLG, &m_tab);
    m_profile_dlg.Create(IDD_STAT_PROFILE_DLG, &m_tab);

    // 添加到 Tab（本批 8 页：概览/趋势/歌手/专辑/曲目/流派/明细/洞察）
    m_tab.AddWindow(&m_overview_dlg, L"概览", IconMgr::IconType::IT_Info);
    m_tab.AddWindow(&m_trend_dlg, L"趋势", IconMgr::IconType::IT_Statistics);
    m_tab.AddWindow(&m_artist_rank_dlg, L"歌手", IconMgr::IconType::IT_Artist);
    m_tab.AddWindow(&m_album_rank_dlg, L"专辑", IconMgr::IconType::IT_Album);
    m_tab.AddWindow(&m_song_rank_dlg, L"曲目", IconMgr::IconType::IT_Music);
    m_tab.AddWindow(&m_genre_dlg, L"流派", IconMgr::IconType::IT_Genre);
    m_tab.AddWindow(&m_songs_dlg, L"明细", IconMgr::IconType::IT_File_Relate);
    m_tab.AddWindow(&m_profile_dlg, L"洞察", IconMgr::IconType::IT_Star);

    m_tab.SetItemSize(CSize(theApp.DPI(56), theApp.DPI(24)));
    m_tab.AdjustTabWindowSize();

    // 首次过滤 + 广播 + 可见页刷新
    ApplyFilter();
    BroadcastContext();
    m_tab.SetCurTab(0);

    // 近实时：注册通知目标 + 60s 兜底定时器
    CPlayStatistics::GetInstance().SetNotifyTarget(m_hWnd);
    m_timer_id = SetTimer(TIMER_ID_REFRESH, 60000, nullptr);

    return TRUE;
}

// 加载所有播放记录（全量解析到 m_all_records）
void CPlayStatisticsDlg::LoadRecords()
{
    m_all_records.clear();

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

                    m_all_records.push_back(std::move(record));
                }
                ifs.close();
            }
        }
    } while (FindNextFile(hFind, &find_data));

    FindClose(hFind);

    std::sort(m_all_records.begin(), m_all_records.end(), [](const PlayRecord& a, const PlayRecord& b) {
        return a.played_at > b.played_at;
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// 过滤条消息处理
// ─────────────────────────────────────────────────────────────────────────────

void CPlayStatisticsDlg::OnCbnSelchangeRangePreset()
{
    CComboBox* pCombo = &m_preset_combo;
    if (pCombo == nullptr) return;
    int sel = pCombo->GetCurSel();
    if (sel < 0) return;

    RangePreset preset = static_cast<RangePreset>(sel);
    ApplyPresetToFilter(preset);
    SyncDatePickersFromFilter();

    ApplyFilter();
    BroadcastContext();
}

void CPlayStatisticsDlg::OnBnClickedExportAggButton()
{
    // 按当前粒度聚合导出（REQ-116）：字段为按粒度聚合指标，UTF-8 BOM，Excel 可直接打开
    std::vector<PeriodBucket> buckets = CStatAnalysis::ComputeBuckets(m_filtered_records, m_filter.grain);

    wchar_t file_path[MAX_PATH] = { 0 };
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = GetSafeHwnd();
    ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"csv";
    ofn.lpstrTitle = L"导出聚合统计 (CSV)";
    if (!GetSaveFileName(&ofn))
        return;

    std::ofstream ofs(file_path, std::ios::binary);
    if (!ofs.is_open())
    {
        AfxMessageBox(L"无法创建文件", MB_ICONERROR);
        return;
    }

    auto to_utf8 = [](const std::wstring& w) -> std::string {
        if (w.empty()) return std::string();
        int len = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(len, 0);
        ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
        return s;
        };
    auto esc = [](const std::wstring& s) -> std::wstring {
        if (s.find(L',') == std::wstring::npos && s.find(L'"') == std::wstring::npos) return s;
        std::wstring r = s;
        size_t pos = 0;
        while ((pos = r.find(L'"', pos)) != std::wstring::npos) { r.insert(pos, 1, L'"'); pos += 2; }
        return L"\"" + r + L"\"";
        };

    ofs << "\xEF\xBB\xBF";     // UTF-8 BOM
    ofs << "周期,播放次数,播放时长(秒),完播次数,跳过次数\r\n";
    for (const auto& b : buckets)
    {
        std::wstring line = esc(b.label) + L"," + std::to_wstring(b.count) + L"," +
            std::to_wstring(b.duration_sec) + L"," + std::to_wstring(b.completed_count) + L"," +
            std::to_wstring(b.skipped_count) + L"\r\n";
        ofs << to_utf8(line);
    }
    ofs.flush();
    ofs.close();
    AfxMessageBox(L"导出完成", MB_ICONINFORMATION);
}

void CPlayStatisticsDlg::OnBnClickedReportButton()
{
    // 报告/海报导出在批次 3 实现（REQ-206）
    AfxMessageBox(L"报告生成将在后续版本提供。", MB_ICONINFORMATION);
}

// ─────────────────────────────────────────────────────────────────────────────
// 近实时刷新
// ─────────────────────────────────────────────────────────────────────────────

void CPlayStatisticsDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_ID_REFRESH)
    {
        RefreshAllViews();
    }
    CBaseDialog::OnTimer(nIDEvent);
}

LRESULT CPlayStatisticsDlg::OnStatRecordAppended(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    RefreshAllViews();
    return 0;
}

void CPlayStatisticsDlg::OnDestroy()
{
    // 先解除通知目标，避免窗口销毁后仍被 PostMessage 到已析构窗口
    CPlayStatistics::GetInstance().SetNotifyTarget(nullptr);
    if (m_timer_id != 0)
    {
        KillTimer(m_timer_id);
        m_timer_id = 0;
    }
    CBaseDialog::OnDestroy();
}

// ─────────────────────────────────────────────────────────────────────────────
// 导出（明细，保持原有行为：导出全部原始记录）
// ─────────────────────────────────────────────────────────────────────────────

void CPlayStatisticsDlg::OnBnClickedExportCsvButton()
{
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

    ofs << L'\xFEFF';
    ofs << L"序号,播放时间,标题,艺术家,专辑,播放时长(秒),歌曲长度(秒),结果,来源\n";

    int idx = 1;
    for (const auto& r : m_all_records)
    {
        std::wstring reason;
        switch (r.finish_reason)
        {
        case PlayRecord::FinishReason::COMPLETED:  reason = L"播完"; break;
        case PlayRecord::FinishReason::SKIPPED:    reason = L"跳过"; break;
        case PlayRecord::FinishReason::STOPPED:    reason = L"停止"; break;
        case PlayRecord::FinishReason::PLAY_ERROR:   reason = L"出错"; break;
        }

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
    for (size_t i = 0; i < m_all_records.size(); i++)
    {
        std::string json = m_all_records[i].ToJson();
        ofs << "  " << json;
        if (i + 1 < m_all_records.size())
            ofs << ",";
        ofs << "\n";
    }
    ofs << "]\n";
    ofs.flush();
    ofs.close();

    AfxMessageBox(L"导出完成", MB_ICONINFORMATION);
}

void CPlayStatisticsDlg::OnEnKillfocusDateFrom()
{
    CString text;
    m_date_from.GetWindowText(text);

    int new_from = TextToYmd(text);
    if (new_from < 0)
    {
        // 非法输入：恢复为过滤器当前值（或今天），不弹窗打断
        SYSTEMTIME st;
        GetLocalTime(&st);
        int today = st.wYear * 10000 + st.wMonth * 100 + st.wDay;
        m_date_from.SetWindowText(YmdToText(m_filter.from_ymd != 0 ? m_filter.from_ymd : today));
        return;
    }

    // 越界：起始晚于结束则拒绝并回退
    if (m_filter.to_ymd != 0 && new_from > m_filter.to_ymd)
    {
        AfxMessageBox(L"\u8d77\u59cb\u65e5\u671f\u665a\u4e8e\u7ed3\u675f\u65e5\u671f\uff0c\u8bf7\u91cd\u65b0\u8f93\u5165\u3002", MB_ICONWARNING);
        m_date_from.SetWindowText(YmdToText(m_filter.from_ymd));
        return;
    }

    if (new_from == m_filter.from_ymd)
    {
        m_date_from.SetWindowText(YmdToText(new_from));   // \u89c4\u8303\u5316\u663e\u793a\uff08\u5982 2026-9-1 -> 2026-09-01\uff09
        return;
    }

    m_filter.from_ymd = new_from;
    m_filter.preset = RangePreset::Custom;
    m_date_from.SetWindowText(YmdToText(new_from));
    SyncPresetComboToFilter();

    ApplyFilter();
    BroadcastContext();
}

void CPlayStatisticsDlg::OnEnKillfocusDateTo()
{
    CString text;
    m_date_to.GetWindowText(text);

    int new_to = TextToYmd(text);
    if (new_to < 0)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        int today = st.wYear * 10000 + st.wMonth * 100 + st.wDay;
        m_date_to.SetWindowText(YmdToText(m_filter.to_ymd != 0 ? m_filter.to_ymd : today));
        return;
    }

    if (m_filter.from_ymd != 0 && new_to < m_filter.from_ymd)
    {
        AfxMessageBox(L"\u7ed3\u675f\u65e5\u671f\u65e9\u4e8e\u8d77\u59cb\u65e5\u671f\uff0c\u8bf7\u91cd\u65b0\u8f93\u5165\u3002", MB_ICONWARNING);
        m_date_to.SetWindowText(YmdToText(m_filter.to_ymd));
        return;
    }

    if (new_to == m_filter.to_ymd)
    {
        m_date_to.SetWindowText(YmdToText(new_to));
        return;
    }

    m_filter.to_ymd = new_to;
    m_filter.preset = RangePreset::Custom;
    m_date_to.SetWindowText(YmdToText(new_to));
    SyncPresetComboToFilter();

    ApplyFilter();
    BroadcastContext();
}

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

    // 日期键（YYYYMMDD）与 SYSTEMTIME 互转（原生日期时间选择器使用）
    int SystemTimeToYmd(const SYSTEMTIME& st)
    {
        return st.wYear * 10000 + st.wMonth * 100 + st.wDay;
    }

    SYSTEMTIME YmdToSystemTime(int ymd)
    {
        SYSTEMTIME st{};
        st.wYear = ymd / 10000;
        st.wMonth = (ymd / 100) % 100;
        st.wDay = ymd % 100;
        return st;
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

    // 主对话框最小尺寸（默认 560x360）
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
    ON_NOTIFY(DTN_DATETIMECHANGE, IDC_STAT_DATE_FROM, &CPlayStatisticsDlg::OnDateTimeChangeFrom)
    ON_NOTIFY(DTN_DATETIMECHANGE, IDC_STAT_DATE_TO, &CPlayStatisticsDlg::OnDateTimeChangeTo)
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_WM_SIZE()
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

    int from_disp = today;
    int to_disp = today;
    if (m_filter.preset == RangePreset::All)
    {
        // 「全部」：把两个选择器显示为实际数据范围（无记录则回落到今天）
        int min_ymd = 0, max_ymd = 0;
        GetDataRange(min_ymd, max_ymd);
        if (min_ymd != 0) from_disp = min_ymd;
        if (max_ymd != 0) to_disp = max_ymd;
    }
    else
    {
        if (m_filter.from_ymd != 0) from_disp = m_filter.from_ymd;
        if (m_filter.to_ymd != 0) to_disp = m_filter.to_ymd;
    }

    SetDatePickerYmd(m_date_from, from_disp);
    SetDatePickerYmd(m_date_to, to_disp);
}

void CPlayStatisticsDlg::SetDatePickerYmd(CDateTimeCtrl& dtp, int ymd)
{
    if (dtp.GetSafeHwnd() == nullptr || ymd <= 0) return;
    SYSTEMTIME st = YmdToSystemTime(ymd);
    m_updating_filter = true;               // 抑制写入触发的 DTN_DATETIMECHANGE 联动
    dtp.SetFormat(L"yyyy-MM-dd");           // 统一显示格式（覆盖资源里的默认格式）
    dtp.SetTime(&st);
    m_updating_filter = false;
}

void CPlayStatisticsDlg::GetDataRange(int& min_ymd, int& max_ymd) const
{
    min_ymd = 0;
    max_ymd = 0;
    for (const auto& r : m_all_records)
    {
        int ymd = CStatAnalysis::YmdOf(r.played_at);
        if (ymd == 0) continue;
        if (min_ymd == 0 || ymd < min_ymd) min_ymd = ymd;
        if (max_ymd == 0 || ymd > max_ymd) max_ymd = ymd;
    }
}

void CPlayStatisticsDlg::UpdateDatePickerEnabled()
{
    BOOL enable = (m_filter.preset == RangePreset::All) ? FALSE : TRUE;
    m_date_from.EnableWindow(enable);
    m_date_to.EnableWindow(enable);
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

    // 原生日期时间选择器：统一显示格式（下拉日历默认可用）
    m_date_from.SetFormat(L"yyyy-MM-dd");
    m_date_to.SetFormat(L"yyyy-MM-dd");

    // 默认：全部时间，粒度 天
    m_filter = StatFilter{};
    m_filter.grain = Grain::Day;
    ApplyPresetToFilter(RangePreset::All);

    SyncDatePickersFromFilter();
    SyncPresetComboToFilter();
    UpdateDatePickerEnabled();
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
    m_artist_rank_dlg.SetContext(&m_context);
    m_album_rank_dlg.SetContext(&m_context);
    m_song_rank_dlg.SetContext(&m_context);
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

    // 先解析全量记录（打开期只解析一次），使「全部」预设能把两个日期选择器
    // 显示为实际数据范围；再初始化过滤条（默认：全部时间）
    LoadRecords();
    InitFilterControls();

    // 创建子对话框
    m_overview_dlg.Create(IDD_STAT_OVERVIEW_DLG, &m_tab);
    m_artist_rank_dlg.Create(IDD_STAT_ARTIST_RANK_DLG, &m_tab);
    m_album_rank_dlg.Create(IDD_STAT_ALBUM_RANK_DLG, &m_tab);
    m_song_rank_dlg.Create(IDD_STAT_SONG_RANK_DLG, &m_tab);
    m_songs_dlg.Create(IDD_STAT_SONGS_DLG, &m_tab);
    m_profile_dlg.Create(IDD_STAT_PROFILE_DLG, &m_tab);

    // 保存子对话框（照 OptionsDlg）
    m_tab_vect.clear();
    m_tab_height.clear();
    m_tab_vect.push_back(&m_overview_dlg);
    m_tab_vect.push_back(&m_artist_rank_dlg);
    m_tab_vect.push_back(&m_album_rank_dlg);
    m_tab_vect.push_back(&m_song_rank_dlg);
    m_tab_vect.push_back(&m_songs_dlg);
    m_tab_vect.push_back(&m_profile_dlg);

    // 获取子对话框的初始高度（必须在 AddWindow/MoveWindow 拉伸之前取）
    for (const auto* pDlg : m_tab_vect)
    {
        CRect rect;
        pDlg->GetWindowRect(rect);
        m_tab_height.push_back(rect.Height());
    }

    // 添加到 Tab（6 页：概览/歌手/专辑/曲目/明细/洞察）
    m_tab.AddWindow(&m_overview_dlg, L"概览", IconMgr::IconType::IT_Info);
    m_tab.AddWindow(&m_artist_rank_dlg, L"歌手", IconMgr::IconType::IT_Artist);
    m_tab.AddWindow(&m_album_rank_dlg, L"专辑", IconMgr::IconType::IT_Album);
    m_tab.AddWindow(&m_song_rank_dlg, L"曲目", IconMgr::IconType::IT_Music);
    m_tab.AddWindow(&m_songs_dlg, L"明细", IconMgr::IconType::IT_File_Relate);
    m_tab.AddWindow(&m_profile_dlg, L"洞察", IconMgr::IconType::IT_Star);

    m_tab.SetItemSize(CSize(theApp.DPI(56), theApp.DPI(24)));
    m_tab.AdjustTabWindowSize();

    // 为每个子窗口设置滚动信息（照 OptionsDlg）
    for (size_t i = 0; i < m_tab_vect.size(); i++)
    {
        m_tab_vect[i]->SetScrollbarInfo(m_tab.m_tab_rect.Height(), m_tab_height[i]);
    }

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
    UpdateDatePickerEnabled();

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
    if (m_filtered_records.empty())
    {
        AfxMessageBox(L"\u6ca1\u6709\u64ad\u653e\u8bb0\u5f55\uff0c\u65e0\u6cd5\u751f\u6210\u62a5\u544a\u3002", MB_ICONINFORMATION);
        return;
    }
    CStatHtmlReport::GenerateAndOpen(m_filtered_records, m_context.summary, m_filter);
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

// 主对话框尺寸变化时，为每个子窗口更新滚动信息（照 OptionsDlg::OnSize）
void CPlayStatisticsDlg::OnSize(UINT nType, int cx, int cy)
{
    CBaseDialog::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED)
    {
        //为每个子窗口更新滚动信息
        for (size_t i = 0; i < m_tab_vect.size(); i++)
        {
            m_tab_vect[i]->SetScrollbarInfo(m_tab.m_tab_rect.Height(), m_tab_height[i]);
        }
    }
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

// 原生日期时间选择器：起始日期变化（DTN_DATETIMECHANGE）
void CPlayStatisticsDlg::OnDateTimeChangeFrom(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMDATETIMECHANGE pdt = reinterpret_cast<LPNMDATETIMECHANGE>(pNMHDR);
    if (pResult != nullptr) *pResult = 0;
    if (m_updating_filter) return;              // 程序化写入 DTP，忽略联动
    if (pdt == nullptr) return;

    int new_from = SystemTimeToYmd(pdt->st);

    // 越界：起始晚于结束 → 警告并用原值回滚该选择器
    if (m_filter.to_ymd != 0 && new_from > m_filter.to_ymd)
    {
        AfxMessageBox(L"起始日期晚于结束日期，请重新选择。", MB_ICONWARNING);
        SetDatePickerYmd(m_date_from, m_filter.from_ymd != 0 ? m_filter.from_ymd : new_from);
        return;
    }

    if (new_from == m_filter.from_ymd && m_filter.preset == RangePreset::Custom)
        return;

    m_filter.from_ymd = new_from;
    m_filter.preset = RangePreset::Custom;
    SyncPresetComboToFilter();
    ApplyFilter();
    BroadcastContext();
}

// 原生日期时间选择器：结束日期变化（DTN_DATETIMECHANGE）
void CPlayStatisticsDlg::OnDateTimeChangeTo(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMDATETIMECHANGE pdt = reinterpret_cast<LPNMDATETIMECHANGE>(pNMHDR);
    if (pResult != nullptr) *pResult = 0;
    if (m_updating_filter) return;              // 程序化写入 DTP，忽略联动
    if (pdt == nullptr) return;

    int new_to = SystemTimeToYmd(pdt->st);

    // 越界：结束早于起始 → 警告并用原值回滚该选择器
    if (m_filter.from_ymd != 0 && new_to < m_filter.from_ymd)
    {
        AfxMessageBox(L"结束日期早于起始日期，请重新选择。", MB_ICONWARNING);
        SetDatePickerYmd(m_date_to, m_filter.to_ymd != 0 ? m_filter.to_ymd : new_to);
        return;
    }

    if (new_to == m_filter.to_ymd && m_filter.preset == RangePreset::Custom)
        return;

    m_filter.to_ymd = new_to;
    m_filter.preset = RangePreset::Custom;
    SyncPresetComboToFilter();
    ApplyFilter();
    BroadcastContext();
}

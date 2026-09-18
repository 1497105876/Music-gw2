// PlayLogStatisticsDlg.cpp: 「歌曲详细记录」页面实现
//
// 主对话框只干三件事：读日志、按时间范围过滤、把算好的快照发给子页。
// 指标口径全部收口在 CStatAnalysis，界面里不出现第二套算法。

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "PlayLogStatisticsDlg.h"
#include <algorithm>

IMPLEMENT_DYNAMIC(CPlayLogStatDlg, CBaseDialog)

namespace
{
    // 范围预设下拉的显示顺序，索引与 RangePreset 枚举一一对应
    const wchar_t* const kRangeText[] = {
        L"近 7 天", L"近 30 天", L"近 90 天", L"今年", L"去年", L"全部", L"自定义"
    };

    int TodayYmd()
    {
        CTime now = CTime::GetCurrentTime();
        return now.GetYear() * 10000 + now.GetMonth() * 100 + now.GetDay();
    }

    int YmdOfTime(const CTime& t)
    {
        return t.GetYear() * 10000 + t.GetMonth() * 100 + t.GetDay();
    }

    // YYYYMMDD -> CTime（非法值返回今天的零点）
    CTime TimeFromYmd(int ymd)
    {
        if (ymd <= 0)
        {
            CTime now = CTime::GetCurrentTime();
            return CTime(now.GetYear(), now.GetMonth(), now.GetDay(), 0, 0, 0);
        }
        return CTime(ymd / 10000, (ymd / 100) % 100, ymd % 100, 0, 0, 0);
    }

    // 按预设算出起止日期（YYYYMMDD，含首尾；0 表示无界）
    void RangeOfPreset(RangePreset preset, int& from_ymd, int& to_ymd)
    {
        CTime now = CTime::GetCurrentTime();
        from_ymd = 0;
        to_ymd = 0;
        switch (preset)
        {
        case RangePreset::Last7:
            from_ymd = YmdOfTime(now - CTimeSpan(6, 0, 0, 0));
            to_ymd = YmdOfTime(now);
            break;
        case RangePreset::Last30:
            from_ymd = YmdOfTime(now - CTimeSpan(29, 0, 0, 0));
            to_ymd = YmdOfTime(now);
            break;
        case RangePreset::Last90:
            from_ymd = YmdOfTime(now - CTimeSpan(89, 0, 0, 0));
            to_ymd = YmdOfTime(now);
            break;
        case RangePreset::ThisYear:
            from_ymd = YmdOfTime(CTime(now.GetYear(), 1, 1, 0, 0, 0));
            to_ymd = YmdOfTime(now);
            break;
        case RangePreset::LastYear:
            from_ymd = YmdOfTime(CTime(now.GetYear() - 1, 1, 1, 0, 0, 0));
            to_ymd = YmdOfTime(CTime(now.GetYear() - 1, 12, 31, 0, 0, 0));
            break;
        case RangePreset::All:
        case RangePreset::Custom:
        default:
            break;
        }
    }
}

CPlayLogStatDlg::CPlayLogStatDlg(CWnd* pParent /*= nullptr*/)
    : CBaseDialog(IDD_PLAY_LOG_STATISTICS_DLG, pParent)
{
}

CPlayLogStatDlg::~CPlayLogStatDlg()
{
}

CString CPlayLogStatDlg::GetDialogName() const
{
    return _T("PlayLogStatDlg");
}

bool CPlayLogStatDlg::InitializeControls()
{
    SetWindowTextW(L"歌曲详细记录");
    SetDlgItemTextW(IDC_PLAYLOG_LABEL_RANGE, L"范围");

    RepositionTextBasedControls({
        { CtrlTextInfo::R3, IDC_PLAYLOG_BTN_REPORT, CtrlTextInfo::W32 },
        { CtrlTextInfo::R2, IDCANCEL, CtrlTextInfo::W32 },
        { CtrlTextInfo::R1, IDC_PLAYLOG_BTN_REFRESH, CtrlTextInfo::W32 }
        });
    return true;
}

void CPlayLogStatDlg::DoDataExchange(CDataExchange* pDX)
{
    CBaseDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PLAYLOG_TAB, m_tab);
    DDX_Control(pDX, IDC_PLAYLOG_RANGE_PRESET, m_range_combo);
    DDX_Control(pDX, IDC_PLAYLOG_DATE_FROM, m_date_from);
    DDX_Control(pDX, IDC_PLAYLOG_DATE_TO, m_date_to);
}

BEGIN_MESSAGE_MAP(CPlayLogStatDlg, CBaseDialog)
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_BN_CLICKED(IDC_PLAYLOG_BTN_REFRESH, &CPlayLogStatDlg::OnBnClickedRefresh)
    ON_BN_CLICKED(IDC_PLAYLOG_BTN_HELP, &CPlayLogStatDlg::OnBnClickedHelp)
    ON_BN_CLICKED(IDC_PLAYLOG_BTN_REPORT, &CPlayLogStatDlg::OnBnClickedReport)
    ON_CBN_SELCHANGE(IDC_PLAYLOG_RANGE_PRESET, &CPlayLogStatDlg::OnCbnSelchangeRangePreset)
    ON_NOTIFY(DTN_DATETIMECHANGE, IDC_PLAYLOG_DATE_FROM, &CPlayLogStatDlg::OnDatetimeChange)
    ON_NOTIFY(DTN_DATETIMECHANGE, IDC_PLAYLOG_DATE_TO, &CPlayLogStatDlg::OnDatetimeChange)
    ON_MESSAGE(WM_STAT_RECORD_APPENDED, &CPlayLogStatDlg::OnRecordAppended)
END_MESSAGE_MAP()

BOOL CPlayLogStatDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    SetIcon(IconMgr::IconType::IT_Statistics, FALSE);

    // ── 范围预设 ──
    for (const wchar_t* text : kRangeText)
        m_range_combo.AddString(text);
    SetRangePreset(RangePreset::Last30);
    m_range_combo.SetCurSel(static_cast<int>(RangePreset::Last30));

    m_date_from.SetFormat(L"yyyy-MM-dd");
    m_date_to.SetFormat(L"yyyy-MM-dd");

    // ── 子页 ──
    m_overview_dlg.Create(IDD_PLAYLOG_STAT_OVERVIEW_DLG);
    m_artist_dlg.Create(IDD_PLAYLOG_STAT_ARTIST_DLG);
    m_album_dlg.Create(IDD_PLAYLOG_STAT_ALBUM_DLG);
    m_song_dlg.Create(IDD_PLAYLOG_STAT_SONG_DLG);
    m_detail_dlg.Create(IDD_PLAYLOG_STAT_DETAIL_DLG);

    m_tab.AddWindow(&m_overview_dlg, L"概览", IconMgr::IconType::IT_Statistics);
    m_tab.AddWindow(&m_artist_dlg, L"歌手", IconMgr::IconType::IT_Music);
    m_tab.AddWindow(&m_album_dlg, L"专辑", IconMgr::IconType::IT_Playlist);
    m_tab.AddWindow(&m_song_dlg, L"曲目", IconMgr::IconType::IT_Music);
    m_tab.AddWindow(&m_detail_dlg, L"明细", IconMgr::IconType::IT_Edit);

    m_tab.SetItemSize(CSize(theApp.DPI(60), theApp.DPI(24)));
    m_tab.AdjustTabWindowSize();
    m_tab.SetCurTab(0);

    // ── 数据 ──
    RefreshAll();

    // 数据读完后才知道日志的真实起止日期，这里再同步一次日期控件
    SyncDateControls();

    // 播放记录写入后通知本窗口刷新；定时兜底 60 秒重读一次
    CPlayStatistics::GetInstance().SetNotifyTarget(GetSafeHwnd());
    SetTimer(TIMER_PERIODIC, 60000, nullptr);

    return TRUE;
}

void CPlayLogStatDlg::OnDestroy()
{
    CPlayStatistics::GetInstance().SetNotifyTarget(nullptr);
    KillTimer(TIMER_PERIODIC);
    KillTimer(TIMER_DEBOUNCE);
    CBaseDialog::OnDestroy();
}

void CPlayLogStatDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_DEBOUNCE)
    {
        KillTimer(TIMER_DEBOUNCE);
        RefreshAll();
    }
    else if (nIDEvent == TIMER_PERIODIC)
    {
        RefreshAll();
    }
    CBaseDialog::OnTimer(nIDEvent);
}

LRESULT CPlayLogStatDlg::OnRecordAppended(WPARAM, LPARAM)
{
    // 播放记录会连续写入，做个防抖，避免每首歌结束都整盘重读
    KillTimer(TIMER_DEBOUNCE);
    SetTimer(TIMER_DEBOUNCE, 3000, nullptr);
    return 0;
}

// ───────────────────────── 数据流水线 ─────────────────────────

void CPlayLogStatDlg::LoadRecords()
{
    m_broken_lines = 0;
    m_failed_files = 0;
    m_all_records = CStatHtmlReport::LoadRecords(&m_broken_lines, &m_failed_files);

    // 倒序：最新的排最前，明细页直接按此顺序展示
    std::sort(m_all_records.begin(), m_all_records.end(),
        [](const PlayRecord& a, const PlayRecord& b) { return a.played_at > b.played_at; });
}

void CPlayLogStatDlg::ApplyFilter()
{
    const int from_ymd = m_data.filter.from_ymd;
    const int to_ymd = m_data.filter.to_ymd;

    m_filtered.clear();
    m_filtered.reserve(m_all_records.size());
    for (const auto& r : m_all_records)
    {
        int ymd = CStatAnalysis::YmdOf(r.played_at);
        if (ymd != 0)
        {
            if (from_ymd > 0 && ymd < from_ymd) continue;
            if (to_ymd > 0 && ymd > to_ymd) continue;
        }
        else if (from_ymd > 0 || to_ymd > 0)
        {
            continue;   // 时间解析不出来，只在「全部」下保留
        }
        m_filtered.push_back(r);
    }

    // ── 聚合：全部走聚合层，界面不自己算 ──
    m_data.records = &m_filtered;
    m_data.summary = CStatAnalysis::ComputeSummary(m_filtered);
    m_data.finish = CStatAnalysis::ComputeFinishBreakdown(m_filtered);
    m_data.artists = CStatAnalysis::ComputeArtistRank(m_filtered, 200);
    m_data.albums = CStatAnalysis::ComputeAlbumRank(m_filtered, 200);
    m_data.songs = CStatAnalysis::ComputeSongRank(m_filtered, 200);
    m_data.day_buckets = CStatAnalysis::ComputeBuckets(m_filtered, Grain::Day);

    int hist[24]{};
    CStatAnalysis::ComputeHourHistogram(m_filtered, hist);
    memcpy(m_data.hour_hist, hist, sizeof(hist));

    int first_ymd = 0, last_ymd = 0;
    for (const auto& b : m_data.day_buckets)
    {
        if (b.key <= 0) continue;
        if (first_ymd == 0 || b.key < first_ymd) first_ymd = b.key;
        if (b.key > last_ymd) last_ymd = b.key;
    }
    m_data.first_ymd = first_ymd;
    m_data.last_ymd = last_ymd;
    m_data.broken_lines = m_broken_lines;
    m_data.failed_files = m_failed_files;
    m_data.load_ok = true;
    m_data.valid = true;

    // ── 下发给子页；SetData 会把各页标脏，切到哪页哪页才真正重算 ──
    m_overview_dlg.SetData(&m_data);
    m_artist_dlg.SetData(&m_data);
    m_album_dlg.SetData(&m_data);
    m_song_dlg.SetData(&m_data);
    m_detail_dlg.SetData(&m_data);

    // 当前可见页立刻刷新（其余页留到切过去时再刷）
    CTabDlg* cur = dynamic_cast<CTabDlg*>(m_tab.GetCurrentTab());
    if (cur != nullptr)
        cur->OnTabEntered();

    UpdateWarningText();
}

void CPlayLogStatDlg::RefreshAll()
{
    LoadRecords();
    ApplyFilter();
}

void CPlayLogStatDlg::UpdateWarningText()
{
    std::wstring text;
    if (m_broken_lines > 0 && m_failed_files > 0)
        text = L"已跳过 " + std::to_wstring(m_broken_lines) + L" 条损坏记录；" + std::to_wstring(m_failed_files) + L" 个日志文件读取失败";
    else if (m_broken_lines > 0)
        text = L"已跳过 " + std::to_wstring(m_broken_lines) + L" 条损坏记录";
    else if (m_failed_files > 0)
        text = std::to_wstring(m_failed_files) + L" 个日志文件读取失败";
    SetDlgItemTextW(IDC_PLAYLOG_WARN_LABEL, text.c_str());
}

void CPlayLogStatDlg::SetRangePreset(RangePreset preset)
{
    int from_ymd = 0, to_ymd = 0;
    if (preset == RangePreset::Custom)
    {
        from_ymd = YmdFromCtrl(m_date_from);
        to_ymd = YmdFromCtrl(m_date_to);
    }
    else
    {
        RangeOfPreset(preset, from_ymd, to_ymd);
    }
    m_data.filter.preset = preset;
    m_data.filter.from_ymd = from_ymd;
    m_data.filter.to_ymd = to_ymd;
    m_data.filter.grain = Grain::Day;
    SyncDateControls();
    EnableDateControls(preset == RangePreset::Custom);
}

void CPlayLogStatDlg::SyncDateControls()
{
    int from_ymd = m_data.filter.from_ymd;
    int to_ymd = m_data.filter.to_ymd;
    if (from_ymd <= 0) from_ymd = m_data.first_ymd > 0 ? m_data.first_ymd : TodayYmd();
    if (to_ymd <= 0) to_ymd = m_data.last_ymd > 0 ? m_data.last_ymd : TodayYmd();

    CTime t_from = TimeFromYmd(from_ymd);
    CTime t_to = TimeFromYmd(to_ymd);
    // SetTime 会触发 DTN_DATETIMECHANGE，用守卫避免被误判成用户手动改日期
    m_syncing_date = true;
    m_date_from.SetTime(&t_from);
    m_date_to.SetTime(&t_to);
    m_syncing_date = false;
}

void CPlayLogStatDlg::EnableDateControls(bool enable)
{
    m_date_ctrl_enabled = enable;
    EnableDlgCtrl(IDC_PLAYLOG_DATE_FROM, enable);
    EnableDlgCtrl(IDC_PLAYLOG_DATE_TO, enable);
}

int CPlayLogStatDlg::YmdFromCtrl(CDateTimeCtrl& ctrl) const
{
    CTime t;
    if (ctrl.GetTime(t) != GDT_VALID) return 0;
    return YmdOfTime(t);
}

// ───────────────────────── 交互 ─────────────────────────

void CPlayLogStatDlg::OnBnClickedRefresh()
{
    RefreshAll();
}

void CPlayLogStatDlg::OnBnClickedHelp()
{
    CPlayLogStatHelpDlg dlg(this);
    dlg.DoModal();
}

void CPlayLogStatDlg::OnBnClickedReport()
{
    if (m_filtered.empty())
    {
        MessageBox(L"当前时间范围内没有播放记录，无法生成报告。", L"歌曲详细记录", MB_ICONINFORMATION);
        return;
    }
    CStatHtmlReport::GenerateAndOpen(m_filtered, m_data.summary, m_data.filter);
}

void CPlayLogStatDlg::OnCbnSelchangeRangePreset()
{
    int index = m_range_combo.GetCurSel();
    if (index < 0 || index >= static_cast<int>(sizeof(kRangeText) / sizeof(kRangeText[0]))) return;
    SetRangePreset(static_cast<RangePreset>(index));
    ApplyFilter();
}

void CPlayLogStatDlg::OnDatetimeChange(NMHDR* pNMHDR, LRESULT* pResult)
{
    if (pResult != nullptr) *pResult = 0;
    // 程序自己同步日期控件时不当作筛选条件变更
    if (m_syncing_date) return;

    // 手工改日期一律切到「自定义」
    m_range_combo.SetCurSel(static_cast<int>(RangePreset::Custom));
    m_data.filter.preset = RangePreset::Custom;
    m_data.filter.from_ymd = YmdFromCtrl(m_date_from);
    m_data.filter.to_ymd = YmdFromCtrl(m_date_to);
    EnableDateControls(true);
    ApplyFilter();
}

// ───────────────────────── 口径说明 ─────────────────────────

IMPLEMENT_DYNAMIC(CPlayLogStatHelpDlg, CBaseDialog)

CPlayLogStatHelpDlg::CPlayLogStatHelpDlg(CWnd* pParent /*= nullptr*/)
    : CBaseDialog(IDD_PLAYLOG_STAT_HELP_DLG, pParent)
{
}

CString CPlayLogStatHelpDlg::GetDialogName() const
{
    return CString();   // 说明框不需要记忆大小
}

void CPlayLogStatHelpDlg::DoDataExchange(CDataExchange* pDX)
{
    CBaseDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PLAYLOG_HELP_EDIT, m_edit);
}

BOOL CPlayLogStatHelpDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    const std::wstring text =
L"数据来源\r\n"
L"本页只读取播放日志 playlog（statistics 目录下的 playlog_YYYY-MM.jsonl），\r\n"
L"每首歌播完写一条，包含开始时间、实际播放时长、曲目总长与结束原因。\r\n"
L"菜单里的「播放统计」用的是另一份数据（每首歌累计一个总秒数，没有时间点），\r\n"
L"两处口径不同，数字对不上是正常的。\r\n"
L"\r\n"
L"计入统计的门槛\r\n"
L"实际播放时长达到 15 秒才计入。除「明细」页外，所有数字都过了这一道过滤；\r\n"
L"明细页展示全部原始记录，末列「计入统计」标了是否达标。\r\n"
L"\r\n"
L"时间范围\r\n"
L"按播放开始时间所在日期（本地时间）取，含首尾两天。\r\n"
L"「自定义」以外的预设会按当天日期自动重算，打开页面时不会停留在旧范围。\r\n"
L"\r\n"
L"几个指标的定义\r\n"
L"播放次数：计入统计的记录条数，不是听完的遍数。\r\n"
L"播放时长：每次实际听的秒数之和，不是曲长之和。\r\n"
L"完成度：本次播放时长 / 曲目总长，超过 100% 按 100% 算。\r\n"
L"播完/跳过/停止/出错：播放结束时的原因，取自播放器的结束状态。\r\n"
L"\r\n"
L"排序口径\r\n"
L"歌手、专辑按累计播放时长排；曲目按播放次数排，次数相同再比时长。\r\n"
L"名字为空的归入「未知歌手」「未知专辑」「未知标题」。\r\n"
L"\r\n"
L"刷新时机\r\n"
L"打开页面读一次；每播完一首歌后自动重读（做了防抖，不会切一次歌就读一次盘）；\r\n"
L"另有 60 秒兜底。也可以点「刷新」手动重读。\r\n"
L"\r\n"
L"网页报告\r\n"
L"按当前时间范围生成，报告里看到的就是页面上这个筛选范围的数据。\r\n"
L"报告文件放在配置目录下的 statistics\\reports，文件名带时间戳，默认保留最近 10 份。\r\n"
L"\r\n"
L"关于损坏记录\r\n"
L"日志里解析不出来的行会被跳过，只在页面底部提示跳过了几条，不影响其它统计。";

    m_edit.SetWindowTextW(text.c_str());
    return TRUE;
}

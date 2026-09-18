// PlayLogStatisticsDlg.cpp: 「歌曲详细记录」页面实现
//
// 一个窗口、一个列表：顶部 5 个按钮切换「概览/歌手/专辑/曲目/明细」，切视图时列表删列重建。
// 主对话框只干三件事：读日志、按时间范围过滤、把算好的快照填进列表。
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

    // 明细视图的上限和批量大小在头文件里（kDetailMaxRows / kDetailBatchSize）

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

    // YYYYMMDD -> SYSTEMTIME（给月历用）；非法值返回 false
    bool SysTimeFromYmd(int ymd, SYSTEMTIME& st)
    {
        if (ymd <= 0) return false;
        ::ZeroMemory(&st, sizeof(st));
        st.wYear = static_cast<WORD>(ymd / 10000);
        st.wMonth = static_cast<WORD>((ymd / 100) % 100);
        st.wDay = static_cast<WORD>(ymd % 100);
        return true;
    }

    // SYSTEMTIME -> YYYYMMDD（给月历用）
    int YmdOfSystemTime(const SYSTEMTIME& st)
    {
        if (st.wYear == 0) return 0;
        return st.wYear * 10000 + st.wMonth * 100 + st.wDay;
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

    // 数字 -> 带一位小数的百分比字符串
    std::wstring PctText(double percent)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f%%", percent);
        return buf;
    }

    std::wstring CountText(int n)
    {
        return std::to_wstring(n);
    }
}

// ───────────────────────── 范围下拉的薄子类 ─────────────────────────

BEGIN_MESSAGE_MAP(CStatRangeComboBox, CMyComboBox)
    ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

HBRUSH CStatRangeComboBox::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    // 只做配色：进到这里的一定是自己的子部件，不会波及页面上其它控件，所以不判断 nCtlColor
    CMyComboBox::OnCtlColor(pDC, pWnd, nCtlColor);
    if (m_bk_brush.GetSafeHandle() == NULL)
        m_bk_brush.CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(::GetSysColor(COLOR_BTNTEXT));
    return (HBRUSH)m_bk_brush.GetSafeHandle();
}

// ───────────────────────── 构造 / 初始化 ─────────────────────────

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
    DDX_Control(pDX, IDC_PLAYLOG_RANGE_PRESET, m_range_combo);
    DDX_Control(pDX, IDC_PLAYLOG_MAIN_LIST, m_list);
    DDX_Control(pDX, IDC_PLAYLOG_VIEW_TAB, m_view_tab);
    DDX_Control(pDX, IDC_PLAYLOG_CALENDAR, m_cal);
}

BEGIN_MESSAGE_MAP(CPlayLogStatDlg, CBaseDialog)
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_WM_CTLCOLOR()
    ON_BN_CLICKED(IDC_PLAYLOG_BTN_REFRESH, &CPlayLogStatDlg::OnBnClickedRefresh)
    ON_BN_CLICKED(IDC_PLAYLOG_BTN_REPORT, &CPlayLogStatDlg::OnBnClickedReport)
    ON_BN_CLICKED(IDC_PLAYLOG_BTN_RANGE_PICK, &CPlayLogStatDlg::OnBnClickedRangePick)
    ON_NOTIFY(MCN_SELCHANGE, IDC_PLAYLOG_CALENDAR, &CPlayLogStatDlg::OnCalendarSelChange)
    ON_CBN_SELCHANGE(IDC_PLAYLOG_RANGE_PRESET, &CPlayLogStatDlg::OnCbnSelchangeRangePreset)
    ON_NOTIFY(TCN_SELCHANGE, IDC_PLAYLOG_VIEW_TAB, &CPlayLogStatDlg::OnTabSelChange)
    ON_MESSAGE(WM_STAT_RECORD_APPENDED, &CPlayLogStatDlg::OnRecordAppended)
END_MESSAGE_MAP()

BOOL CPlayLogStatDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    SetIcon(IconMgr::IconType::IT_Statistics, FALSE);

    // ── 范围预设 ──
    for (const wchar_t* text : kRangeText)
        m_range_combo.AddString(text);
    // 鼠标滚轮滑过下拉框时不要把预设滚乱
    m_range_combo.SetMouseWheelEnable(false);
    // 默认看全部，进来不用先猜自己想看哪一段
    m_range_combo.SetCurSel(static_cast<int>(RangePreset::All));
    SetRangePreset(RangePreset::All);

    // ── 主列表：扩展样式只在初始化时设一次 ──
    m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);

    // ── 顶部原生页签条 ──
    InitTabCtrl();
    ShowDlgCtrl(IDC_PLAYLOG_DETAIL_NOTICE, false);

    // 月历默认收起，并按它自己的最佳尺寸摆一下，免得留一圈空白
    m_cal.ShowWindow(SW_HIDE);
    CRect rc_min;
    if (m_cal.GetMinReqRect(rc_min))
    {
        CRect rc_cal;
        m_cal.GetWindowRect(rc_cal);
        ScreenToClient(rc_cal);
        m_cal.MoveWindow(rc_cal.left, rc_cal.top, rc_min.Width(), rc_min.Height());
    }

    InitListColumns();

    // ── 数据 ──
    RefreshAll();

    // 播放记录写入后通知本窗口刷新；定时兜底 60 秒重读一次
    CPlayStatistics::GetInstance().SetNotifyTarget(GetSafeHwnd());
    SetTimer(TIMER_PERIODIC, 60000, nullptr);

    return TRUE;
}

void CPlayLogStatDlg::OnDestroy()
{
    StopDetailBatch();      // 先掐掉批次，再清 NotifyTarget
    CPlayStatistics::GetInstance().SetNotifyTarget(nullptr);
    KillTimer(TIMER_PERIODIC);
    KillTimer(TIMER_DEBOUNCE);
    KillTimer(TIMER_RANGE_DEBOUNCE);
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
    else if (nIDEvent == TIMER_DETAIL_BATCH)
    {
        // 明细一批一批插，插完自己停
        if (!AppendDetailBatch())
            KillTimer(TIMER_DETAIL_BATCH);
        return;
    }
    else if (nIDEvent == TIMER_RANGE_DEBOUNCE)
    {
        // 月历里拖选会连续触发，等手停下来再真正重算
        KillTimer(TIMER_RANGE_DEBOUNCE);
        ApplyFilter();
        return;
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
    // m_filtered 马上要被重新填充，明细批次里存的指针会全部失效，先停掉
    StopDetailBatch();

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

    // 只重填当前视图的行，不重建列（避免列宽跳变、避免 2 万行重建两次）
    FillCurrentView();

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
        // 「自定义」是月历里选出来的，沿用已经设好的范围，不要清零
        from_ymd = m_data.filter.from_ymd;
        to_ymd = m_data.filter.to_ymd;
    }
    else
    {
        RangeOfPreset(preset, from_ymd, to_ymd);
    }
    m_data.filter.preset = preset;
    m_data.filter.from_ymd = from_ymd;
    m_data.filter.to_ymd = to_ymd;
    m_data.filter.grain = Grain::Day;
    UpdateRangeButtonText();
}

// 把当前范围写回按钮：有界就显示「起 至 止」，全都不限就显示「不限」
void CPlayLogStatDlg::UpdateRangeButtonText()
{
    const int from_ymd = m_data.filter.from_ymd;
    const int to_ymd = m_data.filter.to_ymd;

    std::wstring text;
    if (from_ymd > 0 && to_ymd > 0)
        text = CStatAnalysis::FormatYmd(from_ymd) + L" 至 " + CStatAnalysis::FormatYmd(to_ymd);
    else if (from_ymd > 0)
        text = CStatAnalysis::FormatYmd(from_ymd) + L" 起";
    else if (to_ymd > 0)
        text = L"截至 " + CStatAnalysis::FormatYmd(to_ymd);
    else
        text = L"不限";

    SetDlgItemTextW(IDC_PLAYLOG_BTN_RANGE_PICK, text.c_str());
}

// ───────────────────────── 日期范围：弹出月历拖选 ─────────────────────────

void CPlayLogStatDlg::OnBnClickedRangePick()
{
    ShowRangeCalendar(!m_cal_visible);      // 再点一次收起
}

void CPlayLogStatDlg::ShowRangeCalendar(bool show)
{
    // 月历是 rc 模板建的，句柄由对话框创建；这里是防御，模板万一没建成就别硬点
    if (m_cal.GetSafeHwnd() == NULL) return;

    if (!show)
    {
        m_cal.ShowWindow(SW_HIDE);
        m_cal_visible = false;
        return;
    }

    // 把当前筛选范围选上，打开就能看到自己正在看哪一段
    SYSTEMTIME st_from{}, st_to{};
    const bool has_from = SysTimeFromYmd(m_data.filter.from_ymd, st_from);
    const bool has_to = SysTimeFromYmd(m_data.filter.to_ymd, st_to);
    if (has_from && has_to)
    {
        m_cal.SetSelRange(&st_from, &st_to);
    }
    else
    {
        // 没有范围时定位到今天（MCS_MULTISELECT 下 SetCurSel 只负责定位，不产生选区）
        SYSTEMTIME st_today{};
        ::GetLocalTime(&st_today);
        m_cal.SetCurSel(&st_today);
    }

    m_cal.ShowWindow(SW_SHOW);
    m_cal.SetFocus();
    m_cal_visible = true;
}

void CPlayLogStatDlg::OnCalendarSelChange(NMHDR* pNMHDR, LRESULT* pResult)
{
    if (pResult != nullptr) *pResult = 0;
    LPNMSELCHANGE pSel = reinterpret_cast<LPNMSELCHANGE>(pNMHDR);
    if (pSel == nullptr) return;

    const int from_ymd = YmdOfSystemTime(pSel->stSelStart);
    const int to_ymd = YmdOfSystemTime(pSel->stSelEnd);
    if (from_ymd <= 0 && to_ymd <= 0) return;

    ApplyCalendarRange(from_ymd, to_ymd);
}

void CPlayLogStatDlg::ApplyCalendarRange(int from_ymd, int to_ymd)
{
    // 月历选出来的一律算「自定义」
    m_range_combo.SetCurSel(static_cast<int>(RangePreset::Custom));
    m_data.filter.preset = RangePreset::Custom;
    m_data.filter.from_ymd = from_ymd;
    m_data.filter.to_ymd = to_ymd;
    UpdateRangeButtonText();

    // 拖选过程中会连续触发，做个防抖：手停下来 400ms 后才真正重算
    KillTimer(TIMER_RANGE_DEBOUNCE);
    SetTimer(TIMER_RANGE_DEBOUNCE, 400, nullptr);
}

// 月历开着的时候，点到月历和「选择日期范围」按钮以外的地方就收起。
// 放在 PreTranslateMessage 里按坐标判断，而不是靠 NM_KILLFOCUS ——
// 失焦通知跟按钮的 toggle 会打架（点按钮收起的瞬间月历失焦，又被弹回来）。
BOOL CPlayLogStatDlg::PreTranslateMessage(MSG* pMsg)
{
    if (m_cal_visible && pMsg->message == WM_LBUTTONDOWN)
    {
        CRect rc_cal;
        m_cal.GetWindowRect(rc_cal);
        CRect rc_btn;
        CWnd* p_btn = GetDlgItem(IDC_PLAYLOG_BTN_RANGE_PICK);
        if (p_btn != nullptr)
            p_btn->GetWindowRect(rc_btn);

        const CPoint pt(pMsg->pt);
        if (!rc_cal.PtInRect(pt) && !rc_btn.PtInRect(pt))
            ShowRangeCalendar(false);
    }
    return CBaseDialog::PreTranslateMessage(pMsg);
}

// ───────────────────────── 视图切换 ─────────────────────────

void CPlayLogStatDlg::InitTabCtrl()
{
    // 页签图标，顺序跟 PlayLogStatView 枚举一致
    const IconMgr::IconType icons[kViewCount] = {
        IconMgr::IconType::IT_Statistics,   // 概览
        IconMgr::IconType::IT_Artist,       // 歌手
        IconMgr::IconType::IT_Album,        // 专辑
        IconMgr::IconType::IT_Music,        // 曲目
        IconMgr::IconType::IT_History,      // 明细
        IconMgr::IconType::IT_Info,         // 洞察
        IconMgr::IconType::IT_Online,       // AI 对话
    };

    for (int i = 0; i < kViewCount; ++i)
        m_view_tab.InsertItem(i, kViewTabText[i], i);

    // 做法同 CTabCtrlEx::AdjustTabWindowSize，只是这里我们把 ImageList 放在成员里，
    // 让它活到窗口销毁，避免局部对象析构后页签图标变空白。
    CSize icon_size = IconMgr::GetIconSize(IconMgr::IconSize::IS_DPI_16);
    m_tab_img_list.Create(icon_size.cx, icon_size.cy, ILC_COLOR32 | ILC_MASK, kViewCount, 1);
    for (int i = 0; i < kViewCount; ++i)
    {
        HICON hIcon = theApp.m_icon_mgr.GetHICON(icons[i], IconMgr::IconStyle::IS_OutlinedDark, IconMgr::IconSize::IS_DPI_16);
        m_tab_img_list.Add(hIcon);
    }
    m_view_tab.SetImageList(&m_tab_img_list);

    m_view_tab.SetCurSel(static_cast<int>(m_cur_view));
}

void CPlayLogStatDlg::SwitchView(PlayLogStatView view)
{
    if (view == m_cur_view) return;     // 重复点同一个页签：直接忽略
    m_cur_view = view;

    // 切走了就别再往旧视图里插行
    StopDetailBatch();

    ShowDlgCtrl(IDC_PLAYLOG_DETAIL_NOTICE, view == PlayLogStatView::Detail);

    InitListColumns();      // 删列 + 重建列
    FillCurrentView();      // 立刻填当前数据
}

void CPlayLogStatDlg::InitListColumns()
{
    // 1) 删掉所有旧列（MFC 没有 DeleteAllColumns，从后往前删）
    if (CHeaderCtrl* pHeader = m_list.GetHeaderCtrl())
    {
        for (int n = pHeader->GetItemCount(); n > 0; --n)
            m_list.DeleteColumn(n - 1);
    }
    m_list.DeleteAllItems();

    // 2) 按当前视图建列：固定列写死 DPI(n)，主列吃掉剩余宽度（做法同 CPlayStatisticsDlg）
    CRect rect;
    m_list.GetWindowRect(rect);

    switch (m_cur_view)
    {
    case PlayLogStatView::Overview:
    {
        int width[2];
        width[0] = theApp.DPI(150);                                 // 统计项（固定）
        width[1] = rect.Width() - width[0] - theApp.DPI(20) - 1;    // 数值（主列，吃剩余）
        if (width[1] < theApp.DPI(120)) width[1] = theApp.DPI(120);
        m_list.InsertColumn(0, L"统计项", LVCFMT_LEFT, width[0]);
        m_list.InsertColumn(1, L"数值", LVCFMT_LEFT, width[1]);
        break;
    }
    case PlayLogStatView::Artist:
    {
        int width[5];
        width[0] = theApp.DPI(40);   // 名次（固定）
        width[2] = theApp.DPI(90);   // 播放时长（固定）
        width[3] = theApp.DPI(60);   // 次数（固定）
        width[4] = theApp.DPI(70);   // 曲目数（固定）
        width[1] = rect.Width() - width[0] - width[2] - width[3] - width[4] - theApp.DPI(20) - 1;   // 歌手（主列）
        if (width[1] < theApp.DPI(80)) width[1] = theApp.DPI(80);
        m_list.InsertColumn(0, L"名次", LVCFMT_LEFT, width[0]);
        m_list.InsertColumn(1, L"歌手", LVCFMT_LEFT, width[1]);
        m_list.InsertColumn(2, L"播放时长", LVCFMT_LEFT, width[2]);
        m_list.InsertColumn(3, L"次数", LVCFMT_LEFT, width[3]);
        m_list.InsertColumn(4, L"曲目数", LVCFMT_LEFT, width[4]);
        break;
    }
    case PlayLogStatView::Album:
    {
        int width[4];
        width[0] = theApp.DPI(40);   // 名次（固定）
        width[2] = theApp.DPI(90);   // 播放时长（固定）
        width[3] = theApp.DPI(60);   // 次数（固定）
        width[1] = rect.Width() - width[0] - width[2] - width[3] - theApp.DPI(20) - 1;   // 专辑（主列）
        if (width[1] < theApp.DPI(80)) width[1] = theApp.DPI(80);
        m_list.InsertColumn(0, L"名次", LVCFMT_LEFT, width[0]);
        m_list.InsertColumn(1, L"专辑", LVCFMT_LEFT, width[1]);
        m_list.InsertColumn(2, L"播放时长", LVCFMT_LEFT, width[2]);
        m_list.InsertColumn(3, L"次数", LVCFMT_LEFT, width[3]);
        break;
    }
    case PlayLogStatView::Song:
    {
        int width[6];
        width[0] = theApp.DPI(40);   // 名次（固定）
        width[3] = theApp.DPI(50);   // 次数（固定）
        width[4] = theApp.DPI(90);   // 累计时长（固定）
        width[5] = theApp.DPI(80);   // 最后播放（固定）
        int rest = rect.Width() - width[0] - width[3] - width[4] - width[5] - theApp.DPI(20) - 1;
        width[1] = rest * 5 / 8;     // 标题（主列）
        width[2] = rest * 3 / 8;     // 歌手
        if (width[1] < theApp.DPI(80)) width[1] = theApp.DPI(80);
        if (width[2] < theApp.DPI(60)) width[2] = theApp.DPI(60);
        m_list.InsertColumn(0, L"名次", LVCFMT_LEFT, width[0]);
        m_list.InsertColumn(1, L"标题", LVCFMT_LEFT, width[1]);
        m_list.InsertColumn(2, L"歌手", LVCFMT_LEFT, width[2]);
        m_list.InsertColumn(3, L"次数", LVCFMT_LEFT, width[3]);
        m_list.InsertColumn(4, L"累计时长", LVCFMT_LEFT, width[4]);
        m_list.InsertColumn(5, L"最后播放", LVCFMT_LEFT, width[5]);
        break;
    }
    case PlayLogStatView::Detail:
    {
        int width[9];
        width[0] = theApp.DPI(40);    // 序号（固定）
        width[1] = theApp.DPI(110);   // 播放时间（固定）
        width[4] = theApp.DPI(110);   // 专辑（固定）
        width[5] = theApp.DPI(70);    // 本次播放（固定）
        width[6] = theApp.DPI(70);    // 曲目总长（固定）
        width[7] = theApp.DPI(45);    // 结果（固定）
        width[8] = theApp.DPI(50);    // 计入统计（固定）
        int rest = rect.Width() - width[0] - width[1] - width[4] - width[5] - width[6] - width[7] - width[8] - theApp.DPI(20) - 1;
        width[2] = rest * 5 / 8;      // 标题（主列）
        width[3] = rest * 3 / 8;      // 歌手
        if (width[2] < theApp.DPI(80)) width[2] = theApp.DPI(80);
        if (width[3] < theApp.DPI(60)) width[3] = theApp.DPI(60);
        m_list.InsertColumn(0, L"序号", LVCFMT_LEFT, width[0]);
        m_list.InsertColumn(1, L"播放时间", LVCFMT_LEFT, width[1]);
        m_list.InsertColumn(2, L"标题", LVCFMT_LEFT, width[2]);
        m_list.InsertColumn(3, L"歌手", LVCFMT_LEFT, width[3]);
        m_list.InsertColumn(4, L"专辑", LVCFMT_LEFT, width[4]);
        m_list.InsertColumn(5, L"本次播放", LVCFMT_LEFT, width[5]);
        m_list.InsertColumn(6, L"曲目总长", LVCFMT_LEFT, width[6]);
        m_list.InsertColumn(7, L"结果", LVCFMT_LEFT, width[7]);
        m_list.InsertColumn(8, L"计入统计", LVCFMT_LEFT, width[8]);
        break;
    }
    case PlayLogStatView::Insight:
    case PlayLogStatView::AiChat:
    {
        // 占位页：只有一列说明文字，宽度吃满
        int w = rect.Width() - theApp.DPI(20) - 1;
        if (w < theApp.DPI(120)) w = theApp.DPI(120);
        m_list.InsertColumn(0, L"说明", LVCFMT_LEFT, w);
        break;
    }
    default:
        break;
    }
}

void CPlayLogStatDlg::FillCurrentView()
{
    // 明细是分批插的，它自己管重绘；这里再包一层 SetRedraw 会和批次里的打架
    if (m_cur_view == PlayLogStatView::Detail)
    {
        m_list.DeleteAllItems();
        FillDetailView();
        return;
    }

    m_list.SetRedraw(FALSE);
    m_list.DeleteAllItems();
    switch (m_cur_view)
    {
    case PlayLogStatView::Overview: FillOverviewView(); break;
    case PlayLogStatView::Artist:   FillArtistView();   break;
    case PlayLogStatView::Album:    FillAlbumView();    break;
    case PlayLogStatView::Song:     FillSongView();     break;
    case PlayLogStatView::Insight:  FillPlaceholderView(L"「洞察」还在做，先留个位置"); break;
    case PlayLogStatView::AiChat:   FillPlaceholderView(L"「AI 对话」还在做，先留个位置"); break;
    default: break;
    }
    m_list.SetRedraw(TRUE);
    m_list.Invalidate();
    m_list.UpdateWindow();
}

void CPlayLogStatDlg::AddOverviewRow(int group, const wchar_t* item, const std::wstring& value)
{
    if (group != m_overview_group)
    {
        m_overview_group = group;
        const wchar_t* titles[] = { L"【基本统计】", L"【播放行为】", L"【听歌习惯】", L"【最常听】" };
        int r = m_list.InsertItem(m_overview_row, titles[group]);
        if (r >= 0)
        {
            m_list.SetItemText(r, 1, L"");
            m_overview_row++;
        }
    }
    int r = m_list.InsertItem(m_overview_row, item);
    if (r >= 0)
    {
        m_list.SetItemText(r, 1, value.c_str());
        m_overview_row++;
    }
}

void CPlayLogStatDlg::ShowEmptyRow(const wchar_t* text)
{
    m_list.DeleteAllItems();
    int row = m_list.InsertItem(0, text);
    if (row >= 0 && m_list.GetHeaderCtrl() != nullptr && m_list.GetHeaderCtrl()->GetItemCount() > 1)
        m_list.SetItemText(row, 1, L"");
}

// ───────────────────────── 概 览 ─────────────────────────

void CPlayLogStatDlg::FillOverviewView()
{
    m_list.DeleteAllItems();
    m_overview_row = 0;
    m_overview_group = -1;

    if (!m_data.valid)
    {
        ShowEmptyRow(L"暂无数据");
        return;
    }

    const StatSummary& s = m_data.summary;
    const FinishBreakdown& f = m_data.finish;
    int total = f.total;

    // ── 基本统计 ──
    AddOverviewRow(0, L"总播放时长", CStatAnalysis::FormatDuration(s.total_duration_sec));
    AddOverviewRow(0, L"播放次数", CountText(s.total_count));
    AddOverviewRow(0, L"涉及曲目数", CountText(s.total_songs));
    AddOverviewRow(0, L"活跃天数", CountText(s.active_days));
    AddOverviewRow(0, L"日均时长", s.active_days > 0 ? CStatAnalysis::FormatDuration(s.total_duration_sec / s.active_days) : L"—");
    AddOverviewRow(0, L"日均次数", s.active_days > 0 ? CountText(s.total_count / s.active_days) : L"—");
    AddOverviewRow(0, L"数据起始日", m_data.first_ymd > 0 ? CStatAnalysis::FormatYmd(m_data.first_ymd) : L"—");

    // ── 播放行为 ──
    AddOverviewRow(1, L"播完", total > 0 ? CountText(f.completed) + L"（" + PctText(f.completed * 100.0 / total) + L"）" : L"—");
    AddOverviewRow(1, L"跳过", total > 0 ? CountText(f.skipped) + L"（" + PctText(f.skipped * 100.0 / total) + L"）" : L"—");
    AddOverviewRow(1, L"停止", total > 0 ? CountText(f.stopped) + L"（" + PctText(f.stopped * 100.0 / total) + L"）" : L"—");
    AddOverviewRow(1, L"出错", total > 0 ? CountText(f.errored) + L"（" + PctText(f.errored * 100.0 / total) + L"）" : L"—");
    AddOverviewRow(1, L"平均单次时长", total > 0 ? CStatAnalysis::FormatDuration(s.total_duration_sec / total) : L"—");
    AddOverviewRow(1, L"平均完成度", f.avg_completion > 0 ? PctText(f.avg_completion) : L"—");
    AddOverviewRow(1, L"平均跳过位置", f.avg_skip_completion >= 0 ? PctText(f.avg_skip_completion) : L"—");

    // ── 听歌习惯 ──
    int peak_hour = -1, peak_cnt = 0;
    for (int h = 0; h < 24; h++)
    {
        if (m_data.hour_hist[h] > peak_cnt)
        {
            peak_cnt = m_data.hour_hist[h];
            peak_hour = h;
        }
    }
    AddOverviewRow(2, L"最活跃时段", peak_hour >= 0 ? std::to_wstring(peak_hour) + L":00 - " + std::to_wstring(peak_hour) + L":59" : L"—");

    int night = 0;
    for (int h = 0; h < 24; h++)
        if (h >= 23 || h <= 4) night += m_data.hour_hist[h];
    AddOverviewRow(2, L"深夜占比（23:00-04:59）", total > 0 ? PctText(night * 100.0 / total) : L"—");
    AddOverviewRow(2, L"周末占比", PctText(static_cast<double>(s.weekend_percent)));
    AddOverviewRow(2, L"当前连续天数", CountText(s.current_streak));
    AddOverviewRow(2, L"最长连续天数", CountText(s.longest_streak));
    AddOverviewRow(2, L"平均播放次数", s.total_songs > 0 ? std::to_wstring(s.total_count) + L" / " + std::to_wstring(s.total_songs) : L"—");

    // ── 最常听 ──
    AddOverviewRow(3, L"最常听歌手", s.top_artist.empty() ? L"—" : s.top_artist + L"（" + CStatAnalysis::FormatDuration(s.top_artist_sec) + L"）");
    AddOverviewRow(3, L"最常听专辑", m_data.albums.empty() ? L"—" : m_data.albums.front().album + L"（" + CStatAnalysis::FormatDuration(m_data.albums.front().duration_sec) + L"）");
    AddOverviewRow(3, L"最常听曲目", s.top_song.empty() ? L"—" : s.top_song + L"（" + CountText(s.top_song_count) + L" 次）");

    std::wstring top_day = L"—";
    int top_day_sec = 0;
    for (const auto& b : m_data.day_buckets)
    {
        if (b.duration_sec > top_day_sec)
        {
            top_day_sec = b.duration_sec;
            top_day = b.label + L"（" + CStatAnalysis::FormatDuration(b.duration_sec) + L"）";
        }
    }
    AddOverviewRow(3, L"听得最久的一天", top_day);
}

// ───────────────────────── 歌 手 排 行 ─────────────────────────

void CPlayLogStatDlg::FillArtistView()
{
    m_list.DeleteAllItems();
    if (!m_data.valid || m_data.artists.empty())
    {
        ShowEmptyRow(L"所选时间范围内没有记录");
        return;
    }
    int i = 0;
    for (const auto& it : m_data.artists)
    {
        int row = m_list.InsertItem(i, std::to_wstring(i + 1).c_str());
        if (row < 0) break;
        m_list.SetItemText(row, 1, it.artist.c_str());
        m_list.SetItemText(row, 2, CStatAnalysis::FormatDuration(it.duration_sec).c_str());
        m_list.SetItemText(row, 3, CountText(it.count).c_str());
        m_list.SetItemText(row, 4, CountText(it.song_count).c_str());
        i++;
    }
}

// ───────────────────────── 专 辑 排 行 ─────────────────────────

void CPlayLogStatDlg::FillAlbumView()
{
    m_list.DeleteAllItems();
    if (!m_data.valid || m_data.albums.empty())
    {
        ShowEmptyRow(L"所选时间范围内没有记录");
        return;
    }
    int i = 0;
    for (const auto& it : m_data.albums)
    {
        int row = m_list.InsertItem(i, std::to_wstring(i + 1).c_str());
        if (row < 0) break;
        m_list.SetItemText(row, 1, it.album.c_str());
        m_list.SetItemText(row, 2, CStatAnalysis::FormatDuration(it.duration_sec).c_str());
        m_list.SetItemText(row, 3, CountText(it.count).c_str());
        i++;
    }
}

// ───────────────────────── 曲 目 排 行 ─────────────────────────

void CPlayLogStatDlg::FillSongView()
{
    m_list.DeleteAllItems();
    if (!m_data.valid || m_data.songs.empty())
    {
        ShowEmptyRow(L"所选时间范围内没有记录");
        return;
    }
    int i = 0;
    for (const auto& it : m_data.songs)
    {
        int row = m_list.InsertItem(i, std::to_wstring(i + 1).c_str());
        if (row < 0) break;
        std::wstring title = it.title.empty() ? L"未知标题" : it.title;
        m_list.SetItemText(row, 1, title.c_str());
        m_list.SetItemText(row, 2, it.artist.empty() ? L"未知歌手" : it.artist.c_str());
        m_list.SetItemText(row, 3, CountText(it.count).c_str());
        m_list.SetItemText(row, 4, CStatAnalysis::FormatDuration(it.duration_sec).c_str());
        m_list.SetItemText(row, 5, it.last_ymd > 0 ? CStatAnalysis::FormatYmd(it.last_ymd).c_str() : L"—");
        i++;
    }
}

// ───────────────────────── 播 放 明 细 ─────────────────────────

// 明细的数据量可能很大，一次全塞进列表会明显卡一下。
// 这里先挑出要展示的行，再一批一批插：第一批同步插完让用户马上看到东西，
// 剩下的交给 TIMER_DETAIL_BATCH 慢慢补。
void CPlayLogStatDlg::FillDetailView()
{
    m_detail_rows.clear();
    m_detail_next = 0;
    m_detail_truncated = false;
    m_list.DeleteAllItems();

    if (!m_data.valid || m_data.records == nullptr || m_data.records->empty())
    {
        ShowEmptyRow(L"所选时间范围内没有记录");
        return;
    }

    // 时长为 0 的不展示：那种基本是刚点开就切走，没有任何可看的信息
    for (const auto& r : *m_data.records)
    {
        if (r.play_duration_sec <= 0) continue;
        if (static_cast<int>(m_detail_rows.size()) >= kDetailMaxRows)
        {
            m_detail_truncated = true;
            break;
        }
        m_detail_rows.push_back(&r);
    }

    if (m_detail_rows.empty())
    {
        ShowEmptyRow(L"所选时间范围内没有有效播放记录");
        return;
    }

    StartDetailBatch();
}

void CPlayLogStatDlg::StartDetailBatch()
{
    // 返回 true 说明还剩下没插完，挂个定时器继续
    if (AppendDetailBatch())
        SetTimer(TIMER_DETAIL_BATCH, 30, nullptr);
}

bool CPlayLogStatDlg::AppendDetailBatch()
{
    const int total = static_cast<int>(m_detail_rows.size());
    const int end = (std::min)(m_detail_next + kDetailBatchSize, total);

    m_list.SetRedraw(FALSE);
    for (; m_detail_next < end; ++m_detail_next)
        InsertDetailRow(*m_detail_rows[m_detail_next], m_detail_next);
    m_list.SetRedraw(TRUE);

    if (m_detail_next < total)
        return true;        // 还没插完，等下一批

    // 全部插完了：如果是因为撞到上限才停的，末尾补一行说明
    if (m_detail_truncated)
    {
        const std::wstring tip = L"仅显示最近 " + std::to_wstring(kDetailMaxRows) + L" 条";
        int row = m_list.InsertItem(m_detail_next, L"…");
        if (row >= 0)
            m_list.SetItemText(row, 1, tip.c_str());
    }
    return false;
}

void CPlayLogStatDlg::StopDetailBatch()
{
    KillTimer(TIMER_DETAIL_BATCH);
    m_detail_rows.clear();
    m_detail_next = 0;
    m_detail_truncated = false;
}

void CPlayLogStatDlg::InsertDetailRow(const PlayRecord& r, int index)
{
    int row = m_list.InsertItem(index, std::to_wstring(index + 1).c_str());
    if (row < 0) return;

    std::wstring time_text = r.played_at;
    if (time_text.size() >= 10 && time_text[10] == L'T') time_text[10] = L' ';
    m_list.SetItemText(row, 1, time_text.c_str());
    m_list.SetItemText(row, 2, r.title.empty() ? L"未知标题" : r.title.c_str());
    m_list.SetItemText(row, 3, r.artist.empty() ? L"未知歌手" : r.artist.c_str());
    m_list.SetItemText(row, 4, r.album.empty() ? L"未知专辑" : r.album.c_str());
    m_list.SetItemText(row, 5, CStatAnalysis::FormatDuration(r.play_duration_sec).c_str());
    m_list.SetItemText(row, 6, r.song_length_sec > 0 ? CStatAnalysis::FormatDuration(r.song_length_sec / 1000).c_str() : L"—");

    const wchar_t* reason = L"播完";
    switch (r.finish_reason)
    {
    case PlayRecord::FinishReason::SKIPPED:    reason = L"跳过"; break;
    case PlayRecord::FinishReason::STOPPED:    reason = L"停止"; break;
    case PlayRecord::FinishReason::PLAY_ERROR: reason = L"出错"; break;
    default: break;
    }
    m_list.SetItemText(row, 7, reason);
    m_list.SetItemText(row, 8, CStatAnalysis::IsCounted(r) ? L"是" : L"否");
}

void CPlayLogStatDlg::FillPlaceholderView(const wchar_t* text)
{
    m_list.DeleteAllItems();
    ShowEmptyRow(text);
}

// ───────────────────────── 交互 ─────────────────────────

void CPlayLogStatDlg::OnTabSelChange(NMHDR* pNMHDR, LRESULT* pResult)
{
    if (pResult != nullptr) *pResult = 0;
    int sel = m_view_tab.GetCurSel();
    if (sel < 0 || sel >= kViewCount) return;
    SwitchView(static_cast<PlayLogStatView>(sel));
}

void CPlayLogStatDlg::OnBnClickedRefresh()
{
    RefreshAll();
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

HBRUSH CPlayLogStatDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    // 先走基类，别抢 Static/Button 的既有主题处理
    HBRUSH hbr = CBaseDialog::OnCtlColor(pDC, pWnd, nCtlColor);

    if (m_ctl_bk_brush.GetSafeHandle() == NULL)
        m_ctl_bk_brush.CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));

    // CBS_DROPDOWNLIST 的 ComboBox 是复合控件，WM_CTLCOLOR 里的 pWnd
    // 是它内部的显示子窗口，所以要往上比父窗口。
    HWND hwnd = (pWnd != nullptr ? pWnd->GetSafeHwnd() : NULL);
    HWND parent = (hwnd != nullptr ? ::GetParent(hwnd) : NULL);

    const bool in_combo = (parent == m_range_combo.GetSafeHwnd() || hwnd == m_range_combo.GetSafeHwnd());

    if (in_combo)
    {
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(::GetSysColor(COLOR_BTNTEXT));
        return (HBRUSH)m_ctl_bk_brush.GetSafeHandle();
    }
    // 下拉展开后的列表框（ComboLBox 是顶级窗口，父窗口不是 combo，单独兜一下）
    if (nCtlColor == CTLCOLOR_LISTBOX)
    {
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(::GetSysColor(COLOR_BTNTEXT));
        return (HBRUSH)m_ctl_bk_brush.GetSafeHandle();
    }
    return hbr;
}

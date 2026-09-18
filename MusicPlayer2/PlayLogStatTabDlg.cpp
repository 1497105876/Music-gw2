// PlayLogStatTabDlg.cpp: 「歌曲详细记录」页面各子页的实现
//
// 说明：本文件只做数据填充与展示，所有指标由聚合层算好后通过 PlayLogStatData 下发。

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "PlayLogStatTabDlg.h"

IMPLEMENT_DYNAMIC(CPlayLogStatTabDlg, CTabDlg)

CPlayLogStatTabDlg::CPlayLogStatTabDlg(UINT nIDTemplate, CWnd* pParent)
    : CTabDlg(nIDTemplate, pParent)
{
}

void CPlayLogStatTabDlg::SetData(const PlayLogStatData* data)
{
    m_data = data;
    m_dirty = true;
}

void CPlayLogStatTabDlg::OnTabEntered()
{
    if (m_dirty)
    {
        RefreshView();
        m_dirty = false;
    }
}

void CPlayLogStatTabDlg::PrepareList(CListCtrlEx& list)
{
    list.SetExtendedStyle(list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
}

void CPlayLogStatTabDlg::ShowEmptyRow(CListCtrlEx& list, const wchar_t* text)
{
    list.DeleteAllItems();
    int row = list.InsertItem(0, text);
    if (row >= 0 && list.GetHeaderCtrl() != nullptr && list.GetHeaderCtrl()->GetItemCount() > 1)
        list.SetItemText(row, 1, L"");
}

// 数字 -> 带一位小数的百分比字符串
static std::wstring PctText(double percent)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f%%", percent);
    return buf;
}

static std::wstring CountText(int n)
{
    return std::to_wstring(n);
}

// ─────────────────────────── 概 览 页 ───────────────────────────

BEGIN_MESSAGE_MAP(CPlayLogStatOverviewTabDlg, CPlayLogStatTabDlg)
END_MESSAGE_MAP()

void CPlayLogStatOverviewTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CPlayLogStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PLAYLOG_OVERVIEW_LIST, m_list);
}

BOOL CPlayLogStatOverviewTabDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();
    InitListColumns();
    RefreshView();
    m_dirty = false;
    return TRUE;
}

void CPlayLogStatOverviewTabDlg::InitListColumns()
{
    PrepareList(m_list);
    CRect rect;
    m_list.GetWindowRect(rect);
    int w0 = theApp.DPI(150);
    int w1 = rect.Width() - w0 - theApp.DPI(20) - 1;
    m_list.InsertColumn(0, L"统计项", LVCFMT_LEFT, w0);
    m_list.InsertColumn(1, L"数值", LVCFMT_LEFT, w1);
}

void CPlayLogStatOverviewTabDlg::AddRow(int group, const wchar_t* item, const std::wstring& value)
{
    if (group != m_group)
    {
        m_group = group;
        const wchar_t* titles[] = { L"【基本统计】", L"【播放行为】", L"【听歌习惯】", L"【最常听】" };
        int r = m_list.InsertItem(m_row, titles[group]);
        if (r >= 0)
        {
            m_list.SetItemText(r, 1, L"");
            m_row++;
        }
    }
    int r = m_list.InsertItem(m_row, item);
    if (r >= 0)
    {
        m_list.SetItemText(r, 1, value.c_str());
        m_row++;
    }
}

void CPlayLogStatOverviewTabDlg::RefreshView()
{
    m_list.DeleteAllItems();
    m_row = 0;
    m_group = -1;

    if (m_data == nullptr || !m_data->valid)
    {
        ShowEmptyRow(m_list, L"暂无数据");
        return;
    }

    const StatSummary& s = m_data->summary;
    const FinishBreakdown& f = m_data->finish;
    int total = f.total;

    // ── 基本统计 ──
    AddRow(0, L"总播放时长", CStatAnalysis::FormatDuration(s.total_duration_sec));
    AddRow(0, L"播放次数", CountText(s.total_count));
    AddRow(0, L"涉及曲目数", CountText(s.total_songs));
    AddRow(0, L"活跃天数", CountText(s.active_days));
    AddRow(0, L"日均时长", s.active_days > 0 ? CStatAnalysis::FormatDuration(s.total_duration_sec / s.active_days) : L"—");
    AddRow(0, L"日均次数", s.active_days > 0 ? CountText(s.total_count / s.active_days) : L"—");
    AddRow(0, L"数据起始日", m_data->first_ymd > 0 ? CStatAnalysis::FormatYmd(m_data->first_ymd) : L"—");

    // ── 播放行为 ──
    AddRow(1, L"播完", total > 0 ? CountText(f.completed) + L"（" + PctText(f.completed * 100.0 / total) + L"）" : L"—");
    AddRow(1, L"跳过", total > 0 ? CountText(f.skipped) + L"（" + PctText(f.skipped * 100.0 / total) + L"）" : L"—");
    AddRow(1, L"停止", total > 0 ? CountText(f.stopped) + L"（" + PctText(f.stopped * 100.0 / total) + L"）" : L"—");
    AddRow(1, L"出错", total > 0 ? CountText(f.errored) + L"（" + PctText(f.errored * 100.0 / total) + L"）" : L"—");
    AddRow(1, L"平均单次时长", total > 0 ? CStatAnalysis::FormatDuration(s.total_duration_sec / total) : L"—");
    AddRow(1, L"平均完成度", f.avg_completion > 0 ? PctText(f.avg_completion) : L"—");
    AddRow(1, L"平均跳过位置", f.avg_skip_completion >= 0 ? PctText(f.avg_skip_completion) : L"—");

    // ── 听歌习惯 ──
    int peak_hour = -1, peak_cnt = 0;
    for (int h = 0; h < 24; h++)
    {
        if (m_data->hour_hist[h] > peak_cnt)
        {
            peak_cnt = m_data->hour_hist[h];
            peak_hour = h;
        }
    }
    AddRow(2, L"最活跃时段", peak_hour >= 0 ? std::to_wstring(peak_hour) + L":00 - " + std::to_wstring(peak_hour) + L":59" : L"—");

    int night = 0;
    for (int h = 0; h < 24; h++)
        if (h >= 23 || h <= 4) night += m_data->hour_hist[h];
    AddRow(2, L"深夜占比（23:00-04:59）", total > 0 ? PctText(night * 100.0 / total) : L"—");
    AddRow(2, L"周末占比", PctText(static_cast<double>(s.weekend_percent)));
    AddRow(2, L"当前连续天数", CountText(s.current_streak));
    AddRow(2, L"最长连续天数", CountText(s.longest_streak));
    AddRow(2, L"平均播放次数", s.total_songs > 0 ? std::to_wstring(s.total_count) + L" / " + std::to_wstring(s.total_songs) : L"—");

    // ── 最常听 ──
    AddRow(3, L"最常听歌手", s.top_artist.empty() ? L"—" : s.top_artist + L"（" + CStatAnalysis::FormatDuration(s.top_artist_sec) + L"）");
    AddRow(3, L"最常听专辑", m_data->albums.empty() ? L"—" : m_data->albums.front().album + L"（" + CStatAnalysis::FormatDuration(m_data->albums.front().duration_sec) + L"）");
    AddRow(3, L"最常听曲目", s.top_song.empty() ? L"—" : s.top_song + L"（" + CountText(s.top_song_count) + L" 次）");

    std::wstring top_day = L"—";
    int top_day_sec = 0;
    for (const auto& b : m_data->day_buckets)
    {
        if (b.duration_sec > top_day_sec)
        {
            top_day_sec = b.duration_sec;
            top_day = b.label + L"（" + CStatAnalysis::FormatDuration(b.duration_sec) + L"）";
        }
    }
    AddRow(3, L"听得最久的一天", top_day);
}

// ─────────────────────────── 歌 手 排 行 ───────────────────────────

BEGIN_MESSAGE_MAP(CPlayLogStatArtistTabDlg, CPlayLogStatTabDlg)
END_MESSAGE_MAP()

void CPlayLogStatArtistTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CPlayLogStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PLAYLOG_ARTIST_LIST, m_list);
}

BOOL CPlayLogStatArtistTabDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();
    InitListColumns();
    RefreshView();
    m_dirty = false;
    return TRUE;
}

void CPlayLogStatArtistTabDlg::InitListColumns()
{
    PrepareList(m_list);
    CRect rect;
    m_list.GetWindowRect(rect);
    int w0 = theApp.DPI(40);
    int w2 = theApp.DPI(90);
    int w3 = theApp.DPI(60);
    int w4 = theApp.DPI(70);
    int w1 = rect.Width() - w0 - w2 - w3 - w4 - theApp.DPI(20) - 1;
    if (w1 < theApp.DPI(80)) w1 = theApp.DPI(80);
    m_list.InsertColumn(0, L"名次", LVCFMT_LEFT, w0);
    m_list.InsertColumn(1, L"歌手", LVCFMT_LEFT, w1);
    m_list.InsertColumn(2, L"播放时长", LVCFMT_LEFT, w2);
    m_list.InsertColumn(3, L"次数", LVCFMT_LEFT, w3);
    m_list.InsertColumn(4, L"曲目数", LVCFMT_LEFT, w4);
}

void CPlayLogStatArtistTabDlg::RefreshView()
{
    m_list.DeleteAllItems();
    if (m_data == nullptr || !m_data->valid || m_data->artists.empty())
    {
        ShowEmptyRow(m_list, L"所选时间范围内没有记录");
        return;
    }
    int i = 0;
    for (const auto& it : m_data->artists)
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

// ─────────────────────────── 专 辑 排 行 ───────────────────────────

BEGIN_MESSAGE_MAP(CPlayLogStatAlbumTabDlg, CPlayLogStatTabDlg)
END_MESSAGE_MAP()

void CPlayLogStatAlbumTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CPlayLogStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PLAYLOG_ALBUM_LIST, m_list);
}

BOOL CPlayLogStatAlbumTabDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();
    InitListColumns();
    RefreshView();
    m_dirty = false;
    return TRUE;
}

void CPlayLogStatAlbumTabDlg::InitListColumns()
{
    PrepareList(m_list);
    CRect rect;
    m_list.GetWindowRect(rect);
    int w0 = theApp.DPI(40);
    int w2 = theApp.DPI(90);
    int w3 = theApp.DPI(60);
    int w1 = rect.Width() - w0 - w2 - w3 - theApp.DPI(20) - 1;
    if (w1 < theApp.DPI(80)) w1 = theApp.DPI(80);
    m_list.InsertColumn(0, L"名次", LVCFMT_LEFT, w0);
    m_list.InsertColumn(1, L"专辑", LVCFMT_LEFT, w1);
    m_list.InsertColumn(2, L"播放时长", LVCFMT_LEFT, w2);
    m_list.InsertColumn(3, L"次数", LVCFMT_LEFT, w3);
}

void CPlayLogStatAlbumTabDlg::RefreshView()
{
    m_list.DeleteAllItems();
    if (m_data == nullptr || !m_data->valid || m_data->albums.empty())
    {
        ShowEmptyRow(m_list, L"所选时间范围内没有记录");
        return;
    }
    int i = 0;
    for (const auto& it : m_data->albums)
    {
        int row = m_list.InsertItem(i, std::to_wstring(i + 1).c_str());
        if (row < 0) break;
        m_list.SetItemText(row, 1, it.album.c_str());
        m_list.SetItemText(row, 2, CStatAnalysis::FormatDuration(it.duration_sec).c_str());
        m_list.SetItemText(row, 3, CountText(it.count).c_str());
        i++;
    }
}

// ─────────────────────────── 曲 目 排 行 ───────────────────────────

BEGIN_MESSAGE_MAP(CPlayLogStatSongTabDlg, CPlayLogStatTabDlg)
END_MESSAGE_MAP()

void CPlayLogStatSongTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CPlayLogStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PLAYLOG_SONG_LIST, m_list);
}

BOOL CPlayLogStatSongTabDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();
    InitListColumns();
    RefreshView();
    m_dirty = false;
    return TRUE;
}

void CPlayLogStatSongTabDlg::InitListColumns()
{
    PrepareList(m_list);
    CRect rect;
    m_list.GetWindowRect(rect);
    int w0 = theApp.DPI(40);
    int w3 = theApp.DPI(50);
    int w4 = theApp.DPI(90);
    int w5 = theApp.DPI(80);
    int w1 = (rect.Width() - w0 - w3 - w4 - w5 - theApp.DPI(20) - 1) * 5 / 8;
    int w2 = (rect.Width() - w0 - w3 - w4 - w5 - theApp.DPI(20) - 1) * 3 / 8;
    if (w1 < theApp.DPI(80)) w1 = theApp.DPI(80);
    if (w2 < theApp.DPI(60)) w2 = theApp.DPI(60);
    m_list.InsertColumn(0, L"名次", LVCFMT_LEFT, w0);
    m_list.InsertColumn(1, L"标题", LVCFMT_LEFT, w1);
    m_list.InsertColumn(2, L"歌手", LVCFMT_LEFT, w2);
    m_list.InsertColumn(3, L"次数", LVCFMT_LEFT, w3);
    m_list.InsertColumn(4, L"累计时长", LVCFMT_LEFT, w4);
    m_list.InsertColumn(5, L"最后播放", LVCFMT_LEFT, w5);
}

void CPlayLogStatSongTabDlg::RefreshView()
{
    m_list.DeleteAllItems();
    if (m_data == nullptr || !m_data->valid || m_data->songs.empty())
    {
        ShowEmptyRow(m_list, L"所选时间范围内没有记录");
        return;
    }
    int i = 0;
    for (const auto& it : m_data->songs)
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

// ─────────────────────────── 播 放 明 细 ───────────────────────────

BEGIN_MESSAGE_MAP(CPlayLogStatDetailTabDlg, CPlayLogStatTabDlg)
END_MESSAGE_MAP()

void CPlayLogStatDetailTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CPlayLogStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PLAYLOG_DETAIL_LIST, m_list);
}

BOOL CPlayLogStatDetailTabDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();
    InitListColumns();
    RefreshView();
    m_dirty = false;
    return TRUE;
}

void CPlayLogStatDetailTabDlg::InitListColumns()
{
    PrepareList(m_list);
    CRect rect;
    m_list.GetWindowRect(rect);
    int w0 = theApp.DPI(40);
    int w1 = theApp.DPI(110);
    int w2 = theApp.DPI(110);
    int w5 = theApp.DPI(70);
    int w6 = theApp.DPI(70);
    int w7 = theApp.DPI(45);
    int w8 = theApp.DPI(50);
    int w3 = (rect.Width() - w0 - w1 - w2 - w5 - w6 - w7 - w8 - theApp.DPI(20) - 1) * 5 / 8;
    int w4 = (rect.Width() - w0 - w1 - w2 - w5 - w6 - w7 - w8 - theApp.DPI(20) - 1) * 3 / 8;
    if (w3 < theApp.DPI(80)) w3 = theApp.DPI(80);
    if (w4 < theApp.DPI(60)) w4 = theApp.DPI(60);
    m_list.InsertColumn(0, L"序号", LVCFMT_LEFT, w0);
    m_list.InsertColumn(1, L"播放时间", LVCFMT_LEFT, w1);
    m_list.InsertColumn(2, L"标题", LVCFMT_LEFT, w3);
    m_list.InsertColumn(3, L"歌手", LVCFMT_LEFT, w4);
    m_list.InsertColumn(4, L"专辑", LVCFMT_LEFT, w2);
    m_list.InsertColumn(5, L"本次播放", LVCFMT_LEFT, w5);
    m_list.InsertColumn(6, L"曲目总长", LVCFMT_LEFT, w6);
    m_list.InsertColumn(7, L"结果", LVCFMT_LEFT, w7);
    m_list.InsertColumn(8, L"计入统计", LVCFMT_LEFT, w8);
}

void CPlayLogStatDetailTabDlg::RefreshView()
{
    m_list.DeleteAllItems();
    if (m_data == nullptr || !m_data->valid || m_data->records == nullptr || m_data->records->empty())
    {
        ShowEmptyRow(m_list, L"所选时间范围内没有记录");
        return;
    }

    const std::vector<PlayRecord>& records = *m_data->records;
    const int LIMIT = 20000;
    int i = 0;
    for (const auto& r : records)
    {
        if (i >= LIMIT) break;
        int row = m_list.InsertItem(i, std::to_wstring(i + 1).c_str());
        if (row < 0) break;

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
        case PlayRecord::FinishReason::SKIPPED:   reason = L"跳过"; break;
        case PlayRecord::FinishReason::STOPPED:   reason = L"停止"; break;
        case PlayRecord::FinishReason::PLAY_ERROR: reason = L"出错"; break;
        default: break;
        }
        m_list.SetItemText(row, 7, reason);
        m_list.SetItemText(row, 8, CStatAnalysis::IsCounted(r) ? L"是" : L"否");
        i++;
    }

    if ((int)records.size() > LIMIT)
    {
        int row = m_list.InsertItem(i, L"…");
        if (row >= 0)
            m_list.SetItemText(row, 1, L"仅显示最近 20000 条");
    }
}

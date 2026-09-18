#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatOverviewTabDlg.h"
#include <algorithm>

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
    ON_WM_DRAWITEM()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CStatOverviewTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    m_list.InsertColumn(0, L"统计项", LVCFMT_LEFT, 0);
    m_list.InsertColumn(1, L"数值", LVCFMT_LEFT, 0);
    // 列宽自适应：按权重填满整页宽度（缩放时自动重算）
    EnableColumnFit(&m_list, { 160, 340 });
    return TRUE;
}

void CStatOverviewTabDlg::BuildData()
{
    for (int i = 0; i < 24; i++) m_hour[i] = 0;
    m_skip.clear();
    m_playlist.clear();
    m_streak_miss = 0;

    if (m_stat_ctx == nullptr)
    {
        m_summary = StatSummary();
        return;
    }
    m_summary = m_stat_ctx->summary;

    if (m_stat_ctx->records == nullptr) return;
    const std::vector<PlayRecord>& records = *m_stat_ctx->records;

    CStatAnalysis::ComputeHourHistogram(records, m_hour);
    m_skip = CStatAnalysis::ComputeSkipDistribution(records);
    m_playlist = CStatAnalysis::ComputePlaylistContribution(records);
    if (m_playlist.size() > 5) m_playlist.resize(5);
    m_streak_miss = CStatAnalysis::ComputeStreakMiss(records);
}

void CStatOverviewTabDlg::Refresh()
{
    m_dirty = false;
    BuildData();
    ShowData();
}

void CStatOverviewTabDlg::ShowData()
{
    m_list.DeleteAllItems();
    const StatSummary& s = m_summary;
    bool has = (s.total_count > 0);
    int row = 0;
    auto add = [&](const std::wstring& k, const std::wstring& v)
    {
        m_list.InsertItem(row, k.c_str());
        m_list.SetItemText(row, COL_VALUE, v.c_str());
        row++;
    };
    auto dur = [](int sec) { return CStatAnalysis::FormatDuration(sec); };

    add(L"── 基本统计 ──", L"");
    add(L"今日播放",      has ? std::to_wstring(s.today_count) + L" 首" : L"—");
    add(L"本周播放",      has ? std::to_wstring(s.week_count) + L" 首" : L"—");
    add(L"本月播放",      has ? std::to_wstring(s.month_count) + L" 首" : L"—");
    add(L"累计播放次数",  has ? std::to_wstring(s.total_count) + L" 首" : L"—");
    add(L"累计播放时长",  has ? dur(s.total_duration_sec) : L"—");
    add(L"活跃天数",      has ? std::to_wstring(s.active_days) + L" 天" : L"—");
    add(L"日均播放",      has ? std::to_wstring(s.avg_plays_per_day) + L" 首" : L"—");

    add(L"── 播放结果 ──", L"");
    add(L"完整收听率",    has ? std::to_wstring((int)(s.completed_rate + 0.5)) + L"%" : L"—");
    add(L"跳过率",        has ? std::to_wstring((int)(s.skip_rate + 0.5)) + L"%" : L"—");
    add(L"平均完成度",    has ? std::to_wstring((int)(s.avg_completion + 0.5)) + L"%" : L"—");

    add(L"── 行为习惯 ──", L"");
    add(L"连续听歌",      has ? std::to_wstring(s.current_streak) + L" 天" : L"—");
    add(L"最长连续",      has ? std::to_wstring(s.longest_streak) + L" 天" : L"—");
    if (m_streak_miss > 0)
        add(L"差点就连续", std::to_wstring(m_streak_miss + 1) + L" 天");
    add(L"深夜占比",      has ? std::to_wstring(s.night_owl_percent) + L"%" : L"—");
    add(L"周末占比",      has ? std::to_wstring(s.weekend_percent) + L"%" : L"—");

    add(L"── 口味 ──", L"");
    if (!s.top_artist.empty())
        add(L"最爱歌手", s.top_artist + L"（" + dur(s.top_artist_sec) + L"）");
    if (!s.top_song.empty())
        add(L"最爱歌曲", s.top_song + (s.top_song_artist.empty() ? L"" : L" - " + s.top_song_artist) + L"（" + std::to_wstring(s.top_song_count) + L" 次）");
    add(L"本月新歌",      has ? std::to_wstring(s.new_songs_month) + L" 首" : L"—");
    add(L"反复循环",      has ? std::to_wstring(s.repeat_depth) + L" 首" : L"—");

    add(L"── 歌单 / 来源贡献 ──", L"");
    if (m_playlist.empty()) add(L"—", L"");
    for (const auto& c : m_playlist)
        add(c.source, dur(c.duration_sec) + L"（" + std::to_wstring((int)(c.percent + 0.5)) + L"%）");

    add(L"── 跳过位置分布 ──", L"");
    for (const auto& b : m_skip)
        add(b.label, std::to_wstring(b.count) + L" 次（" + std::to_wstring((int)(b.percent + 0.5)) + L"%）");

    add(L"── 听歌时段分布 ──", L"");
    for (int h = 0; h < 24; h++)
    {
        if (m_hour[h] <= 0) continue;
        wchar_t buf[24];
        swprintf_s(buf, L"%02d:00-%02d:00", h, (h + 1) % 24);
        add(buf, std::to_wstring(m_hour[h]) + L" 首");
    }
}

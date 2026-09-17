#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatGenreTabDlg.h"
#include "StatAnalysis.h"
#include "StatChart.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace
{
    const double kPi = 3.14159265358979323846;

    // 角度（自 12 点方向、顺时针，单位度）转换为以 center 为圆心、半径 r 的点
    CPoint PointOnCircle(const CPoint& center, int r, double angle_deg)
    {
        double rad = angle_deg * kPi / 180.0;
        int x = center.x + (int)(r * std::sin(rad));
        int y = center.y - (int)(r * std::cos(rad));
        return CPoint(x, y);
    }
}

IMPLEMENT_DYNAMIC(CStatGenreTabDlg, CStatTabDlg)

CStatGenreTabDlg::CStatGenreTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_GENRE_DLG, pParent)
{
}

CStatGenreTabDlg::~CStatGenreTabDlg()
{
}

void CStatGenreTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_GENRE_LIST, m_list);
}

BEGIN_MESSAGE_MAP(CStatGenreTabDlg, CStatTabDlg)
    ON_WM_DRAWITEM()
END_MESSAGE_MAP()

BOOL CStatGenreTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();
    m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    m_list.InsertColumn(0, L"流派", LVCFMT_LEFT, theApp.DPI(140));
    m_list.InsertColumn(1, L"播放时长", LVCFMT_LEFT, theApp.DPI(90));
    m_list.InsertColumn(2, L"占比", LVCFMT_RIGHT, theApp.DPI(70));


    return TRUE;
}

void CStatGenreTabDlg::BuildData()
{
    m_share.clear();
    m_quarter_share.clear();
    m_quarter_labels.clear();
    m_quarter_count = 0;
    m_gems.clear();
    m_similarity = 0.0;
    m_has_similarity = false;

    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr) return;
    const std::vector<PlayRecord>& records = *m_stat_ctx->records;

    // 占比（>5 类合并为“其他”）
    m_share = CStatAnalysis::ComputeGenreShare(records, 5);

    // 最近 4 个季度主导流派（漂移叠图）
    m_quarter_share = CStatAnalysis::ComputeGenreShareByQuarter(records, 4, m_quarter_labels);
    m_quarter_count = (int)m_quarter_share.size();

    // 遗珠挖掘（REQ-113）：反复听 >=3 次却从未完整听完
    m_gems = CStatAnalysis::ComputeRetiredGems(records, 3);
    if (m_gems.size() > 3) m_gems.resize(3);

    // 口味对比（REQ-114）：按时间中位数把记录一分为二，比较两段流派分布的余弦相似度
    if (records.size() > 3)
    {
        std::vector<PlayRecord> sorted = records;
        std::sort(sorted.begin(), sorted.end(),
            [](const PlayRecord& a, const PlayRecord& b) { return a.played_at < b.played_at; });
        size_t mid = sorted.size() / 2;
        std::vector<PlayRecord> older(sorted.begin(), sorted.begin() + mid);
        std::vector<PlayRecord> newer(sorted.begin() + mid, sorted.end());
        std::vector<GenreShare> a = CStatAnalysis::ComputeGenreShare(older, 8);
        std::vector<GenreShare> b = CStatAnalysis::ComputeGenreShare(newer, 8);
        if (!a.empty() && !b.empty())
        {
            m_similarity = CStatAnalysis::ComputeCosineSimilarity(a, b);
            m_has_similarity = true;
        }
    }
}

void CStatGenreTabDlg::Refresh()
{
    m_list.DeleteAllItems();
    int row = 0;
    auto add3 = [&](const std::wstring& a, const std::wstring& b, const std::wstring& c)
    { m_list.InsertItem(row, a.c_str()); m_list.SetItemText(row, 1, b.c_str()); m_list.SetItemText(row, 2, c.c_str()); row++; };
    for (const auto& g : m_share)
        add3(g.genre, CStatAnalysis::FormatDuration(g.duration_sec), std::to_wstring((int)(g.percent + 0.5)) + L"%");
    add3(L"", L"", L"");
    add3(L"── 遗珠挖掘（反复听却从未听完）──", L"", L"");
    for (const auto& g : m_gems)
        add3(g.title, std::to_wstring(g.count) + L" 次", L"未完播");
    add3(L"口味一致性", m_has_similarity ? std::to_wstring((int)(m_similarity * 100 + 0.5)) + L"%" : L"无数据", L"");

    m_dirty = false;
    BuildData();
}

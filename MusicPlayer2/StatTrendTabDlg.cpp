#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatTrendTabDlg.h"
#include "StatAnalysis.h"
#include <vector>

namespace
{
    const wchar_t* GrainName(Grain g)
    {
        switch (g)
        {
        case Grain::Day:   return L"天";
        case Grain::Week:  return L"周";
        case Grain::Month: return L"月";
        case Grain::Year:  return L"年";
        }
        return L"天";
    }
}

/////////////////////////////////////////////////////////////////////////////
// CTrendChart
/////////////////////////////////////////////////////////////////////////////

IMPLEMENT_DYNAMIC(CTrendChart, CStatChart)

void CTrendChart::SetData(const Data& data)
{
    m_data = data;
}

void CTrendChart::DrawChart(CDrawCommon& /*draw*/, CDC* /*pDC*/, const CRect& /*rect*/)
{
    // 【占位】趋势图绘制先留白，等待与用户确认呈现方式后再实现。
    // 基类 CStatChart 已铺好与子页一致的背景，此处不绘制任何内容；
    // 数据通路（Refresh() -> SetData()）保持完整，后续只需在此函数内实现绘制即可。
}

/////////////////////////////////////////////////////////////////////////////
// CStatTrendTabDlg
/////////////////////////////////////////////////////////////////////////////

IMPLEMENT_DYNAMIC(CStatTrendTabDlg, CStatTabDlg)

CStatTrendTabDlg::CStatTrendTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_TREND_DLG, pParent)
{
}

CStatTrendTabDlg::~CStatTrendTabDlg()
{
}

void CStatTrendTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_TREND_CHART, m_chart);
}

BEGIN_MESSAGE_MAP(CStatTrendTabDlg, CStatTabDlg)
END_MESSAGE_MAP()

BOOL CStatTrendTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    // 不再需要运行时改 SS_OWNERDRAW：m_chart 是 CStatChart 派生控件，自绘在其 OnPaint 内完成。
    // 控件位置与大小完全交给 rc + AFX_DIALOG_LAYOUT，此处不做任何 SetWindowLongPtr / MoveWindow。

    if (m_stat_ctx != nullptr)
    {
        Refresh();
        m_dirty = false;
    }
    return TRUE;
}

void CStatTrendTabDlg::Refresh()
{
    m_dirty = false;

    CTrendChart::Data d;
    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr)
    {
        d.title = std::wstring(L"播放趋势（按") + GrainName(Grain::Day) + L"）";
        m_chart.SetData(d);
        m_chart.Invalidate(FALSE);
        return;
    }

    const std::vector<PlayRecord>& records = *m_stat_ctx->records;
    const Grain grain = m_stat_ctx->filter.grain;

    d.buckets = CStatAnalysis::ComputeBuckets(records, grain);
    d.pc      = CStatAnalysis::ComputePeriodComparison(records, m_stat_ctx->filter);
    d.title   = std::wstring(L"播放趋势（按") + GrainName(grain) + L"）";

    std::vector<PeriodBucket> news = CStatAnalysis::ComputeNewSongTrend(records);
    if (!news.empty())
    {
        d.has_news   = true;
        d.news_label = news.back().label;
        d.news_count = news.back().count;
        for (const auto& b : news) d.news_total += b.count;
    }

    m_chart.SetData(d);
    m_chart.Invalidate(FALSE);
}

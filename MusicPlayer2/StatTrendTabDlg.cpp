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

void CTrendChart::DrawChart(CDrawCommon& draw, CDC* /*pDC*/, const CRect& rect)
{
    const StatPalette::StatPaletteColors& th = StatPalette::Get();

    // ── 尺寸：全部按当前 DPI 换算，随容器自适应 ──
    const int pad           = theApp.DPI(10);
    const int title_h       = theApp.DPI(22);
    const int info_h        = theApp.DPI(16);
    const int margin_left   = theApp.DPI(26);
    const int margin_right  = theApp.DPI(12);
    const int margin_top    = theApp.DPI(40);
    const int margin_bottom = theApp.DPI(18);

    // 标题（含粒度）
    draw.SetFont(&theApp.m_font_set.GetFontBySize(StatFont::SubHead).GetFont());
    draw.DrawWindowText(CRect(rect.left + pad, rect.top + theApp.DPI(4),
                              rect.right - pad, rect.top + title_h),
                        m_data.title.c_str(), th.text_primary);

    if (m_data.buckets.empty())
    {
        draw.SetFont(&theApp.m_font_set.GetFontBySize(StatFont::Body).GetFont());
        draw.DrawWindowText(CRect(rect.left + pad, rect.top + title_h,
                                  rect.right - pad, rect.top + title_h + info_h),
                            L"无记录", th.text_disabled);
        return;
    }

    // 环比（年粒度下“去年同期”即“上一周期”，由 same_as_previous 标记避免重复渲染）
    draw.SetFont(&theApp.m_font_set.GetFontBySize(StatFont::Body).GetFont());
    {
        wchar_t buf[160];
        if (m_data.pc.has_previous)
            swprintf_s(buf, L"环比（vs %s）：%+d 次（%+.0f%%）", m_data.pc.previous.label.c_str(),
                       m_data.pc.count_delta, m_data.pc.count_delta_percent);
        else
            swprintf_s(buf, L"环比：无上期数据");
        draw.DrawWindowText(CRect(rect.left + pad, rect.top + title_h,
                                  rect.right - pad, rect.top + title_h + info_h),
                            buf, th.text_secondary);

        if (m_data.pc.has_last_year && !m_data.pc.same_as_previous)
        {
            int delta = m_data.pc.current.count - m_data.pc.last_year.count;
            double pct = (m_data.pc.last_year.count > 0)
                ? (double)delta / m_data.pc.last_year.count * 100.0 : 0.0;
            swprintf_s(buf, L"同比（vs %s）：%+d 次（%+.0f%%）", m_data.pc.last_year.label.c_str(), delta, pct);
            draw.DrawWindowText(CRect(rect.left + pad + theApp.DPI(220), rect.top + title_h,
                                      rect.right - pad, rect.top + title_h + info_h),
                                buf, th.text_secondary);
        }
    }

    // 新发现趋势
    {
        wchar_t buf[128];
        if (m_data.has_news)
            swprintf_s(buf, L"新发现：共 %d 首，最近一个月 %s 新增 %d 首",
                       m_data.news_total, m_data.news_label.c_str(), m_data.news_count);
        else
            swprintf_s(buf, L"新发现：无");
        draw.DrawWindowText(CRect(rect.left + pad, rect.top + title_h + info_h,
                                  rect.right - pad, rect.top + title_h + info_h * 2),
                            buf, th.text_secondary);
    }

    const int chart_w = rect.Width() - margin_left - margin_right;
    const int chart_h = rect.Height() - margin_top - margin_bottom;
    if (chart_w <= 0 || chart_h <= 0)
        return;

    int max_count = 1;
    for (const auto& b : m_data.buckets)
        if (b.count > max_count) max_count = b.count;

    // 坐标轴
    draw.DrawLine(CPoint(rect.left + margin_left, rect.top + margin_top),
                  CPoint(rect.left + margin_left, rect.top + margin_top + chart_h),
                  th.axis, 1, false);
    draw.DrawLine(CPoint(rect.left + margin_left, rect.top + margin_top + chart_h),
                  CPoint(rect.left + margin_left + chart_w, rect.top + margin_top + chart_h),
                  th.axis, 1, false);

    draw.SetFont(&theApp.m_font_set.GetFontBySize(StatFont::Tiny).GetFont());

    // Y 轴刻度（上界与 0）
    {
        wchar_t ybuf[16];
        swprintf_s(ybuf, L"%d", max_count);
        draw.DrawWindowText(CRect(rect.left, rect.top + margin_top - theApp.DPI(6),
                                  rect.left + margin_left, rect.top + margin_top + theApp.DPI(8)),
                            ybuf, th.text_secondary);
        draw.DrawWindowText(CRect(rect.left, rect.top + margin_top + chart_h - theApp.DPI(6),
                                  rect.left + margin_left, rect.top + margin_top + chart_h + theApp.DPI(8)),
                            L"0", th.text_secondary);
    }

    const int n = static_cast<int>(m_data.buckets.size());
    int bar_w = chart_w / n;
    if (bar_w < 2) bar_w = 2;
    if (bar_w > theApp.DPI(28)) bar_w = theApp.DPI(28);   // 柱子不随容器无限变宽

    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < n; i++)
    {
        int x = rect.left + margin_left + chart_w * i / n + bar_w / 2;
        int bar_h = static_cast<int>((double)m_data.buckets[i].count / max_count * (chart_h - theApp.DPI(8)));
        int y_pos = rect.top + margin_top + chart_h - bar_h;

        // 柱
        draw.FillRect(CRect(x - bar_w / 2, y_pos, x + bar_w / 2, rect.top + margin_top + chart_h),
                      th.series[0]);

        // 折线
        if (prev_x >= 0)
            draw.DrawLine(CPoint(prev_x, prev_y), CPoint(x, y_pos), th.highlight, 2, false);
        prev_x = x;
        prev_y = y_pos;
    }

    // X 轴标签：按可用宽度抽稀，避免重叠
    {
        const int label_gap = theApp.DPI(46);
        int step = 1;
        if (n > 1)
        {
            int per = chart_w / n;
            if (per > 0 && per < label_gap)
                step = (label_gap + per - 1) / per;
        }
        for (int i = 0; i < n; i += step)
        {
            int x = rect.left + margin_left + chart_w * i / n;
            const std::wstring& label = m_data.buckets[i].label;
            CSize sz = draw.GetTextExtent(label.c_str());
            draw.DrawWindowText(CRect(x + bar_w / 2 - sz.cx / 2,
                                      rect.top + margin_top + chart_h + theApp.DPI(2),
                                      x + bar_w / 2 + sz.cx / 2 + theApp.DPI(4),
                                      rect.bottom),
                                label.c_str(), th.text_secondary);
        }
    }
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

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatTrendTabDlg.h"
#include "StatAnalysis.h"
#include "StatTheme.h"
#include <map>
#include <vector>
#include <algorithm>

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
    ON_WM_DRAWITEM()
END_MESSAGE_MAP()

BOOL CStatTrendTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW);

    CStatTheme::ApplyDialog(this);
    return TRUE;
}

void CStatTrendTabDlg::Refresh()
{
    m_dirty = false;
    m_chart.Invalidate(FALSE);
}

void CStatTrendTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_TREND_CHART)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        CRect rect(lpDrawItemStruct->rcItem);
        if (rect.Width() < 80 || rect.Height() < 80) return;

        pDC->FillSolidRect(rect, CStatTheme::Get().panel_back);
        pDC->SetBkMode(TRANSPARENT);

        DrawTrendChart(pDC, rect);
    }
    else
    {
        CTabDlg::OnDrawItem(nIDCtl, lpDrawItemStruct);
    }
}

void CStatTrendTabDlg::DrawTrendChart(CDC* pDC, const CRect& rect)
{
    const StatThemeColors& th = CStatTheme::Get();

    CFont fTitle;
    fTitle.CreatePointFont(140, L"Microsoft YaHei", pDC);
    CFont* pOldFont = pDC->SelectObject(&fTitle);
    pDC->SetTextColor(th.text_primary);

    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr)
    {
        pDC->TextOutW(rect.left + 20, rect.top + 10, L"无记录");
        pDC->SelectObject(pOldFont);
        return;
    }

    const std::vector<PlayRecord>& records = *m_stat_ctx->records;
    Grain grain = m_stat_ctx->filter.grain;

    // 按当前粒度聚合（口径统一走 CStatAnalysis）
    std::vector<PeriodBucket> buckets = CStatAnalysis::ComputeBuckets(records, grain);

    std::wstring title = std::wstring(L"播放趋势（按") + GrainName(grain) + L"）";
    pDC->TextOutW(rect.left + 30, rect.top + 8, title.c_str(), (int)title.size());

    if (buckets.empty())
    {
        pDC->SetTextColor(th.text_disabled);
        pDC->TextOutW(rect.left + 30, rect.top + 50, L"无记录");
        pDC->SelectObject(pOldFont);
        return;
    }

    // 环比 / 同比 文案：
    //   年粒度下“去年同期”即“上一周期”，数据层以 same_as_previous 显式标记，
    //   显示层据此不再重复渲染同比行，避免出现与环比重复或语义空白的“同比”行。
    {
        PeriodComparison pc = CStatAnalysis::ComputePeriodComparison(records, m_stat_ctx->filter);
        CFont info_font;
        info_font.CreatePointFont(88, L"Microsoft YaHei", pDC);
        pDC->SelectObject(&info_font);
        pDC->SetTextColor(th.text_secondary);

        wchar_t buf[160];
        if (pc.has_previous)
            swprintf_s(buf, L"环比（vs %s）：%+d 次（%+.0f%%）", pc.previous.label.c_str(),
                pc.count_delta, pc.count_delta_percent);
        else
            swprintf_s(buf, L"环比：无上期数据");
        pDC->TextOutW(rect.left + 30, rect.top + 30, buf, (int)wcslen(buf));

        if (pc.has_last_year && !pc.same_as_previous)
        {
            int delta = pc.current.count - pc.last_year.count;
            double pct = (pc.last_year.count > 0) ? (double)delta / pc.last_year.count * 100.0 : 0.0;
            swprintf_s(buf, L"同比（vs %s）：%+d 次（%+.0f%%）", pc.last_year.label.c_str(), delta, pct);
            pDC->TextOutW(rect.left + 30 + 320, rect.top + 30, buf, (int)wcslen(buf));
        }
        pDC->SelectObject(pOldFont);
    }

    // 新发现趋势（每月首次听的歌）
    {
        std::vector<PeriodBucket> news = CStatAnalysis::ComputeNewSongTrend(records);
        int total_new = 0;
        for (const auto& b : news) total_new += b.count;

        CFont info_font;
        info_font.CreatePointFont(84, L"Microsoft YaHei", pDC);
        pDC->SelectObject(&info_font);
        pDC->SetTextColor(th.text_secondary);
        wchar_t buf[128];
        if (!news.empty())
            swprintf_s(buf, L"新发现：共 %d 首，最近一个月 %s 新增 %d 首",
                total_new, news.back().label.c_str(), news.back().count);
        else
            swprintf_s(buf, L"新发现：无");
        pDC->TextOutW(rect.left + 30, rect.top + 50, buf, (int)wcslen(buf));
        pDC->SelectObject(pOldFont);
    }

    // 图表区
    int margin_left = 50, margin_right = 16, margin_top = 74, margin_bottom = 34;
    int chart_w = rect.Width() - margin_left - margin_right;
    int chart_h = rect.Height() - margin_top - margin_bottom;
    if (chart_w <= 0 || chart_h <= 0) return;

    int max_count = 1;
    for (const auto& b : buckets)
        if (b.count > max_count) max_count = b.count;

    // 坐标轴
    CPen axis_pen(PS_SOLID, 1, th.axis);
    CPen* old_pen = pDC->SelectObject(&axis_pen);
    pDC->MoveTo(rect.left + margin_left, rect.top + margin_top);
    pDC->LineTo(rect.left + margin_left, rect.top + margin_top + chart_h);
    pDC->LineTo(rect.left + margin_left + chart_w, rect.top + margin_top + chart_h);
    pDC->SelectObject(old_pen);

    CFont small_font;
    small_font.CreatePointFont(78, L"Microsoft YaHei", pDC);
    CFont* pOldSmall = pDC->SelectObject(&small_font);

    // Y 轴刻度
    pDC->SetTextColor(th.text_secondary);
    wchar_t ybuf[16];
    swprintf_s(ybuf, L"%d", max_count);
    pDC->TextOutW(rect.left + 8, rect.top + margin_top - 5, ybuf);
    pDC->TextOutW(rect.left + 20, rect.top + margin_top + chart_h - 6, L"0");

    int n = (int)buckets.size();
    int bar_w = chart_w / n;
    if (bar_w < 2) bar_w = 2;

    CPen line_pen(PS_SOLID, 2, th.highlight);
    CBrush bar_brush(th.series[0]);

    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < n; i++)
    {
        int x = rect.left + margin_left + chart_w * i / n + bar_w / 2;
        int bar_h = (int)((double)buckets[i].count / max_count * (chart_h - 10));
        int y_pos = rect.top + margin_top + chart_h - bar_h;

        pDC->SelectObject(&bar_brush);
        pDC->SelectObject(GetStockObject(NULL_PEN));
        pDC->Rectangle(x - bar_w / 2, y_pos, x + bar_w / 2, rect.top + margin_top + chart_h);

        if (prev_x >= 0)
        {
            pDC->SelectObject(&line_pen);
            pDC->MoveTo(prev_x, prev_y);
            pDC->LineTo(x, y_pos);
        }
        prev_x = x;
        prev_y = y_pos;
    }

    // X 轴标签：标签过多时隔段显示
    pDC->SetTextColor(th.text_secondary);
    int step = (n > 12) ? (n / 10 + 1) : 1;
    for (int i = 0; i < n; i += step)
    {
        int x = rect.left + margin_left + chart_w * i / n;
        std::wstring label = buckets[i].label;
        CSize sz = pDC->GetTextExtent(label.c_str(), (int)label.size());
        pDC->TextOutW(x + bar_w / 2 - sz.cx / 2, rect.top + margin_top + chart_h + 4,
            label.c_str(), (int)label.size());
    }

    pDC->SelectObject(pOldSmall);
    pDC->SelectObject(pOldFont);
}

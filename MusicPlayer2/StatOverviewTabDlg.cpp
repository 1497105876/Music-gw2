#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatOverviewTabDlg.h"
#include "StatChart.h"
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
    DDX_Control(pDX, IDC_STAT_OVERVIEW_CHART, m_chart);
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

    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW | WS_VSCROLL);

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
    m_scroll_pos = 0;
    UpdateScrollbar();
    m_chart.Invalidate(FALSE);
}

int CStatOverviewTabDlg::CalcContentHeight(int width)
{
    int y = 8;
    y += 30;                                   // 核心指标 标题
    y += theApp.DPI(78) + 10;                  // 四指标卡
    y += 30;                                   // 24 小时 标题
    y += theApp.DPI(120) + 10;                 // 24h 柱状图
    y += 30;                                   // 播放结果 标题
    y += theApp.DPI(40) + 6;                   // 完播/跳过文本行
    y += (int)m_skip.size() * theApp.DPI(22) + 8;
    if (m_streak_miss > 0) y += theApp.DPI(30); // 差点就连续
    y += 30;                                   // 歌单贡献 标题
    y += (int)m_playlist.size() * theApp.DPI(22) + 8;
    y += 12;
    return y;
}

void CStatOverviewTabDlg::UpdateScrollbar()
{
    if (!m_chart.GetSafeHwnd()) return;
    CRect rc;
    m_chart.GetClientRect(&rc);
    m_page_size = rc.Height();
    m_scroll_max = CalcContentHeight(rc.Width());

    if (m_scroll_max <= m_page_size)
    {
        m_scroll_pos = 0;
        m_chart.EnableScrollBarCtrl(SB_VERT, FALSE);
        m_chart.ShowScrollBar(SB_VERT, FALSE);
    }
    else
    {
        m_chart.EnableScrollBarCtrl(SB_VERT, TRUE);
        m_chart.ShowScrollBar(SB_VERT, TRUE);
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = m_scroll_max - 1;
        si.nPage = m_page_size;
        si.nPos = m_scroll_pos;
        m_chart.SetScrollInfo(SB_VERT, &si, TRUE);
    }
}

void CStatOverviewTabDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (m_scroll_max > m_page_size)
    {
        int step = theApp.DPI(36);
        switch (nSBCode)
        {
        case SB_LINEUP:        m_scroll_pos -= step; break;
        case SB_LINEDOWN:      m_scroll_pos += step; break;
        case SB_PAGEUP:        m_scroll_pos -= m_page_size; break;
        case SB_PAGEDOWN:      m_scroll_pos += m_page_size; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: m_scroll_pos = nPos; break;
        case SB_TOP:           m_scroll_pos = 0; break;
        case SB_BOTTOM:        m_scroll_pos = m_scroll_max - m_page_size; break;
        }
        int max_pos = m_scroll_max - m_page_size;
        if (max_pos < 0) max_pos = 0;
        m_scroll_pos = max(0, min(m_scroll_pos, max_pos));
        m_chart.SetScrollPos(SB_VERT, m_scroll_pos, TRUE);
        m_chart.Invalidate(FALSE);
    }
    CTabDlg::OnVScroll(nSBCode, nPos, pScrollBar);
}

BOOL CStatOverviewTabDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    CRect rc;
    m_chart.GetWindowRect(&rc);
    if (rc.PtInRect(pt) && m_scroll_max > m_page_size)
    {
        int step = theApp.DPI(60);
        m_scroll_pos -= zDelta / 120 * step;
        int max_pos = m_scroll_max - m_page_size;
        if (max_pos < 0) max_pos = 0;
        m_scroll_pos = max(0, min(m_scroll_pos, max_pos));
        m_chart.SetScrollPos(SB_VERT, m_scroll_pos, TRUE);
        m_chart.Invalidate(FALSE);
        return TRUE;
    }
    return CTabDlg::OnMouseWheel(nFlags, zDelta, pt);
}

void CStatOverviewTabDlg::OnSize(UINT nType, int cx, int cy)
{
    CTabDlg::OnSize(nType, cx, cy);
    if (m_chart.GetSafeHwnd())
    {
        UpdateScrollbar();
        m_chart.Invalidate(FALSE);
    }
}

void CStatOverviewTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_OVERVIEW_CHART)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        CRect rect;
        m_chart.GetClientRect(&rect);
        if (rect.Width() < 60 || rect.Height() < 60) return;

        pDC->FillSolidRect(rect, StatPalette::Get().panel_back);
        pDC->SetBkMode(TRANSPARENT);

        DrawOverview(pDC, rect);
    }
    else
    {
        CTabDlg::OnDrawItem(nIDCtl, lpDrawItemStruct);
    }
}

void CStatOverviewTabDlg::DrawSectionTitle(CDC* pDC, const CRect& rect, int y, const std::wstring& title)
{
    const StatPalette::StatPaletteColors& th = StatPalette::Get();
    int pad = 12;
    CRect bar(rect.left + pad, y + 2, rect.left + pad + 4, y + 18);
    pDC->FillSolidRect(bar, th.accent);

    CFont font;
    font.CreatePointFont(100, L"Microsoft YaHei", pDC);
    HFONT old = (HFONT)pDC->SelectObject(font.GetSafeHandle());
    pDC->SetTextColor(th.text_primary);
    pDC->TextOutW(rect.left + pad + 10, y, title.c_str(), (int)title.size());
    pDC->SelectObject(old);
}

void CStatOverviewTabDlg::DrawOverview(CDC* pDC, const CRect& rect)
{
    const StatPalette::StatPaletteColors& th = StatPalette::Get();
    int pad = 12;
    int y = 8 - m_scroll_pos;
    bool has_data = (m_summary.total_count > 0);

    auto draw_text = [&](const std::wstring& text, int x, int yy, COLORREF color, int pt, bool bold = false) {
        CFont font;
        font.CreatePointFont(pt * 10, bold ? L"Microsoft YaHei" : L"Microsoft YaHei", pDC);
        HFONT old = (HFONT)pDC->SelectObject(font.GetSafeHandle());
        pDC->SetTextColor(color);
        pDC->TextOutW(x, yy, text.c_str(), (int)text.size());
        pDC->SelectObject(old);
        };

    // ── 核心指标：四张卡 ──
    DrawSectionTitle(pDC, rect, y, L"核心指标");
    y += 30;
    {
        int card_w = (rect.Width() - pad * 2 - theApp.DPI(24)) / 4;
        int card_h = theApp.DPI(70);
        int x0 = rect.left + pad;

        struct Card { std::wstring label; std::wstring value; COLORREF color; };
        wchar_t rate_buf[32];
        swprintf_s(rate_buf, L"%.0f%%", m_summary.completed_rate);

        Card cards[4] = {
            { L"累计时长", has_data ? CStatAnalysis::FormatDuration(m_summary.total_duration_sec) : L"—", th.series[0] },
            { L"累计次数", has_data ? (std::to_wstring(m_summary.total_count) + L" 首") : L"—", th.series[1] },
            { L"完整收听率", has_data ? std::wstring(rate_buf) : L"—", th.series[2] },
            { L"活跃天数", has_data ? (std::to_wstring(m_summary.active_days) + L" 天") : L"—", th.series[3] },
        };

        for (int i = 0; i < 4; i++)
        {
            CRect rc(x0 + i * (card_w + 8), y, x0 + i * (card_w + 8) + card_w, y + card_h);
            pDC->FillSolidRect(rc, th.card_back);
            pDC->FillSolidRect(CRect(rc.left, rc.top, rc.left + 4, rc.bottom), cards[i].color);

            draw_text(cards[i].label, rc.left + 10, rc.top + 6, th.text_secondary, 80);

            std::wstring val = cards[i].value;
            CFont big;
            big.CreatePointFont(120, L"Microsoft YaHei", pDC);
            HFONT old = (HFONT)pDC->SelectObject(big.GetSafeHandle());
            pDC->SetTextColor(has_data ? th.text_primary : th.text_disabled);
            CSize sz = pDC->GetTextExtent(val.c_str(), (int)val.size());
            if (sz.cx > card_w - 20)
            {
                CFont mid;
                mid.CreatePointFont(95, L"Microsoft YaHei", pDC);
                pDC->SelectObject(mid.GetSafeHandle());
            }
            pDC->TextOutW(rc.left + 10, rc.top + 30, val.c_str(), (int)val.size());
            pDC->SelectObject(old);
        }
        y += card_h + 10;
    }

    // ── 24 小时收听分布（24 根柱 + 峰值标注）──
    DrawSectionTitle(pDC, rect, y, L"24 小时收听分布");
    y += 30;
    {
        int chart_h = theApp.DPI(120);
        int chart_w = rect.Width() - pad * 2;
        int x0 = rect.left + pad;
        int base_y = y + chart_h - 16;

        pDC->FillSolidRect(CRect(x0, y, x0 + chart_w, y + chart_h), th.card_back_alt);

        int max_h = 1, peak = -1;
        for (int h = 0; h < 24; h++)
        {
            if (m_hour[h] > max_h) { max_h = m_hour[h]; peak = h; }
        }
        // 峰值初始化为最大值所在小时（即便全部相等也取 0）
        if (peak < 0) peak = 0;

        int bar_w = (chart_w - 16) / 24;
        if (bar_w < 3) bar_w = 3;
        for (int h = 0; h < 24; h++)
        {
            int bh = has_data ? (int)((double)m_hour[h] / max_h * (chart_h - 34)) : 0;
            int bx = x0 + 8 + h * bar_w;
            CRect bar(bx, base_y - bh, bx + bar_w - 2, base_y);
            pDC->FillSolidRect(bar, (h == peak && has_data) ? th.highlight : th.series[4]);
        }

        for (int h = 0; h < 24; h += 6)
        {
            wchar_t buf[8];
            swprintf_s(buf, L"%02d", h);
            draw_text(buf, x0 + 8 + h * bar_w, base_y + 2, th.text_secondary, 76);
        }

        if (has_data)
        {
            wchar_t buf[64];
            swprintf_s(buf, L"峰值 %02d:00（%d 次）", peak, m_hour[peak]);
            draw_text(buf, x0 + 8, y + 2, th.text_primary, 80);
        }
        else
        {
            draw_text(L"无记录", x0 + 8, y + 2, th.text_disabled, 80);
        }
        y += chart_h + 10;
    }

    // ── 播放结果分布（完播率/跳过率 + 跳过位置 4 桶）──
    DrawSectionTitle(pDC, rect, y, L"播放结果分布");
    y += 30;
    {
        wchar_t buf[96];
        swprintf_s(buf, L"完整收听率 %.0f%%    跳过率 %.0f%%",
            m_summary.completed_rate, m_summary.skip_rate);
        draw_text(has_data ? std::wstring(buf) : L"—", rect.left + pad + 10, y, th.text_primary, 84);
        y += theApp.DPI(28);

        int line_x = rect.left + pad + 10;
        int line_w = rect.Width() - pad * 2 - 20;
        for (size_t i = 0; i < m_skip.size(); i++)
        {
            const auto& b = m_skip[i];
            std::wstring label = b.label;
            draw_text(label, line_x, y, th.text_secondary, 80);
            draw_text(std::to_wstring(b.count) + L" 次", line_x + theApp.DPI(70), y, th.text_secondary, 80);

            int bar_w = (int)(b.percent / 100.0 * (line_w - theApp.DPI(160)));
            if (bar_w < 0) bar_w = 0;
            CRect bar(line_x + theApp.DPI(130), y + 2, line_x + theApp.DPI(130) + bar_w, y + 14);
            pDC->FillSolidRect(bar, th.warn);

            wchar_t pbuf[32];
            swprintf_s(pbuf, L"%.0f%%", b.percent);
            draw_text(pbuf, line_x + theApp.DPI(130) + bar_w + 8, y, th.text_secondary, 78);
            y += theApp.DPI(22);
        }
        y += 4;
    }

    // ── 差点就连续 x 天 ──
    if (m_streak_miss > 0)
    {
        wchar_t buf[64];
        swprintf_s(buf, L"差点就连续 %d 天：上一次连续听了 %d 天后中断了。", m_streak_miss + 1, m_streak_miss);
        draw_text(buf, rect.left + pad + 10, y, th.accent, 84);
        y += theApp.DPI(30);
    }

    // ── 歌单/来源贡献 ──
    DrawSectionTitle(pDC, rect, y, L"歌单 / 来源贡献");
    y += 30;
    {
        if (m_playlist.empty())
        {
            draw_text(L"—", rect.left + pad + 10, y, th.text_disabled, 82);
            y += theApp.DPI(22);
        }
        for (const auto& c : m_playlist)
        {
            wchar_t buf[96];
            swprintf_s(buf, L"%s    %s    %.0f%%", c.source.c_str(),
                CStatAnalysis::FormatDuration(c.duration_sec).c_str(), c.percent);
            draw_text(buf, rect.left + pad + 10, y, th.text_primary, 82);
            y += theApp.DPI(22);
        }
    }
}

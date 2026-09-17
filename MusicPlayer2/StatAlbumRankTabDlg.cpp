#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatAlbumRankTabDlg.h"
#include "StatAnalysis.h"
#include "StatChart.h"
#include <vector>

IMPLEMENT_DYNAMIC(CStatAlbumRankTabDlg, CStatTabDlg)

CStatAlbumRankTabDlg::CStatAlbumRankTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_ALBUM_RANK_DLG, pParent)
{
}

CStatAlbumRankTabDlg::~CStatAlbumRankTabDlg()
{
}

void CStatAlbumRankTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_ALBUM_RANK_LIST, m_list);
    DDX_Control(pDX, IDC_STAT_ALBUM_RANK_CHART, m_chart);
}

BEGIN_MESSAGE_MAP(CStatAlbumRankTabDlg, CStatTabDlg)
    ON_WM_DRAWITEM()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CStatAlbumRankTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    // 列宽在填充数据时按 DPI 设定（B3 约定第 6 条），此处只建列
    m_list.InsertColumn(COL_RANK, L"#", LVCFMT_LEFT, theApp.DPI(40));
    m_list.InsertColumn(COL_ALBUM, L"专辑", LVCFMT_LEFT, theApp.DPI(200));
    m_list.InsertColumn(COL_VALUE, L"播放时长", LVCFMT_RIGHT, theApp.DPI(90));

    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW | WS_VSCROLL);

    return TRUE;
}

void CStatAlbumRankTabDlg::BuildRankData()
{
    m_rank_data.clear();
    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr) return;
    // 统一走聚合层（15 秒口径收口在 CStatAnalysis）
    m_rank_data = CStatAnalysis::ComputeAlbumRank(*m_stat_ctx->records);
}

void CStatAlbumRankTabDlg::Refresh()
{
    m_dirty = false;
    BuildRankData();

    // 列宽按 DPI 设定（不依赖 OnInitDialog 的固定宽度）
    m_list.SetColumnWidth(COL_RANK, theApp.DPI(40));
    m_list.SetColumnWidth(COL_ALBUM, theApp.DPI(200));
    m_list.SetColumnWidth(COL_VALUE, theApp.DPI(90));

    m_list.DeleteAllItems();
    for (int i = 0; i < (int)m_rank_data.size(); i++)
    {
        const auto& item = m_rank_data[i];
        wchar_t rank[8];
        swprintf_s(rank, L"%d", i + 1);
        wchar_t val[64];
        swprintf_s(val, L"%s / %d次", CStatAnalysis::FormatDuration(item.duration_sec).c_str(), item.count);

        m_list.InsertItem(i, rank);
        m_list.SetItemText(i, COL_ALBUM, item.album.c_str());
        m_list.SetItemText(i, COL_VALUE, val);
    }

    m_scroll_pos = 0;
    UpdateScrollbar();
    m_chart.Invalidate(FALSE);
}

void CStatAlbumRankTabDlg::UpdateScrollbar()
{
    CRect rc;
    m_chart.GetClientRect(&rc);
    m_page_size = rc.Height();

    int title_h = 30;  // margin_top(8) + title_h(22)
    int content_h = (int)m_rank_data.size() * BAR_HEIGHT;
    m_scroll_max = title_h + content_h;

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

void CStatAlbumRankTabDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (m_scroll_max > m_page_size)
    {
        int step = BAR_HEIGHT;
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
        if (m_scroll_pos < 0) m_scroll_pos = 0;
        if (m_scroll_pos > max_pos) m_scroll_pos = max_pos;

        m_chart.SetScrollPos(SB_VERT, m_scroll_pos, TRUE);
        m_chart.Invalidate(FALSE);
    }
    CTabDlg::OnVScroll(nSBCode, nPos, pScrollBar);
}

BOOL CStatAlbumRankTabDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    CRect rc;
    m_chart.GetWindowRect(&rc);
    if (rc.PtInRect(pt) && m_scroll_max > m_page_size)
    {
        int step = BAR_HEIGHT * 3;
        m_scroll_pos -= zDelta / 120 * step;

        int max_pos = m_scroll_max - m_page_size;
        if (m_scroll_pos < 0) m_scroll_pos = 0;
        if (m_scroll_pos > max_pos) m_scroll_pos = max_pos;

        m_chart.SetScrollPos(SB_VERT, m_scroll_pos, TRUE);
        m_chart.Invalidate(FALSE);
        return TRUE;
    }
    return CTabDlg::OnMouseWheel(nFlags, zDelta, pt);
}

void CStatAlbumRankTabDlg::OnSize(UINT nType, int cx, int cy)
{
    CTabDlg::OnSize(nType, cx, cy);
    if (m_chart.GetSafeHwnd())
    {
        UpdateScrollbar();
        m_chart.Invalidate(FALSE);
    }
}

void CStatAlbumRankTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_ALBUM_RANK_CHART)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        CRect rect;
        m_chart.GetClientRect(&rect);
        if (rect.Width() < 40 || rect.Height() < 40) return;

        pDC->FillSolidRect(rect, StatPalette::Get().panel_back);
        pDC->SetBkMode(TRANSPARENT);

        DrawBarChart(pDC, rect);
    }
    else
    {
        CTabDlg::OnDrawItem(nIDCtl, lpDrawItemStruct);
    }
}

void CStatAlbumRankTabDlg::DrawBarChart(CDC* pDC, const CRect& rect)
{
    const StatPalette::StatPaletteColors& th = StatPalette::Get();

    if (m_rank_data.empty())
    {
        CFont font;
        font.CreatePointFont(88, L"Microsoft YaHei", pDC);
        CFont* old = pDC->SelectObject(&font);
        pDC->SetTextColor(th.text_disabled);
        pDC->TextOutW(rect.left + 20, rect.top + 20, L"暂无专辑记录");
        pDC->SelectObject(old);
        return;
    }

    int show_count = (int)m_rank_data.size();

    int max_value = 1;
    for (int i = 0; i < show_count; i++)
    {
        if (m_rank_data[i].duration_sec > max_value)
            max_value = m_rank_data[i].duration_sec;
    }

    int margin_top = 8;
    int margin_left = 20;
    int margin_right = 50;
    int title_h = 22;
    int chart_w = rect.Width() - margin_left - margin_right;

    CFont fTitle;
    fTitle.CreatePointFont(92, L"Microsoft YaHei", pDC);
    CFont* pOldFont = pDC->SelectObject(&fTitle);
    pDC->SetTextColor(th.text_primary);
    pDC->TextOutW(rect.left + margin_left, rect.top + margin_top - 2, L"专辑播放时长");

    int content_top = rect.top + margin_top + title_h;
    int content_bottom = rect.bottom;

    CRgn clipRgn;
    clipRgn.CreateRectRgn(rect.left, content_top, rect.right, content_bottom);
    pDC->SelectClipRgn(&clipRgn);

    CFont small_font;
    small_font.CreatePointFont(78, L"Microsoft YaHei", pDC);
    pDC->SelectObject(&small_font);

    int bar_gap = 6;
    for (int i = 0; i < show_count; i++)
    {
        int y = content_top + i * BAR_HEIGHT - m_scroll_pos;
        int bar_y = y + bar_gap / 2;
        int bar_actual_h = BAR_HEIGHT - bar_gap;
        if (bar_actual_h < 4) bar_actual_h = 4;

        if (bar_y + bar_actual_h < content_top || bar_y > content_bottom)
            continue;

        const auto& item = m_rank_data[i];

        float ratio = (float)item.duration_sec / max_value;
        int bar_w = (int)(ratio * (chart_w - 50));
        if (bar_w < 2) bar_w = 2;

        int bar_x = rect.left + margin_left;

        COLORREF bar_color = th.series[i % 8];
        CBrush brush(bar_color);
        CBrush* old_brush = pDC->SelectObject(&brush);
        pDC->SelectObject(GetStockObject(NULL_PEN));
        pDC->Rectangle(bar_x, bar_y, bar_x + bar_w + 2, bar_y + bar_actual_h);
        pDC->SelectObject(old_brush);

        wchar_t rank_buf[8];
        swprintf_s(rank_buf, L"%d.", i + 1);
        pDC->SetTextColor(th.text_secondary);
        pDC->TextOutW(bar_x, bar_y, rank_buf, (int)wcslen(rank_buf));

        CSize rank_sz = pDC->GetTextExtent(rank_buf, (int)wcslen(rank_buf));
        int name_x = bar_x + rank_sz.cx + 6;

        pDC->SetTextColor(th.text_primary);
        std::wstring name = item.album;
        if (name.size() > 8) name = name.substr(0, 8) + L"..";
        pDC->TextOutW(name_x, bar_y, name.c_str(), (int)name.size());

        pDC->SetTextColor(th.text_secondary);
        std::wstring val = CStatAnalysis::FormatDuration(item.duration_sec);
        pDC->TextOutW(bar_x + bar_w + 6, bar_y, val.c_str(), (int)val.size());
    }

    pDC->SelectClipRgn(nullptr);
    pDC->SelectObject(pOldFont);
}

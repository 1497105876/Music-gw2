#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatSongRankTabDlg.h"
#include "StatAnalysis.h"
#include "StatChart.h"
#include <map>
#include <vector>
#include <algorithm>

IMPLEMENT_DYNAMIC(CStatSongRankTabDlg, CStatTabDlg)

CStatSongRankTabDlg::CStatSongRankTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_SONG_RANK_DLG, pParent)
{
}

CStatSongRankTabDlg::~CStatSongRankTabDlg()
{
}

void CStatSongRankTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_SONG_RANK_LIST, m_list);
    DDX_Control(pDX, IDC_STAT_SONG_RANK_CHART, m_chart);
}

BEGIN_MESSAGE_MAP(CStatSongRankTabDlg, CStatTabDlg)
    ON_WM_DRAWITEM()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CStatSongRankTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_list.InsertColumn(COL_RANK, L"#", LVCFMT_LEFT, theApp.DPI(40));
    m_list.InsertColumn(COL_NAME, L"歌曲", LVCFMT_LEFT, theApp.DPI(500));
    m_list.InsertColumn(COL_VALUE, L"播放次数", LVCFMT_RIGHT, theApp.DPI(78));

    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW | WS_VSCROLL);

    return TRUE;
}

void CStatSongRankTabDlg::BuildRankData()
{
    m_rank_data.clear();
    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr) return;
    const std::vector<PlayRecord>& records = *m_stat_ctx->records;

    std::map<std::wstring, int> song_count;
    for (const auto& r : records)
    {
        if (!CStatAnalysis::IsCounted(r)) continue;     // 15 秒口径唯一入口
        song_count[r.file_path]++;
    }

    std::vector<std::pair<std::wstring, int>> sorted(song_count.begin(), song_count.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& [path, count] : sorted)
    {
        SongRankItem item;
        item.file_path = path;
        item.play_count = count;

        std::wstring name = path;
        size_t pos = name.find_last_of(L"\\/");
        if (pos != std::wstring::npos) name = name.substr(pos + 1);
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos) name = name.substr(0, dot);
        item.name = name;

        m_rank_data.push_back(std::move(item));
    }
}

void CStatSongRankTabDlg::Refresh()
{
    m_dirty = false;
    BuildRankData();

    m_list.DeleteAllItems();

    for (int i = 0; i < (int)m_rank_data.size(); i++)
    {
        const auto& item = m_rank_data[i];
        wchar_t rank[8];
        swprintf_s(rank, L"%d", i + 1);
        wchar_t val[16];
        swprintf_s(val, L"%d次", item.play_count);

        m_list.InsertItem(i, rank);
        m_list.SetItemText(i, COL_NAME, item.name.c_str());
        m_list.SetItemText(i, COL_VALUE, val);
    }

    m_scroll_pos = 0;
    UpdateScrollbar();
    m_chart.Invalidate(FALSE);
}

void CStatSongRankTabDlg::UpdateScrollbar()
{
    CRect rc;
    m_chart.GetClientRect(&rc);
    m_page_size = rc.Height();

    int title_h = 36;  // margin_top(8) + title_h(28)
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

void CStatSongRankTabDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (m_scroll_max > m_page_size)
    {
        int step = BAR_HEIGHT;
        switch (nSBCode)
        {
        case SB_LINEUP:
            m_scroll_pos -= step;
            break;
        case SB_LINEDOWN:
            m_scroll_pos += step;
            break;
        case SB_PAGEUP:
            m_scroll_pos -= m_page_size;
            break;
        case SB_PAGEDOWN:
            m_scroll_pos += m_page_size;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            m_scroll_pos = nPos;
            break;
        case SB_TOP:
            m_scroll_pos = 0;
            break;
        case SB_BOTTOM:
            m_scroll_pos = m_scroll_max - m_page_size;
            break;
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

BOOL CStatSongRankTabDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
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

// 窗口缩放时重新计算滚动条并重绘图表
void CStatSongRankTabDlg::OnSize(UINT nType, int cx, int cy)
{
    CTabDlg::OnSize(nType, cx, cy);
    if (m_chart.GetSafeHwnd())
    {
        UpdateScrollbar();
        m_chart.Invalidate(FALSE);
    }
}

void CStatSongRankTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_SONG_RANK_CHART)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        // 用 GetClientRect 拿控件真实区域，lpDrawItemStruct->rcItem 在窗口缩放后可能不准
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

void CStatSongRankTabDlg::DrawBarChart(CDC* pDC, const CRect& rect)
{
    const StatPalette::StatPaletteColors& th = StatPalette::Get();

    if (m_rank_data.empty()) return;

    int show_count = (int)m_rank_data.size();

    int max_value = 1;
    for (int i = 0; i < show_count; i++)
    {
        if (m_rank_data[i].play_count > max_value)
            max_value = m_rank_data[i].play_count;
    }

    int margin_top = 8;
    int margin_left = 4;
    int margin_right = 4;
    int title_h = 28;

    int chart_w = rect.Width() - margin_left - margin_right;

    // 标题
    CFont fTitle;
    fTitle.CreatePointFont(100, L"Microsoft YaHei", pDC);
    CFont* pOldFont = pDC->SelectObject(&fTitle);
    pDC->SetTextColor(th.text_primary);
    pDC->TextOutW(rect.left + margin_left, rect.top + margin_top - 2, L"歌曲播放次数");

    int content_top = rect.top + margin_top + title_h;
    int content_bottom = rect.bottom;

    CRgn clipRgn;
    clipRgn.CreateRectRgn(rect.left, content_top, rect.right, content_bottom);
    pDC->SelectClipRgn(&clipRgn);

    CFont small_font;
    small_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
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

        float ratio = (float)item.play_count / max_value;
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

        // 测量排名文字宽度，后面歌曲名留出固定间距
        CSize rank_sz = pDC->GetTextExtent(rank_buf, (int)wcslen(rank_buf));
        int name_x = bar_x + rank_sz.cx + 6;

        pDC->SetTextColor(th.text_primary);
        std::wstring name = item.name;
        if (name.size() > 8) name = name.substr(0, 8) + L"..";
        pDC->TextOutW(name_x, bar_y, name.c_str(), (int)name.size());

        pDC->SetTextColor(th.text_secondary);
        wchar_t val_buf[16];
        swprintf_s(val_buf, L"%d次", item.play_count);
        pDC->TextOutW(bar_x + bar_w + 6, bar_y, val_buf, (int)wcslen(val_buf));
    }

    pDC->SelectClipRgn(nullptr);
    pDC->SelectObject(pOldFont);
}

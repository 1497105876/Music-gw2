#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatTabDlg.h"
#include "ListCtrlEx.h"
#include <vector>

IMPLEMENT_DYNAMIC(CStatTabDlg, CTabDlg)

CStatTabDlg::CStatTabDlg(UINT nIDTemplate, CWnd* pParent)
    : CTabDlg(nIDTemplate, pParent)
{
}

CStatTabDlg::~CStatTabDlg()
{
}

BEGIN_MESSAGE_MAP(CStatTabDlg, CTabDlg)
    ON_WM_SIZE()
    ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

// 子页焦点在对话框自身，滚轮不会自动转给鼠标下的控件，这里手工转发
BOOL CStatTabDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    CWnd* pUnder = WindowFromPoint(pt);
    if (pUnder != nullptr && pUnder->GetSafeHwnd() != m_hWnd && IsChild(pUnder))
    {
        wchar_t cls[32] = {};
        ::GetClassName(pUnder->GetSafeHwnd(), cls, 32);
        if (_wcsicmp(cls, L"Edit") == 0)
        {
            // 只读多行编辑框不处理 WM_MOUSEWHEEL，改发 VSCROLL 逐行滚
            int lines = (zDelta < 0) ? -zDelta : zDelta;
            lines = lines * 3 / 120;
            if (lines < 1) lines = 1;
            UINT sb = (zDelta > 0) ? SB_LINEUP : SB_LINEDOWN;
            for (int i = 0; i < lines; i++)
                pUnder->SendMessage(WM_VSCROLL, MAKEWPARAM(sb, 0), 0);
        }
        else
        {
            // 列表等控件自己处理 WM_MOUSEWHEEL
            pUnder->SendMessage(WM_MOUSEWHEEL, MAKEWPARAM(nFlags, zDelta), MAKELPARAM(pt.x, pt.y));
        }
        return TRUE;
    }
    return CTabDlg::OnMouseWheel(nFlags, zDelta, pt);
}

void CStatTabDlg::EnableColumnFit(CListCtrlEx* list, int flex_column, const std::vector<int>& widths)
{
    m_fit_list = list;
    m_flex_column = flex_column;
    m_fit_widths = widths;
    FitColumns();
}

// 窗口尺寸变化时，列表控件已被 AFX_DIALOG_LAYOUT 拉伸到整页，这里按新宽度重排列宽
void CStatTabDlg::OnSize(UINT nType, int cx, int cy)
{
    CTabDlg::OnSize(nType, cx, cy);
    FitColumns();
}

// 固定列给固定值、弹性列独占剩余宽度（照 CListenTimeStatisticsDlg 做法），
// 避免整表按比例缩放时列宽溢出/留白。
void CStatTabDlg::FitColumns()
{
    if (m_fit_list == nullptr || m_fitting) return;
    if (m_fit_list->GetSafeHwnd() == nullptr) return;
    if (m_fit_widths.empty()) return;

    CHeaderCtrl* pHeader = m_fit_list->GetHeaderCtrl();
    if (pHeader == nullptr || pHeader->GetSafeHwnd() == nullptr) return;

    CRect hr;
    pHeader->GetClientRect(&hr);
    const int avail = hr.Width();
    if (avail <= 0) return;

    const int n = static_cast<int>(m_fit_widths.size());
    int flex = m_flex_column;
    if (flex < 0 || flex >= n) flex = n - 1;         // 下标非法时退化为最后一列

    // 固定列占用总量（含 DPI 缩放；非正设计宽度按 0 处理）
    int used = 0;
    for (int i = 0; i < n; i++)
    {
        if (i == flex) continue;
        used += theApp.DPI(m_fit_widths[i] > 0 ? m_fit_widths[i] : 0);
    }

    // 弹性列：独占剩余宽度；不足时不低于其设计宽度（视为最小宽，未给则默认 120）
    const int flex_min = theApp.DPI(m_fit_widths[flex] > 0 ? m_fit_widths[flex] : 120);
    int flex_w = avail - used - theApp.DPI(8);       // 预留 8 个单位吸收表头边框/滚动条误差
    if (flex_w < flex_min) flex_w = flex_min;

    // 先设固定列，再设弹性列，保证弹性列拿到全部剩余宽度
    m_fitting = true;
    for (int i = 0; i < n; i++)
    {
        if (i == flex) continue;
        int cw = theApp.DPI(m_fit_widths[i] > 0 ? m_fit_widths[i] : 0);
        if (cw < 0) cw = 0;
        m_fit_list->SetColumnWidth(i, cw);
    }
    m_fit_list->SetColumnWidth(flex, flex_w);
    m_fitting = false;
}

void CStatTabDlg::SetContext(const StatContext* ctx)
{
    m_stat_ctx = ctx;
    m_dirty = true;

    // 可见页立即重算（不可见页保持脏标记，待 OnTabEntered 补算）
    if (GetSafeHwnd() != nullptr && IsWindowVisible())
        Refresh();
}

void CStatTabDlg::OnTabEntered()
{
    if (m_dirty)
        Refresh();
    CTabDlg::OnTabEntered();
}

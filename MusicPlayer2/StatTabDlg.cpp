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

void CStatTabDlg::EnableColumnFit(CListCtrlEx* list, const std::vector<int>& weights)
{
    m_fit_list = list;
    m_fit_weights = weights;
    FitColumns();
}

// 窗口尺寸变化时，列表控件已被 AFX_DIALOG_LAYOUT 拉伸到整页，这里按新宽度重排列宽
void CStatTabDlg::OnSize(UINT nType, int cx, int cy)
{
    CTabDlg::OnSize(nType, cx, cy);
    FitColumns();
}

// 按权重把可用宽度分配给各列（余量给最后一列），避免固定像素列宽在缩放时溢出/留白
void CStatTabDlg::FitColumns()
{
    if (m_fit_list == nullptr || m_fitting) return;
    if (m_fit_list->GetSafeHwnd() == nullptr) return;
    if (m_fit_weights.empty()) return;

    CHeaderCtrl* pHeader = m_fit_list->GetHeaderCtrl();
    if (pHeader == nullptr || pHeader->GetSafeHwnd() == nullptr) return;

    CRect hr;
    pHeader->GetClientRect(&hr);
    int avail = hr.Width();
    if (avail <= 0) return;

    const int min_w = theApp.DPI(40);
    int n = static_cast<int>(m_fit_weights.size());
    long long sum = 0;
    for (int w : m_fit_weights) sum += (w > 0 ? w : 0);
    if (sum <= 0) return;

    m_fitting = true;
    int used = 0;
    for (int i = 0; i < n; i++)
    {
        int cw;
        if (i == n - 1)
            cw = avail - used;                                          // 余量给最后一列
        else
            cw = static_cast<int>((double)avail * m_fit_weights[i] / (double)sum);
        if (cw < min_w) cw = min_w;
        m_fit_list->SetColumnWidth(i, cw);
        used += cw;
    }
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

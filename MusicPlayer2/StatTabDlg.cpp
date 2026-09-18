#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatTabDlg.h"

IMPLEMENT_DYNAMIC(CStatTabDlg, CTabDlg)

CStatTabDlg::CStatTabDlg(UINT nIDTemplate, CWnd* pParent)
    : CTabDlg(nIDTemplate, pParent)
{
}

CStatTabDlg::~CStatTabDlg()
{
}

BEGIN_MESSAGE_MAP(CStatTabDlg, CTabDlg)
END_MESSAGE_MAP()

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

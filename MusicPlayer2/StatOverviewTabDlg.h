#pragma once
#include "StatTabDlg.h"
#include "ListCtrlEx.h"

class CStatOverviewTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatOverviewTabDlg)
public:
    CStatOverviewTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatOverviewTabDlg();

    enum { IDD = IDD_STAT_OVERVIEW_DLG };

    virtual void Refresh() override;

protected:
    CListCtrlEx m_list;

    enum Column
    {
        COL_ITEM = 0,
        COL_VALUE,
    };

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    DECLARE_MESSAGE_MAP()
};

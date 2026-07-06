#pragma once
#include "TabDlg.h"
#include "ListCtrlEx.h"
#include "PlayStatistics.h"

class CStatOverviewTabDlg : public CTabDlg
{
    DECLARE_DYNAMIC(CStatOverviewTabDlg)
public:
    CStatOverviewTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatOverviewTabDlg();

    enum { IDD = IDD_STAT_OVERVIEW_DLG };

    void SetRecords(const std::vector<PlayRecord>& records);

protected:
    CListCtrlEx m_list;

    enum Column
    {
        COL_ITEM = 0,
        COL_VALUE,
    };

    void ShowOverview();

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    DECLARE_MESSAGE_MAP()
};

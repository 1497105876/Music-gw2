#pragma once
#include "StatTabDlg.h"

class CStatTrendTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatTrendTabDlg)
public:
    CStatTrendTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatTrendTabDlg();

    enum { IDD = IDD_STAT_TREND_DLG };

    virtual void Refresh() override;

protected:
    CStatic m_chart;

    void DrawTrendChart(CDC* pDC, const CRect& rect);

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);

    DECLARE_MESSAGE_MAP()
};

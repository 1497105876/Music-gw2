#pragma once
#include "TabDlg.h"
#include "PlayStatistics.h"

class CStatTrendTabDlg : public CTabDlg
{
    DECLARE_DYNAMIC(CStatTrendTabDlg)
public:
    CStatTrendTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatTrendTabDlg();

    enum { IDD = IDD_STAT_TREND_DLG };

    void SetRecords(const std::vector<PlayRecord>& records);

protected:
    CStatic m_chart;
    std::vector<PlayRecord> m_records;

    void DrawTrendChart(CDC* pDC, const CRect& rect);

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);

    DECLARE_MESSAGE_MAP()
};

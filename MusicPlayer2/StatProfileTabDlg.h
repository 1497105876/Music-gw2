#pragma once
#include "TabDlg.h"
#include "PlayStatistics.h"
#include "StatAnalysis.h"

class CStatProfileTabDlg : public CTabDlg
{
    DECLARE_DYNAMIC(CStatProfileTabDlg)
public:
    CStatProfileTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatProfileTabDlg();

    enum { IDD = IDD_STAT_PROFILE_DLG };

    void SetRecords(const std::vector<PlayRecord>& records);

protected:
    CStatic m_chart;
    std::vector<PlayRecord> m_records;
    StatSummary m_summary;

    // 自绘整页内容
    void DrawProfile(CDC* pDC, const CRect& rect);

    // 分区块绘制辅助
    void DrawSectionTitle(CDC* pDC, const CRect& rect, int y, const std::wstring& title);
    void DrawBadges(CDC* pDC, const CRect& rect, int y, int* out_height);
    void DrawInsights(CDC* pDC, const CRect& rect, int y, int* out_height);

    int CalcContentHeight(int width);
    void UpdateScrollbar();

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnSize(UINT nType, int cx, int cy);

    DECLARE_MESSAGE_MAP()

private:
    int m_scroll_pos{ 0 };
    int m_scroll_max{ 0 };
    int m_page_size{ 0 };
};

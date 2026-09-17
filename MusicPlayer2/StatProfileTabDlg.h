#pragma once
#include "StatTabDlg.h"
#include "StatAnalysis.h"
#include "StatAiInsight.h"
#include "StatCommon.h"
#include <vector>

class CStatProfileTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatProfileTabDlg)
public:
    CStatProfileTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatProfileTabDlg();

    enum { IDD = IDD_STAT_PROFILE_DLG };

    virtual void Refresh() override;

protected:
    CStatic m_chart;
    StatSummary m_summary;

    // 音乐 DNA 报告（REQ-118）与按年归档回顾（REQ-120）
    DnaReport m_dna;
    std::vector<YearReview> m_yearly;

    // 自绘整页内容
    void DrawProfile(CDC* pDC, const CRect& rect);

    // 分区块绘制辅助
    void DrawSectionTitle(CDC* pDC, const CRect& rect, int y, const std::wstring& title);
    void DrawBadges(CDC* pDC, const CRect& rect, int y, int* out_height);
    void DrawDna(CDC* pDC, const CRect& rect, int y, int* out_height);
    void DrawInsights(CDC* pDC, const CRect& rect, int y, int* out_height);
    void DrawYearly(CDC* pDC, const CRect& rect, int y, int* out_height);

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

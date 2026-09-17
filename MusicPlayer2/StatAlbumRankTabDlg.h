#pragma once
#include "StatTabDlg.h"
#include "ListCtrlEx.h"
#include "StatCommon.h"
#include <vector>

// 专辑排行页（REQ-105）：左侧按累计时长降序的水平条形图，右侧列表。
// 空专辑名统一归入“未知专辑”；仅统计 >=15 秒的记录（口径唯一入口在 CStatAnalysis）。
class CStatAlbumRankTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatAlbumRankTabDlg)
public:
    CStatAlbumRankTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatAlbumRankTabDlg();

    enum { IDD = IDD_STAT_ALBUM_RANK_DLG };

    virtual void Refresh() override;

protected:
    CListCtrlEx m_list;
    CStatic m_chart;
    std::vector<AlbumRankItem> m_rank_data;   // 由 CStatAnalysis::ComputeAlbumRank 产出

    int m_scroll_pos{ 0 };
    int m_scroll_max{ 0 };
    int m_page_size{ 0 };
    static const int BAR_HEIGHT = 44;

    enum Column
    {
        COL_RANK = 0,
        COL_ALBUM,
        COL_VALUE,
    };

    void BuildRankData();
    void DrawBarChart(CDC* pDC, const CRect& rect);
    void UpdateScrollbar();

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnSize(UINT nType, int cx, int cy);

    DECLARE_MESSAGE_MAP()
};

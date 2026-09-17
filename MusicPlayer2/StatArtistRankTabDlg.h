#pragma once
#include "StatTabDlg.h"
#include "ListCtrlEx.h"
#include <vector>

struct RankItem
{
    std::wstring name;
    int value{};        // 播放时长(秒) 或 播放次数
    std::wstring display_value;
};

class CStatArtistRankTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatArtistRankTabDlg)
public:
    CStatArtistRankTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatArtistRankTabDlg();

    enum { IDD = IDD_STAT_ARTIST_RANK_DLG };

    virtual void Refresh() override;

protected:
    CListCtrlEx m_list;
    CStatic m_chart;
    std::vector<RankItem> m_rank_data;

    int m_scroll_pos{ 0 };      // 当前滚动位置（像素）
    int m_scroll_max{ 0 };      // 内容总高度（像素）
    int m_page_size{ 0 };       // 可视区域高度（像素）
    static const int BAR_HEIGHT = 44;  // 每条固定高度

    enum Column
    {
        COL_RANK = 0,
        COL_NAME,
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

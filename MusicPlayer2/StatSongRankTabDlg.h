#pragma once
#include "StatTabDlg.h"
#include "ListCtrlEx.h"
#include <vector>

struct RankItem;  // forward declaration

class CStatSongRankTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatSongRankTabDlg)
public:
    CStatSongRankTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatSongRankTabDlg();

    enum { IDD = IDD_STAT_SONG_RANK_DLG };

    virtual void Refresh() override;

protected:
    CListCtrlEx m_list;
    CStatic m_chart;

    struct SongRankItem
    {
        std::wstring name;
        std::wstring file_path;
        int play_count{};
    };
    std::vector<SongRankItem> m_rank_data;

    int m_scroll_pos{ 0 };
    int m_scroll_max{ 0 };
    int m_page_size{ 0 };
    static const int BAR_HEIGHT = 44;

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

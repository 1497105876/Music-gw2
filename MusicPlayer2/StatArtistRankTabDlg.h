#pragma once
#include "TabDlg.h"
#include "ListCtrlEx.h"
#include "PlayStatistics.h"
#include <vector>

struct RankItem
{
    std::wstring name;
    int value{};        // 播放时长(秒) 或 播放次数
    std::wstring display_value;
};

class CStatArtistRankTabDlg : public CTabDlg
{
    DECLARE_DYNAMIC(CStatArtistRankTabDlg)
public:
    CStatArtistRankTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatArtistRankTabDlg();

    enum { IDD = IDD_STAT_ARTIST_RANK_DLG };

    void SetRecords(const std::vector<PlayRecord>& records);

protected:
    CListCtrlEx m_list;
    CStatic m_chart;
    std::vector<PlayRecord> m_records;
    std::vector<RankItem> m_rank_data;

    enum Column
    {
        COL_RANK = 0,
        COL_NAME,
        COL_VALUE,
    };

    void BuildRankData();
    void DrawBarChart(CDC* pDC, const CRect& rect);

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);

    DECLARE_MESSAGE_MAP()
};

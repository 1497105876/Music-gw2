#pragma once
#include "BaseDialog.h"
#include "CTabCtrlEx.h"
#include "PlayStatistics.h"
#include "StatOverviewTabDlg.h"
#include "StatArtistRankTabDlg.h"
#include "StatSongRankTabDlg.h"
#include "StatTrendTabDlg.h"
#include "StatSongsTabDlg.h"
#include <vector>

class CPlayStatisticsDlg : public CBaseDialog
{
    DECLARE_DYNAMIC(CPlayStatisticsDlg)

public:
    CPlayStatisticsDlg(CWnd* pParent = nullptr);
    virtual ~CPlayStatisticsDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_PLAY_STATISTICS_DIALOG };
#endif

protected:
    CTabCtrlEx m_tab;

    CStatOverviewTabDlg m_overview_dlg;
    CStatArtistRankTabDlg m_artist_rank_dlg;
    CStatSongRankTabDlg m_song_rank_dlg;
    CStatTrendTabDlg m_trend_dlg;
    CStatSongsTabDlg m_songs_dlg;

    std::vector<PlayRecord> m_records;

    void LoadRecords();

protected:
    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual bool IsRememberDialogSizeEnable() const override { return false; }
    virtual void DoDataExchange(CDataExchange* pDX);

    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
    afx_msg void OnBnClickedExportCsvButton();
    afx_msg void OnBnClickedExportJsonButton();
};

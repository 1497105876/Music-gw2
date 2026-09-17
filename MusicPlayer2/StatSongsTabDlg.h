#pragma once
#include "StatTabDlg.h"
#include "ListCtrlEx.h"

class CStatSongsTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatSongsTabDlg)
public:
    CStatSongsTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatSongsTabDlg();

    enum { IDD = IDD_STAT_SONGS_DLG };

    virtual void Refresh() override;

protected:
    CListCtrlEx m_list;

    enum Column
    {
        DCOL_INDEX = 0,
        DCOL_TIME,
        DCOL_TITLE,
        DCOL_ARTIST,
        DCOL_ALBUM,
        DCOL_PLAY_DUR,
        DCOL_SONG_LEN,
        DCOL_RESULT,
        DCOL_SOURCE,
    };

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    DECLARE_MESSAGE_MAP()
};

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatSongsTabDlg.h"

IMPLEMENT_DYNAMIC(CStatSongsTabDlg, CStatTabDlg)

CStatSongsTabDlg::CStatSongsTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_SONGS_DLG, pParent)
{
}

CStatSongsTabDlg::~CStatSongsTabDlg()
{
}

void CStatSongsTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_SONGS_LIST, m_list);
}

BEGIN_MESSAGE_MAP(CStatSongsTabDlg, CStatTabDlg)
END_MESSAGE_MAP()

BOOL CStatSongsTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_list.InsertColumn(DCOL_INDEX, L"序号", LVCFMT_LEFT, 0);
    m_list.InsertColumn(DCOL_TIME, L"播放时间", LVCFMT_LEFT, 0);
    m_list.InsertColumn(DCOL_TITLE, L"标题", LVCFMT_LEFT, 0);
    m_list.InsertColumn(DCOL_ARTIST, L"艺术家", LVCFMT_LEFT, 0);
    m_list.InsertColumn(DCOL_ALBUM, L"专辑", LVCFMT_LEFT, 0);
    m_list.InsertColumn(DCOL_PLAY_DUR, L"播放时长", LVCFMT_RIGHT, 0);
    m_list.InsertColumn(DCOL_SONG_LEN, L"歌曲长度", LVCFMT_RIGHT, 0);
    m_list.InsertColumn(DCOL_RESULT, L"结果", LVCFMT_CENTER, 0);
    // 列宽自适应：按权重填满整页宽度（缩放时自动重算）
    EnableColumnFit(&m_list, { 50, 140, 200, 120, 120, 70, 70, 60 });

    return TRUE;
}

// 明细页：展示过滤后的全部记录（不做 15 秒过滤，明细用于核查）
void CStatSongsTabDlg::Refresh()
{
    m_dirty = false;
    m_list.DeleteAllItems();

    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr)
        return;
    const std::vector<PlayRecord>& records = *m_stat_ctx->records;

    auto format_dur = [](int sec) -> std::wstring {
        int m = sec / 60;
        int s = sec % 60;
        wchar_t buf[16];
        swprintf_s(buf, L"%d:%02d", m, s);
        return buf;
    };

    auto reason_str = [](PlayRecord::FinishReason r) -> std::wstring {
        switch (r)
        {
        case PlayRecord::FinishReason::COMPLETED:  return L"播完";
        case PlayRecord::FinishReason::SKIPPED:    return L"跳过";
        case PlayRecord::FinishReason::STOPPED:    return L"停止";
        case PlayRecord::FinishReason::PLAY_ERROR:   return L"出错";
        }
        return L"";
    };

    int row = 0;
    for (const auto& r : records)
    {
        m_list.InsertItem(row, std::to_wstring(row + 1).c_str());
        m_list.SetItemText(row, DCOL_TIME, r.played_at.c_str());
        m_list.SetItemText(row, DCOL_TITLE, r.title.c_str());
        m_list.SetItemText(row, DCOL_ARTIST, r.artist.c_str());
        m_list.SetItemText(row, DCOL_ALBUM, r.album.c_str());
        m_list.SetItemText(row, DCOL_PLAY_DUR, format_dur(r.play_duration_sec).c_str());
        m_list.SetItemText(row, DCOL_SONG_LEN, format_dur(r.song_length_sec / 1000).c_str());
        m_list.SetItemText(row, DCOL_RESULT, reason_str(r.finish_reason).c_str());
        row++;
    }
}

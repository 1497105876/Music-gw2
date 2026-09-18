#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatAlbumRankTabDlg.h"
#include "StatAnalysis.h"
#include <vector>

IMPLEMENT_DYNAMIC(CStatAlbumRankTabDlg, CStatTabDlg)

CStatAlbumRankTabDlg::CStatAlbumRankTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_ALBUM_RANK_DLG, pParent)
{
}

CStatAlbumRankTabDlg::~CStatAlbumRankTabDlg()
{
}

void CStatAlbumRankTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_ALBUM_RANK_LIST, m_list);
}

BEGIN_MESSAGE_MAP(CStatAlbumRankTabDlg, CStatTabDlg)
    ON_WM_DRAWITEM()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CStatAlbumRankTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    m_list.InsertColumn(COL_RANK, L"#", LVCFMT_LEFT, 0);
    m_list.InsertColumn(COL_ALBUM, L"专辑", LVCFMT_LEFT, 0);
    m_list.InsertColumn(COL_VALUE, L"播放时长", LVCFMT_RIGHT, 0);
    // 列宽自适应：专辑名列（弹性列）独占剩余宽度，排名/时长列固定（缩放时自动重算）
    EnableColumnFit(&m_list, 1, { 40, 200, 90 });

    return TRUE;
}

void CStatAlbumRankTabDlg::BuildRankData()
{
    m_rank_data.clear();
    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr) return;
    // 统一走聚合层（15 秒口径收口在 CStatAnalysis）
    m_rank_data = CStatAnalysis::ComputeAlbumRank(*m_stat_ctx->records);
}

void CStatAlbumRankTabDlg::Refresh()
{
    m_dirty = false;
    BuildRankData();

    m_list.DeleteAllItems();
    for (int i = 0; i < (int)m_rank_data.size(); i++)
    {
        const auto& item = m_rank_data[i];
        wchar_t rank[8];
        swprintf_s(rank, L"%d", i + 1);
        wchar_t val[64];
        swprintf_s(val, L"%s / %d次", CStatAnalysis::FormatDuration(item.duration_sec).c_str(), item.count);

        m_list.InsertItem(i, rank);
        m_list.SetItemText(i, COL_ALBUM, item.album.c_str());
        m_list.SetItemText(i, COL_VALUE, val);
    }

    m_scroll_pos = 0;
}

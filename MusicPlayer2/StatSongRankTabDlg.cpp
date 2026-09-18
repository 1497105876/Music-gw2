#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatSongRankTabDlg.h"
#include "StatAnalysis.h"
#include <map>
#include <vector>
#include <algorithm>

IMPLEMENT_DYNAMIC(CStatSongRankTabDlg, CStatTabDlg)

CStatSongRankTabDlg::CStatSongRankTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_SONG_RANK_DLG, pParent)
{
}

CStatSongRankTabDlg::~CStatSongRankTabDlg()
{
}

void CStatSongRankTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_SONG_RANK_LIST, m_list);
}

BEGIN_MESSAGE_MAP(CStatSongRankTabDlg, CStatTabDlg)
    ON_WM_DRAWITEM()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CStatSongRankTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    // 列宽一次性按设计宽度算好（照 CListenTimeStatisticsDlg::OnInitDialog）：
    // 排名/次数列固定，歌曲名列吃剩余宽度，不随窗口缩放重算
    CRect rect;
    m_list.GetWindowRect(rect);
    int width_rank = theApp.DPI(40);
    int width_value = theApp.DPI(78);
    int width_name = rect.Width() - width_rank - width_value - theApp.DPI(20) - 1;
    m_list.InsertColumn(COL_RANK, L"#", LVCFMT_LEFT, width_rank);
    m_list.InsertColumn(COL_NAME, L"歌曲", LVCFMT_LEFT, width_name);
    m_list.InsertColumn(COL_VALUE, L"播放次数", LVCFMT_RIGHT, width_value);

    return TRUE;
}

void CStatSongRankTabDlg::BuildRankData()
{
    m_rank_data.clear();
    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr) return;
    const std::vector<PlayRecord>& records = *m_stat_ctx->records;

    std::map<std::wstring, int> song_count;
    for (const auto& r : records)
    {
        if (!CStatAnalysis::IsCounted(r)) continue;     // 15 秒口径唯一入口
        song_count[r.file_path]++;
    }

    std::vector<std::pair<std::wstring, int>> sorted(song_count.begin(), song_count.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& [path, count] : sorted)
    {
        SongRankItem item;
        item.file_path = path;
        item.play_count = count;

        std::wstring name = path;
        size_t pos = name.find_last_of(L"\\/");
        if (pos != std::wstring::npos) name = name.substr(pos + 1);
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos) name = name.substr(0, dot);
        item.name = name;

        m_rank_data.push_back(std::move(item));
    }
}

void CStatSongRankTabDlg::Refresh()
{
    m_dirty = false;
    BuildRankData();

    m_list.DeleteAllItems();

    for (int i = 0; i < (int)m_rank_data.size(); i++)
    {
        const auto& item = m_rank_data[i];
        wchar_t rank[8];
        swprintf_s(rank, L"%d", i + 1);
        wchar_t val[16];
        swprintf_s(val, L"%d次", item.play_count);

        m_list.InsertItem(i, rank);
        m_list.SetItemText(i, COL_NAME, item.name.c_str());
        m_list.SetItemText(i, COL_VALUE, val);
    }

    m_scroll_pos = 0;
}

// 窗口缩放时重新计算滚动条并重绘图表
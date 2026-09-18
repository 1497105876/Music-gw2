#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatArtistRankTabDlg.h"
#include "StatAnalysis.h"
#include <map>
#include <vector>
#include <algorithm>

IMPLEMENT_DYNAMIC(CStatArtistRankTabDlg, CStatTabDlg)

CStatArtistRankTabDlg::CStatArtistRankTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_ARTIST_RANK_DLG, pParent)
{
}

CStatArtistRankTabDlg::~CStatArtistRankTabDlg()
{
}

void CStatArtistRankTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_ARTIST_RANK_LIST, m_list);
}

BEGIN_MESSAGE_MAP(CStatArtistRankTabDlg, CStatTabDlg)
    ON_WM_DRAWITEM()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CStatArtistRankTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    // 列表控件：整行选中 + 网格线 + 双缓冲防闪烁
    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    m_list.InsertColumn(COL_RANK, L"#", LVCFMT_LEFT, 0);       // 排名列
    m_list.InsertColumn(COL_NAME, L"歌手", LVCFMT_LEFT, 0);    // 歌手名列
    m_list.InsertColumn(COL_VALUE, L"播放时长", LVCFMT_RIGHT, 0); // 时长列
    // 列宽自适应：歌手名列（弹性列）独占剩余宽度，排名/时长列固定（缩放时自动重算）
    EnableColumnFit(&m_list, 1, { 40, 200, 78 });

    return TRUE;
}

// 汇总每个歌手的总播放时长，按降序排列（口径统一：走 IsCounted）
void CStatArtistRankTabDlg::BuildRankData()
{
    m_rank_data.clear();
    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr) return;
    const std::vector<PlayRecord>& records = *m_stat_ctx->records;

    // 按歌手名累加播放时长
    std::map<std::wstring, int> artist_time;
    for (const auto& r : records)
    {
        if (!CStatAnalysis::IsCounted(r)) continue;  // 15 秒口径唯一入口
        if (!r.artist.empty())
            artist_time[r.artist] += r.play_duration_sec;
    }

    // 转成 vector 排序，时长长的排前面
    std::vector<std::pair<std::wstring, int>> sorted(artist_time.begin(), artist_time.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    // 构建显示数据，把秒转成"X时XX分XX秒"的可读格式
    for (const auto& [name, dur] : sorted)
    {
        RankItem item;
        item.name = name;
        item.value = dur;

        int h = dur / 3600, m = (dur % 3600) / 60, s = dur % 60;
        wchar_t val[32];
        if (h > 0)
            swprintf_s(val, L"%d时%02d分%02d秒", h, m, s);
        else
            swprintf_s(val, L"%d分%02d秒", m, s);
        item.display_value = val;

        m_rank_data.push_back(std::move(item));
    }
}

// 全局上下文变化时重算并刷新排行榜与图表
void CStatArtistRankTabDlg::Refresh()
{
    m_dirty = false;
    BuildRankData();

    // 填充右侧列表
    m_list.DeleteAllItems();
    for (int i = 0; i < (int)m_rank_data.size(); i++)
    {
        const auto& item = m_rank_data[i];
        wchar_t rank[8];
        swprintf_s(rank, L"%d", i + 1);

        m_list.InsertItem(i, rank);
        m_list.SetItemText(i, COL_NAME, item.name.c_str());
        m_list.SetItemText(i, COL_VALUE, item.display_value.c_str());
    }

    // 重置滚动位置，刷新滚动条和图表
    m_scroll_pos = 0;
}

// 根据数据条数和控件高度，决定是否显示滚动条
// 处理滚动条的上下滚动（点箭头、拖滑块、点空白处翻页）
// 鼠标滚轮：每次滚3条
// 窗口缩放时重新计算滚动条并重绘图表
// 系统发来的自绘消息，把绘图工作转发给 DrawBarChart
// 核心绘图函数：在左侧画水平条形图
// rect 是图表控件的完整绘制区域
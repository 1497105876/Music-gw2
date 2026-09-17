#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatArtistRankTabDlg.h"
#include "StatAnalysis.h"
#include "StatTheme.h"
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
    DDX_Control(pDX, IDC_STAT_ARTIST_RANK_CHART, m_chart);
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
    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_list.InsertColumn(COL_RANK, L"#", LVCFMT_LEFT, theApp.DPI(40));       // 排名列
    m_list.InsertColumn(COL_NAME, L"歌手", LVCFMT_LEFT, theApp.DPI(200));    // 歌手名列
    m_list.InsertColumn(COL_VALUE, L"播放时长", LVCFMT_RIGHT, theApp.DPI(78)); // 时长列

    // 把图表静态控件改成自绘模式（SS_OWNERDRAW），系统会发 WM_DRAWITEM 来让我们自己画
    // 同时加上 WS_VSCROLL，数据多了可以滚动
    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW | WS_VSCROLL);

    CStatTheme::ApplyDialog(this);
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
    UpdateScrollbar();
    m_chart.Invalidate(FALSE);
}

// 根据数据条数和控件高度，决定是否显示滚动条
void CStatArtistRankTabDlg::UpdateScrollbar()
{
    CRect rc;
    m_chart.GetClientRect(&rc);
    m_page_size = rc.Height();  // 可视区域高度

    // 内容总高度 = 标题区(margin_top 8 + title_h 28 = 36) + 每条 BAR_HEIGHT(44) * 条数
    int title_h = 36;  // margin_top(8) + title_h(28)
    int content_h = (int)m_rank_data.size() * BAR_HEIGHT;
    m_scroll_max = title_h + content_h;

    if (m_scroll_max <= m_page_size)
    {
        // 内容不够高，不需要滚动条
        m_scroll_pos = 0;
        m_chart.EnableScrollBarCtrl(SB_VERT, FALSE);
        m_chart.ShowScrollBar(SB_VERT, FALSE);
    }
    else
    {
        // 内容超出可视区域，显示滚动条
        m_chart.EnableScrollBarCtrl(SB_VERT, TRUE);
        m_chart.ShowScrollBar(SB_VERT, TRUE);

        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = m_scroll_max - 1;  // 滚动范围 = 内容总高度
        si.nPage = m_page_size;       // 每页高度 = 控件高度
        si.nPos = m_scroll_pos;       // 当前位置
        m_chart.SetScrollInfo(SB_VERT, &si, TRUE);
    }
}

// 处理滚动条的上下滚动（点箭头、拖滑块、点空白处翻页）
void CStatArtistRankTabDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (m_scroll_max > m_page_size)
    {
        int step = BAR_HEIGHT;  // 每次滚动一个条形的高度
        switch (nSBCode)
        {
        case SB_LINEUP:          // 点上箭头，上移一条
            m_scroll_pos -= step;
            break;
        case SB_LINEDOWN:        // 点下箭头，下移一条
            m_scroll_pos += step;
            break;
        case SB_PAGEUP:          // 点滑块上方空白，上移一页
            m_scroll_pos -= m_page_size;
            break;
        case SB_PAGEDOWN:        // 点滑块下方空白，下移一页
            m_scroll_pos += m_page_size;
            break;
        case SB_THUMBTRACK:      // 拖动滑块中
        case SB_THUMBPOSITION:   // 拖动松开
            m_scroll_pos = nPos;
            break;
        case SB_TOP:             // 拉到最顶
            m_scroll_pos = 0;
            break;
        case SB_BOTTOM:          // 拉到最底
            m_scroll_pos = m_scroll_max - m_page_size;
            break;
        }

        // 限制滚动范围，不能超出头尾
        int max_pos = m_scroll_max - m_page_size;
        if (max_pos < 0) max_pos = 0;
        if (m_scroll_pos < 0) m_scroll_pos = 0;
        if (m_scroll_pos > max_pos) m_scroll_pos = max_pos;

        m_chart.SetScrollPos(SB_VERT, m_scroll_pos, TRUE);
        m_chart.Invalidate(FALSE);  // 重绘图表
    }

    CTabDlg::OnVScroll(nSBCode, nPos, pScrollBar);
}

// 鼠标滚轮：每次滚3条
BOOL CStatArtistRankTabDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    CRect rc;
    m_chart.GetWindowRect(&rc);
    if (rc.PtInRect(pt) && m_scroll_max > m_page_size)
    {
        int step = BAR_HEIGHT * 3;  // 每次滚3条
        m_scroll_pos -= zDelta / 120 * step;  // zDelta 正数向上滚，负数向下

        int max_pos = m_scroll_max - m_page_size;
        if (m_scroll_pos < 0) m_scroll_pos = 0;
        if (m_scroll_pos > max_pos) m_scroll_pos = max_pos;

        m_chart.SetScrollPos(SB_VERT, m_scroll_pos, TRUE);
        m_chart.Invalidate(FALSE);
        return TRUE;
    }

    return CTabDlg::OnMouseWheel(nFlags, zDelta, pt);
}

// 窗口缩放时重新计算滚动条并重绘图表
void CStatArtistRankTabDlg::OnSize(UINT nType, int cx, int cy)
{
    CTabDlg::OnSize(nType, cx, cy);
    if (m_chart.GetSafeHwnd())
    {
        UpdateScrollbar();
        m_chart.Invalidate(FALSE);
    }
}

// 系统发来的自绘消息，把绘图工作转发给 DrawBarChart
void CStatArtistRankTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_ARTIST_RANK_CHART)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        // 用 GetClientRect 拿控件真实区域，lpDrawItemStruct->rcItem 在窗口缩放后可能不准
        CRect rect;
        m_chart.GetClientRect(&rect);
        if (rect.Width() < 40 || rect.Height() < 40) return;

        // 填充背景色（跟随主题，无硬编码浅色）
        pDC->FillSolidRect(rect, CStatTheme::Get().panel_back);
        pDC->SetBkMode(TRANSPARENT);

        DrawBarChart(pDC, rect);
    }
    else
    {
        CTabDlg::OnDrawItem(nIDCtl, lpDrawItemStruct);
    }
}

// 核心绘图函数：在左侧画水平条形图
// rect 是图表控件的完整绘制区域
void CStatArtistRankTabDlg::DrawBarChart(CDC* pDC, const CRect& rect)
{
    const StatThemeColors& th = CStatTheme::Get();

    if (m_rank_data.empty()) return;

    int show_count = (int)m_rank_data.size();

    // 找到最大值，用来计算每条条的宽度比例
    int max_value = 1;
    for (int i = 0; i < show_count; i++)
    {
        if (m_rank_data[i].value > max_value)
            max_value = m_rank_data[i].value;
    }

    // ===== 布局参数 =====
    int margin_top = 8;     // 顶部留白
    int margin_left = 20;    // 左侧留白
    int margin_right = 50;   // 右侧留白
    int title_h = 28;       // 标题行高度（标题和条形图之间的间距）

    int chart_w = rect.Width() - margin_left - margin_right;  // 实际可用宽度

    // ===== 标题（固定不滚动，始终在顶部） =====
    CFont fTitle;
    fTitle.CreatePointFont(100, L"Microsoft YaHei", pDC);
    CFont* pOldFont = pDC->SelectObject(&fTitle);
    pDC->SetTextColor(th.text_primary);
    pDC->TextOutW(rect.left + margin_left, rect.top + margin_top - 2, L"歌手播放时长");

    // ===== 内容区域（滚动时需要裁剪，防止画到标题上面） =====
    int content_top = rect.top + margin_top + title_h;  // 内容起始 y = 8 + 28 = 36
    int content_bottom = rect.bottom;                    // 内容结束 y = 控件底部

    // 创建裁剪区域，只允许在 content_top ~ content_bottom 之间画
    CRgn clipRgn;
    clipRgn.CreateRectRgn(rect.left, content_top, rect.right, content_bottom);
    pDC->SelectClipRgn(&clipRgn);

    // 条形图用的字体，比标题小一号
    CFont small_font;
    small_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
    pDC->SelectObject(&small_font);

    int bar_gap = 6;  // 条与条之间的间距

    for (int i = 0; i < show_count; i++)
    {
        // 每条的 y 坐标 = 内容起始 + 第 i 条 * 每条高度 - 滚动偏移
        int y = content_top + i * BAR_HEIGHT - m_scroll_pos;
        int bar_y = y + bar_gap / 2;                // 条形实际起始 y（留上间距）
        int bar_actual_h = BAR_HEIGHT - bar_gap;     // 条形实际高度（减去间距）
        if (bar_actual_h < 4) bar_actual_h = 4;

        // 跳过完全在可视区域外的条（优化性能）
        if (bar_y + bar_actual_h < content_top || bar_y > content_bottom)
            continue;

        const auto& item = m_rank_data[i];

        // 条形宽度 = 占比 * 可用宽度（留 50px 给文字标签）
        float ratio = (float)item.value / max_value;
        int bar_w = (int)(ratio * (chart_w - 50));
        if (bar_w < 2) bar_w = 2;

        int bar_x = rect.left + margin_left;

        // 画条形（用 NULL_PEN 避免边框线）；颜色取主题系列色
        COLORREF bar_color = th.series[i % 8];
        CBrush brush(bar_color);
        CBrush* old_brush = pDC->SelectObject(&brush);
        pDC->SelectObject(GetStockObject(NULL_PEN));
        pDC->Rectangle(bar_x, bar_y, bar_x + bar_w + 2, bar_y + bar_actual_h);
        pDC->SelectObject(old_brush);

        // 排名标号（条形左侧）
        wchar_t rank_buf[8];
        swprintf_s(rank_buf, L"%d.", i + 1);
        pDC->SetTextColor(th.text_secondary);
        pDC->TextOutW(bar_x, bar_y, rank_buf, (int)wcslen(rank_buf));

        // 测量排名文字宽度，后面歌手名留出固定间距
        CSize rank_sz = pDC->GetTextExtent(rank_buf, (int)wcslen(rank_buf));
        int name_x = bar_x + rank_sz.cx + 6;  // 排名右边留6px间距

        // 歌手名（超8字截断）
        pDC->SetTextColor(th.text_primary);
        std::wstring name = item.name;
        if (name.size() > 8) name = name.substr(0, 8) + L"..";
        pDC->TextOutW(name_x, bar_y, name.c_str(), (int)name.size());

        // 时长数值（条形右侧）
        pDC->SetTextColor(th.text_secondary);
        std::wstring val = item.display_value;
        pDC->TextOutW(bar_x + bar_w + 6, bar_y, val.c_str(), (int)val.size());
    }

    // 恢复 DC 状态
    pDC->SelectClipRgn(nullptr);
    pDC->SelectObject(pOldFont);
}

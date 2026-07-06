#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatArtistRankTabDlg.h"
#include <map>
#include <vector>
#include <algorithm>

IMPLEMENT_DYNAMIC(CStatArtistRankTabDlg, CTabDlg)

CStatArtistRankTabDlg::CStatArtistRankTabDlg(CWnd* pParent)
    : CTabDlg(IDD_STAT_ARTIST_RANK_DLG, pParent)
{
}

CStatArtistRankTabDlg::~CStatArtistRankTabDlg()
{
}

void CStatArtistRankTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_ARTIST_RANK_LIST, m_list);
    DDX_Control(pDX, IDC_STAT_ARTIST_RANK_CHART, m_chart);
}

BEGIN_MESSAGE_MAP(CStatArtistRankTabDlg, CTabDlg)
    ON_WM_DRAWITEM()
END_MESSAGE_MAP()

BOOL CStatArtistRankTabDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_list.InsertColumn(COL_RANK, L"#", LVCFMT_LEFT, 40);
    m_list.InsertColumn(COL_NAME, L"歌手", LVCFMT_LEFT, 180);
    m_list.InsertColumn(COL_VALUE, L"播放时长", LVCFMT_RIGHT, 76);

    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW);

    return TRUE;
}

void CStatArtistRankTabDlg::BuildRankData()
{
    m_rank_data.clear();

    std::map<std::wstring, int> artist_time;
    for (const auto& r : m_records)
    {
        if (r.play_duration_sec < 15) continue;
        if (!r.artist.empty())
            artist_time[r.artist] += r.play_duration_sec;
    }

    std::vector<std::pair<std::wstring, int>> sorted(artist_time.begin(), artist_time.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

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

void CStatArtistRankTabDlg::SetRecords(const std::vector<PlayRecord>& records)
{
    m_records = records;
    BuildRankData();

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

    m_chart.Invalidate(FALSE);
}

void CStatArtistRankTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_ARTIST_RANK_CHART)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        CRect rect(lpDrawItemStruct->rcItem);
        if (rect.Width() < 40 || rect.Height() < 40) return;

        pDC->FillSolidRect(rect, RGB(252, 252, 255));
        pDC->SetBkMode(TRANSPARENT);

        DrawBarChart(pDC, rect);
    }
    else
    {
        CTabDlg::OnDrawItem(nIDCtl, lpDrawItemStruct);
    }
}

void CStatArtistRankTabDlg::DrawBarChart(CDC* pDC, const CRect& rect)
{
    if (m_rank_data.empty()) return;

    // 最多显示前15名
    int show_count = (int)m_rank_data.size();
    if (show_count > 15) show_count = 15;

    int max_value = 1;
    for (int i = 0; i < show_count; i++)
    {
        if (m_rank_data[i].value > max_value)
            max_value = m_rank_data[i].value;
    }

    // 布局参数
    int margin_top = 8;
    int margin_bottom = 8;
    int margin_left = 4;
    int margin_right = 4;
    int title_h = 20;

    int chart_top = rect.top + margin_top + title_h;
    int chart_h = rect.Height() - margin_top - margin_bottom - title_h;
    int chart_w = rect.Width() - margin_left - margin_right;
    if (chart_h <= 0 || chart_w <= 0) return;

    // 标题
    CFont fTitle;
    fTitle.CreatePointFont(100, L"Microsoft YaHei", pDC);
    CFont* pOldFont = pDC->SelectObject(&fTitle);
    pDC->SetTextColor(RGB(40, 40, 40));
    pDC->TextOutW(rect.left + margin_left, rect.top + margin_top - 2, L"歌手播放时长Top15", 14);

    // 每条的高度
    int bar_total_h = chart_h;
    int bar_count = show_count;
    int bar_h = bar_total_h / bar_count;
    if (bar_h < 8) bar_h = 8;
    int bar_gap = 2;
    if (bar_h > 22) bar_gap = 3;

    CFont small_font;
    small_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
    pDC->SelectObject(&small_font);

    // 颜色调色板（渐变蓝→紫→橙）
    auto get_color = [](int idx, int total) -> COLORREF {
        float t = (total > 1) ? (float)idx / (total - 1) : 0.f;
        // 从蓝色(70,130,200)渐变到紫色(160,100,200)再到橙色(230,140,70)
        if (t < 0.5f)
        {
            float t2 = t * 2.f;
            int r = (int)(70 + (160 - 70) * t2);
            int g = (int)(130 + (100 - 130) * t2);
            int b = (int)(200 + (200 - 200) * t2);
            return RGB(r, g, b);
        }
        else
        {
            float t2 = (t - 0.5f) * 2.f;
            int r = (int)(160 + (230 - 160) * t2);
            int g = (int)(100 + (140 - 100) * t2);
            int b = (int)(200 + (70 - 200) * t2);
            return RGB(r, g, b);
        }
    };

    for (int i = 0; i < show_count; i++)
    {
        const auto& item = m_rank_data[i];
        int y = chart_top + i * bar_h;
        if (y + bar_h > rect.bottom - margin_bottom) break;

        float ratio = (float)item.value / max_value;
        int bar_w = (int)(ratio * (chart_w - 50));  // 右侧留50px给文字
        if (bar_w < 2) bar_w = 2;

        int bar_x = rect.left + margin_left;
        int bar_y = y + bar_gap / 2;
        int bar_actual_h = bar_h - bar_gap;
        if (bar_actual_h < 4) bar_actual_h = 4;

        // 绘制条形
        COLORREF bar_color = get_color(i, show_count);
        CBrush brush(bar_color);
        CBrush* old_brush = pDC->SelectObject(&brush);
        pDC->SelectObject(GetStockObject(NULL_PEN));
        pDC->Rectangle(bar_x, bar_y, bar_x + bar_w + 2, bar_y + bar_actual_h);
        pDC->SelectObject(old_brush);

        // 排名标号
        wchar_t rank_buf[8];
        swprintf_s(rank_buf, L"%d", i + 1);
        pDC->SetTextColor(RGB(100, 100, 100));
        pDC->TextOutW(bar_x, bar_y, rank_buf, (int)wcslen(rank_buf));

        // 歌手名（截断显示）
        pDC->SetTextColor(RGB(60, 60, 60));
        std::wstring name = item.name;
        if (name.size() > 6) name = name.substr(0, 6) + L"..";
        pDC->TextOutW(bar_x + 16, bar_y, name.c_str(), (int)name.size());

        // 数值标签（在条形右侧）
        pDC->SetTextColor(RGB(80, 80, 80));
        std::wstring val = item.display_value;
        pDC->TextOutW(bar_x + bar_w + 6, bar_y, val.c_str(), (int)val.size());
    }

    pDC->SelectObject(pOldFont);
}

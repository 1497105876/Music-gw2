#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatSongRankTabDlg.h"
#include <map>
#include <vector>
#include <algorithm>

IMPLEMENT_DYNAMIC(CStatSongRankTabDlg, CTabDlg)

CStatSongRankTabDlg::CStatSongRankTabDlg(CWnd* pParent)
    : CTabDlg(IDD_STAT_SONG_RANK_DLG, pParent)
{
}

CStatSongRankTabDlg::~CStatSongRankTabDlg()
{
}

void CStatSongRankTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_SONG_RANK_LIST, m_list);
    DDX_Control(pDX, IDC_STAT_SONG_RANK_CHART, m_chart);
}

BEGIN_MESSAGE_MAP(CStatSongRankTabDlg, CTabDlg)
    ON_WM_DRAWITEM()
END_MESSAGE_MAP()

BOOL CStatSongRankTabDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_list.InsertColumn(COL_RANK, L"#", LVCFMT_LEFT, 40);
    m_list.InsertColumn(COL_NAME, L"歌曲", LVCFMT_LEFT, 200);
    m_list.InsertColumn(COL_VALUE, L"播放次数", LVCFMT_RIGHT, 56);

    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW);

    return TRUE;
}

void CStatSongRankTabDlg::BuildRankData()
{
    m_rank_data.clear();

    std::map<std::wstring, int> song_count;
    for (const auto& r : m_records)
    {
        if (r.play_duration_sec < 15) continue;
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

        // 从路径提取显示名
        std::wstring name = path;
        size_t pos = name.find_last_of(L"\\/");
        if (pos != std::wstring::npos) name = name.substr(pos + 1);
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos) name = name.substr(0, dot);
        item.name = name;

        m_rank_data.push_back(std::move(item));
    }
}

void CStatSongRankTabDlg::SetRecords(const std::vector<PlayRecord>& records)
{
    m_records = records;
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

    m_chart.Invalidate(FALSE);
}

void CStatSongRankTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_SONG_RANK_CHART)
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

void CStatSongRankTabDlg::DrawBarChart(CDC* pDC, const CRect& rect)
{
    if (m_rank_data.empty()) return;

    // 最多显示前15名
    int show_count = (int)m_rank_data.size();
    if (show_count > 15) show_count = 15;

    int max_value = 1;
    for (int i = 0; i < show_count; i++)
    {
        if (m_rank_data[i].play_count > max_value)
            max_value = m_rank_data[i].play_count;
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
    pDC->TextOutW(rect.left + margin_left, rect.top + margin_top - 2, L"歌曲播放次数Top15", 14);

    // 每条的高度
    int bar_count = show_count;
    int bar_h = chart_h / bar_count;
    if (bar_h < 8) bar_h = 8;
    int bar_gap = 2;
    if (bar_h > 22) bar_gap = 3;

    CFont small_font;
    small_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
    pDC->SelectObject(&small_font);

    // 颜色调色板（渐变绿→青→蓝）
    auto get_color = [](int idx, int total) -> COLORREF {
        float t = (total > 1) ? (float)idx / (total - 1) : 0.f;
        // 从绿色(70,200,130)渐变到青色(60,180,210)再到蓝色(80,130,220)
        if (t < 0.5f)
        {
            float t2 = t * 2.f;
            int r = (int)(70 + (60 - 70) * t2);
            int g = (int)(200 + (180 - 200) * t2);
            int b = (int)(130 + (210 - 130) * t2);
            return RGB(r, g, b);
        }
        else
        {
            float t2 = (t - 0.5f) * 2.f;
            int r = (int)(60 + (80 - 60) * t2);
            int g = (int)(180 + (130 - 180) * t2);
            int b = (int)(210 + (220 - 210) * t2);
            return RGB(r, g, b);
        }
    };

    for (int i = 0; i < show_count; i++)
    {
        const auto& item = m_rank_data[i];
        int y = chart_top + i * bar_h;
        if (y + bar_h > rect.bottom - margin_bottom) break;

        float ratio = (float)item.play_count / max_value;
        int bar_w = (int)(ratio * (chart_w - 50));  // 右侧留50px给数值
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

        // 歌曲名（截断显示）
        pDC->SetTextColor(RGB(60, 60, 60));
        std::wstring name = item.name;
        if (name.size() > 6) name = name.substr(0, 6) + L"..";
        pDC->TextOutW(bar_x + 16, bar_y, name.c_str(), (int)name.size());

        // 数值标签
        pDC->SetTextColor(RGB(80, 80, 80));
        wchar_t val_buf[16];
        swprintf_s(val_buf, L"%d次", item.play_count);
        pDC->TextOutW(bar_x + bar_w + 6, bar_y, val_buf, (int)wcslen(val_buf));
    }

    pDC->SelectObject(pOldFont);
}

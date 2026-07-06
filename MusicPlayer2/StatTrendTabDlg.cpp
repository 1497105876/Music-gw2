#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatTrendTabDlg.h"
#include <map>
#include <vector>
#include <algorithm>

IMPLEMENT_DYNAMIC(CStatTrendTabDlg, CTabDlg)

CStatTrendTabDlg::CStatTrendTabDlg(CWnd* pParent)
    : CTabDlg(IDD_STAT_TREND_DLG, pParent)
{
}

CStatTrendTabDlg::~CStatTrendTabDlg()
{
}

void CStatTrendTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_TREND_CHART, m_chart);
}

BEGIN_MESSAGE_MAP(CStatTrendTabDlg, CTabDlg)
    ON_WM_DRAWITEM()
END_MESSAGE_MAP()

BOOL CStatTrendTabDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();

    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW);

    return TRUE;
}

void CStatTrendTabDlg::SetRecords(const std::vector<PlayRecord>& records)
{
    m_records = records;
    m_chart.Invalidate(FALSE);
}

void CStatTrendTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_TREND_CHART)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        CRect rect(lpDrawItemStruct->rcItem);
        if (rect.Width() < 80 || rect.Height() < 80) return;

        pDC->FillSolidRect(rect, RGB(252, 252, 255));
        pDC->SetBkMode(TRANSPARENT);

        DrawTrendChart(pDC, rect);
    }
    else
    {
        CTabDlg::OnDrawItem(nIDCtl, lpDrawItemStruct);
    }
}

void CStatTrendTabDlg::DrawTrendChart(CDC* pDC, const CRect& rect)
{
    // 聚合每日播放次数
    std::map<std::wstring, int> daily_count;
    for (const auto& r : m_records)
    {
        if (r.play_duration_sec < 15) continue;
        if (r.played_at.size() >= 10)
        {
            std::wstring date = r.played_at.substr(0, 10);
            daily_count[date]++;
        }
    }

    time_t now = time(nullptr);
    std::vector<std::wstring> dates;
    std::vector<int> counts;

    for (int i = 29; i >= 0; i--)
    {
        time_t t = now - i * 86400;
        struct tm tm_buf;
        localtime_s(&tm_buf, &t);
        wchar_t buf[16];
        swprintf_s(buf, L"%04d-%02d-%02d",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
        std::wstring date(buf);
        dates.push_back(date);

        auto it = daily_count.find(date);
        counts.push_back(it != daily_count.end() ? it->second : 0);
    }

    int max_count = 1;
    for (int c : counts) if (c > max_count) max_count = c;

    int margin_left = 45, margin_right = 15, margin_top = 30, margin_bottom = 30;
    int chart_w = rect.Width() - margin_left - margin_right;
    int chart_h = rect.Height() - margin_top - margin_bottom;
    if (chart_w <= 0 || chart_h <= 0) return;

    // 标题
    CFont fTitle;
    fTitle.CreatePointFont(140, L"Microsoft YaHei", pDC);
    pDC->SelectObject(&fTitle);
    pDC->SetTextColor(RGB(30, 30, 30));
    pDC->TextOutW(rect.left + margin_left, rect.top + 5, L"30天播放趋势", 10);

    // 坐标轴
    CPen axis_pen(PS_SOLID, 1, RGB(180, 180, 180));
    CPen* old_pen = pDC->SelectObject(&axis_pen);
    pDC->MoveTo(rect.left + margin_left, rect.top + margin_top);
    pDC->LineTo(rect.left + margin_left, rect.top + margin_top + chart_h);
    pDC->LineTo(rect.left + margin_left + chart_w, rect.top + margin_top + chart_h);
    pDC->SelectObject(old_pen);

    CFont small_font;
    small_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
    CFont* pOldFont = pDC->SelectObject(&small_font);

    // Y轴刻度
    pDC->SetTextColor(RGB(120, 120, 120));
    wchar_t buf[16];
    swprintf_s(buf, L"%d", max_count);
    pDC->TextOutW(rect.left + 10, rect.top + margin_top - 5, buf);
    pDC->TextOutW(rect.left + 15, rect.top + margin_top + chart_h - 5, L"0");

    // 柱+折线
    int bar_w = chart_w / 30;
    if (bar_w < 2) bar_w = 2;
    CPen line_pen(PS_SOLID, 2, RGB(80, 140, 220));
    CBrush bar_brush(RGB(100, 170, 230));

    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < 30; i++)
    {
        int x = rect.left + margin_left + chart_w * i / 30 + bar_w / 2;
        int bar_h = (int)((double)counts[i] / max_count * chart_h);
        int y_pos = rect.top + margin_top + chart_h - bar_h;

        pDC->SelectObject(&bar_brush);
        pDC->Rectangle(x - bar_w / 2, y_pos,
            x + bar_w / 2, rect.top + margin_top + chart_h);

        if (prev_x >= 0)
        {
            pDC->SelectObject(&line_pen);
            pDC->MoveTo(prev_x, prev_y);
            pDC->LineTo(x, y_pos);
        }
        prev_x = x;
        prev_y = y_pos;
    }

    // X轴标签
    pDC->SetTextColor(RGB(100, 100, 100));
    for (int i = 0; i < 30; i += 5)
    {
        int x = rect.left + margin_left + chart_w * i / 30;
        std::wstring label = dates[i].substr(5);
        pDC->TextOutW(x, rect.top + margin_top + chart_h + 5,
            label.c_str(), (int)label.size());
    }

    pDC->SelectObject(pOldFont);
    pDC->SelectObject(old_pen);
}

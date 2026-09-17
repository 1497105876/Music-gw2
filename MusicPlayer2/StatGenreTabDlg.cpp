#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatGenreTabDlg.h"
#include "StatAnalysis.h"
#include "StatTheme.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace
{
    const double kPi = 3.14159265358979323846;

    // 角度（自 12 点方向、顺时针，单位度）转换为以 center 为圆心、半径 r 的点
    CPoint PointOnCircle(const CPoint& center, int r, double angle_deg)
    {
        double rad = angle_deg * kPi / 180.0;
        int x = center.x + (int)(r * std::sin(rad));
        int y = center.y - (int)(r * std::cos(rad));
        return CPoint(x, y);
    }
}

IMPLEMENT_DYNAMIC(CStatGenreTabDlg, CStatTabDlg)

CStatGenreTabDlg::CStatGenreTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_GENRE_DLG, pParent)
{
}

CStatGenreTabDlg::~CStatGenreTabDlg()
{
}

void CStatGenreTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_GENRE_CHART, m_chart);
}

BEGIN_MESSAGE_MAP(CStatGenreTabDlg, CStatTabDlg)
    ON_WM_DRAWITEM()
END_MESSAGE_MAP()

BOOL CStatGenreTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW | SS_NOTIFY);

    CStatTheme::ApplyDialog(this);
    return TRUE;
}

void CStatGenreTabDlg::BuildData()
{
    m_share.clear();
    m_quarter_share.clear();
    m_quarter_labels.clear();
    m_quarter_count = 0;
    m_gems.clear();
    m_similarity = 0.0;
    m_has_similarity = false;

    if (m_stat_ctx == nullptr || m_stat_ctx->records == nullptr) return;
    const std::vector<PlayRecord>& records = *m_stat_ctx->records;

    // 占比（>5 类合并为“其他”）
    m_share = CStatAnalysis::ComputeGenreShare(records, 5);

    // 最近 4 个季度主导流派（漂移叠图）
    m_quarter_share = CStatAnalysis::ComputeGenreShareByQuarter(records, 4, m_quarter_labels);
    m_quarter_count = (int)m_quarter_share.size();

    // 遗珠挖掘（REQ-113）：反复听 >=3 次却从未完整听完
    m_gems = CStatAnalysis::ComputeRetiredGems(records, 3);
    if (m_gems.size() > 3) m_gems.resize(3);

    // 口味对比（REQ-114）：按时间中位数把记录一分为二，比较两段流派分布的余弦相似度
    if (records.size() > 3)
    {
        std::vector<PlayRecord> sorted = records;
        std::sort(sorted.begin(), sorted.end(),
            [](const PlayRecord& a, const PlayRecord& b) { return a.played_at < b.played_at; });
        size_t mid = sorted.size() / 2;
        std::vector<PlayRecord> older(sorted.begin(), sorted.begin() + mid);
        std::vector<PlayRecord> newer(sorted.begin() + mid, sorted.end());
        std::vector<GenreShare> a = CStatAnalysis::ComputeGenreShare(older, 8);
        std::vector<GenreShare> b = CStatAnalysis::ComputeGenreShare(newer, 8);
        if (!a.empty() && !b.empty())
        {
            m_similarity = CStatAnalysis::ComputeCosineSimilarity(a, b);
            m_has_similarity = true;
        }
    }
}

void CStatGenreTabDlg::Refresh()
{
    m_dirty = false;
    BuildData();
    m_chart.Invalidate(FALSE);
}

void CStatGenreTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_GENRE_CHART)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        CRect rect;
        m_chart.GetClientRect(&rect);
        if (rect.Width() < 60 || rect.Height() < 60) return;

        pDC->FillSolidRect(rect, CStatTheme::Get().panel_back);
        pDC->SetBkMode(TRANSPARENT);

        DrawGenre(pDC, rect);
    }
    else
    {
        CTabDlg::OnDrawItem(nIDCtl, lpDrawItemStruct);
    }
}

void CStatGenreTabDlg::DrawGenre(CDC* pDC, const CRect& rect)
{
    const StatThemeColors& th = CStatTheme::Get();

    CFont title_font;
    title_font.CreatePointFont(110, L"Microsoft YaHei", pDC);
    CFont* pOldFont = pDC->SelectObject(&title_font);
    pDC->SetTextColor(th.text_primary);
    pDC->TextOutW(rect.left + 14, rect.top + 6, L"流派分布");
    pDC->SelectObject(pOldFont);

    if (m_share.empty())
    {
        CFont empty_font;
        empty_font.CreatePointFont(95, L"Microsoft YaHei", pDC);
        pDC->SelectObject(&empty_font);
        pDC->SetTextColor(th.text_disabled);
        pDC->TextOutW(rect.left + 20, rect.top + 44, L"暂无流派数据");
        pDC->SelectObject(pOldFont);
        return;
    }

    // 底栏（遗珠 + 口味一致性）固定高度
    bool has_footer = (!m_gems.empty() || m_has_similarity);
    int footer_h = has_footer ? theApp.DPI(92) : 0;

    // 顶部区域：环形图（左）+ 图例（右）
    int top = rect.top + 40;
    int total_h = rect.Height() - 60 - footer_h;
    if (total_h < 120) total_h = 120;
    int top_h = (int)(total_h * 0.60);
    if (top_h < 80) top_h = 80;

    int ring_size = min(top_h, rect.Width() / 2) - 20;
    if (ring_size < 60) ring_size = 60;
    CPoint center(rect.left + 20 + ring_size / 2, top + top_h / 2);

    // 环形（甜甜圈）绘制
    double start_angle = 0.0;
    for (size_t i = 0; i < m_share.size(); i++)
    {
        double sweep = m_share[i].percent * 3.6;   // percent(0~100) -> 度
        if (sweep <= 0.0) continue;
        CPoint p0 = PointOnCircle(center, ring_size / 2, start_angle);
        CPoint p1 = PointOnCircle(center, ring_size / 2, start_angle + sweep);

        CRect rc(center.x - ring_size / 2, center.y - ring_size / 2,
                 center.x + ring_size / 2, center.y + ring_size / 2);

        COLORREF color = th.series[i % 8];
        CBrush brush(color);
        CBrush* old_brush = pDC->SelectObject(&brush);
        CPen pen(PS_SOLID, 1, th.panel_back);
        CPen* old_pen = pDC->SelectObject(&pen);
        pDC->Pie(rc, p0, p1);
        pDC->SelectObject(old_pen);
        pDC->SelectObject(old_brush);

        start_angle += sweep;
    }

    // 中心挖空 -> 环形
    int inner = ring_size / 2 * 55 / 100;
    CRect rc_inner(center.x - inner, center.y - inner, center.x + inner, center.y + inner);
    CBrush back_brush(th.panel_back);
    CBrush* ob = pDC->SelectObject(&back_brush);
    pDC->SelectObject(GetStockObject(NULL_PEN));
    pDC->Ellipse(rc_inner);
    pDC->SelectObject(ob);

    // 图例
    CFont legend_font;
    legend_font.CreatePointFont(82, L"Microsoft YaHei", pDC);
    pDC->SelectObject(&legend_font);
    int lx = center.x + ring_size / 2 + 24;
    int ly = top + 8;
    for (size_t i = 0; i < m_share.size(); i++)
    {
        COLORREF color = th.series[i % 8];
        CRect box(lx, ly + 2, lx + 12, ly + 14);
        pDC->FillSolidRect(box, color);

        wchar_t buf[96];
        swprintf_s(buf, L"%s  %.1f%%", m_share[i].genre.c_str(), m_share[i].percent);
        pDC->SetTextColor(th.text_primary);
        pDC->TextOutW(lx + 18, ly, buf, (int)wcslen(buf));
        ly += 22;
    }
    pDC->SelectObject(pOldFont);

    // 中部区域：口味漂移叠图
    int drift_top = top + top_h + 12;
    int drift_bottom = rect.bottom - footer_h - 6;
    int drift_h = drift_bottom - drift_top;
    if (drift_h >= 40)
    {
        pDC->SelectObject(&title_font);
        pDC->SetTextColor(th.text_primary);
        pDC->TextOutW(rect.left + 14, drift_top - 4, L"口味漂移（近季度主导流派）");
        pDC->SelectObject(pOldFont);

        int dx = rect.left + 40;
        int dy_top = drift_top + 22;
        int dy_bottom = drift_top + drift_h - 14;
        int dw = rect.Width() - 60;
        if (dw >= 40 && dy_bottom > dy_top)
        {
            if (m_quarter_count < 2)
            {
                CFont hint_font;
                hint_font.CreatePointFont(82, L"Microsoft YaHei", pDC);
                pDC->SelectObject(&hint_font);
                pDC->SetTextColor(th.text_disabled);
                pDC->TextOutW(dx, dy_top + 6, L"记录还不足以看出口味漂移");
                pDC->SelectObject(pOldFont);
            }
            else
            {
                double max_pct = 1.0;
                for (const auto& s : m_quarter_share)
                    if (s.percent > max_pct) max_pct = s.percent;

                CPen axis_pen(PS_SOLID, 1, th.axis);
                CPen* old_pen = pDC->SelectObject(&axis_pen);
                pDC->MoveTo(dx, dy_top);
                pDC->LineTo(dx, dy_bottom);
                pDC->LineTo(dx + dw, dy_bottom);
                pDC->SelectObject(old_pen);

                CFont small_font;
                small_font.CreatePointFont(78, L"Microsoft YaHei", pDC);
                pDC->SelectObject(&small_font);

                CPen line_pen(PS_SOLID, 2, th.highlight);
                CPen* op = pDC->SelectObject(&line_pen);
                int prev_x = -1, prev_y = -1;
                int n = (int)m_quarter_share.size();
                for (int i = 0; i < n; i++)
                {
                    int x = (n > 1) ? (dx + dw * i / (n - 1)) : dx;
                    int y = dy_bottom - (int)((m_quarter_share[i].percent / max_pct) * (dy_bottom - dy_top));
                    if (m_quarter_share[i].genre.empty())
                    {
                        prev_x = -1;
                        continue;
                    }
                    if (prev_x >= 0)
                    {
                        pDC->MoveTo(prev_x, prev_y);
                        pDC->LineTo(x, y);
                    }
                    prev_x = x;
                    prev_y = y;

                    CBrush dot_brush(th.highlight);
                    CBrush* odb = pDC->SelectObject(&dot_brush);
                    pDC->Ellipse(x - 3, y - 3, x + 4, y + 4);
                    pDC->SelectObject(odb);
                }
                pDC->SelectObject(op);

                pDC->SetTextColor(th.text_secondary);
                for (int i = 0; i < n; i++)
                {
                    int x = (n > 1) ? (dx + dw * i / (n - 1)) : dx;
                    int y = dy_bottom - (int)((m_quarter_share[i].percent / max_pct) * (dy_bottom - dy_top));
                    std::wstring lbl = m_quarter_labels[i];
                    CSize sz = pDC->GetTextExtent(lbl.c_str(), (int)lbl.size());
                    pDC->TextOutW(x - sz.cx / 2, dy_bottom + 2, lbl.c_str(), (int)lbl.size());
                    if (!m_quarter_share[i].genre.empty())
                    {
                        std::wstring g = m_quarter_share[i].genre;
                        if (g.size() > 6) g = g.substr(0, 6) + L"..";
                        CSize gs = pDC->GetTextExtent(g.c_str(), (int)g.size());
                        pDC->TextOutW(x - gs.cx / 2, y - 16, g.c_str(), (int)g.size());
                    }
                }
                pDC->SelectObject(pOldFont);
            }
        }
    }

    // 底栏：遗珠 + 口味一致性
    if (has_footer)
        DrawFooter(pDC, rect, rect.bottom - footer_h + 2);
}

void CStatGenreTabDlg::DrawFooter(CDC* pDC, const CRect& rect, int top)
{
    const StatThemeColors& th = CStatTheme::Get();

    CFont font;
    font.CreatePointFont(82, L"Microsoft YaHei", pDC);
    CFont* old = pDC->SelectObject(&font);

    int y = top;
    // 遗珠
    if (!m_gems.empty())
    {
        pDC->SetTextColor(th.text_primary);
        pDC->TextOutW(rect.left + 14, y, L"遗珠（反复听却从未完整听完）：", 15);
        y += theApp.DPI(20);

        pDC->SetTextColor(th.text_secondary);
        for (const auto& g : m_gems)
        {
            wchar_t buf[160];
            swprintf_s(buf, L"· %s - %s（%d 次）",
                g.artist.empty() ? L"未知艺术家" : g.artist.c_str(), g.title.c_str(), g.count);
            pDC->TextOutW(rect.left + 26, y, buf, (int)wcslen(buf));
            y += theApp.DPI(18);
        }
    }

    // 口味一致性
    if (m_has_similarity)
    {
        pDC->SetTextColor(th.text_primary);
        wchar_t buf[160];
        swprintf_s(buf, L"口味一致性：%.0f%%（前后两段时间的流派相似度，越接近 100%% 口味越稳定）",
            m_similarity * 100.0);
        pDC->TextOutW(rect.left + 14, y, buf, (int)wcslen(buf));
    }

    pDC->SelectObject(old);
}

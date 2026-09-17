#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatProfileTabDlg.h"
#include "StatTheme.h"
#include <algorithm>

IMPLEMENT_DYNAMIC(CStatProfileTabDlg, CStatTabDlg)

CStatProfileTabDlg::CStatProfileTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_PROFILE_DLG, pParent)
{
}

CStatProfileTabDlg::~CStatProfileTabDlg()
{
}

void CStatProfileTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_PROFILE_CHART, m_chart);
}

BEGIN_MESSAGE_MAP(CStatProfileTabDlg, CStatTabDlg)
    ON_WM_DRAWITEM()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CStatProfileTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();

    // 与排行页一致：SS_BLACKFRAME 改为运行时自绘（SS_OWNERDRAW），带垂直滚动条
    ::SetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE,
        (::GetWindowLongPtr(m_chart.GetSafeHwnd(), GWL_STYLE) & ~SS_BLACKFRAME) | SS_OWNERDRAW | WS_VSCROLL);

    CStatTheme::ApplyDialog(this);
    return TRUE;
}

// 全局上下文变化时重算并刷新（汇总由主对话框统一算一次，直接复用）
void CStatProfileTabDlg::Refresh()
{
    m_dirty = false;

    if (m_stat_ctx != nullptr)
        m_summary = m_stat_ctx->summary;
    else
        m_summary = StatSummary();

    // 音乐 DNA（REQ-118）与按年归档回顾（REQ-120）
    m_dna = CStatAiInsight::BuildDnaReport(m_summary);
    m_yearly.clear();
    if (m_stat_ctx != nullptr && m_stat_ctx->records != nullptr)
        m_yearly = CStatAnalysis::ComputeYearlyReviews(*m_stat_ctx->records);

    m_scroll_pos = 0;
    UpdateScrollbar();
    m_chart.Invalidate(FALSE);
}

// 根据控件宽度估算整页内容高度（与 DrawProfile 的布局保持一致）
int CStatProfileTabDlg::CalcContentHeight(int width)
{
    int y = 8;
    int pad = 12;
    int card_gap = 8;
    UNREFERENCED_PARAMETER(width);

    // 头部大数字卡片
    y += 78 + card_gap;
    // 音乐 DNA
    y += 30 + theApp.DPI(84) + card_gap;
    // 听歌档案（徽章）
    int badges_h = 0;
    {
        int cols = max(1, (width - pad * 2) / theApp.DPI(170));
        int rows = max(1, ((int)m_summary.badges.size() + cols - 1) / cols);
        badges_h = 30 + rows * theApp.DPI(56);
    }
    y += badges_h + card_gap;

    // AI 洞察（按每条最多两行估算）
    {
        auto insights = CStatAiInsight::GenerateInsights(m_summary);
        int text_w = width - pad * 2 - theApp.DPI(24);
        int lines = 0;
        CDC* pDC = GetDC();
        CFont font;
        font.CreatePointFont(88, L"Microsoft YaHei", pDC);
        HFONT old = (HFONT)pDC->SelectObject(font.GetSafeHandle());
        for (const auto& text : insights)
        {
            CRect rc(0, 0, text_w, 1000);
            int h = pDC->DrawText(text.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK);
            UNREFERENCED_PARAMETER(h);
            lines += max(1, rc.Height());
            lines += theApp.DPI(10);
        }
        pDC->SelectObject(old);
        ReleaseDC(pDC);
        y += 30 + lines + theApp.DPI(14);
    }

    // 深度数字网格（2 行固定高度）
    y += 30 + theApp.DPI(110) + card_gap;

    // 年度回顾（每行 + 头部）
    y += 30 + (int)m_yearly.size() * theApp.DPI(24) + theApp.DPI(10) + card_gap;

    return y + 12;
}

void CStatProfileTabDlg::UpdateScrollbar()
{
    if (!m_chart.GetSafeHwnd()) return;
    CRect rc;
    m_chart.GetClientRect(&rc);
    m_page_size = rc.Height();
    m_scroll_max = CalcContentHeight(rc.Width());

    if (m_scroll_max <= m_page_size)
    {
        m_scroll_pos = 0;
        m_chart.EnableScrollBarCtrl(SB_VERT, FALSE);
        m_chart.ShowScrollBar(SB_VERT, FALSE);
    }
    else
    {
        m_chart.EnableScrollBarCtrl(SB_VERT, TRUE);
        m_chart.ShowScrollBar(SB_VERT, TRUE);
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = m_scroll_max - 1;
        si.nPage = m_page_size;
        si.nPos = m_scroll_pos;
        m_chart.SetScrollInfo(SB_VERT, &si, TRUE);
    }
}

void CStatProfileTabDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (m_scroll_max > m_page_size)
    {
        int step = theApp.DPI(36);
        switch (nSBCode)
        {
        case SB_LINEUP:        m_scroll_pos -= step; break;
        case SB_LINEDOWN:      m_scroll_pos += step; break;
        case SB_PAGEUP:        m_scroll_pos -= m_page_size; break;
        case SB_PAGEDOWN:      m_scroll_pos += m_page_size; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: m_scroll_pos = nPos; break;
        case SB_TOP:           m_scroll_pos = 0; break;
        case SB_BOTTOM:        m_scroll_pos = m_scroll_max - m_page_size; break;
        }
        int max_pos = m_scroll_max - m_page_size;
        if (max_pos < 0) max_pos = 0;
        m_scroll_pos = max(0, min(m_scroll_pos, max_pos));

        m_chart.SetScrollPos(SB_VERT, m_scroll_pos, TRUE);
        m_chart.Invalidate(FALSE);
    }
    CTabDlg::OnVScroll(nSBCode, nPos, pScrollBar);
}

BOOL CStatProfileTabDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    CRect rc;
    m_chart.GetWindowRect(&rc);
    if (rc.PtInRect(pt) && m_scroll_max > m_page_size)
    {
        int step = theApp.DPI(60);
        m_scroll_pos -= zDelta / 120 * step;
        int max_pos = m_scroll_max - m_page_size;
        if (max_pos < 0) max_pos = 0;
        m_scroll_pos = max(0, min(m_scroll_pos, max_pos));

        m_chart.SetScrollPos(SB_VERT, m_scroll_pos, TRUE);
        m_chart.Invalidate(FALSE);
        return TRUE;
    }
    return CTabDlg::OnMouseWheel(nFlags, zDelta, pt);
}

void CStatProfileTabDlg::OnSize(UINT nType, int cx, int cy)
{
    CTabDlg::OnSize(nType, cx, cy);
    if (m_chart.GetSafeHwnd())
    {
        UpdateScrollbar();
        m_chart.Invalidate(FALSE);
    }
}

void CStatProfileTabDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_STAT_PROFILE_CHART)
    {
        CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
        CRect rect;
        m_chart.GetClientRect(&rect);
        if (rect.Width() < 40 || rect.Height() < 40) return;

        pDC->FillSolidRect(rect, CStatTheme::Get().panel_back);
        pDC->SetBkMode(TRANSPARENT);

        DrawProfile(pDC, rect);
    }
    else
    {
        CTabDlg::OnDrawItem(nIDCtl, lpDrawItemStruct);
    }
}

// 分区标题：左侧竖条 + 文字
void CStatProfileTabDlg::DrawSectionTitle(CDC* pDC, const CRect& rect, int y, const std::wstring& title)
{
    const StatThemeColors& th = CStatTheme::Get();
    int pad = 12;
    CRect bar(rect.left + pad, y + 2, rect.left + pad + 4, y + 18);
    pDC->FillSolidRect(bar, th.accent);

    CFont font;
    font.CreatePointFont(100, L"Microsoft YaHei", pDC);
    HFONT old = (HFONT)pDC->SelectObject(font.GetSafeHandle());
    pDC->SetTextColor(th.text_primary);
    pDC->TextOutW(rect.left + pad + 10, y, title.c_str(), (int)title.size());
    pDC->SelectObject(old);
}

// 听歌档案徽章：圆角色块 + 徽章名 + 说明
void CStatProfileTabDlg::DrawBadges(CDC* pDC, const CRect& rect, int y, int* out_height)
{
    const StatThemeColors& th = CStatTheme::Get();
    int pad = 12;
    int card_w = theApp.DPI(170);
    int card_h = theApp.DPI(56);
    int cols = max(1, (rect.Width() - pad * 2) / card_w);

    if (m_summary.badges.empty())
    {
        CFont font;
        font.CreatePointFont(88, L"Microsoft YaHei", pDC);
        HFONT old = (HFONT)pDC->SelectObject(font.GetSafeHandle());
        pDC->SetTextColor(th.text_disabled);
        std::wstring empty = L"继续听歌，解锁你的专属听歌档案";
        pDC->TextOutW(rect.left + pad + 10, y + card_h / 2 - 8, empty.c_str(), (int)empty.size());
        pDC->SelectObject(old);
        *out_height = 30 + card_h;
        return;
    }

    for (int i = 0; i < (int)m_summary.badges.size(); i++)
    {
        int row = i / cols, col = i % cols;
        int x = rect.left + pad + 10 + col * card_w;
        int yy = y + row * card_h;

        const auto& badge = m_summary.badges[i];
        COLORREF color = th.series[i % 8];

        // 半透明效果：用浅色底 + 深色左边条模拟
        COLORREF light = RGB(
            (GetRValue(color) + 255 * 2) / 3,
            (GetGValue(color) + 255 * 2) / 3,
            (GetBValue(color) + 255 * 2) / 3);
        CRect rc_card(x, yy, x + card_w - 8, yy + card_h - 8);
        pDC->FillSolidRect(rc_card, th.card_back);
        pDC->FillSolidRect(CRect(rc_card.left, rc_card.top, rc_card.left + 4, rc_card.bottom), color);

        // 徽章名
        CFont font;
        font.CreatePointFont(92, L"Microsoft YaHei", pDC);
        HFONT old = (HFONT)pDC->SelectObject(font.GetSafeHandle());
        pDC->SetTextColor(th.text_primary);
        pDC->TextOutW(rc_card.left + 10, rc_card.top + 5, badge.title.c_str(), (int)badge.title.size());
        pDC->SelectObject(old);

        // 说明文字（截断到卡片宽度）
        CFont small_font;
        small_font.CreatePointFont(78, L"Microsoft YaHei", pDC);
        old = (HFONT)pDC->SelectObject(small_font.GetSafeHandle());
        pDC->SetTextColor(th.text_secondary);
        std::wstring text = badge.text;
        CSize sz = pDC->GetTextExtent(text.c_str(), (int)text.size());
        int max_w = card_w - 36;
        while (sz.cx > max_w && text.size() > 4)
        {
            text.resize(text.size() - 2);
            text += L"..";
            sz = pDC->GetTextExtent(text.c_str(), (int)text.size());
        }
        pDC->TextOutW(rc_card.left + 10, rc_card.top + 26, text.c_str(), (int)text.size());
        pDC->SelectObject(old);
    }

    int rows = ((int)m_summary.badges.size() + cols - 1) / cols;
    *out_height = 30 + rows * card_h;
}

// 音乐 DNA 报告：大标题 + 标签 + 一段描述（模板 + 数据插槽）
void CStatProfileTabDlg::DrawDna(CDC* pDC, const CRect& rect, int y, int* out_height)
{
    const StatThemeColors& th = CStatTheme::Get();
    int pad = 12;
    int card_h = theApp.DPI(84);

    CRect card(rect.left + pad, y, rect.right - pad, y + card_h);
    pDC->FillSolidRect(card, th.card_back_alt);
    pDC->FillSolidRect(CRect(card.left, card.top, card.left + 4, card.bottom), th.highlight);

    // 标题
    CFont title_font;
    title_font.CreatePointFont(130, L"Microsoft YaHei", pDC);
    HFONT old = (HFONT)pDC->SelectObject(title_font.GetSafeHandle());
    pDC->SetTextColor(th.text_primary);
    pDC->TextOutW(card.left + 14, card.top + 8, m_dna.title.c_str(), (int)m_dna.title.size());
    pDC->SelectObject(old);

    // 标签（横向排列）
    int lx = card.left + 14;
    int ly = card.top + 34;
    CFont tag_font;
    tag_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
    old = (HFONT)pDC->SelectObject(tag_font.GetSafeHandle());
    for (const auto& tag : m_dna.tags)
    {
        CSize sz = pDC->GetTextExtent(tag.c_str(), (int)tag.size());
        CRect rc(lx, ly, lx + sz.cx + 14, ly + 18);
        pDC->FillSolidRect(rc, th.series[5]);
        pDC->SetTextColor(th.text_primary);
        pDC->TextOutW(lx + 7, ly + 2, tag.c_str(), (int)tag.size());
        lx += sz.cx + 22;
    }
    pDC->SelectObject(old);

    // 描述
    CFont text_font;
    text_font.CreatePointFont(84, L"Microsoft YaHei", pDC);
    old = (HFONT)pDC->SelectObject(text_font.GetSafeHandle());
    pDC->SetTextColor(th.text_secondary);
    CRect rc_text(card.left + 14, card.top + 56, card.right - 12, card.bottom);
    pDC->DrawText(m_dna.text.c_str(), -1, &rc_text, DT_WORDBREAK | DT_END_ELLIPSIS);
    pDC->SelectObject(old);

    *out_height = 30 + card_h;
}

// AI 洞察列表：每条一个小圆点 + 自然语言段落
void CStatProfileTabDlg::DrawInsights(CDC* pDC, const CRect& rect, int y, int* out_height)
{
    const StatThemeColors& th = CStatTheme::Get();
    int pad = 12;
    int text_x = rect.left + pad + 10 + theApp.DPI(14);
    int text_w = rect.Width() - (text_x - rect.left) - pad;

    auto insights = CStatAiInsight::GenerateInsights(m_summary);
    if (insights.empty())
    {
        *out_height = 30;
        return;
    }

    CFont font;
    font.CreatePointFont(88, L"Microsoft YaHei", pDC);
    HFONT old = (HFONT)pDC->SelectObject(font.GetSafeHandle());

    int yy = y;
    for (const auto& text : insights)
    {
        // 圆点
        CRect dot_rc(rect.left + pad + 10, yy + 6, rect.left + pad + 10 + 6, yy + 12);
        CBrush brush(th.highlight);
        HBRUSH old_brush = (HBRUSH)pDC->SelectObject(brush.GetSafeHandle());
        HPEN pen = CreatePen(PS_NULL, 0, 0);
        HPEN old_pen = (HPEN)pDC->SelectObject(pen);
        pDC->Ellipse(dot_rc);
        pDC->SelectObject(old_brush);
        pDC->SelectObject(old_pen);
        DeleteObject(pen);

        // 文字自动换行
        CRect rc_text(text_x, yy, text_x + text_w, yy + 1000);
        pDC->SetTextColor(th.text_primary);
        pDC->DrawText(text.c_str(), -1, &rc_text, DT_WORDBREAK);
        int line_h = rc_text.Height();

        yy += line_h + theApp.DPI(10);
    }

    pDC->SelectObject(old);
    *out_height = yy - y + theApp.DPI(6);
}

// 按年归档回顾（REQ-120）：每行 “YYYY 年你听了 …”；无数据年份不列出
void CStatProfileTabDlg::DrawYearly(CDC* pDC, const CRect& rect, int y, int* out_height)
{
    const StatThemeColors& th = CStatTheme::Get();
    int pad = 12;

    if (m_yearly.empty())
    {
        CFont font;
        font.CreatePointFont(84, L"Microsoft YaHei", pDC);
        HFONT old = (HFONT)pDC->SelectObject(font.GetSafeHandle());
        pDC->SetTextColor(th.text_disabled);
        pDC->TextOutW(rect.left + pad + 10, y, L"无记录", 3);
        pDC->SelectObject(old);
        *out_height = 30 + theApp.DPI(22);
        return;
    }

    CFont font;
    font.CreatePointFont(84, L"Microsoft YaHei", pDC);
    HFONT old = (HFONT)pDC->SelectObject(font.GetSafeHandle());

    int yy = y;
    for (const auto& r : m_yearly)
    {
        std::wstring line = std::to_wstring(r.year) + L" 年你听了 " + std::to_wstring(r.count) +
            L" 首歌（" + CStatAnalysis::FormatDuration(r.duration_sec) + L"）";
        if (!r.top_artist.empty()) line += L"，最常听「" + r.top_artist + L"」";
        if (!r.top_genre.empty()) line += L"，偏爱「" + r.top_genre + L"」";
        line += L"。";
        pDC->SetTextColor(th.text_primary);
        pDC->TextOutW(rect.left + pad + 10, yy, line.c_str(), (int)line.size());
        yy += theApp.DPI(24);
    }
    pDC->SelectObject(old);
    *out_height = 30 + (int)m_yearly.size() * theApp.DPI(24);
}

// 整页绘制：头部大数字 → 音乐 DNA → 听歌档案徽章 → AI 洞察 → 深度数字 → 年度回顾
void CStatProfileTabDlg::DrawProfile(CDC* pDC, const CRect& rect)
{
    const StatThemeColors& th = CStatTheme::Get();
    int pad = 12;
    int card_gap = 8;
    int y = 8 - m_scroll_pos;

    // ── 头部：4 个大数字卡片（今日 / 本周 / 本月 / 累计） ──
    {
        int card_w = (rect.Width() - pad * 2 - theApp.DPI(24)) / 4;
        int card_h = theApp.DPI(70);
        int x0 = rect.left + pad;

        struct HeadCard { std::wstring label; std::wstring value; COLORREF color; };
        HeadCard cards[4] = {
            { L"今日",      std::to_wstring(m_summary.today_count) + L" 首",  th.series[0] },
            { L"本周",      std::to_wstring(m_summary.week_count) + L" 首",   th.series[1] },
            { L"本月",      std::to_wstring(m_summary.month_count) + L" 首",  th.series[2] },
            { L"累计时长",  CStatAnalysis::FormatDuration(m_summary.total_duration_sec), th.series[3] },
        };

        for (int i = 0; i < 4; i++)
        {
            CRect rc(x0 + i * (card_w + 8), y, x0 + i * (card_w + 8) + card_w, y + card_h);
            pDC->FillSolidRect(rc, th.card_back);
            pDC->FillSolidRect(CRect(rc.left, rc.top, rc.left + 4, rc.bottom), cards[i].color);

            CFont small_font;
            small_font.CreatePointFont(80, L"Microsoft YaHei", pDC);
            HFONT old = (HFONT)pDC->SelectObject(small_font.GetSafeHandle());
            pDC->SetTextColor(th.text_secondary);
            pDC->TextOutW(rc.left + 10, rc.top + 6, cards[i].label.c_str(), (int)cards[i].label.size());
            pDC->SelectObject(old);

            CFont big;
            big.CreatePointFont(120, L"Microsoft YaHei", pDC);
            old = (HFONT)pDC->SelectObject(big.GetSafeHandle());
            pDC->SetTextColor(th.text_primary);
            CSize sz = pDC->GetTextExtent(cards[i].value.c_str(), (int)cards[i].value.size());
            std::wstring val = cards[i].value;
            if (sz.cx > card_w - 20)
            {
                CFont mid;
                mid.CreatePointFont(95, L"Microsoft YaHei", pDC);
                pDC->SelectObject(mid.GetSafeHandle());
            }
            pDC->TextOutW(rc.left + 10, rc.top + 30, val.c_str(), (int)val.size());
            pDC->SelectObject(old);
        }
        y += card_h + card_gap + 6;
    }

    // ── 音乐 DNA ──
    DrawSectionTitle(pDC, rect, y, L"你的音乐 DNA");
    y += 30;
    int dna_h = 0;
    DrawDna(pDC, rect, y, &dna_h);
    y += dna_h + card_gap;

    // ── 听歌档案徽章 ──
    DrawSectionTitle(pDC, rect, y, L"你的听歌档案");
    y += 30;
    int badges_h = 0;
    DrawBadges(pDC, rect, y, &badges_h);
    y += badges_h + card_gap;

    // ── AI 洞察 ──
    DrawSectionTitle(pDC, rect, y, L"AI 洞察");
    y += 30;
    int insights_h = 0;
    DrawInsights(pDC, rect, y, &insights_h);
    y += insights_h + card_gap;

    // ── 深度数字网格：2 行指标 ──
    DrawSectionTitle(pDC, rect, y, L"深度数字");
    y += 30;
    {
        int cell_w = (rect.Width() - pad * 2 - theApp.DPI(24)) / 4;
        int cell_h = theApp.DPI(48);

        struct Metric { std::wstring label; std::wstring value; };
        Metric metrics[8] = {
            { L"日均播放",   std::to_wstring(m_summary.avg_plays_per_day) + L" 首" },
            { L"完整收听率", std::to_wstring((int)(m_summary.completed_rate + 0.5)) + L"%" },
            { L"连续听歌",   std::to_wstring(m_summary.current_streak) + L" 天" },
            { L"最长连续",   std::to_wstring(m_summary.longest_streak) + L" 天" },
            { L"深夜占比",   std::to_wstring(m_summary.night_owl_percent) + L"%" },
            { L"黄金时段",   m_summary.first_hour >= 0
                ? (std::wstring(m_summary.first_hour < 10 ? L"0" : L"") + std::to_wstring(m_summary.first_hour) +
                   L":00-" + (m_summary.last_hour < 10 ? L"0" : L"") + std::to_wstring(m_summary.last_hour) + L":00")
                : L"-" },
            { L"反复循环",   std::to_wstring(m_summary.repeat_depth) + L" 首" },
            { L"本月新歌",   std::to_wstring(m_summary.new_songs_month) + L" 首" },
        };

        for (int i = 0; i < 8; i++)
        {
            int row = i / 4, col = i % 4;
            int x = rect.left + pad + col * (cell_w + 8);
            int yy = y + row * (cell_h + 6);

            CRect rc(x, yy, x + cell_w, yy + cell_h);
            pDC->FillSolidRect(rc, th.card_back_alt);

            CFont small_font;
            small_font.CreatePointFont(78, L"Microsoft YaHei", pDC);
            HFONT old = (HFONT)pDC->SelectObject(small_font.GetSafeHandle());
            pDC->SetTextColor(th.text_secondary);
            pDC->TextOutW(rc.left + 8, rc.top + 5, metrics[i].label.c_str(), (int)metrics[i].label.size());
            pDC->SelectObject(old);

            CFont val_font;
            val_font.CreatePointFont(95, L"Microsoft YaHei", pDC);
            old = (HFONT)pDC->SelectObject(val_font.GetSafeHandle());
            pDC->SetTextColor(th.text_primary);
            std::wstring val = metrics[i].value;
            CSize sz = pDC->GetTextExtent(val.c_str(), (int)val.size());
            if (sz.cx > cell_w - 16)
            {
                while (val.size() > 2)
                {
                    val.resize(val.size() - 1);
                    sz = pDC->GetTextExtent(val.c_str(), (int)val.size());
                    if (sz.cx <= cell_w - 16) break;
                }
            }
            pDC->TextOutW(rc.left + 8, rc.top + 22, val.c_str(), (int)val.size());
            pDC->SelectObject(old);
        }
        y += 2 * (cell_h + 6) + card_gap;
    }

    // ── 年度回顾 ──
    DrawSectionTitle(pDC, rect, y, L"年度回顾");
    y += 30;
    int yearly_h = 0;
    DrawYearly(pDC, rect, y, &yearly_h);
    y += yearly_h + card_gap;
}

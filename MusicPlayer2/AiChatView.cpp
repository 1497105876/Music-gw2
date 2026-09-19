// AiChatView.cpp：「歌曲详细记录」第 7 页 —— AI 对话面板
//
// 这一页不用列表控件，整块面板自己画：
//   顶部条（模式切换 / 上下文说明 / 清空）→ 提示横幅 → 消息区 → 快捷提问 → 输入行
//
// 网络请求一律走工作线程（AiStartChatJob），结果用 PostMessage 回到本窗口，
// 用「代号 gen」作废迟到的结果 —— 窗口销毁时 gen 加一，在途结果就被安全丢掉。

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "AiChatView.h"
#include "StatAnalysis.h"
#include <algorithm>
#include <random>

// ───────────────────────── 输入框：回车发送 ─────────────────────────

BEGIN_MESSAGE_MAP(CAiChatInput, CEdit)
    ON_WM_KEYDOWN()
END_MESSAGE_MAP()

void CAiChatInput::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    // 回车直接发；想换行用 Ctrl+回车
    if (nChar == VK_RETURN && (::GetKeyState(VK_CONTROL) & 0x8000) == 0)
    {
        if (on_enter) on_enter();
        return;
    }
    CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
}

// ───────────────────────── 面板本体 ─────────────────────────

namespace
{
    // 多轮对话最多留几轮（一轮 = 一问一答）
    constexpr int kMaxHistoryRounds = 6;
    // Max 档最多带多少条原始记录出门
    constexpr int kMaxRawRows = 500;

    std::wstring Trim(const std::wstring& s)
    {
        size_t b = s.find_first_not_of(L" \t\r\n");
        if (b == std::wstring::npos) return std::wstring();
        size_t e = s.find_last_not_of(L" \t\r\n");
        return s.substr(b, e - b + 1);
    }
}

CAiChatView::CAiChatView()
{
}

CAiChatView::~CAiChatView()
{
    StopAll();
}

bool CAiChatView::CreatePanel(CWnd* parent, const CRect& rect)
{
    if (parent == nullptr || parent->GetSafeHwnd() == nullptr) return false;

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
        ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    if (cls == nullptr) return false;

    if (!Create(cls, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP,
        rect, parent, 0))
        return false;

    UpdatePalette();
    CreateChildCtrls();
    ReseedQuickQuestions();

    // 子控件都建好了，从这一刻起才允许布局/绘制（见 m_ready 的注释）
    m_ready = true;

    RecalcLayout();
    ScrollToBottom();
    return true;
}

void CAiChatView::CreateChildCtrls()
{
    CFont* p_font = &theApp.m_font_set.dlg.GetFont();

    m_mode_btn.Create(L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, kIdModeBtn);
    m_mode_btn.SetFont(p_font);

    m_clear_btn.Create(L"清空对话", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, kIdClearBtn);
    m_clear_btn.SetFont(p_font);

    m_send_btn.Create(L"发送", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, kIdSendBtn);
    m_send_btn.SetFont(p_font);

    m_retry_btn.Create(L"重试", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, kIdRetryBtn);
    m_retry_btn.SetFont(p_font);

    m_input.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        CRect(0, 0, 0, 0), this, kIdInput);
    m_input.SetFont(p_font);
    m_input.SetLimitText(2000);
    m_input.on_enter = [this]() { OnSendClick(); };

    UpdateModeButton();
}

void CAiChatView::UpdatePalette()
{
    const bool dark = theApp.m_app_setting_data.dark_mode;
    if (dark)
    {
        m_pal.bg = RGB(38, 38, 43);
        m_pal.top_bg = RGB(47, 47, 53);
        m_pal.bubble_me = RGB(58, 74, 102);
        m_pal.bubble_ai = RGB(51, 51, 58);
        m_pal.bubble_border = RGB(69, 69, 79);
        m_pal.text = RGB(232, 232, 238);
        m_pal.text_dim = RGB(154, 154, 166);
        m_pal.chip_bg = RGB(51, 51, 58);
        m_pal.chip_border = RGB(74, 74, 85);
        m_pal.chip_text = RGB(216, 216, 224);
        m_pal.warn_bg = RGB(74, 63, 34);
        m_pal.warn_border = RGB(106, 90, 48);
        m_pal.warn_text = RGB(240, 208, 144);
        m_pal.err_bg = RGB(74, 42, 42);
        m_pal.err_border = RGB(106, 58, 58);
        m_pal.err_text = RGB(240, 160, 160);
    }
    else
    {
        m_pal.bg = RGB(250, 250, 252);
        m_pal.top_bg = RGB(242, 243, 247);
        m_pal.bubble_me = RGB(227, 240, 255);
        m_pal.bubble_ai = RGB(255, 255, 255);
        m_pal.bubble_border = RGB(226, 229, 236);
        m_pal.text = RGB(26, 26, 26);
        m_pal.text_dim = RGB(138, 143, 154);
        m_pal.chip_bg = RGB(255, 255, 255);
        m_pal.chip_border = RGB(216, 221, 232);
        m_pal.chip_text = RGB(58, 65, 80);
        m_pal.warn_bg = RGB(255, 247, 230);
        m_pal.warn_border = RGB(240, 217, 168);
        m_pal.warn_text = RGB(138, 90, 0);
        m_pal.err_bg = RGB(253, 236, 236);
        m_pal.err_border = RGB(243, 196, 196);
        m_pal.err_text = RGB(163, 32, 32);
    }
    if (m_edit_brush.m_hObject != nullptr) m_edit_brush.DeleteObject();
    m_edit_brush.CreateSolidBrush(m_pal.bg);
}

// ───────────────────────── 布局 ─────────────────────────

// 三条横带的高度都压得比较紧：这块面板实际只有主列表那么大
// （rc 里是 326x138 DLU，约 489x276 像素），固定带多占一点，气泡就少看一行。
int CAiChatView::TopBarHeight() const { return theApp.DPI(30); }
int CAiChatView::InputHeight() const { return theApp.DPI(48); }
int CAiChatView::BannerHeight() const
{
    return (m_banner_kind == BannerKind::None) ? 0 : theApp.DPI(28);
}

void CAiChatView::RecalcLayout()
{
    if (!m_ready || !::IsWindow(m_hWnd)) return;
    CRect rc;
    GetClientRect(rc);
    if (rc.Width() <= 0 || rc.Height() <= 0) return;

    const int pad = theApp.DPI(6);

    // ① 顶部条
    int y = 0;
    m_top_rect = CRect(0, 0, rc.Width(), TopBarHeight());
    y = m_top_rect.bottom;

    // ② 提示横幅（没有就不占地方）
    const int ban_h = BannerHeight();
    m_banner_rect = CRect(0, y, rc.Width(), y + ban_h);
    y = m_banner_rect.bottom;

    // ③ 输入区贴着底边先定下来
    m_input_rect = CRect(0, rc.Height() - InputHeight(), rc.Width(), rc.Height());
    if (m_input_rect.top < y) m_input_rect.top = y;      // 窗口实在太矮，别压到横幅上

    // ④ 消息区吃剩下的全部
    m_msg_rect = CRect(0, y, rc.Width(), m_input_rect.top);
    if (m_msg_rect.bottom < m_msg_rect.top) m_msg_rect.bottom = m_msg_rect.top;

    // 顶部条里的两个按钮
    int mode_w = theApp.DPI(100);
    int btn_h = theApp.DPI(22);
    int btn_y = m_top_rect.top + (m_top_rect.Height() - btn_h) / 2;
    m_mode_btn.MoveWindow(pad, btn_y, mode_w, btn_h);
    int clear_w = theApp.DPI(60);
    m_clear_btn.MoveWindow(m_top_rect.right - pad - clear_w, btn_y, clear_w, btn_h);

    // 横幅右侧的「重试」
    if (ban_h > 0)
    {
        int rb_w = theApp.DPI(50);
        int rb_h = theApp.DPI(20);
        m_retry_btn.MoveWindow(m_banner_rect.right - pad - rb_w,
            m_banner_rect.top + (ban_h - rb_h) / 2, rb_w, rb_h);
    }

    // 输入行：输入框吃满剩余宽度，发送按钮贴右边
    int send_w = theApp.DPI(60);
    int send_h = theApp.DPI(24);
    int inp_h = InputHeight() - theApp.DPI(12);
    if (inp_h < theApp.DPI(28)) inp_h = theApp.DPI(28);
    int inp_y = m_input_rect.top + (InputHeight() - inp_h) / 2;
    int inp_w = m_input_rect.Width() - pad * 2 - send_w - pad;
    if (inp_w < theApp.DPI(80)) inp_w = theApp.DPI(80);
    m_input.MoveWindow(pad, inp_y, inp_w, inp_h);
    m_send_btn.MoveWindow(m_input_rect.right - pad - send_w,
        inp_y + (inp_h - send_h) / 2, send_w, send_h);

    RelayoutBubbles();      // 里面会顺带把空态位置也算好
    ScrollToBottom();
}

std::wstring CAiChatView::EmptyHintText() const
{
    if (AiConfig::Get().chat_mode == AiChatMode::Local)
        return L"现在这套是「本地」档，什么都不往外发，答案由本机自己算。";
    return L"发出去的内容只有统计数字 —— 文件路径、歌词、封面都不会离开这台电脑。";
}

// 空态整块（标题 / 说明 / 快捷提问）在消息区里居中摆放。
// chips 必须按顺序填进 m_chip_rects，点击时靠下标去 m_chips 取原文。
void CAiChatView::LayoutEmptyState()
{
    m_chip_rects.clear();
    m_empty_title_rect.SetRectEmpty();
    m_empty_text_rect.SetRectEmpty();

    if (!m_ready || !::IsWindow(m_hWnd)) return;
    if (!m_messages.empty()) return;        // 聊过天了就不再显示空态
    if (m_msg_rect.Width() < theApp.DPI(120) || m_msg_rect.Height() < theApp.DPI(48)) return;

    CClientDC dc(this);
    CFont* p_old = dc.SelectObject(&theApp.m_font_set.dlg.GetFont());

    const int side = theApp.DPI(14);
    const int text_w = (std::max)(m_msg_rect.Width() - side * 2, theApp.DPI(80));
    const std::wstring title = L"想问点什么？";
    const std::wstring hint = EmptyHintText();
    const int title_h = MeasureTextHeight(dc, title, text_w);
    const int hint_h = MeasureTextHeight(dc, hint, text_w);

    const int chip_h = theApp.DPI(24);
    const int gap_x = theApp.DPI(7);
    const int gap_y = theApp.DPI(7);
    const int inner = theApp.DPI(11);

    std::vector<int> widths;
    widths.reserve(m_chips.size());
    for (const auto& q : m_chips)
    {
        CSize sz = dc.GetTextExtent(q.c_str(), static_cast<int>(q.size()));
        int w = sz.cx + inner * 2;
        if (w < theApp.DPI(56)) w = theApp.DPI(56);
        if (w > text_w) w = text_w;
        widths.push_back(w);
    }

    // 按可用宽度分行
    std::vector<std::vector<size_t>> rows;
    std::vector<size_t> cur;
    int cur_w = 0;
    for (size_t i = 0; i < widths.size(); i++)
    {
        if (!cur.empty() && cur_w + gap_x + widths[i] > text_w)
        {
            rows.push_back(cur);        // 这一行放不下了，换个行
            cur.clear();
            cur_w = 0;
        }
        cur_w += (cur.empty() ? 0 : gap_x) + widths[i];
        cur.push_back(i);
    }
    if (!cur.empty()) rows.push_back(cur);

    const int chips_h = rows.empty() ? 0
        : (static_cast<int>(rows.size()) * chip_h + (static_cast<int>(rows.size()) - 1) * gap_y);

    const int gap_title_hint = theApp.DPI(6);
    const int gap_hint_chips = (chips_h > 0) ? theApp.DPI(12) : 0;
    const int block_h = title_h + gap_title_hint + hint_h + gap_hint_chips + chips_h;

    int top = m_msg_rect.top + (m_msg_rect.Height() - block_h) / 2;
    if (top < m_msg_rect.top + theApp.DPI(6))
        top = m_msg_rect.top + theApp.DPI(6);

    m_empty_title_rect = CRect(m_msg_rect.left + side, top, m_msg_rect.right - side, top + title_h);
    int y = m_empty_title_rect.bottom + gap_title_hint;
    m_empty_text_rect = CRect(m_msg_rect.left + side, y, m_msg_rect.right - side, y + hint_h);
    y = m_empty_text_rect.bottom + gap_hint_chips;

    for (const auto& row : rows)
    {
        int row_w = 0;
        for (size_t k = 0; k < row.size(); k++)
            row_w += widths[row[k]] + (k == 0 ? 0 : gap_x);
        int x = m_msg_rect.left + (m_msg_rect.Width() - row_w) / 2;
        for (size_t k = 0; k < row.size(); k++)
        {
            m_chip_rects.push_back(CRect(x, y, x + widths[row[k]], y + chip_h));
            x += widths[row[k]] + gap_x;
        }
        y += chip_h + gap_y;
    }

    if (p_old != nullptr) dc.SelectObject(p_old);
}

void CAiChatView::RelayoutBubbles()
{
    if (!m_ready || !::IsWindow(m_hWnd) || m_msg_rect.Width() <= 0) return;

    CClientDC dc(this);
    CFont* p_old = dc.SelectObject(&theApp.m_font_set.dlg.GetFont());

    const int pad_x = theApp.DPI(12);   // 气泡内边距（左右）
    const int pad_y = theApp.DPI(8);    // 气泡内边距（上下）
    const int gap = theApp.DPI(12);     // 气泡间距
    const int margin = theApp.DPI(12);  // 气泡离左右边
    int max_w = m_msg_rect.Width() - margin * 2 - theApp.DPI(14);
    if (max_w > static_cast<int>(m_msg_rect.Width() * 0.84))
        max_w = static_cast<int>(m_msg_rect.Width() * 0.84);    // 同原型的 max-width: 84%
    if (max_w < theApp.DPI(100)) max_w = theApp.DPI(100);

    int y = pad_y;
    for (auto& b : m_messages)
    {
        if (b.thinking)
        {
            b.width = theApp.DPI(64);
            b.height = pad_y * 2 + theApp.DPI(14);
        }
        else
        {
            std::wstring src_line = b.source.empty() ? std::wstring() : (L"依据：" + b.source);
            int full = MeasureTextWidth(dc, b.text);
            if (!src_line.empty())
                full = (std::max)(full, MeasureTextWidth(dc, src_line));

            int want = full + pad_x * 2;
            if (want > max_w) want = max_w;
            if (want < theApp.DPI(40)) want = theApp.DPI(40);
            b.width = want;

            int text_w = want - pad_x * 2;
            if (text_w < theApp.DPI(20)) text_w = theApp.DPI(20);
            int h = MeasureTextHeight(dc, b.text, text_w);
            if (!src_line.empty())
                h += theApp.DPI(6) + theApp.DPI(6) + MeasureTextHeight(dc, src_line, text_w);   // 分隔线上下各留 6
            b.height = h + pad_y * 2;
        }
        b.top = y;
        y += b.height + gap;
    }
    m_content_h = y - gap + pad_y;      // 最后一条下面不留间距，只留一点底部内边距
    if (m_content_h < 0) m_content_h = 0;

    if (p_old != nullptr) dc.SelectObject(p_old);

    // 空态的那些位置跟着消息数量走：没消息才摆，有消息就清掉。
    // 放这儿是为了让所有「动了 m_messages」的地方都不用额外记得调一次。
    LayoutEmptyState();
}

int CAiChatView::MeasureTextHeight(CDC& dc, const std::wstring& text, int width)
{
    if (text.empty()) return 0;
    CRect rc(0, 0, width, 0);
    dc.DrawText(text.c_str(), static_cast<int>(text.size()), &rc,
        DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS | DT_CALCRECT);
    return rc.Height();
}

int CAiChatView::MeasureTextWidth(CDC& dc, const std::wstring& text)
{
    if (text.empty()) return 0;
    CRect rc(0, 0, 100000, 0);
    dc.DrawText(text.c_str(), static_cast<int>(text.size()), &rc,
        DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS | DT_CALCRECT | DT_SINGLELINE);
    return rc.Width();
}

// ───────────────────────── 绘制 ─────────────────────────

void CAiChatView::DrawAll(CDC& dc, const CRect& client)
{
    // 背景
    CBrush bg_brush(m_pal.bg);
    dc.FillRect(client, &bg_brush);

    DrawTopBar(dc);
    if (m_banner_kind != BannerKind::None) DrawBanner(dc);
    if (m_messages.empty())
        DrawEmptyHint(dc);      // 空态自己把快捷提问一起画了
    else
        DrawMessages(dc);
    DrawScrollbar(dc);
}

void CAiChatView::DrawTopBar(CDC& dc)
{
    CBrush br(m_pal.top_bg);
    dc.FillRect(m_top_rect, &br);

    // 底部一条分隔线，让顶部条和消息区有层次
    CPen line_pen(PS_SOLID, 1, m_pal.bubble_border);
    CPen* p_old_pen = dc.SelectObject(&line_pen);
    dc.MoveTo(m_top_rect.left, m_top_rect.bottom - 1);
    dc.LineTo(m_top_rect.right, m_top_rect.bottom - 1);
    dc.SelectObject(p_old_pen);

    // 中间那行说明：模式按钮右边、清空按钮左边
    CRect r = m_top_rect;
    r.left += theApp.DPI(104) + theApp.DPI(12);
    r.right -= theApp.DPI(64) + theApp.DPI(12);
    if (r.Width() <= 0) return;

    std::wstring text = L"上下文：全部记录";
    if (m_all_count > 0)
        text += L" · 共 " + std::to_wstring(m_all_count) + L" 条";
    switch (AiConfig::Get().chat_mode)
    {
    case AiChatMode::Local: text += L" · 本次不联网"; break;
    case AiChatMode::Model: text += L" · 本次发送聚合值"; break;
    default:                text += L" · 本次发送全部原始记录"; break;
    }

    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(m_pal.text_dim);
    dc.DrawText(text.c_str(), static_cast<int>(text.size()), &r,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void CAiChatView::DrawBanner(CDC& dc)
{
    COLORREF bg = m_pal.warn_bg, border = m_pal.warn_border, fg = m_pal.warn_text;
    if (m_banner_kind == BannerKind::Error) { bg = m_pal.err_bg; border = m_pal.err_border; fg = m_pal.err_text; }

    CBrush br(bg);
    dc.FillRect(m_banner_rect, &br);
    CPen pen(PS_SOLID, 1, border);
    CPen* p_old = dc.SelectObject(&pen);
    dc.MoveTo(m_banner_rect.left, m_banner_rect.bottom - 1);
    dc.LineTo(m_banner_rect.right, m_banner_rect.bottom - 1);
    dc.SelectObject(p_old);

    CRect r = m_banner_rect;
    r.left += theApp.DPI(10);
    r.right -= theApp.DPI(10);
    if (m_banner_kind == BannerKind::Error) r.right -= theApp.DPI(56);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(fg);
    dc.DrawText(m_banner_text.c_str(), static_cast<int>(m_banner_text.size()), &r,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void CAiChatView::DrawMessages(CDC& dc)
{
    if (m_messages.empty()) return;

    // 只画消息区，滚出去的不画
    CRect clip = m_msg_rect;
    CRgn rgn;
    rgn.CreateRectRgnIndirect(clip);
    dc.SelectClipRgn(&rgn);

    for (const auto& b : m_messages)
    {
        int top = m_msg_rect.top + b.top - m_scroll_pos;
        int bottom = top + b.height;
        if (bottom < m_msg_rect.top || top > m_msg_rect.bottom) continue;
        Bubble bb = b;
        bb.top = top;
        DrawOneBubble(dc, bb);
    }

    dc.SelectClipRgn(nullptr);
}

void CAiChatView::DrawOneBubble(CDC& dc, const Bubble& b)
{
    const int margin = theApp.DPI(12);
    const int pad_x = theApp.DPI(12);
    const int pad_y = theApp.DPI(8);
    int x = b.mine ? (m_msg_rect.right - margin - b.width) : (m_msg_rect.left + margin);
    CRect rc(x, b.top, x + b.width, b.top + b.height);
    if (rc.right > m_msg_rect.right) { rc.right = m_msg_rect.right; rc.left = rc.right - b.width; }

    CBrush br(b.mine ? m_pal.bubble_me : m_pal.bubble_ai);
    CPen pen(PS_SOLID, 1, m_pal.bubble_border);
    CBrush* p_old_br = dc.SelectObject(&br);
    CPen* p_old_pen = dc.SelectObject(&pen);
    POINT round{ theApp.DPI(9), theApp.DPI(9) };
    dc.RoundRect(rc, round);
    dc.SelectObject(p_old_br);
    dc.SelectObject(p_old_pen);
    dc.SetBkMode(TRANSPARENT);

    CRect tr = rc;
    tr.DeflateRect(pad_x, pad_y);
    if (tr.Width() < theApp.DPI(16)) tr.right = tr.left + theApp.DPI(16);

    if (b.thinking)
    {
        if (tr.Height() <= 0) return;
        // 三个跳动的点（原型是 6px 圆点、间隔 4px）
        const int r = theApp.DPI(3);
        const int gap = theApp.DPI(4);
        const int dot_w = r * 2 + gap;
        int cx = tr.left + r + (tr.Width() - dot_w * 3 + gap) / 2;
        if (cx < tr.left + r) cx = tr.left + r;
        int cy = tr.top + tr.Height() / 2;
        for (int i = 0; i < 3; i++)
        {
            int level = (m_think_phase + i) % 3;
            COLORREF c = (level == 0) ? m_pal.text
                : (level == 1 ? m_pal.text_dim : m_pal.bubble_border);
            CBrush dot(c);
            CBrush* p_old = dc.SelectObject(&dot);
            dc.Ellipse(cx - r, cy - r, cx + r, cy + r);
            dc.SelectObject(p_old);
            cx += dot_w;
        }
        return;
    }

    dc.SetTextColor(m_pal.text);
    int used = MeasureTextHeight(dc, b.text, tr.Width());
    dc.DrawText(b.text.c_str(), static_cast<int>(b.text.size()), &tr,
        DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);

    if (!b.source.empty())
    {
        const int line_y = tr.top + used + theApp.DPI(6);
        // 跟原型一样，「依据」上面拉一条细线把它跟正文分开
        if (line_y < rc.bottom - pad_y)
        {
            CPen sep(PS_SOLID, 1, m_pal.bubble_border);
            CPen* p_old_sep = dc.SelectObject(&sep);
            dc.MoveTo(tr.left, line_y);
            dc.LineTo(tr.right, line_y);
            dc.SelectObject(p_old_sep);
        }
        CRect sr = tr;
        sr.top = line_y + (line_y < rc.bottom - pad_y ? theApp.DPI(6) : theApp.DPI(2));
        sr.bottom = rc.bottom - pad_y;
        dc.SetTextColor(m_pal.text_dim);
        std::wstring src_line = L"依据：" + b.source;
        dc.DrawText(src_line.c_str(), static_cast<int>(src_line.size()), &sr,
            DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
    }
}

void CAiChatView::DrawScrollbar(CDC& dc)
{
    const int view_h = m_msg_rect.Height();
    m_scroll_track.SetRectEmpty();
    m_scroll_thumb.SetRectEmpty();
    if (view_h <= 0 || m_content_h <= view_h) return;

    const int w = theApp.DPI(6);
    CRect track(m_msg_rect.right - w - theApp.DPI(4), m_msg_rect.top + theApp.DPI(4),
        m_msg_rect.right - theApp.DPI(4), m_msg_rect.bottom - theApp.DPI(4));
    m_scroll_track = track;

    int track_h = track.Height();
    int thumb_h = track_h * view_h / m_content_h;
    if (thumb_h < theApp.DPI(24)) thumb_h = theApp.DPI(24);
    int max_pos = m_content_h - view_h;
    int y = track.top + (track_h - thumb_h) * m_scroll_pos / (max_pos > 0 ? max_pos : 1);
    m_scroll_thumb = CRect(track.left, y, track.right, y + thumb_h);

    CBrush br(m_pal.bubble_border);
    dc.FillRect(track, &br);
    CBrush tb(m_pal.text_dim);
    dc.FillRect(m_scroll_thumb, &tb);
}

void CAiChatView::DrawQuickChips(CDC& dc)
{
    if (m_chip_rects.size() != m_chips.size()) return;
    CFont* p_old = dc.SelectObject(&theApp.m_font_set.dlg.GetFont());
    dc.SetBkMode(TRANSPARENT);

    for (size_t i = 0; i < m_chip_rects.size(); i++)
    {
        bool hover = (static_cast<int>(i) == m_hover_chip);
        CBrush br(hover ? m_pal.bubble_me : m_pal.chip_bg);
        CPen pen(PS_SOLID, 1, m_pal.chip_border);
        CBrush* p_old_br = dc.SelectObject(&br);
        CPen* p_old_pen = dc.SelectObject(&pen);
        POINT round{ theApp.DPI(12), theApp.DPI(12) };      // 高 24，圆角给一半就是胶囊形
        dc.RoundRect(m_chip_rects[i], round);
        dc.SelectObject(p_old_br);
        dc.SelectObject(p_old_pen);

        dc.SetTextColor(m_pal.chip_text);
        dc.DrawText(m_chips[i].c_str(), static_cast<int>(m_chips[i].size()), &m_chip_rects[i],
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (p_old != nullptr) dc.SelectObject(p_old);
}

void CAiChatView::DrawEmptyHint(CDC& dc)
{
    if (m_busy) return;
    if (m_msg_rect.Width() <= 0 || m_msg_rect.Height() <= 0) return;

    // 空态只画在消息区里，别溢出到顶栏/输入区上
    CRgn rgn;
    rgn.CreateRectRgnIndirect(m_msg_rect);
    dc.SelectClipRgn(&rgn);
    dc.SetBkMode(TRANSPARENT);

    if (!m_empty_title_rect.IsRectEmpty())
    {
        dc.SetTextColor(m_pal.text);
        CRect r = m_empty_title_rect;
        dc.DrawText(L"想问点什么？", -1, &r,
            DT_SINGLELINE | DT_NOPREFIX | DT_CENTER | DT_END_ELLIPSIS);
    }
    if (!m_empty_text_rect.IsRectEmpty())
    {
        dc.SetTextColor(m_pal.text_dim);
        CRect r = m_empty_text_rect;
        const std::wstring t = EmptyHintText();
        dc.DrawText(t.c_str(), static_cast<int>(t.size()), &r,
            DT_WORDBREAK | DT_NOPREFIX | DT_CENTER);
    }
    DrawQuickChips(dc);

    dc.SelectClipRgn(nullptr);
}

// ───────────────────────── 收发 ─────────────────────────

std::wstring CAiChatView::LanguageInstruction() const
{
    switch (AiConfig::Get().prompt.language)
    {
    case AiAnswerLanguage::Chinese: return L"请用简体中文回答。";
    case AiAnswerLanguage::English: return L"Please answer in English.";
    default:                        return L"请用与界面一致的语言回答。";
    }
}

std::wstring CAiChatView::BuildContextText(AiStatSnapshot& snap) const
{
    const bool allow_meta = AiConfig::Get().privacy.allow_song_meta;
    std::wstring t = AiStatContext::BuildSummaryText(snap, allow_meta);
    if (AiConfig::Get().chat_mode == AiChatMode::Max)
        t += AiStatContext::BuildRawRecordsText(snap, kMaxRawRows, allow_meta);

    std::wstring head = L"下面是我的听歌数据汇总。\n\n";
    std::wstring tail = L"\n\n只准使用上面给出的数据作答；数据里没有的信息不要编造。"
        L"不要提到文件路径。\n\n我的问题：";
    return head + t + tail;
}

void CAiChatView::OnSendClick()
{
    CString s;
    m_input.GetWindowText(s);
    std::wstring q = Trim(s.GetString());
    if (q.empty() || m_busy) return;
    DoSend(q, true);
}

void CAiChatView::DoSend(const std::wstring& question, bool push_user_bubble)
{
    if (m_busy) return;
    m_last_question = question;
    HideBanner();

    if (m_all_count <= 0)
    {
        ShowBanner(BannerKind::Info, L"还没有播放记录。先去听几首歌，再回来问我。");
        return;
    }

    AiStatSnapshot snap = m_snapshot_fn ? m_snapshot_fn() : AiStatSnapshot{};
    if (!snap.Valid())
    {
        ShowBanner(BannerKind::Info, L"统计数据还没算出来，等一下再试。");
        return;
    }

    if (!AiConfig::Get().enabled)
    {
        ShowBanner(BannerKind::Warn, L"AI 功能还没打开。到「选项设置 → AI 设置」里启用它，并添加一套模型。");
        return;
    }

    // 本地档不需要模型，规则引擎自己就能答
    const AiChatMode mode = AiConfig::Get().chat_mode;
    const AiModelConfig* model = CurrentModelOrNull();
    if (mode != AiChatMode::Local && model == nullptr)
    {
        ShowBanner(BannerKind::Warn, L"还没配可用的模型。到「选项设置 → AI 设置」里添加一套，再双击它设为「当前使用」。");
        return;
    }

    if (push_user_bubble) PushUser(question);

    if (mode == AiChatMode::Local)
    {
        const bool allow_meta = AiConfig::Get().privacy.allow_song_meta;
        std::wstring answer = AiStatContext::BuildLocalAnswer(snap, question, allow_meta);
        std::wstring source = AiStatContext::BuildSourceText(snap, question);
        PushAi(answer, source);
        return;
    }

    AiCallParams params = AiCallParams::FromModel(*model, AiConfig::Get().request);
    std::wstring why;
    if (!params.Valid(why))
    {
        ShowBanner(BannerKind::Warn, L"这套模型还差东西：" + why);
        return;
    }

    std::vector<AiChatMessage> msgs;
    AiChatMessage sys;
    sys.role = L"system";
    std::wstring sys_text = AiConfig::Get().prompt.system;
    if (sys_text.empty()) sys_text = AiConfig::DefaultSystemPrompt();
    sys.content = sys_text + L"\n" + LanguageInstruction();
    msgs.push_back(sys);

    // 多轮历史：只带问题和对答，不带那一大坨数据，省 token
    for (const auto& h : m_history) msgs.push_back(h);

    AiChatMessage user;
    user.role = L"user";
    user.content = BuildContextText(snap) + question;
    msgs.push_back(user);

    std::wstring source = AiStatContext::BuildSourceText(snap, question);

    m_stream_text.clear();
    m_cancel = false;
    SetBusy(true);
    PushThinking();
    AiStartChatJob(m_hWnd, m_gen, params, msgs, source);
}

void CAiChatView::PushUser(const std::wstring& text)
{
    Bubble b;
    b.mine = true;
    b.text = text;
    m_messages.push_back(b);
    RelayoutBubbles();
    ScrollToBottom();
    Invalidate();
    AppendHistoryFile(L"我", text);
}

void CAiChatView::PushThinking()
{
    Bubble b;
    b.thinking = true;
    m_messages.push_back(b);
    RelayoutBubbles();
    ScrollToBottom();
    Invalidate();
}

void CAiChatView::PopThinking()
{
    if (!m_messages.empty() && m_messages.back().thinking)
    {
        m_messages.pop_back();
        RelayoutBubbles();
    }
}

void CAiChatView::PushAi(const std::wstring& text, const std::wstring& source)
{
    PopThinking();
    Bubble b;
    b.mine = false;
    b.text = text;
    b.source = source;
    m_messages.push_back(b);

    // 记一轮历史：上一句是用户的提问
    if (!m_last_question.empty())
    {
        AiChatMessage q; q.role = L"user"; q.content = m_last_question;
        AiChatMessage a; a.role = L"assistant"; a.content = text;
        m_history.push_back(q);
        m_history.push_back(a);
        TrimHistory();
    }

    RelayoutBubbles();
    ScrollToBottom();
    Invalidate();
    AppendHistoryFile(L"AI", text);
}

void CAiChatView::TrimHistory()
{
    const size_t max_n = static_cast<size_t>(kMaxHistoryRounds) * 2;
    if (m_history.size() > max_n)
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - max_n));
}

void CAiChatView::AppendHistoryFile(const std::wstring& role, const std::wstring& text)
{
    if (!AiConfig::Get().privacy.save_chat_history) return;
    const std::wstring dir = AiConfig::Get().ChatHistoryDir();
    if (dir.empty()) return;
    CCommon::CreateDir(dir);

    CTime now = CTime::GetCurrentTime();
    std::wstring path = dir;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L"\\";
    path += L"对话_" + std::wstring(now.Format(L"%Y%m%d").GetString()) + L".txt";

    try
    {
        CStdioFile f;
        BOOL exists = (::GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES);
        CFileException ex;
        UINT flags = CFile::modeWrite | CFile::typeUnicode | (exists ? CFile::modeNoTruncate : CFile::modeCreate);
        if (!f.Open(path.c_str(), flags, &ex)) return;
        f.SeekToEnd();
        std::wstring line = L"[" + std::wstring(now.Format(L"%H:%M:%S").GetString()) + L"] " + role + L"：" + text + L"\n";
        f.WriteString(line.c_str());
        f.Close();
    }
    catch (CFileException*)
    {
        // 存不下就算了，不能因为写日志把对话搞崩
    }
}

// ───────────────────────── 状态 ─────────────────────────

void CAiChatView::ShowBanner(BannerKind kind, const std::wstring& text)
{
    m_banner_kind = kind;
    m_banner_text = text;
    RecalcLayout();
    Invalidate();
    if (::IsWindow(m_retry_btn.m_hWnd))
        m_retry_btn.ShowWindow(kind == BannerKind::Error ? SW_SHOW : SW_HIDE);
}

void CAiChatView::HideBanner()
{
    if (m_banner_kind == BannerKind::None) return;
    m_banner_kind = BannerKind::None;
    m_banner_text.clear();
    if (::IsWindow(m_retry_btn.m_hWnd)) m_retry_btn.ShowWindow(SW_HIDE);
    RecalcLayout();
    Invalidate();
}

void CAiChatView::SetBusy(bool busy)
{
    m_busy = busy;
    if (busy)
    {
        SetTimer(kTimerThink, 350, nullptr);
        m_think_phase = 0;
    }
    else
    {
        KillTimer(kTimerThink);
    }
    if (::IsWindow(m_input.m_hWnd)) m_input.EnableWindow(!busy);
    if (::IsWindow(m_send_btn.m_hWnd)) m_send_btn.EnableWindow(!busy);
}

void CAiChatView::UpdateModeButton()
{
    if (!::IsWindow(m_mode_btn.m_hWnd)) return;
    std::wstring t = L"模式：";
    switch (AiConfig::Get().chat_mode)
    {
    case AiChatMode::Local: t += L"本地"; break;
    case AiChatMode::Model: t += L"模型"; break;
    default:                t += L"Max";  break;
    }
    m_mode_btn.SetWindowText(t.c_str());
}

void CAiChatView::ReseedQuickQuestions()
{
    const std::vector<std::wstring>& pool = AiStatContext::QuickQuestionPool();
    m_chips.clear();
    if (pool.empty()) return;

    std::vector<size_t> idx(pool.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(idx.begin(), idx.end(), rng);

    size_t n = (std::min)(static_cast<size_t>(4), idx.size());
    for (size_t i = 0; i < n; i++) m_chips.push_back(pool[idx[i]]);
}

void CAiChatView::ScrollToBottom()
{
    int view_h = m_msg_rect.Height();
    m_scroll_pos = m_content_h - view_h;
    if (m_scroll_pos < 0) m_scroll_pos = 0;
}

bool CAiChatView::HasRecords() const { return m_all_count > 0; }

const AiModelConfig* CAiChatView::CurrentModelOrNull() const
{
    if (!AiConfig::Get().enabled) return nullptr;
    const AiModelConfig* m = AiConfig::Get().CurrentModel();
    if (m == nullptr) return nullptr;
    if (m->base_url.empty() || m->model.empty() || m->api_key.empty()) return nullptr;
    return m;
}

void CAiChatView::OnModeClick()
{
    if (m_busy) return;
    AiChatMode m = AiConfig::Get().chat_mode;
    m = (m == AiChatMode::Local) ? AiChatMode::Model
        : (m == AiChatMode::Model ? AiChatMode::Max : AiChatMode::Local);
    AiConfig::Get().chat_mode = m;      // 记住上次档位，下次进来还是它
    UpdateModeButton();
    HideBanner();
    Invalidate();
}

void CAiChatView::OnClearClick()
{
    if (m_busy) return;
    if (MessageBox(L"确定清空当前对话？", L"AI 对话", MB_ICONQUESTION | MB_YESNO) != IDYES)
        return;
    m_messages.clear();
    m_history.clear();
    m_stream_text.clear();
    m_scroll_pos = 0;
    HideBanner();
    ReseedQuickQuestions();
    RelayoutBubbles();
    Invalidate();
}

void CAiChatView::OnRetryClick()
{
    if (m_busy) return;
    HideBanner();
    if (m_last_question.empty()) return;
    DoSend(m_last_question, false);
}

void CAiChatView::OnDataChanged(int all_count)
{
    m_all_count = all_count;
    InvalidateRect(m_top_rect, FALSE);
}

void CAiChatView::OnPageActivated()
{
    UpdatePalette();
    UpdateModeButton();
    RecalcLayout();

    // 每次进来（还没聊过的时候）重新抽一批快捷提问，别老是那四条
    if (m_messages.empty())
    {
        ReseedQuickQuestions();
        LayoutEmptyState();
        InvalidateRect(m_msg_rect, FALSE);
    }

    if (m_banner_kind != BannerKind::None) return;
    if (m_all_count <= 0)
    {
        ShowBanner(BannerKind::Info, L"还没有播放记录。先去听几首歌，再来问我。");
    }
    else if (!AiConfig::Get().enabled || CurrentModelOrNull() == nullptr)
    {
        ShowBanner(BannerKind::Warn, L"还没配好模型。到「选项设置 → AI 设置」里启用并添加一套 —— 现在可以先切到「本地」档直接用。");
    }
}

void CAiChatView::StopAll()
{
    m_gen++;
    m_cancel = true;
}

// ───────────────────────── 消息 ─────────────────────────

BEGIN_MESSAGE_MAP(CAiChatView, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_MOUSEWHEEL()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_TIMER()
    ON_WM_CTLCOLOR()
    ON_WM_DESTROY()
    ON_BN_CLICKED(kIdModeBtn, &CAiChatView::OnModeClick)
    ON_BN_CLICKED(kIdClearBtn, &CAiChatView::OnClearClick)
    ON_BN_CLICKED(kIdSendBtn, &CAiChatView::OnSendClick)
    ON_BN_CLICKED(kIdRetryBtn, &CAiChatView::OnRetryClick)
    ON_MESSAGE(WM_AI_CHAT_DELTA, &CAiChatView::OnChatDelta)
    ON_MESSAGE(WM_AI_CHAT_DONE, &CAiChatView::OnChatDone)
END_MESSAGE_MAP()

void CAiChatView::OnPaint()
{
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(rc);
    if (!m_ready || rc.Width() <= 0 || rc.Height() <= 0) return;

    // 双缓冲：滚动/流式追加时才不会闪
    CDC mem;
    mem.CreateCompatibleDC(&dc);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* p_old = mem.SelectObject(&bmp);
    CFont* p_old_font = mem.SelectObject(&theApp.m_font_set.dlg.GetFont());

    DrawAll(mem, rc);

    mem.SelectObject(p_old_font);
    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(p_old);
}

BOOL CAiChatView::OnEraseBkgnd(CDC*)
{
    return TRUE;    // 背景自己画，别让系统擦
}

void CAiChatView::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    if (!m_ready) return;       // 建窗过程中的那次 WM_SIZE 直接跳过
    RecalcLayout();
    Invalidate();
}

void CAiChatView::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kTimerThink)
    {
        m_think_phase++;
        InvalidateRect(m_msg_rect, FALSE);
        return;
    }
    CWnd::OnTimer(nIDEvent);
}

BOOL CAiChatView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    const int view_h = m_msg_rect.Height();
    const int max_pos = m_content_h - view_h;
    if (max_pos > 0)
    {
        int step = theApp.DPI(48) * (zDelta > 0 ? -1 : 1);
        m_scroll_pos += step;
        if (m_scroll_pos < 0) m_scroll_pos = 0;
        if (m_scroll_pos > max_pos) m_scroll_pos = max_pos;
        InvalidateRect(m_msg_rect, FALSE);
    }
    return CWnd::OnMouseWheel(nFlags, zDelta, pt);
}

void CAiChatView::OnLButtonDown(UINT nFlags, CPoint point)
{
    // 快捷提问：点一下填进输入框
    for (size_t i = 0; i < m_chip_rects.size() && i < m_chips.size(); i++)
    {
        if (m_chip_rects[i].PtInRect(point))
        {
            m_input.SetWindowText(m_chips[i].c_str());
            m_input.SetFocus();
            return;
        }
    }

    // 自绘滚动条：点轨道翻页，按住滑块可以拖
    const int view_h = m_msg_rect.Height();
    const int max_pos = m_content_h - view_h;
    if (max_pos > 0 && !m_scroll_thumb.IsRectEmpty())
    {
        if (m_scroll_thumb.PtInRect(point))
        {
            m_drag_scroll = point.y - m_scroll_thumb.top;
            SetCapture();
            return;
        }
        if (m_scroll_track.PtInRect(point))
        {
            m_scroll_pos += (point.y < m_scroll_thumb.top ? -view_h : view_h);
            if (m_scroll_pos < 0) m_scroll_pos = 0;
            if (m_scroll_pos > max_pos) m_scroll_pos = max_pos;
            InvalidateRect(m_msg_rect, FALSE);
            return;
        }
    }

    CWnd::OnLButtonDown(nFlags, point);
}

void CAiChatView::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_drag_scroll >= 0)
    {
        m_drag_scroll = -1;
        ReleaseCapture();
        return;
    }
    CWnd::OnLButtonUp(nFlags, point);
}

void CAiChatView::OnMouseMove(UINT nFlags, CPoint point)
{
    const int view_h = m_msg_rect.Height();
    const int max_pos = m_content_h - view_h;

    if (m_drag_scroll >= 0 && max_pos > 0 && !m_scroll_track.IsRectEmpty() && !m_scroll_thumb.IsRectEmpty())
    {
        int track_h = m_scroll_track.Height();
        int thumb_h = m_scroll_thumb.Height();
        int usable = track_h - thumb_h;
        if (usable > 0)
        {
            int y = point.y - m_drag_scroll - m_scroll_track.top;
            m_scroll_pos = y * max_pos / usable;
            if (m_scroll_pos < 0) m_scroll_pos = 0;
            if (m_scroll_pos > max_pos) m_scroll_pos = max_pos;
            InvalidateRect(m_msg_rect, FALSE);
        }
        return;
    }

    int hover = -1;
    for (size_t i = 0; i < m_chip_rects.size(); i++)
    {
        if (m_chip_rects[i].PtInRect(point)) { hover = static_cast<int>(i); break; }
    }
    if (hover != m_hover_chip)
    {
        m_hover_chip = hover;
        InvalidateRect(m_msg_rect, FALSE);
    }
    CWnd::OnMouseMove(nFlags, point);
}

HBRUSH CAiChatView::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    if (pWnd != nullptr && pWnd->GetSafeHwnd() == m_input.GetSafeHwnd())
    {
        pDC->SetBkColor(m_pal.bg);
        pDC->SetTextColor(m_pal.text);
        return m_edit_brush;
    }
    return CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CAiChatView::OnDestroy()
{
    StopAll();
    CWnd::OnDestroy();
}

LRESULT CAiChatView::OnChatDelta(WPARAM wParam, LPARAM lParam)
{
    std::wstring* p = reinterpret_cast<std::wstring*>(lParam);
    if (p == nullptr) return 0;
    if (static_cast<int>(wParam) != m_gen)     // 迟到的结果，直接丢
    {
        delete p;
        return 0;
    }
    m_stream_text += *p;
    delete p;

    if (!m_messages.empty())
    {
        Bubble& b = m_messages.back();
        b.thinking = false;
        b.text = m_stream_text;
    }
    RelayoutBubbles();
    ScrollToBottom();
    InvalidateRect(m_msg_rect, FALSE);
    return 0;
}

LRESULT CAiChatView::OnChatDone(WPARAM wParam, LPARAM lParam)
{
    AiChatDoneResult* p = reinterpret_cast<AiChatDoneResult*>(lParam);
    if (p == nullptr) return 0;
    if (static_cast<int>(wParam) != m_gen)
    {
        delete p;
        return 0;
    }

    SetBusy(false);

    if (p->ok)
    {
        // 流式已经把文字铺进最后一条气泡了，这里补上「依据」那行并补一句耗时
        std::wstring text = p->text.empty() ? m_stream_text : p->text;
        PopThinking();
        Bubble b;
        b.mine = false;
        b.text = text;
        b.source = p->source;
        m_messages.push_back(b);

        if (!m_last_question.empty())
        {
            AiChatMessage q; q.role = L"user"; q.content = m_last_question;
            AiChatMessage a; a.role = L"assistant"; a.content = text;
            m_history.push_back(q);
            m_history.push_back(a);
            TrimHistory();
        }
        AppendHistoryFile(L"AI", text);
        RelayoutBubbles();
        ScrollToBottom();
        Invalidate();
    }
    else
    {
        // 失败：已经流出来的那截字留着，没流出来的就把「思考中」那块撤掉
        if (!m_messages.empty())
        {
            Bubble& b = m_messages.back();
            if (b.thinking)
            {
                m_messages.pop_back();
            }
            else if (!b.text.empty())
            {
                b.text += L"\n（回答中断）";
            }
        }

        BannerKind kind = BannerKind::Error;
        std::wstring msg = p->error;
        switch (p->kind)
        {
        case AiErrorKind::Auth:
            msg = L"服务商拒绝了这次请求，多半是 API Key 无效或已过期。" + p->error;
            break;
        case AiErrorKind::Timeout:
            msg = L"请求超时了。" + p->error;
            break;
        case AiErrorKind::Network:
            msg = L"连不上服务商。" + p->error;
            break;
        case AiErrorKind::RateLimit:
            msg = L"服务商说请求太频繁（429）。" + p->error;
            break;
        case AiErrorKind::NoConfig:
            kind = BannerKind::Warn;
            msg = p->error;
            break;
        case AiErrorKind::Cancelled:
            kind = BannerKind::Info;
            msg = L"这次请求已经取消。";
            break;
        default:
            msg = p->error;
            break;
        }

        RelayoutBubbles();
        ShowBanner(kind, msg);
        ScrollToBottom();
    }

    m_stream_text.clear();
    delete p;
    return 0;
}

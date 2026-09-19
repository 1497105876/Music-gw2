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
#include "AiProtocol.h"     // CleanMarkdown：模型爱写 Markdown，气泡是纯文本绘制的
#include "StatAnalysis.h"
#include <algorithm>
#include <random>

// ───────────────────────── 输入框：回车发送 ─────────────────────────

BEGIN_MESSAGE_MAP(CAiChatInput, CEdit)
    ON_WM_KEYDOWN()
END_MESSAGE_MAP()

void CAiChatInput::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    // 回车 = 发送；想换行就按住 Shift 或 Ctrl 再回车（Shift+Enter 是聊天的通用习惯）
    if (nChar == VK_RETURN)
    {
        const bool with_mod =
            ((::GetKeyState(VK_SHIFT) & 0x8000) != 0) ||
            ((::GetKeyState(VK_CONTROL) & 0x8000) != 0);
        if (!with_mod)
        {
            if (on_enter) on_enter();
            return;
        }
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
    ShowChipMenu();

    // 子控件都建好了，从这一刻起才允许布局/绘制（见 m_ready 的注释）
    m_ready = true;

    RecalcLayout();
    ScrollToBottom();
    return true;
}

void CAiChatView::CreateChildCtrls()
{
    CFont* p_font = &theApp.m_font_set.dlg.GetFont();

    // 模式做成下拉框，而不是「点一下循环切一档」。
    // 循环按钮有两个硬伤：看不到有哪些选项、也不知道点一下会跳到哪儿
    // （想从 Max 回本地还得点两下）；而且档位之间差的是「往外发多少数据」，
    // 这种事必须摆在明面上让用户挑。下拉项里直接把含义写全。
    // Create 时给的高度是**下拉列表展开后的高度**，控件本身的高度由 MoveWindow 定。
    m_mode_combo.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        CRect(0, 0, theApp.DPI(150), theApp.DPI(120)), this, kIdModeCombo);
    m_mode_combo.SetFont(p_font);
    m_mode_combo.SetMouseWheelEnable(false);    // 免得在消息区滚轮时误改档位
    m_mode_combo.AddString(L"本地 · 不联网");
    m_mode_combo.AddString(L"模型 · 发聚合值");
    m_mode_combo.AddString(L"Max · 发全部记录");

    m_clear_btn.Create(L"清空", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
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
    // 输入框空着的时候显示灰字提示 —— 不然「回车能发」这条规则只有空态里提过一次，
    // 聊起来以后就没人记得了。
    m_input.SetCueBanner(L"问点关于听歌数据的事…（回车发送，Shift+回车换行）", TRUE);
    m_input.on_enter = [this]() { OnEnterPressed(); };

    UpdateModeCombo();
    UpdateSendButton();
}

void CAiChatView::UpdatePalette()
{
    // 一律走系统色。
    // 同一个对话框里的主列表和洞察文本框都是「白底黑字」，之前这里自己按
    // m_app_setting_data.dark_mode 拍了一套深色出来，结果整块面板变成黑底，
    // 跟周围完全不是一路。系统色还能自动跟着系统浅色/深色走，不用自己操心。
    m_pal.bg            = ::GetSysColor(COLOR_BTNFACE);      // 顶栏、输入区：跟对话框同色
    m_pal.top_bg        = ::GetSysColor(COLOR_BTNFACE);
    m_pal.msg_bg        = ::GetSysColor(COLOR_WINDOW);       // 消息区：跟主列表一样的白底
    m_pal.bubble_ai     = ::GetSysColor(COLOR_WINDOW);       // 它说的：白底
    m_pal.bubble_me     = ::GetSysColor(COLOR_BTNFACE);      // 我说的：浅灰底，一眼能分开
    m_pal.bubble_border = ::GetSysColor(COLOR_3DSHADOW);
    m_pal.text          = ::GetSysColor(COLOR_WINDOWTEXT);
    m_pal.text_dim      = ::GetSysColor(COLOR_GRAYTEXT);
    m_pal.chip_bg       = ::GetSysColor(COLOR_WINDOW);
    m_pal.chip_border   = ::GetSysColor(COLOR_3DSHADOW);
    m_pal.chip_text     = ::GetSysColor(COLOR_WINDOWTEXT);
    m_pal.warn_bg       = ::GetSysColor(COLOR_INFOBK);
    m_pal.warn_border   = ::GetSysColor(COLOR_3DSHADOW);
    m_pal.warn_text     = ::GetSysColor(COLOR_WINDOWTEXT);
    m_pal.err_bg        = ::GetSysColor(COLOR_INFOBK);
    m_pal.err_border    = ::GetSysColor(COLOR_3DSHADOW);
    // 系统色里没有「错误」专用色，红字最直接；深色主题下要提亮一点才看得清
    const COLORREF t = m_pal.text;
    const int lum = (GetRValue(t) * 299 + GetGValue(t) * 587 + GetBValue(t) * 114) / 1000;
    m_pal.err_text = (lum > 128) ? RGB(180, 0, 0) : RGB(255, 130, 130);

    if (m_edit_brush.m_hObject != nullptr) m_edit_brush.DeleteObject();
    m_edit_brush.CreateSolidBrush(m_pal.msg_bg);     // 输入框跟编辑框一样白底
}

// ───────────────────────── 布局 ─────────────────────────

// 三条横带的高度都压得比较紧：这块面板实际只有主列表那么大
// （rc 里是 326x138 DLU，约 489x276 像素），固定带多占一点，气泡就少看一行。
int CAiChatView::TopBarHeight() const { return theApp.DPI(32); }   // 32 才塞得下 22px 的下拉
int CAiChatView::InputHeight() const { return theApp.DPI(56); }    // 够看两行，写长问题不用摸黑

// 横幅高度按**实际文字量**算。错误信息里会带上完整请求地址和服务商原话，
// 固定一行的话只显示得下「服务商那边出..」这类半截话（用户真被这么坑过），
// 所以这里量出真实高度，最多给到三行左右，再长就点一下看完整内容。
int CAiChatView::BannerHeight()
{
    if (m_banner_kind == BannerKind::None) return 0;
    const int min_h = theApp.DPI(28);
    if (!m_ready || !::IsWindow(m_hWnd)) return min_h;

    CRect rc;
    GetClientRect(rc);
    int w = rc.Width() - theApp.DPI(10) * 2 - theApp.DPI(56);    // 右边给「重试」留位
    if (w < theApp.DPI(80)) w = theApp.DPI(80);

    CClientDC dc(this);
    CFont* p_old = dc.SelectObject(&theApp.m_font_set.dlg.GetFont());
    int h = MeasureTextHeight(dc, m_banner_text, w);
    if (p_old != nullptr) dc.SelectObject(p_old);

    h += theApp.DPI(10);                                        // 上下内边距
    if (h < min_h) h = min_h;
    const int max_h = theApp.DPI(62);
    if (h > max_h) h = max_h;
    return h;
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

    // ③ 底部两块（选项带 + 输入区）贴着底边先定下来。
    //    选项带没内容时高度为 0，不占地方。
    const int chip_band = m_chips.empty() ? 0 : ChipHeight();
    m_input_rect = CRect(0, rc.Height() - InputHeight(), rc.Width(), rc.Height());
    m_chip_rect = CRect(0, m_input_rect.top - chip_band, rc.Width(), m_input_rect.top);
    if (m_chip_rect.top < y)                             // 窗口实在太矮，别压到横幅上
    {
        m_chip_rect.top = y;
        m_input_rect.top = m_chip_rect.bottom;
    }

    // ④ 消息区吃剩下的全部
    m_msg_rect = CRect(0, y, rc.Width(), m_chip_rect.top);
    if (m_msg_rect.bottom < m_msg_rect.top) m_msg_rect.bottom = m_msg_rect.top;

    // 顶部条：左「模式」下拉 / 中说明 / 右「清空」
    int ctl_h = theApp.DPI(22);
    int ctl_y = m_top_rect.top + (m_top_rect.Height() - ctl_h) / 2;
    int mode_w = theApp.DPI(132);
    m_mode_combo.MoveWindow(pad, ctl_y, mode_w, ctl_h);
    int clear_w = theApp.DPI(52);
    m_clear_btn.MoveWindow(m_top_rect.right - pad - clear_w, ctl_y, clear_w, ctl_h);

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

    LayoutChips();
    RelayoutBubbles();      // 里面会顺带把空态位置也算好
    FollowBottomIfNeeded();
}

std::wstring CAiChatView::EmptyHintText() const
{
    std::wstring s;
    if (AiConfig::Get().chat_mode == AiChatMode::Local)
        s = L"现在这套是「本地」档，什么都不往外发，答案由本机自己算。"
            L"\n下面挑一个问题点一下就行 —— 也可以直接打字问。";
    else
        s = L"发出去的内容只有统计数字 —— 文件路径、歌词、封面都不会离开这台电脑。"
            L"\n下面挑一个问题点一下就行 —— 也可以直接打字问。";
    s += L"\n回车发送，Shift+回车换行；在消息上点右键可以复制。";
    return s;
}

// 空态整块（标题 / 说明 / 快捷提问）在消息区里居中摆放。
// chips 必须按顺序填进 m_chip_rects，点击时靠下标去 m_chips 取原文。
void CAiChatView::LayoutEmptyState()
{
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

    // 选项在下面那条独立带子里，这里只摆标题和说明 —— 以前把选项塞在这里，
    // 结果聊过天之后空态不再绘制，追问就再也显示不出来了。
    const int gap_title_hint = theApp.DPI(6);
    const int block_h = title_h + gap_title_hint + hint_h;

    int top = m_msg_rect.top + (m_msg_rect.Height() - block_h) / 2;
    if (top < m_msg_rect.top + theApp.DPI(6))
        top = m_msg_rect.top + theApp.DPI(6);

    m_empty_title_rect = CRect(m_msg_rect.left + side, top, m_msg_rect.right - side, top + title_h);
    const int y = m_empty_title_rect.bottom + gap_title_hint;
    m_empty_text_rect = CRect(m_msg_rect.left + side, y, m_msg_rect.right - side, y + hint_h);

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
        // 水平位置也在这里定下来：右键命中测试要用，不能再留在绘制里现算
        b.left = b.mine ? (m_msg_rect.right - margin - b.width) : (m_msg_rect.left + margin);
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
    // 面板底用对话框色，消息区单独刷成白 —— 跟旁边的列表、洞察文本框看起来才是一路的
    CBrush bg_brush(m_pal.bg);
    dc.FillRect(client, &bg_brush);
    if (m_msg_rect.Height() > 0 && m_msg_rect.Width() > 0)
    {
        CBrush msg_brush(m_pal.msg_bg);
        dc.FillRect(m_msg_rect, &msg_brush);
    }

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

    // 中间那行说明：模式下拉右边、清空按钮左边。
    // 模式的含义已经写在模式框里了，这里只留最有用的那个数，别重复占地方。
    CRect r = m_top_rect;
    r.left += theApp.DPI(138) + theApp.DPI(8);
    r.right -= theApp.DPI(56) + theApp.DPI(8);
    if (r.Width() <= 0) return;

    std::wstring text;
    if (m_all_count > 0)
        text = L"共 " + std::to_wstring(m_all_count) + L" 条记录";
    else
        text = L"暂无记录";

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
    r.DeflateRect(0, theApp.DPI(5));
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(fg);
    // 多行 + 按词断行：错误信息里带着完整请求地址和服务商原话，
    // 单行省略号会把它截成「服务商那边出..」这种半截话（用户真被坑过）
    dc.DrawText(m_banner_text.c_str(), static_cast<int>(m_banner_text.size()), &r,
        DT_WORDBREAK | DT_NOPREFIX);
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
    const int pad_x = theApp.DPI(12);
    const int pad_y = theApp.DPI(8);
    CRect rc(b.left, b.top, b.left + b.width, b.top + b.height);

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

    const int w = theApp.DPI(9);        // 别太细，不然鼠标点不住
    CRect track(m_msg_rect.right - w - theApp.DPI(4), m_msg_rect.top + theApp.DPI(4),
        m_msg_rect.right - theApp.DPI(4), m_msg_rect.bottom - theApp.DPI(4));
    m_scroll_track = track;

    int track_h = track.Height();
    int thumb_h = track_h * view_h / m_content_h;
    if (thumb_h < theApp.DPI(24)) thumb_h = theApp.DPI(24);
    int max_pos = m_content_h - view_h;
    int y = track.top + (track_h - thumb_h) * m_scroll_pos / (max_pos > 0 ? max_pos : 1);
    m_scroll_thumb = CRect(track.left, y, track.right, y + thumb_h);

    CBrush br(::GetSysColor(COLOR_BTNFACE));        // 轨道
    dc.FillRect(track, &br);
    CBrush tb(::GetSysColor(COLOR_3DSHADOW));       // 滑块
    dc.FillRect(m_scroll_thumb, &tb);
}

void CAiChatView::DrawQuickChips(CDC& dc)
{
    if (m_chip_rect.Width() <= 0 || m_chip_rect.Height() <= 0) return;
    if (m_chip_rects.size() != m_chips.size()) return;

    // 带子底色跟顶栏一致，让它看起来是「操作区」而不是消息内容
    CBrush band(m_pal.top_bg);
    dc.FillRect(m_chip_rect, &band);
    CPen top_line(PS_SOLID, 1, m_pal.bubble_border);
    CPen* p_old_line = dc.SelectObject(&top_line);
    dc.MoveTo(m_chip_rect.left, m_chip_rect.top);
    dc.LineTo(m_chip_rect.right, m_chip_rect.top);
    dc.SelectObject(p_old_line);

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
        dc.DrawText(m_chips[i].text.c_str(), static_cast<int>(m_chips[i].text.size()), &m_chip_rects[i],
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

// 发送键一个按钮两种身份：闲着是「发送」，等待中是「停止」。
// 这样不用再往这条窄顶栏挤一个按钮，用户也不用满界面找「停止」在哪。
// 输入框里按回车。
// 正在生成的时候不响应 —— 「停止」是会打断回答的动作，该由用户明确点按钮，
// 而不是他正打着下一句、顺手一回车就把它撞停了。
void CAiChatView::OnEnterPressed()
{
    if (m_busy) return;
    OnSendOrStop();     // 这时它等价于「发送」
}

void CAiChatView::OnSendOrStop()
{
    if (m_busy)
    {
        OnStop();
        return;
    }

    CString s;
    m_input.GetWindowText(s);
    const std::wstring q = Trim(s.GetString());
    if (q.empty()) return;          // 按钮本来就是灰的，这里只是兜底

    // 先发；发出去了才清输入框 —— 万一被「没记录 / 没配模型」拦下，
    // 用户刚敲的字必须还在框里，不能凭空消失。
    if (DoSend(q, true))
    {
        m_input.SetWindowText(L"");
        UpdateSendButton();
    }
    m_input.SetFocus();             // 焦点还给输入框，好接着打下一句
}

void CAiChatView::OnStop()
{
    if (!m_busy) return;

    // 真的中断：把开关置上，工作线程下一次读数据就收手，服务商那边这次调用不会白花。
    // m_cancel_flag 是 shared_ptr，线程手里也握着一份，不怕这里先失效。
    if (m_cancel_flag) m_cancel_flag->store(true);
    m_gen++;                        // 双保险：迟到的那条结果直接作废
    SetBusy(false);

    // 「思考中」那块收掉；已经吐出来的字留着，补一句说明
    if (!m_messages.empty())
    {
        if (m_messages.back().thinking)
            m_messages.pop_back();
        else if (m_messages.back().streamed)
            m_messages.back().text += L"\n（已停止）";
    }
    ShowBanner(BannerKind::Info, L"已停止生成。");
}

// 返回 true = 真的发出去了（本地档答完也算）。校验没过返回 false，
// 调用方据此决定「输入框里的字要不要留着」。
bool CAiChatView::DoSend(const std::wstring& question, bool push_user_bubble,
    const std::wstring& qa_id)
{
    if (m_busy) return false;
    HideBanner();

    // 本地档是纯本机规则引擎，不联网、也不看模型配置 —— 所以「有没有开 AI」
    // 「有没有配模型」这两关只对联网档生效；「有没有记录可问」才是共同前提。
    const AiChatMode mode = AiConfig::Get().chat_mode;

    if (m_all_count <= 0)
    {
        ShowBanner(BannerKind::Info, L"还没有播放记录。先去听几首歌，再回来问我。");
        return false;
    }

    AiStatSnapshot snap = m_snapshot_fn ? m_snapshot_fn() : AiStatSnapshot{};
    if (!snap.Valid())
    {
        ShowBanner(BannerKind::Info, L"统计数据还没算出来，等一下再试。");
        return false;
    }

    const AiModelConfig* model = CurrentModelOrNull();
    AiCallParams params;
    if (mode != AiChatMode::Local)
    {
        if (!AiConfig::Get().enabled)
        {
            ShowBanner(BannerKind::Warn,
                L"AI 功能还没打开。到「选项设置 → AI 设置」里启用它并添加一套模型；"
                L"也可以把左边切到「本地」档 —— 那一档不用任何配置。");
            return false;
        }
        if (model == nullptr)
        {
            ShowBanner(BannerKind::Warn,
                L"还没配可用的模型。到「选项设置 → AI 设置」里添加一套，双击它设为「当前使用」；"
                L"也可以切到「本地」档直接用。");
            return false;
        }
        params = AiCallParams::FromModel(*model, AiConfig::Get().request);
        std::wstring why;
        if (!params.Valid(why))
        {
            ShowBanner(BannerKind::Warn, L"这套模型还差东西：" + why);
            return false;
        }
    }

    // —— 到这儿才算真的发出去了 ——
    m_last_question = question;
    m_auto_follow = true;           // 自己发的消息，一定要看得见
    if (push_user_bubble) PushUser(question);

    if (mode == AiChatMode::Local)
    {
        // 先看这个问题是不是菜单里的某一条：是就用**专用生成器**（答案确定正确），
        // 不是才退回关键词匹配（那是没法保证准的兜底）。
        // 之所以要有这条路径，就是因为「听得最多的歌手的第三名是谁」被答成了榜首。
        const bool allow_meta = AiConfig::Get().privacy.allow_song_meta;
        const AiStatContext::LocalQa* qa = nullptr;
        if (!qa_id.empty())
            qa = AiStatContext::FindLocalQaById(qa_id);
        if (qa == nullptr)
            qa = AiStatContext::FindLocalQaByText(question);

        std::wstring answer;
        if (qa != nullptr)
        {
            answer = AiStatContext::BuildQaAnswer(snap, qa->id, allow_meta);
            m_last_qa_id = qa->id;
        }
        if (answer.empty())
        {
            answer = AiStatContext::BuildLocalAnswer(snap, question, allow_meta);
            m_last_qa_id.clear();
        }
        std::wstring source = AiStatContext::BuildSourceText(snap, question);
        PushAi(answer, source);

        // 答完就把「接着能问什么」摆出来
        if (!m_last_qa_id.empty())
            ShowChipFollowUp(m_last_qa_id);
        else
            ShowChipMenu();
        Invalidate();
        return true;
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
    m_cancel_flag = std::make_shared<std::atomic<bool>>(false);
    SetBusy(true);
    PushThinking();
    AiStartChatJob(m_hWnd, m_gen, params, msgs, source, m_cancel_flag);
    return true;
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
    b.text = AiProtocol::CleanMarkdown(text);
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
        if (::IsWindow(m_hWnd)) SetTimer(kTimerThink, 350, nullptr);
        m_think_phase = 0;
    }
    else
    {
        if (::IsWindow(m_hWnd)) KillTimer(kTimerThink);
    }
    // 输入框**不**禁用：等回答的这几十秒正好可以先把下一句打好；
    // 而且 EnableWindow(FALSE) 会把焦点抢走 —— 正打着字突然打不了了，最招人烦。
    // 发送键这时变成「停止」，照样可点。
    UpdateSendButton();
}

// 模式下拉：把当前档位选上就行。加进去的顺序跟 AiChatMode 一致（本地/模型/Max）
void CAiChatView::UpdateModeCombo()
{
    if (!::IsWindow(m_mode_combo.m_hWnd)) return;
    int sel = 0;
    switch (AiConfig::Get().chat_mode)
    {
    case AiChatMode::Local: sel = 0; break;
    case AiChatMode::Model: sel = 1; break;
    default:                sel = 2; break;
    }
    if (m_mode_combo.GetCurSel() != sel)
        m_mode_combo.SetCurSel(sel);
}

// 发送键的两种身份，外加「输入框空着就点不动」。
// 后者是即时反馈：用户不用按了才发现没反应。
void CAiChatView::UpdateSendButton()
{
    // 用 GetSafeHwnd 挡一道：MFC 这些成员函数自己会 ASSERT，
    // 控件还没建好或者已经销毁时直接调就会弹断言框。
    if (m_send_btn.GetSafeHwnd() == nullptr) return;
    if (m_busy)
    {
        m_send_btn.SetWindowText(L"停止");
        m_send_btn.EnableWindow(TRUE);
        return;
    }
    m_send_btn.SetWindowText(L"发送");
    if (m_input.GetSafeHwnd() == nullptr)
    {
        m_send_btn.EnableWindow(FALSE);     // 输入框还没建好，先按「空」处理
        return;
    }
    CString s;
    m_input.GetWindowText(s);
    m_send_btn.EnableWindow(Trim(s.GetString()).empty() ? FALSE : TRUE);
}

void CAiChatView::OnInputChanged()
{
    // 输入框一空就把「发送」置灰，一有字就点亮 —— 用户不用按了才发现没反应
    UpdateSendButton();
}

BOOL CAiChatView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    // 能点、能拖的地方给手型光标。自绘控件没有原生控件的那些暗示，
    // 不给光标的话用户只能靠猜 —— 尤其是那条能点开详情的横幅。
    CPoint pt;
    ::GetCursorPos(&pt);
    ScreenToClient(&pt);

    bool hand = false;
    if (m_banner_kind != BannerKind::None && m_banner_rect.PtInRect(pt))
        hand = true;
    if (!hand)
    {
        for (size_t i = 0; i < m_chip_rects.size(); i++)
        {
            if (m_chip_rects[i].PtInRect(pt)) { hand = true; break; }
        }
    }
    if (!hand && !m_scroll_thumb.IsRectEmpty() && m_scroll_thumb.PtInRect(pt))
        hand = true;

    if (hand)
    {
        ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

int CAiChatView::ChipHeight() const
{
    return theApp.DPI(32);
}

// 把当前选项摆成一行。放不下的直接不显示 —— 换行会再吃掉一行消息区高度，
// 而这块地方本来就不大；重要的问题都排在前面，够用。
void CAiChatView::LayoutChips()
{
    m_chip_rects.clear();
    if (!m_ready || !::IsWindow(m_hWnd)) return;
    if (m_chips.empty()) return;
    if (m_chip_rect.Width() <= 0 || m_chip_rect.Height() <= 0) return;

    CClientDC dc(this);
    CFont* p_old = dc.SelectObject(&theApp.m_font_set.dlg.GetFont());

    const int pad = theApp.DPI(6);
    const int inner = theApp.DPI(10);
    const int chip_h = theApp.DPI(24);
    const int gap = theApp.DPI(6);
    int x = m_chip_rect.left + pad;
    const int y = m_chip_rect.top + (m_chip_rect.Height() - chip_h) / 2;
    if (y < m_chip_rect.top) return;

    for (const auto& c : m_chips)
    {
        CSize sz = dc.GetTextExtent(c.text.c_str(), static_cast<int>(c.text.size()));
        int w = sz.cx + inner * 2;
        if (w < theApp.DPI(52)) w = theApp.DPI(52);
        if (x + w > m_chip_rect.right - pad)
            break;
        m_chip_rects.push_back(CRect(x, y, x + w, y + chip_h));
        x += w + gap;
    }
    if (p_old != nullptr) dc.SelectObject(p_old);
}

// 一级：分类。进页面、清空对话、点「换个话题」都回到这里。
void CAiChatView::ShowChipMenu()
{
    m_chips.clear();
    const std::vector<AiStatContext::LocalQaGroup>& menu = AiStatContext::LocalQaMenu();
    for (size_t i = 0; i < menu.size(); ++i)
    {
        ChipItem c;
        c.key = L"g" + std::to_wstring(i);
        c.text = menu[i].name;
        c.kind = 0;
        m_chips.push_back(c);
    }
    RecalcLayout();
}

// 二级：某一类里的问题
void CAiChatView::ShowChipGroup(int group_index)
{
    m_chips.clear();
    const std::vector<AiStatContext::LocalQaGroup>& menu = AiStatContext::LocalQaMenu();
    if (group_index < 0 || group_index >= static_cast<int>(menu.size()))
    {
        ShowChipMenu();
        return;
    }
    for (const auto& id : menu[group_index].ids)
    {
        const AiStatContext::LocalQa* qa = AiStatContext::FindLocalQaById(id);
        if (qa == nullptr) continue;
        ChipItem c;
        c.key = qa->id;
        c.text = qa->question;
        c.kind = 1;
        m_chips.push_back(c);
    }
    ChipItem back;
    back.text = L"← 返回分类";
    back.kind = 2;
    m_chips.push_back(back);
    RecalcLayout();
}

// 回答之后：把这条问题的 next 摆出来。
// 这是整套交互最关键的一步 —— 用户不用猜「还能问什么」，顺着点就行。
void CAiChatView::ShowChipFollowUp(const std::wstring& qa_id)
{
    m_chips.clear();
    const AiStatContext::LocalQa* qa = AiStatContext::FindLocalQaById(qa_id);
    if (qa != nullptr)
    {
        for (const auto& nid : qa->next)
        {
            const AiStatContext::LocalQa* nq = AiStatContext::FindLocalQaById(nid);
            if (nq == nullptr) continue;
            ChipItem c;
            c.key = nq->id;
            c.text = nq->question;
            c.kind = 1;
            m_chips.push_back(c);
            if (m_chips.size() >= 4) break;     // 一行放不下更多
        }
    }
    ChipItem more;
    more.text = L"换个话题";
    more.kind = 2;
    m_chips.push_back(more);
    RecalcLayout();
}

void CAiChatView::OnChipClicked(int index)
{
    if (m_busy) return;
    if (index < 0 || index >= static_cast<int>(m_chips.size())) return;

    const ChipItem c = m_chips[index];

    if (c.kind == 2)                    // 返回分类 / 换个话题
    {
        ShowChipMenu();
        Invalidate();
        return;
    }
    if (c.kind == 0)                    // 展开某一分类
    {
        int gi = 0;
        for (size_t k = 1; k < c.key.size(); ++k)       // key 形如 "g3"
        {
            if (c.key[k] >= L'0' && c.key[k] <= L'9')
                gi = gi * 10 + static_cast<int>(c.key[k] - L'0');
        }
        ShowChipGroup(gi);
        Invalidate();
        return;
    }

    // 问题：直接发出去（用户点它就是要看答案，再让确认一次反而多一步）
    DoSend(c.text, true, c.key);
    Invalidate();
}
void CAiChatView::ScrollToBottom()
{
    const int view_h = m_msg_rect.Height();
    m_scroll_pos = m_content_h - view_h;
    if (m_scroll_pos < 0) m_scroll_pos = 0;
    m_auto_follow = true;
}

bool CAiChatView::AtBottom() const
{
    const int view_h = m_msg_rect.Height();
    const int max_pos = m_content_h - view_h;
    if (max_pos <= 0) return true;
    return m_scroll_pos >= max_pos - theApp.DPI(8);     // 差几个像素也算到底
}

// 只在用户本来就贴着底部时才跟着滚。
// 不然流式回答一边吐字一边把他拽回底部，正在往上翻的历史根本看不住 ——
// 这是聊天界面最经典的「反人类」点之一。
void CAiChatView::FollowBottomIfNeeded()
{
    if (m_auto_follow) ScrollToBottom();
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

void CAiChatView::OnModeChanged()
{
    const int sel = m_mode_combo.GetCurSel();
    AiChatMode m = AiChatMode::Local;
    if (sel == 1)      m = AiChatMode::Model;
    else if (sel == 2) m = AiChatMode::Max;

    const AiChatMode old = AiConfig::Get().chat_mode;
    AiConfig::Get().chat_mode = m;      // 记住上次档位，下次进来还是它

    if (m != AiChatMode::Local && CurrentModelOrNull() == nullptr)
    {
        // 要联网的档位没模型，当场说清楚 —— 别等用户打完字按了发送才拦
        ShowBanner(BannerKind::Warn,
            L"这一档要联网，得先有模型。到「选项设置 → AI 设置」里加一套 —— "
            L"在那之前「本地」档可以照常用。");
    }
    else if (old != m && m_banner_kind != BannerKind::Error)
    {
        // 换了档位，之前的提示多半不适用了，重算一遍
        RefreshStatusBanner();
    }
    Invalidate();
}

void CAiChatView::OnClearClick()
{
    if (m_busy)
    {
        ShowBanner(BannerKind::Info, L"正在回答。等它说完，或者点「停止」再清空。");
        return;
    }
    if (m_messages.empty())
    {
        ShowBanner(BannerKind::Info, L"现在没有对话可清。");
        return;
    }
    if (MessageBox(L"确定清空当前对话？", L"AI 对话", MB_ICONQUESTION | MB_YESNO) != IDYES)
        return;

    m_messages.clear();
    m_history.clear();
    m_stream_text.clear();
    m_last_question.clear();
    m_scroll_pos = 0;
    m_auto_follow = true;
    ShowChipMenu();

    // 先把旧横幅抹掉，再报一句「已清空」—— 动作有反馈，用户才知道这一下点生效了
    m_banner_kind = BannerKind::None;
    m_banner_text.clear();
    if (::IsWindow(m_retry_btn.m_hWnd)) m_retry_btn.ShowWindow(SW_HIDE);
    RecalcLayout();
    ShowBanner(BannerKind::Info, L"对话已清空。下面可以重新挑一个问题。");
    if (::IsWindow(m_input.m_hWnd)) m_input.SetFocus();
}

void CAiChatView::OnRetryClick()
{
    if (m_busy) return;
    if (m_last_question.empty())
    {
        HideBanner();
        return;
    }
    // 重试不用重新打字 —— 但这里不能再补一条用户气泡，那条本来就还在上面
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

    // 每次进来都从「本地」档开始：这一档不联网、不需要任何配置，是稳妥的默认值；
    // 想发给模型，自己在下拉里切一下就行。
    if (AiConfig::Get().chat_mode != AiChatMode::Local)
        AiConfig::Get().chat_mode = AiChatMode::Local;

    UpdateModeCombo();
    RecalcLayout();

    // 还没聊过就回到分类菜单；聊过的话保留当前追问，别把人家的探索路径冲掉
    if (m_messages.empty())
    {
        ShowChipMenu();
        LayoutEmptyState();
        InvalidateRect(m_msg_rect, FALSE);
    }

    RefreshStatusBanner();

    // 进来就能直接打字，不用先点一下输入框
    if (::IsWindow(m_input.m_hWnd)) m_input.SetFocus();
}

// 每次进页面都重新判断一遍状态。
// 以前这里是「已经有横幅就直接 return」短路 —— 结果用户跑去设置里把模型配好了，
// 切回来头顶上还挂着「还没配好模型」的黄条，明明已经配好了，看着莫名其妙。
void CAiChatView::RefreshStatusBanner()
{
    // 错误横幅是「刚才那次失败」的凭据，留着让用户看清，不主动覆盖
    if (m_banner_kind == BannerKind::Error) return;

    if (m_all_count <= 0)
    {
        ShowBanner(BannerKind::Info, L"还没有播放记录。先去听几首歌，再回来问我。");
        return;
    }

    // 本地档是纯本机算的，完全不需要配置，别拿模型的事烦它
    if (AiConfig::Get().chat_mode == AiChatMode::Local)
    {
        HideBanner();
        return;
    }

    if (!AiConfig::Get().enabled || CurrentModelOrNull() == nullptr)
    {
        ShowBanner(BannerKind::Warn,
            L"还没配好模型。到「选项设置 → AI 设置」里启用并添加一套，双击设为「当前使用」；"
            L"也可以把左边切到「本地」档，那一档不用配置。");
        return;
    }

    HideBanner();
}

void CAiChatView::StopAll()
{
    m_gen++;
    if (m_cancel_flag) m_cancel_flag->store(true);
    // 注意：这里**不能**调 SetBusy —— 它在析构函数里也会被走到，
    // 那时 m_hWnd 早就无效了，而 SetBusy 里的 KillTimer 会撞上
    // ASSERT(::IsWindow(m_hWnd))（afxwin2.inl:155）。定时器会随窗口被系统一起清掉，
    // 所以这儿只复位标志就够。
    m_busy = false;
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
    ON_WM_SETCURSOR()
    ON_WM_CONTEXTMENU()
    ON_WM_TIMER()
    ON_WM_CTLCOLOR()
    ON_WM_DESTROY()
    ON_CBN_SELCHANGE(kIdModeCombo, &CAiChatView::OnModeChanged)
    ON_BN_CLICKED(kIdClearBtn, &CAiChatView::OnClearClick)
    ON_BN_CLICKED(kIdSendBtn, &CAiChatView::OnSendOrStop)
    ON_BN_CLICKED(kIdRetryBtn, &CAiChatView::OnRetryClick)
    ON_EN_CHANGE(kIdInput, &CAiChatView::OnInputChanged)
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
        m_auto_follow = AtBottom();     // 滚回底部就恢复自动跟随
        InvalidateRect(m_msg_rect, FALSE);
    }
    return CWnd::OnMouseWheel(nFlags, zDelta, pt);
}

void CAiChatView::OnLButtonDown(UINT nFlags, CPoint point)
{
    // 点提示横幅 → 弹窗看完整内容。横幅最多三行，错误信息里的请求地址、
    // 服务商原话经常放不下；弹窗里的字还能选中、复制。
    if (m_banner_kind != BannerKind::None && !m_banner_text.empty() && m_banner_rect.PtInRect(point))
    {
        bool on_retry = false;
        if (::IsWindow(m_retry_btn.m_hWnd) && m_retry_btn.IsWindowVisible())
        {
            CRect rb;
            m_retry_btn.GetWindowRect(rb);
            ScreenToClient(&rb);
            on_retry = (rb.PtInRect(point) != FALSE);
        }
        if (!on_retry)
        {
            MessageBox(m_banner_text.c_str(), L"详细信息", MB_ICONINFORMATION | MB_OK);
            return;
        }
    }

    // 底部选项：点分类 → 展开；点问题 → 直接发；点「换个话题」→ 回分类。
    // 以前是「填进输入框再让用户回车」，但既然是菜单式引导，多点一步反而别扭。
    for (size_t i = 0; i < m_chip_rects.size() && i < m_chips.size(); i++)
    {
        if (m_chip_rects[i].PtInRect(point))
        {
            OnChipClicked(static_cast<int>(i));
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
            m_auto_follow = AtBottom();
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
            m_auto_follow = AtBottom();
            InvalidateRect(m_msg_rect, FALSE);
        }
        return;
    }

    int hover = -1;
    if (m_chip_rect.PtInRect(point))
    {
        for (size_t i = 0; i < m_chip_rects.size(); i++)
        {
            if (m_chip_rects[i].PtInRect(point)) { hover = static_cast<int>(i); break; }
        }
    }
    if (hover != m_hover_chip)
    {
        m_hover_chip = hover;
        InvalidateRect(m_chip_rect, FALSE);
    }
    CWnd::OnMouseMove(nFlags, point);
}

// ───────────────────────── 右键：复制 ─────────────────────────

int CAiChatView::HitTestBubble(CPoint pt) const
{
    if (!m_msg_rect.PtInRect(pt))
        return -1;
    for (size_t i = 0; i < m_messages.size(); i++)
    {
        const Bubble& b = m_messages[i];
        const int top = m_msg_rect.top + b.top - m_scroll_pos;
        CRect rc(b.left, top, b.left + b.width, top + b.height);
        if (rc.PtInRect(pt))
            return static_cast<int>(i);
    }
    return -1;
}

namespace
{
    void PutTextToClipboard(CWnd* pWnd, const std::wstring& text)
    {
        if (pWnd == nullptr || text.empty())
            return;
        if (!::OpenClipboard(pWnd->GetSafeHwnd()))
            return;
        ::EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            if (void* p = ::GlobalLock(h))
            {
                memcpy(p, text.c_str(), bytes);
                ::GlobalUnlock(h);
                if (::SetClipboardData(CF_UNICODETEXT, h) == nullptr)
                    ::GlobalFree(h);        // 交出所有权失败才自己释放
            }
            else
            {
                ::GlobalFree(h);
            }
        }
        ::CloseClipboard();
    }
}

void CAiChatView::CopyBubbleText(int index)
{
    if (index < 0 || index >= static_cast<int>(m_messages.size()))
        return;
    const Bubble& b = m_messages[index];
    if (b.thinking)     // 「思考中」那块没内容可复制
        return;
    std::wstring text = b.text;
    if (!b.source.empty())
        text += L"\n依据：" + b.source;
    PutTextToClipboard(this, text);
}

void CAiChatView::CopyAllText()
{
    std::wstring all;
    for (const auto& b : m_messages)
    {
        if (b.thinking) continue;
        all += b.mine ? L"我：" : L"AI：";
        all += b.text;
        if (!b.source.empty())
            all += L"\n依据：" + b.source;
        all += L"\n\n";
    }
    while (!all.empty() && (all.back() == L'\n' || all.back() == L'\r'))
        all.pop_back();
    PutTextToClipboard(this, all);
}

// 「重新回答这一个」：找出这条回答对应的那个问题，重新发一次。
// 历史也要跟着退回去 —— 不然同一轮问答会往上下文里塞两遍。
bool CAiChatView::RerunQuestionFor(int ai_bubble_index)
{
    if (m_busy) return false;
    if (ai_bubble_index < 0 || ai_bubble_index >= static_cast<int>(m_messages.size()))
        return false;
    if (m_messages[ai_bubble_index].mine) return false;

    // 往上找最近的一条用户消息，就是它问的
    std::wstring q;
    for (int i = ai_bubble_index - 1; i >= 0; i--)
    {
        if (m_messages[i].mine && !m_messages[i].thinking)
        {
            q = m_messages[i].text;
            break;
        }
    }
    if (q.empty()) return false;

    // 这条回答以及它后面的内容都不要了
    m_messages.erase(m_messages.begin() + ai_bubble_index, m_messages.end());
    // 历史里最后那一轮如果正是这个问题，一并退掉
    if (m_history.size() >= 2 &&
        m_history[m_history.size() - 2].role == L"user" &&
        m_history[m_history.size() - 2].content == q)
    {
        m_history.pop_back();
        m_history.pop_back();
    }

    RelayoutBubbles();
    Invalidate();
    return DoSend(q, false);        // 用户气泡还在上面，不用再补一条
}

// 把气泡对应的问题放回输入框，用户改完自己发
bool CAiChatView::RefillQuestionFor(int bubble_index)
{
    if (bubble_index < 0 || bubble_index >= static_cast<int>(m_messages.size()))
        return false;

    std::wstring q;
    if (m_messages[bubble_index].mine)
    {
        q = m_messages[bubble_index].text;
    }
    else
    {
        for (int i = bubble_index - 1; i >= 0; i--)
        {
            if (m_messages[i].mine && !m_messages[i].thinking)
            {
                q = m_messages[i].text;
                break;
            }
        }
    }
    if (q.empty()) return false;

    m_input.SetWindowText(q.c_str());
    m_input.SetSel(-1, -1);
    m_input.SetFocus();
    UpdateSendButton();
    return true;
}

void CAiChatView::OnContextMenu(CWnd*, CPoint point)
{
    CPoint pt = point;
    ScreenToClient(&pt);
    const int idx = HitTestBubble(pt);
    if (idx < 0)
        return;     // 点在空白处不弹菜单

    CMenu menu;
    if (!menu.CreatePopupMenu())
        return;
    // 菜单跟着气泡的类型走：AI 的回答能给「重新回答」，自己发的问题能给「改一改」
    const bool mine = m_messages[idx].mine;
    const bool thinking = m_messages[idx].thinking;

    menu.AppendMenu(MF_STRING, 1, L"复制这条消息");
    if (!mine && !thinking)
        menu.AppendMenu(MF_STRING, 2, L"重新回答这一个");
    if (mine)
        menu.AppendMenu(MF_STRING, 3, L"放回输入框改一改");
    menu.AppendMenu(MF_SEPARATOR, 0, L"");
    menu.AppendMenu(MF_STRING, 4, L"复制全部对话");

    const int cmd = static_cast<int>(menu.TrackPopupMenu(
        TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, this));
    if (cmd == 1)      CopyBubbleText(idx);
    else if (cmd == 2) RerunQuestionFor(idx);
    else if (cmd == 3) RefillQuestionFor(idx);
    else if (cmd == 4) CopyAllText();
}

HBRUSH CAiChatView::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    if (pWnd != nullptr && pWnd->GetSafeHwnd() == m_input.GetSafeHwnd())
    {
        pDC->SetBkColor(m_pal.msg_bg);
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
        b.streamed = true;      // 收尾时就地补「依据」，不再 push 一条（否则会重复显示）
        b.text = AiProtocol::CleanMarkdown(m_stream_text);
    }
    RelayoutBubbles();
    FollowBottomIfNeeded();     // 用户正往上翻的话别把他拽回来
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
        std::wstring text = p->text.empty() ? m_stream_text : p->text;

        // 流式模式下文字已经被 OnChatDelta 就地铺进最后那条气泡了 ——
        // 这里只补「依据」。以前会再 push 一条，结果同一次回答显示成两条一模一样的。
        bool filled_inline = false;
        if (!m_messages.empty())
        {
            Bubble& last = m_messages.back();
            if (last.streamed && !last.mine)
            {
                last.text = AiProtocol::CleanMarkdown(text);
                last.source = p->source;
                last.streamed = false;
                filled_inline = true;
            }
        }
        if (!filled_inline)
        {
            PopThinking();
            Bubble b;
            b.mine = false;
            b.text = text;
            b.source = p->source;
            m_messages.push_back(b);
        }

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

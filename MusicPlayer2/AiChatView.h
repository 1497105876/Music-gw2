#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "AiConfig.h"
#include "AiClient.h"
#include "AiStatContext.h"
#include "MyComboBox.h"

// 「歌曲详细记录」第 7 页 —— AI 对话的面板。
//
// 它自己开一个子窗口、自己画：消息气泡 / 快捷提问 / 输入行都是自绘或自建控件，
// 不用列表控件（跟洞察页一样是「非列表」布局），所以这里的消息循环、滚动条、
// 气泡排版都得自己来。
//
// 三档模式：
//   本地  —— 什么都不发，规则引擎在本机算，秒回
//   模型  —— 发时间范围 + 聚合值
//   Max   —— 聚合值 + 完整原始记录（慢且贵）
// 上下文一律用「全部记录」，不受页面上方的日期范围影响；每次发消息前重新算一遍聚合值。
//
// 交互上刻意对齐「聊天的通用习惯」：
//   · 模式是**下拉框**而不是循环按钮 —— 点一下就能看到三个选项和各自含义，不用猜
//   · 回车发送 / Shift+回车换行；发送后**清空输入框并把焦点还回来**，好接着打下一句
//   · 等待期间输入框**照常能打字**（只是发送键变成「停止」），不夺焦点
//   · 往上翻历史时**不会被新内容拽回底部**（只在原本就贴着底部时才自动跟随）
//
// 输入框：回车发送，Shift/Ctrl+回车换行
class CAiChatInput : public CEdit
{
public:
    std::function<void()> on_enter;

protected:
    DECLARE_MESSAGE_MAP()
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};

class CAiChatView : public CWnd
{
public:
    CAiChatView();
    virtual ~CAiChatView();

    // 在父窗口上创建这块面板（rc 用像素坐标）
    bool CreatePanel(CWnd* parent, const CRect& rect);
    bool IsReady() const { return m_ready; }

    // 取数据快照的回调：只有真正要发消息时才调，保证拿到的是最新聚合值
    void SetSnapshotProvider(std::function<AiStatSnapshot()> fn) { m_snapshot_fn = std::move(fn); }

    // 数据变了 / 页面切进来时通知一下，刷新顶部那行说明
    void OnDataChanged(int all_count);
    void OnPageActivated();

    void StopAll();         // 窗口要关了：作废在途请求
    bool IsBusy() const { return m_busy; }

private:
    enum : UINT
    {
        kIdModeCombo = 1,   // 模式下拉
        kIdClearBtn  = 2,   // 清空对话
        kIdSendBtn   = 3,   // 发送（等待中变成「停止」）
        kIdRetryBtn  = 4,   // 失败横幅上的「重试」
        kIdInput     = 5,   // 输入框
    };
    enum : UINT_PTR
    {
        kTimerThink = 1,
    };
    enum class BannerKind { None, Info, Warn, Error };

    // 一条气泡
    struct Bubble
    {
        bool mine{ false };
        std::wstring text;
        std::wstring source;        // 气泡底部那行「依据：…」
        bool thinking{ false };     // 正在思考：画三个跳动的点
        // 这条内容是流式「就地」铺进来的。收尾时看到这个标记就只补「依据」，
        // 不再 push 一条新的 —— 否则同一次回答会显示成两条一模一样的气泡。
        bool streamed{ false };
        int top{ 0 };               // 相对内容区顶部的 y（内容坐标系，未减滚动量）
        int left{ 0 };              // 客户区坐标
        int height{ 0 };
        int width{ 0 };
    };

    // 底部选项带里的一个按钮。
    //   分类      —— 点开看这一类有哪些问题（kind 0，key = "g<索引>"）
    //   问题      —— 点了直接发出去（kind 1，key = 目录里的 id）
    //   返回/换话题 —— 回到分类（kind 2）
    struct ChipItem
    {
        std::wstring key;
        std::wstring text;
        int          kind{ 1 };
    };

    struct Palette
    {
        COLORREF bg{};          // 顶栏 / 输入区底
        COLORREF msg_bg{};      // 消息区底（跟主列表一样是白的）
        COLORREF top_bg{};
        COLORREF bubble_me{};
        COLORREF bubble_ai{};
        COLORREF bubble_border{};
        COLORREF text{};
        COLORREF text_dim{};
        COLORREF chip_bg{};
        COLORREF chip_border{};
        COLORREF chip_text{};
        COLORREF warn_bg{};
        COLORREF warn_border{};
        COLORREF warn_text{};
        COLORREF err_bg{};
        COLORREF err_border{};
        COLORREF err_text{};
    };

private:
    // ── 生命周期 ──
    void CreateChildCtrls();
    void UpdatePalette();

    // ── 布局 ──
    // 整块面板只有三条横带：顶部条 / 消息区（吃剩余）/ 输入区。
    // 快捷提问不是单独一条带 —— 它跟空态文案一起居中放在消息区里（跟原型一致），
    // 这样能省下 40px 留给气泡，不然在「歌曲详细记录」这块不大的地方根本不够看。
    void RecalcLayout();        // 算出三块的矩形，并摆放子控件
    void RelayoutBubbles();     // 量每条气泡的高度，算出内容总高
    void LayoutEmptyState();    // 空态：标题 + 说明 + 快捷提问，整块居中
    int  TopBarHeight() const;
    int  BannerHeight();        // 非 const：要按实际文字量量高度（错误信息可能很长）
    int  InputHeight() const;

    // ── 绘制 ──
    void DrawAll(CDC& dc, const CRect& client);
    void DrawTopBar(CDC& dc);
    void DrawBanner(CDC& dc);
    void DrawMessages(CDC& dc);
    void DrawOneBubble(CDC& dc, const Bubble& b);
    void DrawScrollbar(CDC& dc);
    void DrawQuickChips(CDC& dc);
    void DrawEmptyHint(CDC& dc);
    std::wstring EmptyHintText() const;     // 空态那句说明，布局和绘制两处共用
    int  MeasureTextHeight(CDC& dc, const std::wstring& text, int width);
    int  MeasureTextWidth(CDC& dc, const std::wstring& text);

    // ── 收发 ──
    // qa_id 非空表示「这是从菜单里点的问题」，本地档据此用专用答案生成器；
    // false = 校验没过、没发出去
    bool DoSend(const std::wstring& question, bool push_user_bubble,
        const std::wstring& qa_id = std::wstring());
    void OnSendOrStop();        // 发送键：闲着就发，忙着就停
    void OnEnterPressed();      // 输入框回车：只在闲着时当「发送」
    void OnStop();
    void OnModeChanged();
    void OnClearClick();
    void OnRetryClick();
    bool RerunQuestionFor(int ai_bubble_index);    // 「重新回答」：找到对应的问题重发
    bool RefillQuestionFor(int bubble_index);      // 把对应的问题放回输入框（可编辑后再发）
    std::wstring BuildContextText(AiStatSnapshot& snap) const;
    std::wstring LanguageInstruction() const;
    void PushUser(const std::wstring& text);
    void PushThinking();
    void PushAi(const std::wstring& text, const std::wstring& source);
    void PopThinking();
    void TrimHistory();
    void AppendHistoryFile(const std::wstring& role, const std::wstring& text);

    // ── 状态 ──
    void ShowBanner(BannerKind kind, const std::wstring& text);
    void HideBanner();
    void RefreshStatusBanner();     // 每次进页面都重算，不用「已有横幅」短路
    void SetBusy(bool busy);
    void UpdateModeCombo();
    void UpdateSendButton();        // 「发送 / 停止」文字切换 + 空输入时置灰
    int  ChipHeight() const;
    void LayoutChips();                                 // 把当前选项摆成一行
    void ShowChipMenu();                                // 一级：分类
    void ShowChipGroup(int group_index);                // 二级：某一类的问题
    void ShowChipFollowUp(const std::wstring& qa_id);   // 回答之后：接着能问什么
    void OnChipClicked(int index);
    void ScrollToBottom();          // 强制滚到底
    void FollowBottomIfNeeded();    // 只在用户本来就贴着底部时才跟随
    bool AtBottom() const;
    bool HasRecords() const;
    const AiModelConfig* CurrentModelOrNull() const;

    // ── 右键菜单：复制 / 重来 ──
    int  HitTestBubble(CPoint pt) const;        // 客户区坐标，返回气泡下标，-1 没命中
    void CopyBubbleText(int index);
    void CopyAllText();

private:
    // ── 子控件 ──
    CMyComboBox  m_mode_combo;
    CButton      m_clear_btn;
    CButton      m_send_btn;
    CButton      m_retry_btn;
    CAiChatInput m_input;
    CBrush       m_edit_brush;

    // ── 布局结果 ──
    CRect m_top_rect;
    CRect m_banner_rect;
    CRect m_msg_rect;
    CRect m_input_rect;
    CRect m_chip_rect;                      // 底部选项那条带
    CRect m_scroll_track;
    CRect m_scroll_thumb;
    CRect m_empty_title_rect;               // 空态标题
    CRect m_empty_text_rect;                // 空态说明
    std::vector<CRect> m_chip_rects;

    // ── 内容 ──
    std::vector<Bubble> m_messages;
    std::vector<ChipItem> m_chips;          // 底部选项：分类 / 某类的问题 / 追问
    std::wstring m_last_qa_id;              // 最后一条回答来自目录里的哪条（取追问用）
    std::vector<AiChatMessage> m_history;   // 多轮对话历史，最多 6 轮 = 12 条
    std::wstring m_last_question;
    BannerKind m_banner_kind{ BannerKind::None };
    std::wstring m_banner_text;

    // ── 滚动 ──
    int m_scroll_pos{ 0 };
    int m_content_h{ 0 };
    int m_drag_scroll{ -1 };        // 正在拖滚动条时的鼠标 y 偏移
    int m_hover_chip{ -1 };
    // 是否跟着新内容自动滚到底。用户手动往上翻时置 false，滚回底部再置 true ——
    // 不然流式回答一边吐字一边把他拽到底，正在看的历史就丢了。
    bool m_auto_follow{ true };

    // ── 请求 ──
    int  m_gen{ 0 };                // 代号：作废迟到的网络结果
    bool m_busy{ false };
    std::wstring m_stream_text;
    int  m_think_phase{ 0 };
    int  m_all_count{ 0 };
    // 「停止生成」的中断开关。线程持一份、界面持一份，所以窗口先销毁也不会悬空。
    std::shared_ptr<std::atomic<bool>> m_cancel_flag;
    // Create() 还没返回时窗口就会收到 WM_SIZE / WM_PAINT，那时候子控件一个都还没建，
    // 去 MoveWindow 会直接 ENSURE(::IsWindow(m_hWnd)) 弹「Debug Assertion Failed」。
    // 建完子控件才置 true，之前所有布局/绘制一律不干活。
    bool m_ready{ false };
    bool m_entered{ false };        // 是否进过本页面（决定快捷提问要不要重新随机）

    std::function<AiStatSnapshot()> m_snapshot_fn;
    Palette m_pal;

public:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg void OnDestroy();
    afx_msg void OnInputChanged();
    afx_msg LRESULT OnChatDelta(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnChatDone(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()
};

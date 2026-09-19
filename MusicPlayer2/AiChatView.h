#pragma once
#include <functional>
#include <string>
#include <vector>
#include "AiConfig.h"
#include "AiClient.h"
#include "AiStatContext.h"

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

// 输入框：回车发送，Ctrl+回车换行
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
        kIdModeBtn  = 1,    // 模式切换
        kIdClearBtn = 2,    // 清空对话
        kIdSendBtn  = 3,    // 发送
        kIdRetryBtn = 4,    // 失败横幅上的「重试」
        kIdInput    = 5,    // 输入框
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
        int top{ 0 };               // 相对内容区顶部的 y
        int height{ 0 };
        int width{ 0 };
    };

    struct Palette
    {
        COLORREF bg{};
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
    void RecalcLayout();        // 算出五块的矩形，并摆放子控件
    void RelayoutBubbles();     // 量每条气泡的高度，算出内容总高
    void LayoutQuickChips();
    int  TopBarHeight() const;
    int  BannerHeight() const;
    int  QuickHeight() const;
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
    int  MeasureTextHeight(CDC& dc, const std::wstring& text, int width);
    int  MeasureTextWidth(CDC& dc, const std::wstring& text);

    // ── 收发 ──
    void DoSend(const std::wstring& question, bool push_user_bubble);
    void OnSendClick();
    void OnModeClick();
    void OnClearClick();
    void OnRetryClick();
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
    void SetBusy(bool busy);
    void UpdateModeButton();
    void ReseedQuickQuestions();
    void ScrollToBottom();
    bool HasRecords() const;
    const AiModelConfig* CurrentModelOrNull() const;

private:
    // ── 子控件 ──
    CButton      m_mode_btn;
    CButton      m_clear_btn;
    CButton      m_send_btn;
    CButton      m_retry_btn;
    CAiChatInput m_input;
    CBrush       m_edit_brush;

    // ── 布局结果 ──
    CRect m_top_rect;
    CRect m_banner_rect;
    CRect m_msg_rect;
    CRect m_quick_rect;
    CRect m_input_rect;
    CRect m_scroll_track;
    CRect m_scroll_thumb;
    std::vector<CRect> m_chip_rects;

    // ── 内容 ──
    std::vector<Bubble> m_messages;
    std::vector<std::wstring> m_chips;      // 当前展示的快捷提问（4 条）
    std::vector<AiChatMessage> m_history;   // 多轮对话历史，最多 6 轮 = 12 条
    std::wstring m_last_question;
    BannerKind m_banner_kind{ BannerKind::None };
    std::wstring m_banner_text;

    // ── 滚动 ──
    int m_scroll_pos{ 0 };
    int m_content_h{ 0 };
    int m_drag_scroll{ -1 };        // 正在拖滚动条时的鼠标 y 偏移
    int m_hover_chip{ -1 };

    // ── 请求 ──
    int  m_gen{ 0 };                // 代号：作废迟到的网络结果
    bool m_busy{ false };
    std::wstring m_stream_text;
    volatile bool m_cancel{ false };
    int  m_think_phase{ 0 };
    int  m_all_count{ 0 };
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
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg void OnDestroy();
    afx_msg LRESULT OnChatDelta(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnChatDone(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()
};

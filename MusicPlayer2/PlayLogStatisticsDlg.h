#pragma once
#include "BaseDialog.h"
#include "ListCtrlEx.h"
#include "MyComboBox.h"
#include "StatCommon.h"
#include "StatAnalysis.h"
#include "StatHtmlReport.h"
#include "AiChatView.h"

// 「歌曲详细记录」页面：一个窗口、一个列表，顶部用原生页签条切换视图。
// 职责：加载 playlog 原始日志 → 按时间范围过滤（唯一过滤点）→ 调聚合层算好快照 → 填进主列表。
//
// 页签条是原生 SysTabControl32，只用来切换，不往里塞子窗口；
// 主列表是对话框自己的直接子控件，所以不存在「窗口里套窗口」。

// 视图索引，与页签插入顺序一一对应
enum class PlayLogStatView { Overview = 0, Artist, Album, Song, Detail, Insight, AiChat };
static constexpr int kViewCount = 7;

// 页签文字（顺序与上面枚举一致）
const wchar_t* const kViewTabText[kViewCount] = {
    L"概览", L"歌手", L"专辑", L"曲目", L"明细", L"洞察", L"AI 对话"
};

// 明细视图：数据量可能很大，分批次插进去，避免一次性刷几万行把界面卡住
static constexpr int kDetailMaxRows = 2000;      // 明细最多展示多少条
static constexpr int kDetailBatchSize = 200;     // 每批插多少条

// 数据快照：过滤后的记录 + 聚合结果，只在本对话框内部流转
struct PlayLogStatData
{
    const std::vector<PlayRecord>* records{ nullptr };  // 已按时间区间过滤（唯一过滤点）
    StatFilter     filter;
    StatSummary    summary;
    FinishBreakdown finish;
    std::vector<ArtistRankItem> artists;
    std::vector<AlbumRankItem>  albums;
    std::vector<SongRankItem>   songs;
    std::vector<PeriodBucket>   day_buckets;    // 日粒度桶，用于「听得最久的一天」
    int            hour_hist[24]{};             // 24 小时分布（有效记录数）
    int            first_ymd{ 0 };              // 数据最早日期
    int            last_ymd{ 0 };               // 数据最晚日期
    int            broken_lines{ 0 };           // 解析损坏被跳过的行数
    int            failed_files{ 0 };           // 读取失败的文件数
    bool           load_ok{ false };            // 数据是否成功读入
    bool           valid{ false };              // 快照是否已算好
};

// 范围下拉：只为「配色跟周围对齐」而存在的薄子类，不改 MyComboBox.cpp，不影响其它页面。
// 注意：头文件里不写 DECLARE_DYNAMIC（写了就得在 cpp 配 IMPLEMENT_DYNAMIC，漏了会链接错）。
class CStatRangeComboBox : public CMyComboBox
{
public:
    DECLARE_MESSAGE_MAP()
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

private:
    CBrush m_bk_brush;
};

class CPlayLogStatDlg : public CBaseDialog
{
    DECLARE_DYNAMIC(CPlayLogStatDlg)

public:
    CPlayLogStatDlg(CWnd* pParent = nullptr);
    virtual ~CPlayLogStatDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_PLAY_LOG_STATISTICS_DLG };
#endif

private:
    enum TimerId
    {
        TIMER_PERIODIC = 1,     // 定时重读日志
        TIMER_DEBOUNCE = 2,     // 收到「写入新记录」通知后的防抖刷新
        TIMER_DETAIL_BATCH = 3, // 明细视图分批次插入
    };

protected:
    // ── 控件 ──
    CStatRangeComboBox m_range_combo;
    CDateTimeCtrl  m_date_from;
    CDateTimeCtrl  m_date_to;
    CListCtrlEx    m_list;                          // 唯一的主列表，各视图共用
    CTabCtrl       m_view_tab;                      // 顶部原生页签条（只作切换器）
    CImageList     m_tab_img_list;                  // 页签图标，必须活到窗口销毁
    CBrush         m_ctl_bk_brush;                  // 下拉/日期控件的背景刷
    CFont          m_date_font;                     // 日期框专用字体（切到「雅黑小一号」那套才用得到）
    CEdit          m_insight_edit;                  // 洞察页的多行只读文本框（跟列表同区域、互斥显示）
    CAiChatView    m_ai_chat;                       // AI 对话面板（自绘子窗口，跟列表同区域、互斥显示）

    // ── 数据 ──
    std::vector<PlayRecord> m_all_records;      // 全量记录（按播放时间倒序）
    std::vector<PlayRecord> m_filtered;         // 过滤后的记录
    PlayLogStatData m_data;                     // 聚合结果快照

    PlayLogStatView m_cur_view{ PlayLogStatView::Overview };

    // 明细分批次加载：m_detail_rows 里的指针指向 m_filtered 的元素，
    // 所以 m_filtered 一动就必须先 StopDetailBatch()，否则指针全废。
    std::vector<const PlayRecord*> m_detail_rows;
    int  m_detail_next{ 0 };                    // 下一批从哪条开始
    bool m_detail_truncated{ false };           // 是否因为超过 kDetailMaxRows 被截断

    int  m_broken_lines{ 0 };
    int  m_failed_files{ 0 };
    bool m_syncing_date{ false };       // 程序同步日期控件期间置位，用于屏蔽 DTN_DATETIMECHANGE

    int  m_overview_row{ 0 };           // 概览视图填表游标
    int  m_overview_group{ -1 };

protected:
    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    // ── 数据流水线 ──
    void LoadRecords();                 // 读盘
    void ApplyFilter();                 // 过滤 + 聚合 + 重填当前视图
    void RefreshAll();                  // 读盘 + 过滤（外部调用的完整刷新）
    void UpdateWarningText();
    void SetRangePreset(RangePreset preset);    // 切换预设
    void LayoutFilterRow();             // 顶部那行控件按顺序从左往右排（不依赖 rc 里手写的 x）并同步日期控件
    void SyncDateControls();
    int  YmdFromCtrl(CDateTimeCtrl& ctrl) const;

    // ── 视图切换 ──
    void InitTabCtrl();                         // 给页签条插入 7 个页签并挂图标
    void SwitchView(PlayLogStatView view);      // 重建列 + 重填
    void InitListColumns();                     // 只在切换视图和初始化时调用：删列重建
    void FillCurrentView();                     // 只重填当前视图的行，不动列
    void FillOverviewView();
    void FillArtistView();
    void FillAlbumView();
    void FillSongView();
    void FillDetailView();
    void FillInsightView();                     // 洞察页：排版好的纯文字，灌进多行文本框
    void AddOverviewRow(int group, const wchar_t* item, const std::wstring& value);
    void ShowEmptyRow(const wchar_t* text);

    // ── 明细分批次加载 ──
    void StartDetailBatch();                    // 算好待展示的行，插第一批，剩下的交给定时器
    bool AppendDetailBatch();                   // 插一批，返回是否还没插完
    void StopDetailBatch();                     // 停定时器并清空（m_filtered 变动前必须调）
    void InsertDetailRow(const PlayRecord& r, int index);   // 插单条明细行

    // ── AI 对话页 ──
    void CreateAiChatPanel();                   // 在主列表那块区域上建自绘面板
    void LayoutAiChatPanel();                   // 跟着主列表的位置走（窗口缩放时同步）
    void UpdateAiChatData();                    // 数据刷新后把最新条数告诉面板
    AiStatSnapshot BuildAiSnapshot();           // 现取一份快照给面板（发送时才调）

    DECLARE_MESSAGE_MAP()

public:
    virtual BOOL OnInitDialog() override;
    afx_msg void OnDestroy();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBnClickedRefresh();
    afx_msg void OnBnClickedReport();
    afx_msg void OnCbnSelchangeRangePreset();
    afx_msg void OnDatetimeChange(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnTabSelChange(NMHDR* pNMHDR, LRESULT* pResult);   // 页签切换
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);    // 下拉/日期控件配色
    afx_msg LRESULT OnRecordAppended(WPARAM wParam, LPARAM lParam);
};

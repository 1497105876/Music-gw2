#pragma once
#include "BaseDialog.h"
#include "ListCtrlEx.h"
#include "MyComboBox.h"
#include "StatCommon.h"
#include "StatAnalysis.h"
#include "StatHtmlReport.h"

// 「歌曲详细记录」页面：一个窗口、一个列表，顶部 5 个按钮切换视图。
// 职责：加载 playlog 原始日志 → 按时间范围过滤（唯一过滤点）→ 调聚合层算好快照 → 填进主列表。

// 视图索引，与 IDC_PLAYLOG_VIEW_OVERVIEW..IDC_PLAYLOG_VIEW_DETAIL 一一对应（ID 连续，ON_CONTROL_RANGE 用）
enum class PlayLogStatView { Overview = 0, Artist, Album, Song, Detail };
static constexpr int kViewCount = 5;

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
    };

protected:
    // ── 控件 ──
    CStatRangeComboBox m_range_combo;
    CDateTimeCtrl  m_date_from;
    CDateTimeCtrl  m_date_to;
    CListCtrlEx    m_list;                          // 唯一的主列表，5 个视图共用
    CButton        m_view_btn[kViewCount];          // 顶部的 5 个视图切换按钮
    CFont          m_view_bold_font;                // 当前选中按钮的加粗字体
    CBrush         m_ctl_bk_brush;                  // 下拉/日期控件的背景刷

    // ── 数据 ──
    std::vector<PlayRecord> m_all_records;      // 全量记录（按播放时间倒序）
    std::vector<PlayRecord> m_filtered;         // 过滤后的记录
    PlayLogStatData m_data;                     // 聚合结果快照

    PlayLogStatView m_cur_view{ PlayLogStatView::Overview };

    int  m_broken_lines{ 0 };
    int  m_failed_files{ 0 };
    bool m_date_ctrl_enabled{ false };
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
    void SetRangePreset(RangePreset preset);    // 切换预设并同步日期控件
    void SyncDateControls();
    void EnableDateControls(bool enable);
    int  YmdFromCtrl(CDateTimeCtrl& ctrl) const;

    // ── 视图切换 ──
    void SwitchView(PlayLogStatView view);      // 设按钮态 + 重建列 + 重填
    void InitListColumns();                     // 只在切换视图和初始化时调用：删列重建
    void FillCurrentView();                     // 只重填当前视图的行，不动列
    void FillOverviewView();
    void FillArtistView();
    void FillAlbumView();
    void FillSongView();
    void FillDetailView();
    void AddOverviewRow(int group, const wchar_t* item, const std::wstring& value);
    void ShowEmptyRow(const wchar_t* text);

    DECLARE_MESSAGE_MAP()

public:
    virtual BOOL OnInitDialog() override;
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBnClickedRefresh();
    afx_msg void OnBnClickedReport();
    afx_msg void OnCbnSelchangeRangePreset();
    afx_msg void OnDatetimeChange(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnViewSwitch(UINT nID);                                // 5 个视图按钮共用
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);    // 下拉/日期控件配色
    afx_msg LRESULT OnRecordAppended(WPARAM wParam, LPARAM lParam);
};

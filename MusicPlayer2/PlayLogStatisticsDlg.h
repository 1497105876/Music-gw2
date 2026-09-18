#pragma once
#include "BaseDialog.h"
#include "CTabCtrlEx.h"
#include "PlayLogStatTabDlg.h"
#include "StatHtmlReport.h"

// 「歌曲详细记录」主对话框
// 职责：加载 playlog 原始日志 → 按时间范围过滤（唯一过滤点）→ 调聚合层算好快照 → 下发给各子页。
// 子页只做展示，不重复计算。图形部分不在此处，全部交给「生成网页报告」。
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
    // 子页
    CPlayLogStatOverviewTabDlg m_overview_dlg{ this };
    CPlayLogStatArtistTabDlg   m_artist_dlg{ this };
    CPlayLogStatAlbumTabDlg    m_album_dlg{ this };
    CPlayLogStatSongTabDlg     m_song_dlg{ this };
    CPlayLogStatDetailTabDlg   m_detail_dlg{ this };

    CTabCtrlEx m_tab;
    CComboBox  m_range_combo;
    CDateTimeCtrl m_date_from;
    CDateTimeCtrl m_date_to;

    std::vector<PlayRecord> m_all_records;      // 全量记录（按播放时间倒序）
    std::vector<PlayRecord> m_filtered;         // 过滤后的记录（子页共用，不拷贝）
    PlayLogStatData m_data;                     // 下发给子页的只读快照

    int  m_broken_lines{ 0 };
    int  m_failed_files{ 0 };
    bool m_date_ctrl_enabled{ false };
    bool m_syncing_date{ false };       // 程序同步日期控件期间置位，用于屏蔽 DTN_DATETIMECHANGE

protected:
    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual void DoDataExchange(CDataExchange* pDX);

    // ── 数据流水线 ──
    void LoadRecords();                 // 读盘
    void ApplyFilter();                 // 过滤 + 聚合 + 下发
    void RefreshAll();                  // 读盘 + 过滤（外部调用的完整刷新）
    void UpdateWarningText();
    void SetRangePreset(RangePreset preset);    // 切换预设并同步日期控件
    void SyncDateControls();
    void EnableDateControls(bool enable);
    int  YmdFromCtrl(CDateTimeCtrl& ctrl) const;

    DECLARE_MESSAGE_MAP()

public:
    virtual BOOL OnInitDialog();
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBnClickedRefresh();
    afx_msg void OnBnClickedHelp();
    afx_msg void OnBnClickedReport();
    afx_msg void OnCbnSelchangeRangePreset();
    afx_msg void OnDatetimeChange(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg LRESULT OnRecordAppended(WPARAM wParam, LPARAM lParam);
};

// 口径说明对话框（主对话框右上角的「?」）
class CPlayLogStatHelpDlg : public CBaseDialog
{
    DECLARE_DYNAMIC(CPlayLogStatHelpDlg)

public:
    CPlayLogStatHelpDlg(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_PLAYLOG_STAT_HELP_DLG };
#endif

protected:
    virtual CString GetDialogName() const override;
    virtual void DoDataExchange(CDataExchange* pDX);

    CEdit m_edit;

public:
    virtual BOOL OnInitDialog();
};

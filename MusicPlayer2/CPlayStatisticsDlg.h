#pragma once
#include "BaseDialog.h"
#include "CTabCtrlEx.h"
#include "MyComboBox.h"
#include "StatCommon.h"
#include "StatOverviewTabDlg.h"
#include "StatArtistRankTabDlg.h"
#include "StatAlbumRankTabDlg.h"
#include "StatSongRankTabDlg.h"
#include "StatSongsTabDlg.h"
#include "StatProfileTabDlg.h"
#include "StatHtmlReport.h"
#include <afxdtctl.h>
#include <vector>

class CPlayStatisticsDlg : public CBaseDialog
{
    DECLARE_DYNAMIC(CPlayStatisticsDlg)

public:
    CPlayStatisticsDlg(CWnd* pParent = nullptr);
    virtual ~CPlayStatisticsDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_PLAY_STATISTICS_DIALOG };
#endif

protected:
    CTabCtrlEx m_tab;
    CMyComboBox   m_preset_combo;   // 项目统一下拉框（AppearanceSettingDlg / DataSettingsDlg 等同样使用）
    CDateTimeCtrl m_date_from;     // 起始日期：原生日期时间选择器（带下拉日历）
    CDateTimeCtrl m_date_to;       // 结束日期：原生日期时间选择器（带下拉日历）

    bool m_updating_filter{ false }; // 程序化写入 DTP 时的重入守卫（抑制 DTN_DATETIMECHANGE 联动）

    // 6 个子页（移除趋势页/流派页后：概览/歌手/专辑/曲目/明细/洞察）
    CStatOverviewTabDlg m_overview_dlg;
    CStatArtistRankTabDlg m_artist_rank_dlg;
    CStatAlbumRankTabDlg m_album_rank_dlg;
    CStatSongRankTabDlg m_song_rank_dlg;
    CStatSongsTabDlg m_songs_dlg;
    CStatProfileTabDlg m_profile_dlg;

    std::vector<PlayRecord> m_all_records;       // 全量原始记录（对话框打开期只解析一次）
    std::vector<PlayRecord> m_filtered_records;  // 按时间范围过滤后的记录（m_context.records 指向它）
    StatContext m_context;                       // 过滤后视图（唯一数据源，广播给子页）
    StatFilter  m_filter;                        // 当前全局过滤器
    UINT_PTR    m_timer_id{ 0 };                 // 60s 兜底刷新定时器

    // ── 过滤条初始化与联动 ──
    void InitFilterControls();
    void FillPresetCombo();
    void ApplyPresetToFilter(RangePreset preset);  // 按预设回填 from/to
    void SyncDatePickersFromFilter();              // 把 m_filter 的 from/to（或数据范围）写回两个 DTP
    void SyncPresetComboToFilter();                // 下拉框与 m_filter.preset 同步
    void UpdateDatePickerEnabled();                // 预设 == 全部 时禁用两个 DTP（显示数据范围）
    void GetDataRange(int& min_ymd, int& max_ymd) const;  // m_all_records 的实际日期范围（无记录返回 0）
    void SetDatePickerYmd(CDateTimeCtrl& dtp, int ymd);   // 带重入守卫地写 DTP

    void LoadRecords();                            // 解析全部 playlog -> m_all_records
    void ApplyFilter();                            // m_all_records -> m_filtered_records + m_context
    void BroadcastContext();                       // 向 6 个子页投递 const StatContext*
    void RefreshAllViews();                        // LoadRecords + ApplyFilter + Broadcast + label

protected:
    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual bool IsRememberDialogSizeEnable() const override { return true; }
    virtual void DoDataExchange(CDataExchange* pDX);

    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
    afx_msg void OnBnClickedExportCsvButton();
    afx_msg void OnBnClickedExportJsonButton();
    afx_msg void OnCbnSelchangeRangePreset();
    afx_msg void OnDateTimeChangeFrom(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnDateTimeChangeTo(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnBnClickedExportAggButton();
    afx_msg void OnBnClickedReportButton();
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg LRESULT OnStatRecordAppended(WPARAM wParam, LPARAM lParam);
};

#pragma once
#include "BaseDialog.h"
#include "ListCtrlEx.h"
#include "CTabCtrlEx.h"
#include "PlayStatistics.h"
#include <map>
#include <vector>

// CPlayStatisticsDlg 对话框

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
    // 控件
    CTabCtrlEx m_tab;
    CListCtrlEx m_detail_list;       // 详细记录列表
    CListCtrlEx m_overview_list;     // 总览列表
    CStatic m_chart_static;          // 图表显示区域
    CComboBox m_time_filter;         // 时间范围下拉框
    CButton m_view_btns[5];          // 视图切换按钮
    CListCtrlEx m_rank_list;         // 排行榜列表（可滚动）
    CStatic m_rank_chart;            // 排行榜左侧条形图

    // 数据
    std::vector<PlayRecord> m_records;
    int m_chart_type{ 0 };           // 0=概览, 1=排行榜, 2=流派, 3=趋势, 4=时段
    int m_time_filter_days{ 0 };     // 0=全部, 7/30/90=最近N天
    int m_rank_category{ 0 };        // 0=歌手, 1=歌曲

    // 聚合统计结构体
    struct StatsAggregate {
        int total_plays{ 0 };
        int total_duration_sec{ 0 };
        int completed{ 0 }, skipped{ 0 }, stopped{ 0 }, errored{ 0 };
        int hour_count[24]{};
        std::map<std::wstring, int> artist_time;
        std::map<std::wstring, int> album_time;
        std::map<std::wstring, int> song_count;
        std::map<std::wstring, int> genre_time;
        std::map<std::wstring, int> daily_count;
    };

    // 数据过滤与聚合
    std::vector<PlayRecord> GetFilteredRecords() const;
    StatsAggregate AggregateStats(const std::vector<PlayRecord>& records) const;

    // 绘图函数
    void DrawAnalysisChart();
    void DrawOverviewPage(CDC* pDC, const CRect& rect, const StatsAggregate& stats);
    // DrawRankingPage 改为列表控件，不再用 CDC 绘制
    // DrawGenrePage 暂时注释
    // void DrawGenrePage(CDC* pDC, const CRect& rect, const StatsAggregate& stats);
    void DrawTrendChart(CDC* pDC, const CRect& rect, const StatsAggregate& stats);
    void DrawHourChart(CDC* pDC, const CRect& rect, const StatsAggregate& stats);

    // 排行榜列表填充
    void ShowRankingList();
    // 排行榜左侧条形图
    void DrawRankingChart(CDC* pDC, const CRect& rect, const StatsAggregate& stats);

    // 辅助绘图函数
    void DrawSeparator(CDC* pDC, int x1, int x2, int y);
    void DrawHourBars(CDC* pDC, const CRect& rect, const int hour_count[24], bool show_labels);
    void DrawResultBar(CDC* pDC, const CRect& rect, const StatsAggregate& stats);
    void DrawTopBars(CDC* pDC, const CRect& rect,
                     const std::vector<std::pair<std::wstring, int>>& sorted,
                     int top_n, COLORREF colors[], int name_w, int bar_left_offset = 80);

    // 更新按钮高亮状态
    void UpdateViewButtons();

    enum DetailColumn
    {
        DCOL_INDEX = 0,
        DCOL_TIME,
        DCOL_TITLE,
        DCOL_ARTIST,
        DCOL_ALBUM,
        DCOL_PLAY_DUR,
        DCOL_SONG_LEN,
        DCOL_RESULT,
        DCOL_SOURCE,
    };

    enum OverviewColumn
    {
        OCOL_ITEM = 0,
        OCOL_VALUE,
    };

    enum RankColumn
    {
        RCOL_RANK = 0,
        RCOL_NAME,
        RCOL_VALUE,
    };

protected:
    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual void DoDataExchange(CDataExchange* pDX);

    void LoadRecords();
    void ShowOverview();
    void ShowDetail();
    void ExportData(bool csv_format);
    void InitTimeFilter();
    void AutoFitRankColumns();
    void LayoutRankControls();

    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
    afx_msg void OnTcnSelChangeTab(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnBnClickedExportCsvButton();
    afx_msg void OnBnClickedExportJsonButton();
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnCbnSelChangeTimeFilter();
    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
    afx_msg void OnBnClickedViewBtn(UINT nID);
    afx_msg void OnBnClickedRankCatBtn(UINT nID);
    afx_msg void OnSize(UINT nType, int cx, int cy);
};

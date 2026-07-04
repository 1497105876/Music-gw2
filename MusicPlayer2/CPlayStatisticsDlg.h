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
    CListCtrlEx m_detail_list;      // 详细记录列表
    CListCtrlEx m_overview_list;    // 总览列表
    CStatic m_chart_static;         // 图表显示区域

    // 数据
    std::vector<PlayRecord> m_records;
    int m_chart_type{ 0 };          // 0=趋势图, 1=时段图, 2=饼图

    // 绘图
    void DrawTrendChart(CDC* pDC, const CRect& rect);
    void DrawHourChart(CDC* pDC, const CRect& rect);
    void DrawPieChart(CDC* pDC, const CRect& rect);

    // 详细记录列表列索引
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

    // 总览列表列索引
    enum OverviewColumn
    {
        OCOL_ITEM = 0,
        OCOL_VALUE,
    };

protected:
    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual void DoDataExchange(CDataExchange* pDX);

    // 加载播放记录
    void LoadRecords();
    // 显示总览数据
    void ShowOverview();
    // 显示详细记录
    void ShowDetail();
    // 导出数据
    void ExportData(bool csv_format);

    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
    afx_msg void OnTcnSelChangeTab(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnBnClickedExportCsvButton();
    afx_msg void OnBnClickedExportJsonButton();
    afx_msg void OnPaint();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
};

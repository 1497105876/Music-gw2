#pragma once
#include "StatTabDlg.h"
#include "StatChart.h"
#include "StatCommon.h"
#include <vector>

// 趋势图表控件：从 CStatChart（项目内自绘静态控件基类）派生，
// 在控件自己的 OnPaint 里用 CDrawCommon 绘制，不再走“运行时改 SS_OWNERDRAW + 父窗口 OnDrawItem”。
// 这样与项目既有自绘路线（CStaticEx）一致，且不与 AFX_DIALOG_LAYOUT 冲突。
class CTrendChart : public CStatChart
{
    DECLARE_DYNAMIC(CTrendChart)
public:
    // 数据由页面在 Refresh() 时算好传入，控件本身不做业务计算
    struct Data
    {
        std::vector<PeriodBucket> buckets;
        PeriodComparison pc;
        bool has_news{ false };
        int  news_total{ 0 };
        std::wstring news_label;
        int  news_count{ 0 };
        std::wstring title;       // 标题（含粒度名）
    };

    void SetData(const Data& data);

protected:
    virtual void DrawChart(CDrawCommon& draw, CDC* pDC, const CRect& rect) override;

private:
    Data m_data;
};

// 趋势页：只负责按当前粒度聚合数据，绘制交给 CTrendChart
class CStatTrendTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatTrendTabDlg)
public:
    CStatTrendTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatTrendTabDlg();

    enum { IDD = IDD_STAT_TREND_DLG };

    virtual void Refresh() override;

protected:
    CTrendChart m_chart;

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    DECLARE_MESSAGE_MAP()
};

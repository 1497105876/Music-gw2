#pragma once
#include "TabDlg.h"
#include "StatCommon.h"
#include <vector>

class CListCtrlEx;   // 前向声明（列宽自适应用）

// 统计子页中间基类：统一承载全局上下文指针 + 惰性刷新。
// 子页只持有 const StatContext*（不拷贝记录集），聚合一律从 m_stat_ctx 读取。
class CStatTabDlg : public CTabDlg
{
    DECLARE_DYNAMIC(CStatTabDlg)
public:
    CStatTabDlg(UINT nIDTemplate, CWnd* pParent = nullptr);
    virtual ~CStatTabDlg();

    // 主对话框广播上下文：只存指针 + 标脏；若当前可见则立即 Refresh()
    void SetContext(const StatContext* ctx);

    // 各子页实现：从 m_stat_ctx 聚合 + 重绘（实现末尾须将 m_dirty 置 false）
    virtual void Refresh() = 0;

protected:
    // 启用列表列宽自适应：固定列按设计宽度、弹性列独占剩余宽度（照 CListenTimeStatisticsDlg 做法）。
    // flex_column 为弹性列下标；widths 为各列设计宽度（未乘 DPI，单位为 DLU 基准像素，widths[flex] 作该列最小宽度）。
    // 子页 OnInitDialog 中调用一次即可；之后窗口缩放会自动重算。
    void EnableColumnFit(CListCtrlEx* list, int flex_column, const std::vector<int>& widths);

    const StatContext* m_stat_ctx{ nullptr };
    bool m_dirty{ true };

    // 切换到本页时若数据被标脏则补算
    virtual void OnTabEntered() override;

    // 子页尺寸变化时重算列表列宽（配合 AFX_DIALOG_LAYOUT 让列表填满整页）
    afx_msg void OnSize(UINT nType, int cx, int cy);

    // 子页焦点在对话框自身，滚轮不会自动转给鼠标下的控件，这里手工转发
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);

    DECLARE_MESSAGE_MAP()

private:
    void FitColumns();
    CListCtrlEx* m_fit_list{ nullptr };
    int m_flex_column{ 0 };              // 弹性列下标（独占剩余宽度）
    std::vector<int> m_fit_widths;       // 各列设计宽度（未乘 DPI；弹性列该项为最小宽度）
    bool m_fitting{ false };
};

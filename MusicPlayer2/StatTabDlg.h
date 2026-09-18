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
    // 启用列表列宽自适应：按权重在可用宽度内分配（余量给最后一列）。
    // 子页 OnInitDialog 中调用一次即可；之后窗口缩放会自动重算。
    void EnableColumnFit(CListCtrlEx* list, const std::vector<int>& weights);

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
    std::vector<int> m_fit_weights;
    bool m_fitting{ false };
};

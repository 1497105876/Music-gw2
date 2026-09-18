#pragma once
#include "TabDlg.h"
#include "StatCommon.h"

// 统计子页中间基类：统一承载全局上下文指针 + 惰性刷新。
// 子页只持有 const StatContext*（不拷贝记录集），聚合一律从 m_stat_ctx 读取。
// 列表列宽由各子页在 OnInitDialog 中一次性按设计宽度算好（照 CListenTimeStatisticsDlg 做法），
// 不随窗口缩放重算；页面/滚动行为完全复用 CTabDlg / CBaseDialog 原生机制。
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
    // 本页主控件（列表/编辑框等）：进入页签时把焦点交给它，
    // 使鼠标滚轮由控件原生处理（等价 CDialog::OnInitDialog 聚焦首控件的行为）。
    // 无主控件可返回 nullptr。（非 const：子页需返回自身成员控件地址）
    virtual CWnd* GetFocusTarget() { return nullptr; }

    const StatContext* m_stat_ctx{ nullptr };
    bool m_dirty{ true };

    // 切换到本页时若数据被标脏则补算
    virtual void OnTabEntered() override;

    DECLARE_MESSAGE_MAP()
};

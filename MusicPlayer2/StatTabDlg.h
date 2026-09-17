#pragma once
#include "TabDlg.h"
#include "StatCommon.h"

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
    const StatContext* m_stat_ctx{ nullptr };
    bool m_dirty{ true };

    // 切换到本页时若数据被标脏则补算
    virtual void OnTabEntered() override;

    DECLARE_MESSAGE_MAP()
};

#pragma once
#include "StatTabDlg.h"
#include "StatAnalysis.h"
#include "StatAiInsight.h"
#include "StatCommon.h"
#include <vector>

class CStatProfileTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatProfileTabDlg)
public:
    CStatProfileTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatProfileTabDlg();

    enum { IDD = IDD_STAT_PROFILE_DLG };

    virtual void Refresh() override;

    // 进入页签时焦点交给本页只读编辑框，滚轮由其原生滚动
    virtual CWnd* GetFocusTarget() override { return &m_text; }

protected:
    CEdit    m_text;
    StatSummary m_summary;

    // 音乐 DNA 报告（REQ-118）与按年归档回顾（REQ-120）
    DnaReport m_dna;
    std::vector<YearReview> m_yearly;
    std::vector<RetiredGem> m_gems;   // 遗珠挖掘（REQ-113，由原「流派」页迁入）

    // 自绘整页内容

    // 分区块绘制辅助


    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;

    DECLARE_MESSAGE_MAP()

private:
};

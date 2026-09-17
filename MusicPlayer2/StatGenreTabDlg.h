#pragma once
#include "StatTabDlg.h"
#include "StatCommon.h"
#include "ListCtrlEx.h"
#include <vector>

// 流派分布页（REQ-106 / REQ-115 / REQ-113 / REQ-114）：
//   上半区：环形图 + 图例（占比 > 5 类时其余合并为“其他”）；
//   中部：口味漂移叠图（最近若干季度的主导流派占比折线，无数据季度留空）；
//   底栏：遗珠挖掘（反复听却从未完整听完）+ 口味一致性（两段时间的流派余弦相似度）。
// 无任何 genre 数据时显示“暂无流派数据”提示。
class CStatGenreTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatGenreTabDlg)
public:
    CStatGenreTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatGenreTabDlg();

    enum { IDD = IDD_STAT_GENRE_DLG };

    virtual void Refresh() override;

protected:
    CListCtrlEx m_list;

    std::vector<GenreShare> m_share;              // 当前占比（已合并“其他”）
    std::vector<GenreShare> m_quarter_share;      // 漂移叠图用：每季度主导流派
    std::vector<std::wstring> m_quarter_labels;   // 每个季度标签
    int m_quarter_count{ 0 };

    std::vector<RetiredGem> m_gems;               // 遗珠（REQ-113）
    double m_similarity{ 0.0 };                   // 口味一致性（REQ-114，0~1）
    bool m_has_similarity{ false };

    void BuildData();

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;


    DECLARE_MESSAGE_MAP()
};

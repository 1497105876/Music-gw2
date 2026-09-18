#pragma once
#include "StatTabDlg.h"
#include "StatCommon.h"
#include "StatAnalysis.h"
#include "ListCtrlEx.h"
#include <vector>

// 概览页（REQ-101/104/107/109/112）：
//   四指标卡（累计时长 / 累计次数 / 完整收听率 / 活跃天数，空数据显“—”）
//   + 24 小时收听分布柱状图
//   + 播放结果分布（完播率/跳过率 + 跳过位置 4 桶）
//   + “差点就连续 x 天”提示
//   + 歌单/来源贡献
class CStatOverviewTabDlg : public CStatTabDlg
{
    DECLARE_DYNAMIC(CStatOverviewTabDlg)
public:
    CStatOverviewTabDlg(CWnd* pParent = nullptr);
    virtual ~CStatOverviewTabDlg();

    enum { IDD = IDD_STAT_OVERVIEW_DLG };
    enum { COL_ITEM = 0, COL_VALUE = 1 };

    virtual void Refresh() override;

    // 进入页签时焦点交给本页列表，滚轮由列表原生处理
    virtual CWnd* GetFocusTarget() override { return &m_list; }

protected:
    CListCtrlEx m_list;
    StatSummary m_summary;                       // 复用主对话框统一算好的汇总
    int m_hour[24]{};                            // 24 小时直方图
    std::vector<SkipBucket> m_skip;              // 跳过位置 4 桶
    std::vector<PlaylistContribution> m_playlist; // 歌单/来源贡献
    int m_streak_miss{ 0 };                      // 差点就连续的天数


    void BuildData();
    void ShowData();

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;


    DECLARE_MESSAGE_MAP()
};

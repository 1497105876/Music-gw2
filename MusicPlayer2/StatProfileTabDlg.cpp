#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatProfileTabDlg.h"
#include <algorithm>

IMPLEMENT_DYNAMIC(CStatProfileTabDlg, CStatTabDlg)

CStatProfileTabDlg::CStatProfileTabDlg(CWnd* pParent)
    : CStatTabDlg(IDD_STAT_PROFILE_DLG, pParent)
{
}

CStatProfileTabDlg::~CStatProfileTabDlg()
{
}

void CStatProfileTabDlg::DoDataExchange(CDataExchange* pDX)
{
    CStatTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_PROFILE_TEXT, m_text);
}

BEGIN_MESSAGE_MAP(CStatProfileTabDlg, CStatTabDlg)
    ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

BOOL CStatProfileTabDlg::OnInitDialog()
{
    CStatTabDlg::OnInitDialog();


    return TRUE;
}

// 全局上下文变化时重算并刷新（汇总由主对话框统一算一次，直接复用）
void CStatProfileTabDlg::Refresh()
{
    m_dirty = false;

    if (m_stat_ctx != nullptr)
        m_summary = m_stat_ctx->summary;
    else
        m_summary = StatSummary();

    // 音乐 DNA（REQ-118）与按年归档回顾（REQ-120）
    m_dna = CStatAiInsight::BuildDnaReport(m_summary);
    m_yearly.clear();
    if (m_stat_ctx != nullptr && m_stat_ctx->records != nullptr)
        m_yearly = CStatAnalysis::ComputeYearlyReviews(*m_stat_ctx->records);

    // 遗珠挖掘（REQ-113）：反复听 >=3 次却从未完整听完（原「流派」页迁入本页）
    m_gems.clear();
    if (m_stat_ctx != nullptr && m_stat_ctx->records != nullptr)
    {
        m_gems = CStatAnalysis::ComputeRetiredGems(*m_stat_ctx->records, 3);
        if (m_gems.size() > 3) m_gems.resize(3);
    }

    // 全部内容拼成纯文本填入只读编辑框（与其它界面同构，无自绘）
    std::wstring text;
    text += L"【你的音乐 DNA】\r\n";
    if (!m_dna.title.empty())
    {
        text += m_dna.title + L"\r\n";
        if (!m_dna.tags.empty())
        {
            text += L"标签：";
            for (size_t i = 0; i < m_dna.tags.size(); i++)
                text += (i ? L" / " : L"") + m_dna.tags[i];
            text += L"\r\n";
        }
        text += m_dna.text + L"\r\n";
    }
    else
        text += L"暂无\r\n";

    text += L"\r\n【你的听歌档案】\r\n";
    if (m_summary.badges.empty())
        text += L"暂无徽章\r\n";
    for (const auto& b : m_summary.badges)
        text += L"「" + b.title + L"」" + b.text + L"\r\n";

    text += L"\r\n【AI 洞察】\r\n";
    for (const auto& s : CStatAiInsight::GenerateInsights(m_summary))
        text += s + L"\r\n";

    text += L"\r\n【按年归档回顾】\r\n";
    if (m_yearly.empty())
        text += L"无记录\r\n";
    for (const auto& r : m_yearly)
        text += std::to_wstring(r.year) + L" 年你听了 " + std::to_wstring(r.count) +
                L" 首，累计 " + CStatAnalysis::FormatDuration(r.duration_sec) + L"\r\n";

    text += L"\r\n【遗珠挖掘】\r\n";
    if (m_gems.empty())
        text += L"暂无遗珠\r\n";
    for (const auto& g : m_gems)
        text += L"「" + g.title + L"」听了 " + std::to_wstring(g.count) + L" 次却没听完\r\n";

    text += L"\r\n【深度数字】\r\n";
    wchar_t numbuf[160];
    swprintf_s(numbuf, L"日均播放 %d 首    完整收听率 %d%%    连续听歌 %d 天    最长连续 %d 天\r\n"
               "深夜占比 %d%%    反复循环 %d 首    本月新歌 %d 首    探索型 %d%%",
               m_summary.avg_plays_per_day, (int)(m_summary.completed_rate + 0.5),
               m_summary.current_streak, m_summary.longest_streak,
               m_summary.night_owl_percent, m_summary.repeat_depth,
               m_summary.new_songs_month, m_summary.explore_percent);
    text += numbuf;

    m_text.SetWindowTextW(text.c_str());

}

// 只读多行编辑框不响应鼠标滚轮：把滚轮事件转发为编辑框本身的逐行滚动
BOOL CStatProfileTabDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (m_text.GetSafeHwnd() != nullptr)
    {
        int lines = zDelta;
        if (lines < 0) lines = -lines;
        lines = lines * 3 / 120;        // 120 == WHEEL_DELTA
        if (lines < 1) lines = 1;

        UINT sb = (zDelta > 0) ? SB_LINEUP : SB_LINEDOWN;
        for (int i = 0; i < lines; i++)
            m_text.SendMessage(WM_VSCROLL, MAKEWPARAM(sb, 0), 0);
        return TRUE;
    }
    return CStatTabDlg::OnMouseWheel(nFlags, zDelta, pt);
}

// 根据控件宽度估算整页内容高度（与 DrawProfile 的布局保持一致）
// 分区标题：左侧竖条 + 文字
// 听歌档案徽章：圆角色块 + 徽章名 + 说明
// 音乐 DNA 报告：大标题 + 标签 + 一段描述（模板 + 数据插槽）
// AI 洞察列表：每条一个小圆点 + 自然语言段落
// 按年归档回顾（REQ-120）：每行 “YYYY 年你听了 …”；无数据年份不列出
// 整页绘制：头部大数字 → 音乐 DNA → 听歌档案徽章 → AI 洞察 → 深度数字 → 年度回顾
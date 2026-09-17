#pragma once
#include <string>
#include <vector>

struct StatSummary;
struct GenreShare;

// 洞察卡片：一条带分类标识的自然语言结论（REQ-118）
struct Insight
{
    std::wstring key;        // 分类标识（如 trend / taste / anomaly / report）
    std::wstring title;      // 卡片标题
    std::wstring text;       // 正文
    int          target_tab{ -1 };  // 点击定位到的子页索引（-1 = 不跳转）
};

// 音乐 DNA 报告：标题 + 标签 + 一整段描述（模板 + 数据插槽，本地轻随机化）
struct DnaReport
{
    std::wstring title;             // 如“深夜电子系通勤者”
    std::vector<std::wstring> tags; // 如 {深夜型, 电子系, 通勤场景}
    std::wstring text;              // 一段自然语言描述
};

// AI 风格的本地统计洞察：用规则模板从 StatSummary 生成自然语言结论，不联网、零依赖、微秒级
class CStatAiInsight
{
public:
    // 生成 5~8 条洞察（含亮点和建议），纯本地计算
    // 主要输入是 summary（已聚合好，避免重复遍历记录）
    // 保留旧接口以兼容既有调用方（StatProfileTabDlg）。
    static std::vector<std::wstring> GenerateInsights(const StatSummary& summary);

    // 生成带分类的洞察卡片（REQ-118）。target_tab 供点击定位（本批仅填值，跳转在批次 3）。
    static std::vector<Insight> GenerateInsightCards(const StatSummary& summary);

    // 构建音乐 DNA 报告（REQ-118）：模板 + 数据插槽；本地轻随机化，两次调用结果不完全相同。
    static DnaReport BuildDnaReport(const StatSummary& summary);

    // 口味漂移叙述（REQ-115）：基于季度流派占比生成一段描述；数据不足返回提示语。
    static std::vector<std::wstring> BuildDriftNarrative(const std::vector<GenreShare>& shares,
                                                         const std::vector<std::wstring>& quarter_labels);
};

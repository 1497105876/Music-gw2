#pragma once
#include <string>
#include <vector>

struct StatSummary;

// AI 风格的本地统计洞察：用规则模板从 StatSummary 生成自然语言结论，不联网、零依赖、微秒级
class CStatAiInsight
{
public:
    // 生成 5~8 条洞察（含亮点和建议），纯本地计算
    // 主要输入是 summary（已聚合好，避免重复遍历记录）
    static std::vector<std::wstring> GenerateInsights(const StatSummary& summary);
};

#pragma once
#include "StatCommon.h"
#include "StatAnalysis.h"
#include <vector>

// HTML 统计报告生成器（用户选定的"网页"路线）
// 从全局过滤器对应的 records 聚合数据，生成自包含 HTML 文件（内联 CSS/JS，离线可用），
// 然后 ShellExecute 用默认浏览器打开。
class CStatHtmlReport
{
public:
    static void GenerateAndOpen(const std::vector<PlayRecord>& records,
                                const StatSummary& summary,
                                const StatFilter& filter);
};

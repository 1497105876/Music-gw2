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

    // 从 %config%\statistics\playlog_*.jsonl 解析全部播放记录，按时间倒序返回。
    // broken_lines / failed_files 可选输出：被跳过的损坏行数、读取失败的文件数。
    static std::vector<PlayRecord> LoadRecords(int* broken_lines = nullptr, int* failed_files = nullptr);

    // 保留的报告份数（带时间戳命名，超出后清理最旧的）。设置页接入前先用常量。
    static const int kKeepReportCount = 10;
};

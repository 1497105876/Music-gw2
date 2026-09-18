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

    // 网页报告独立入口：自行加载 statistics 目录下的全部播放记录
    // （全量、无时间过滤），聚合后生成并打开 HTML 报告。供主菜单直接调用。
    static void GenerateAndOpenWebReport();

private:
    // 从 %config%\statistics\playlog_*.jsonl 解析全部播放记录，按时间倒序返回
    static std::vector<PlayRecord> LoadRecords();
};

#pragma once
#include <string>
#include <vector>

// 统计口径版本与变更记录管理
// 变更记录落盘 config_dir/statistics/schema_changelog.txt（UTF-8，每行一条）。
// 目录/文件不可写时仅降级（不崩溃、不抛异常）。
class CStatMeta
{
public:
    // 当前口径版本号
    static int GetSchemaVersion();

    // 读取全部变更记录（文件不存在时写入当前版本基线记录后再读）
    static std::vector<std::wstring> GetChangelog();

    // 追加一条变更记录（自动带上当前版本号与日期）
    static void AppendChangelog(const std::wstring& note);
};

#include "stdafx.h"
#include "StatMeta.h"
#include "MusicPlayer2.h"
#include <ctime>
#include <fstream>

namespace
{
    // 当前口径版本号（口径调整时必须递增，并追加一条变更记录）
    const int kSchemaVersion = 2;

    // 计算统计目录：config_dir 下的 statistics 子目录
    std::wstring GetStatsDir()
    {
        return theApp.m_config_dir + L"statistics\\";
    }

    std::wstring GetChangelogPath()
    {
        return GetStatsDir() + L"schema_changelog.txt";
    }

    std::string WstrToUtf8(const std::wstring& wstr)
    {
        if (wstr.empty()) return std::string();
        int len = ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return std::string();
        std::string result(len - 1, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
        return result;
    }

    std::wstring Utf8ToWstr(const std::string& str)
    {
        if (str.empty()) return std::wstring();
        int len = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (len <= 0) return std::wstring();
        std::wstring result(len - 1, L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
        return result;
    }

    std::wstring GetToday()
    {
        time_t now = time(nullptr);
        struct tm tm_buf;
        localtime_s(&tm_buf, &now);
        wchar_t buf[16];
        swprintf_s(buf, L"%04d-%02d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
        return buf;
    }

    // 首条基线记录：当前版本的口径说明（v2：统一 15 秒口径）
    std::wstring BuildBaselineLine()
    {
        return L"v2 | " + GetToday() + L" | 统一 15 秒口径，时段统计纳入 <15 秒过滤";
    }

    // 追加一行（UTF-8）。目录/文件不可写时静默降级。
    void AppendLine(const std::wstring& line)
    {
        std::ofstream ofs(GetChangelogPath(), std::ios::app | std::ios::binary);
        if (!ofs.is_open()) return;     // 目录不可写：降级，不崩溃
        std::string utf8 = WstrToUtf8(line) + "\n";
        ofs.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        ofs.flush();
        ofs.close();
    }

    // 保证记录文件存在（首次运行写入基线）；幂等
    void EnsureBaseline()
    {
        ::CreateDirectory(GetStatsDir().c_str(), nullptr);
        std::ifstream ifs(GetChangelogPath(), std::ios::binary);
        if (ifs.is_open())
        {
            // 文件存在但为空，也补一条基线
            ifs.seekg(0, std::ios::end);
            bool empty = (ifs.tellg() == 0);
            ifs.close();
            if (!empty) return;
        }
        AppendLine(BuildBaselineLine());
    }
}

int CStatMeta::GetSchemaVersion()
{
    return kSchemaVersion;
}

std::vector<std::wstring> CStatMeta::GetChangelog()
{
    std::vector<std::wstring> result;
    EnsureBaseline();

    std::ifstream ifs(GetChangelogPath(), std::ios::binary);
    if (!ifs.is_open()) return result;

    std::string line;
    while (std::getline(ifs, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty()) continue;
        result.push_back(Utf8ToWstr(line));
    }
    ifs.close();
    return result;
}

void CStatMeta::AppendChangelog(const std::wstring& note)
{
    EnsureBaseline();
    wchar_t head[16];
    swprintf_s(head, L"v%d", kSchemaVersion);
    std::wstring line = std::wstring(head) + L" | " + GetToday() + L" | " + note;
    AppendLine(line);
}

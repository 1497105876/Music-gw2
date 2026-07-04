// PlayStatistics.cpp: 播放统计功能实现
//

#include "stdafx.h"
#include "PlayStatistics.h"
#include "Common.h"
#include <ctime>
#include <sstream>
#include <fstream>
#include <iomanip>

// JSON 字符串转义（宽字符→UTF-8后转义）
std::string PlayRecord::ToJson() const
{
    auto wstr_to_utf8 = [](const std::wstring& wstr) -> std::string {
        if (wstr.empty()) return std::string();
        int len = ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return std::string();
        std::string result(len - 1, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
        return result;
    };

    auto escape_json = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                }
                else
                {
                    out += c;
                }
                break;
            }
        }
        return out;
    };

    std::ostringstream oss;
    oss << "{";
    oss << "\"file_path\":\"" << escape_json(wstr_to_utf8(file_path)) << "\"";
    oss << ",\"title\":\"" << escape_json(wstr_to_utf8(title)) << "\"";
    oss << ",\"artist\":\"" << escape_json(wstr_to_utf8(artist)) << "\"";
    oss << ",\"album\":\"" << escape_json(wstr_to_utf8(album)) << "\"";
    oss << ",\"genre\":\"" << escape_json(wstr_to_utf8(genre)) << "\"";
    oss << ",\"played_at\":\"" << escape_json(wstr_to_utf8(played_at)) << "\"";
    oss << ",\"play_duration_sec\":" << play_duration_sec;
    oss << ",\"song_length_sec\":" << song_length_sec;
    oss << ",\"finish_reason\":" << static_cast<int>(finish_reason);
    oss << ",\"volume\":" << volume;
    oss << ",\"was_shuffled\":" << (was_shuffled ? "true" : "false");
    oss << ",\"playlist_source\":\"" << escape_json(wstr_to_utf8(playlist_source)) << "\"";
    oss << ",\"bitrate\":" << bitrate;
    oss << ",\"sample_rate\":" << sample_rate;
    oss << ",\"channels\":" << channels;
    oss << "}";
    return oss.str();
}

CPlayStatistics& CPlayStatistics::GetInstance()
{
    static CPlayStatistics instance;
    return instance;
}

CPlayStatistics::CPlayStatistics()
{
}

void CPlayStatistics::Init(const std::wstring& config_dir)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats_dir = config_dir + L"statistics\\";
    // 创建统计目录
    CreateDirectory(m_stats_dir.c_str(), nullptr);
    m_initialized = true;
}

std::wstring CPlayStatistics::GetNowISO8601() const
{
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_s(&tm_buf, &now);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02d",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return buf;
}

std::wstring CPlayStatistics::GetCurrentLogFile() const
{
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_s(&tm_buf, &now);
    wchar_t buf[32];
    swprintf_s(buf, L"playlog_%04d-%02d.jsonl", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);
    return m_stats_dir + buf;
}

void CPlayStatistics::OnSongStarted(const SongInfo& song, const std::wstring& source)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    // 如果上一首还在记录中，先写入（按跳过处理）
    if (m_recording)
    {
        m_current_record.finish_reason = PlayRecord::FinishReason::SKIPPED;
        WriteRecord(m_current_record);
        m_recording = false;
    }

    // 填充新记录
    m_current_record = PlayRecord{};
    m_current_record.file_path = song.file_path;
    m_current_record.title = song.title;
    m_current_record.artist = song.artist;
    m_current_record.album = song.album;
    m_current_record.genre = song.genre;
    m_current_record.played_at = GetNowISO8601();
    m_current_record.play_duration_sec = 0;
    m_current_record.song_length_sec = song.length().toInt();
    m_current_record.finish_reason = PlayRecord::FinishReason::COMPLETED;
    m_current_record.volume = 0; // 在 OnTick 时更新
    m_current_record.was_shuffled = false;
    m_current_record.playlist_source = source;
    m_current_record.bitrate = song.bitrate;
    m_current_record.sample_rate = song.freq;
    m_current_record.channels = song.channels;

    m_recording = true;
}

void CPlayStatistics::OnTick(int seconds_played)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_recording) return;
    m_current_record.play_duration_sec += seconds_played;
}

void CPlayStatistics::OnSongEnded(PlayRecord::FinishReason reason)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_recording) return;

    m_current_record.finish_reason = reason;
    WriteRecord(m_current_record);
    m_recording = false;
}

void CPlayStatistics::Flush()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_recording) return;
    // 程序退出时，将未写入的记录按 STOPPED 写入
    m_current_record.finish_reason = PlayRecord::FinishReason::STOPPED;
    WriteRecord(m_current_record);
    m_recording = false;
}

void CPlayStatistics::WriteRecord(const PlayRecord& record)
{
    if (m_stats_dir.empty()) return;

    std::wstring file_path = GetCurrentLogFile();
    std::string json = record.ToJson();
    json += "\n";

    // 以追加模式写入，使用 UTF-8 编码
    std::ofstream ofs(file_path, std::ios::app | std::ios::binary);
    if (ofs.is_open())
    {
        ofs.write(json.data(), static_cast<std::streamsize>(json.size()));
        ofs.flush();   // 立即刷新，确保崩溃安全
        ofs.close();
    }
}

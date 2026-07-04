#pragma once
#include "SongInfo.h"
#include <string>
#include <mutex>

// 播放记录结构体
struct PlayRecord
{
    // ── 基本信息 ──
    std::wstring file_path;         // 文件路径（唯一标识）
    std::wstring title;             // 标题
    std::wstring artist;            // 艺术家
    std::wstring album;             // 专辑
    std::wstring genre;             // 流派

    // ── 时间相关 ──
    std::wstring played_at;         // 播放开始时间 ISO 8601 (2026-07-04T13:25:00)
    int play_duration_sec{};        // 实际播放时长（秒）
    int song_length_sec{};          // 歌曲总长度（秒）

    // ── 播放结果 ──
    enum class FinishReason : int {
        COMPLETED = 0,              // 自然播完
        SKIPPED = 1,                // 手动切歌（下一首/上一首）
        STOPPED = 2,                // 停止播放
        PLAY_ERROR = 3,             // 播放出错
    };
    FinishReason finish_reason{};

    // ── 上下文 ──
    int volume{};                   // 播放时的音量
    bool was_shuffled{};            // 是否随机播放
    std::wstring playlist_source;   // 播放来源（文件夹名/播放列表名/媒体库类型）

    // ── 音频属性 ──
    int bitrate{};                  // 比特率
    int sample_rate{};              // 采样率
    int channels{};                 // 声道数

    // 序列化为 JSON 字符串（单行，无换行）
    std::string ToJson() const;
};

// 播放统计管理类（单例）
class CPlayStatistics
{
public:
    static CPlayStatistics& GetInstance();

    // 初始化，创建 statistics 目录
    void Init(const std::wstring& config_dir);

    // 歌曲开始播放时调用
    void OnSongStarted(const SongInfo& song, const std::wstring& source);

    // 每秒计时器调用（seconds_played 为本次累计播放秒数增量）
    void OnTick(int seconds_played);

    // 歌曲结束/切换时调用
    void OnSongEnded(PlayRecord::FinishReason reason);

    // 应用退出时调用
    void Flush();

    // 当前是否正在记录
    bool IsRecording() const { return m_recording; }

private:
    CPlayStatistics();

    void WriteRecord(const PlayRecord& record);
    std::wstring GetCurrentLogFile() const;   // 按月份生成文件名
    std::wstring GetNowISO8601() const;
    static std::wstring JsonEscape(const std::wstring& str);

    PlayRecord m_current_record;
    bool m_recording{};
    bool m_initialized{};
    std::mutex m_mutex;
    std::wstring m_stats_dir;
};

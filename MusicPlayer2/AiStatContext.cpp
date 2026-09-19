// AiStatContext.cpp：AI 对话的喂料层

#include "stdafx.h"
#include "AiStatContext.h"
#include "StatAnalysis.h"
#include <algorithm>

namespace
{
    std::wstring Num(int v)
    {
        return std::to_wstring(v);
    }

    std::wstring Pct(double v)
    {
        wchar_t b[32];
        swprintf_s(b, L"%.1f%%", v);
        return b;
    }

    std::wstring Duration(int sec)
    {
        return CStatAnalysis::FormatDuration(sec);
    }

    std::wstring DateText(int ymd)
    {
        return ymd > 0 ? CStatAnalysis::FormatYmd(ymd) : L"未知";
    }

    // 曲目标题（关掉「发送歌名」时只给一个不带名字的说法）
    std::wstring SongLabel(const std::wstring& title, bool allow_meta)
    {
        if (allow_meta && !title.empty())
            return title;
        return L"（曲名已隐藏）";
    }

    std::wstring ArtistLabel(const std::wstring& artist, bool allow_meta)
    {
        if (!allow_meta)
            return L"未知歌手";
        return artist.empty() ? L"未知歌手" : artist;
    }

    const wchar_t* ReasonText(PlayRecord::FinishReason r)
    {
        switch (r)
        {
        case PlayRecord::FinishReason::COMPLETED:  return L"播完";
        case PlayRecord::FinishReason::SKIPPED:    return L"跳过";
        case PlayRecord::FinishReason::STOPPED:    return L"停止";
        default:                                   return L"出错";
        }
    }

    // 模型档要的聚合值，一律按「全部记录」现算一遍 —— 新写进来的播放记录要能跟上
    void EnsureAggregates(const AiStatSnapshot& s, StatSummary& sum, FinishBreakdown& fin, int hour[24])
    {
        if (s.all_records != nullptr && !s.all_records->empty())
        {
            sum = CStatAnalysis::ComputeSummary(*s.all_records);
            fin = CStatAnalysis::ComputeFinishBreakdown(*s.all_records);
            CStatAnalysis::ComputeHourHistogram(*s.all_records, hour);
        }
        else
        {
            if (s.summary != nullptr) sum = *s.summary;
            if (s.finish != nullptr) fin = *s.finish;
            for (int i = 0; i < 24; ++i)
                hour[i] = (s.hour_hist != nullptr) ? s.hour_hist[i] : 0;
        }
    }
}

namespace AiStatContext
{
    std::wstring BuildSummaryText(const AiStatSnapshot& s, bool allow_song_meta)
    {
        if (!s.Valid() || s.all_records == nullptr || s.all_records->empty())
            return L"（这段时间里没有可统计的播放记录）";

        StatSummary sum;
        FinishBreakdown fin;
        int hour[24]{};
        EnsureAggregates(s, sum, fin, hour);

        std::wstring t;
        auto Line = [&t](const std::wstring& line) { t += L"- " + line + L"\n"; };

        Line(L"数据范围：全部记录，从 " + DateText(s.first_ymd) + L" 到 " + DateText(s.last_ymd));
        Line(L"计入统计：" + Num(fin.total) + L" 次播放 / " + Duration(sum.total_duration_sec) +
             L" / " + Num(sum.total_songs) + L" 首曲目 / 活跃 " + Num(sum.active_days) + L" 天");
        if (fin.total > 0)
        {
            Line(L"完播率 " + Pct(sum.completed_rate) + L"，跳过率 " + Pct(sum.skip_rate) +
                 L"，平均单次 " + Duration(fin.total > 0 ? sum.total_duration_sec / fin.total : 0));
        }

        int peak_hour = -1, peak_cnt = 0;
        for (int h = 0; h < 24; ++h)
        {
            if (hour[h] > peak_cnt) { peak_cnt = hour[h]; peak_hour = h; }
        }
        if (peak_hour >= 0)
        {
            Line(L"最活跃时段 " + Num(peak_hour) + L":00-" + Num(peak_hour) + L":59（" + Num(peak_cnt) + L" 次）");
            std::wstring dist;
            for (int h = 0; h < 24; ++h)
            {
                if (hour[h] == 0) continue;
                dist += Num(h) + L"点" + Num(hour[h]) + L" ";
                if (dist.size() > 200) break;
            }
            Line(L"时段分布：" + dist);
        }
        if (sum.night_owl_percent > 0)
            Line(L"深夜（0-6 点）占 " + Num(sum.night_owl_percent) + L"%，周末占 " + Num(sum.weekend_percent) + L"%");
        Line(L"连续听歌：当前 " + Num(sum.current_streak) + L" 天 / 最长 " + Num(sum.longest_streak) + L" 天");

        // Top 榜
        std::vector<ArtistRankItem> artists;
        std::vector<AlbumRankItem> albums;
        std::vector<SongRankItem> songs;
        if (s.all_records != nullptr)
        {
            artists = CStatAnalysis::ComputeArtistRank(*s.all_records, 5);
            albums = CStatAnalysis::ComputeAlbumRank(*s.all_records, 5);
            songs = CStatAnalysis::ComputeSongRank(*s.all_records, 5);
        }
        if (!artists.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < artists.size() && i < 5; ++i)
            {
                if (i > 0) line += L" / ";
                line += ArtistLabel(artists[i].artist, allow_song_meta) + L" " + Duration(artists[i].duration_sec);
            }
            Line(L"Top5 歌手：" + line);
        }
        if (!albums.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < albums.size() && i < 5; ++i)
            {
                if (i > 0) line += L" / ";
                line += (allow_song_meta ? albums[i].album : L"（专辑名已隐藏）") + L" " + Duration(albums[i].duration_sec);
            }
            Line(L"Top5 专辑：" + line);
        }
        if (!songs.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < songs.size() && i < 5; ++i)
            {
                if (i > 0) line += L" / ";
                line += SongLabel(songs[i].title, allow_song_meta) + L"（" + Num(songs[i].count) + L" 次）";
            }
            Line(L"Top5 曲目：" + line);
        }

        Line(L"行为分解：播完 " + Num(fin.completed) + L" / 跳过 " + Num(fin.skipped) +
             L" / 停止 " + Num(fin.stopped) + L" / 出错 " + Num(fin.errored));

        std::vector<RetiredGem> gems = CStatAnalysis::ComputeRetiredGems(*s.all_records, 3);
        if (!gems.empty())
        {
            std::wstring line;
            for (size_t i = 0; i < gems.size() && i < 5; ++i)
            {
                if (i > 0) line += L" / ";
                line += SongLabel(gems[i].title, allow_song_meta) + L"（听了 " + Num(gems[i].count) + L" 遍）";
            }
            Line(L"遗珠（反复听却没听完）：" + line);
        }

        Line(L"只听过 1 次的曲子 " + Num(sum.one_hit_wonders) + L" 首，听过 5 次以上的 " + Num(sum.repeat_depth) + L" 首");

        return t;
    }

    std::wstring BuildRawRecordsText(const AiStatSnapshot& s, int max_rows, bool allow_song_meta)
    {
        if (s.all_records == nullptr || s.all_records->empty())
            return L"";

        std::wstring t;
        t += L"## 原始播放记录（最多 " + Num(max_rows) + L" 条，时间倒序；文件路径已剔除）\n";
        t += L"格式：时间 | 标题 | 歌手 | 本次播放 | 结果\n";

        int shown = 0;
        for (const auto& r : *s.all_records)
        {
            if (shown >= max_rows)
                break;
            std::wstring played = r.played_at.size() >= 16 ? r.played_at.substr(0, 16) : r.played_at;
            std::replace(played.begin(), played.end(), L'T', L' ');
            t += played + L" | " + SongLabel(r.title, allow_song_meta) +
                 L" | " + ArtistLabel(r.artist, allow_song_meta) +
                 L" | " + Duration(r.play_duration_sec) +
                 L" | " + ReasonText(r.finish_reason) + L"\n";
            ++shown;
        }
        const int total = static_cast<int>(s.all_records->size());
        if (total > shown)
            t += L"（还有 " + Num(total - shown) + L" 条未列出）\n";
        return t;
    }

    std::wstring BuildSourceText(const AiStatSnapshot& s, const std::wstring& question)
    {
        std::wstring src;
        bool hit_artist = question.find(L"歌手") != std::wstring::npos || question.find(L"谁") != std::wstring::npos;
        bool hit_time = question.find(L"时段") != std::wstring::npos || question.find(L"几点") != std::wstring::npos ||
                        question.find(L"什么时候") != std::wstring::npos || question.find(L"晚上") != std::wstring::npos ||
                        question.find(L"深夜") != std::wstring::npos;
        bool hit_behavior = question.find(L"跳过") != std::wstring::npos || question.find(L"切歌") != std::wstring::npos ||
                            question.find(L"完播") != std::wstring::npos || question.find(L"没听完") != std::wstring::npos ||
                            question.find(L"反复") != std::wstring::npos;
        bool hit_change = question.find(L"变化") != std::wstring::npos || question.find(L"比") != std::wstring::npos ||
                          question.find(L"趋势") != std::wstring::npos;

        if (hit_artist) src += L"Top5 歌手";
        if (hit_time) src += (src.empty() ? L"" : L" / ") + std::wstring(L"时段分布");
        if (hit_behavior) src += (src.empty() ? L"" : L" / ") + std::wstring(L"行为分解 / 遗珠");
        if (hit_change) src += (src.empty() ? L"" : L" / ") + std::wstring(L"周期对比");
        if (src.empty()) src = L"总览指标 / Top 榜";
        return src;
    }

    // ── 本地档的「事实清单」 ──
    //
    // 以前本地档是「关键词命中就返回某一条、一条都没命中就甩总览」，所以动不动就答非所问。
    // 现在改成：**先把数据算成一堆现成的人话**（每条都带具体数字，有的还带一句判断），
    // 再按问题挑最相关的几条拼起来。好处是问什么都能答上，也不会拿无关内容硬凑。
    std::vector<LocalFact> BuildLocalFacts(const AiStatSnapshot& s, bool allow_song_meta)
    {
        std::vector<LocalFact> f;

        StatSummary sum;
        FinishBreakdown fin;
        int hour[24]{};
        EnsureAggregates(s, sum, fin, hour);

        // 总览
        {
            std::wstring t = L"这段时间一共听了 " + Num(fin.total) + L" 次、" +
                Duration(sum.total_duration_sec) + L"，涉及 " + Num(sum.total_songs) +
                L" 首曲子，活跃 " + Num(sum.active_days) + L" 天。";
            f.push_back({ t, { L"总", L"概", L"多少", L"统计", L"数据", L"次数", L"时长", L"整体" }, 60 });
        }

        // 歌手
        {
            std::vector<ArtistRankItem> r = CStatAnalysis::ComputeArtistRank(*s.all_records, 3);
            if (!r.empty())
            {
                std::wstring t = L"听得最多的歌手是 " + ArtistLabel(r[0].artist, allow_song_meta) +
                    L"，" + Num(r[0].count) + L" 次、" + Duration(r[0].duration_sec) + L"。";
                if (r.size() > 1 && r[1].count > 0)
                {
                    if (r[0].count >= r[1].count * 2)
                        t += L"比第二名「" + ArtistLabel(r[1].artist, allow_song_meta) +
                             L"」多出一倍还多，听得相当集中。";
                    else
                        t += L"第二名是 " + ArtistLabel(r[1].artist, allow_song_meta) +
                             L"（" + Num(r[1].count) + L" 次），咬得挺紧。";
                }
                f.push_back({ t, { L"歌手", L"谁", L"最爱", L"喜欢", L"唱" }, 85 });
            }
        }

        // 专辑
        {
            std::vector<AlbumRankItem> r = CStatAnalysis::ComputeAlbumRank(*s.all_records, 3);
            if (!r.empty())
            {
                std::wstring t = L"听得最多的专辑是《" + (allow_song_meta ? r[0].album : L"已隐藏") +
                    L"》，" + Num(r[0].count) + L" 次、" + Duration(r[0].duration_sec) + L"。";
                f.push_back({ t, { L"专辑", L"唱片" }, 70 });
            }
        }

        // 曲目
        {
            std::vector<SongRankItem> r = CStatAnalysis::ComputeSongRank(*s.all_records, 3);
            if (!r.empty())
            {
                std::wstring t = L"播得最多的是《" + SongLabel(r[0].title, allow_song_meta) +
                    L"》，" + Num(r[0].count) + L" 次、" + Duration(r[0].duration_sec) + L"。";
                if (r.size() > 1)
                    t += L"紧随其后的是《" + SongLabel(r[1].title, allow_song_meta) +
                         L"》（" + Num(r[1].count) + L" 次）。";
                f.push_back({ t, { L"曲目", L"哪首", L"歌名", L"单曲", L"最常听", L"最多" }, 80 });
            }
        }

        // 时段
        {
            int peak = -1, peak_cnt = 0;
            for (int h = 0; h < 24; ++h)
                if (hour[h] > peak_cnt) { peak_cnt = hour[h]; peak = h; }
            if (peak >= 0 && peak_cnt > 0)
            {
                std::wstring t = L"最常在 " + Num(peak) + L":00-" + Num(peak) + L":59 听，这一段有 " +
                    Num(peak_cnt) + L" 次。";
                if (sum.night_owl_percent > 0)
                    t += L"深夜（0-6 点）占了全部时长的 " + Pct(sum.night_owl_percent) + L"。";
                if (sum.weekend_percent > 0)
                    t += L"周末贡献了 " + Pct(sum.weekend_percent) + L" 的播放次数。";
                f.push_back({ t, { L"时段", L"几点", L"什么时候", L"晚上", L"深夜", L"凌晨", L"周末", L"白天" }, 75 });
            }
        }

        // 完播 / 跳过
        {
            if (fin.total > 0)
            {
                std::wstring t = L"完整听完 " + Num(fin.completed) + L" 次（" +
                    Pct(fin.completed * 100.0 / fin.total) + L"），中途切走 " + Num(fin.skipped) +
                    L" 次（" + Pct(fin.skipped * 100.0 / fin.total) + L"）。";
                if (sum.completed_rate >= 70.0)
                    t += L"完播率挺高，听歌不怎么跳。";
                else if (sum.skip_rate >= 40.0)
                    t += L"跳过偏多，歌单里可能有几首不太合口味。";
                f.push_back({ t, { L"完播", L"跳过", L"切歌", L"没听完", L"听完整", L"听完" }, 70 });
            }
        }

        // 遗珠
        {
            std::vector<RetiredGem> g = CStatAnalysis::ComputeRetiredGems(*s.all_records, 3);
            if (!g.empty())
            {
                std::wstring t = L"有 " + Num(static_cast<int>(g.size())) +
                    L" 首是反复点开、却一次都没听完的：";
                for (size_t i = 0; i < g.size() && i < 5; ++i)
                    t += L"\n· " + SongLabel(g[i].title, allow_song_meta) + L"（点开 " +
                         Num(g[i].count) + L" 遍）";
                f.push_back({ t, { L"遗珠", L"没听完", L"反复", L"可惜", L"浪费" }, 65 });
            }
        }

        // 连续天数
        {
            std::wstring t = L"已经连续 " + Num(sum.current_streak) + L" 天听歌，最长纪录 " +
                Num(sum.longest_streak) + L" 天。";
            const int miss = CStatAnalysis::ComputeStreakMiss(*s.all_records);
            if (miss > 0)
                t += L"中间断过最长的一段是 " + Num(miss) + L" 天。";
            f.push_back({ t, { L"连续", L"纪录", L"记录", L"天数", L"坚持", L"断了" }, 65 });
        }

        // 周期对比
        {
            StatFilter all_filter;
            all_filter.preset = RangePreset::All;
            PeriodComparison pc = CStatAnalysis::ComputePeriodComparison(*s.all_records, all_filter);
            if (pc.has_previous)
            {
                std::wstring t;
                if (pc.count_delta > 0)
                    t = L"比上一个周期多听了 " + Num(pc.count_delta) + L" 次（" +
                        Pct(pc.count_delta_percent) + L"）。";
                else if (pc.count_delta < 0)
                    t = L"比上一个周期少听了 " + Num(-pc.count_delta) + L" 次（" +
                        Pct(pc.count_delta_percent) + L"）。";
                else
                    t = L"跟上一个周期听得一样多。";
                f.push_back({ t, { L"变化", L"比", L"趋势", L"多了", L"少了", L"对比", L"最近" }, 60 });
            }
        }

        // 新歌发现
        {
            std::vector<PeriodBucket> news = CStatAnalysis::ComputeNewSongTrend(*s.all_records);
            if (!news.empty())
            {
                const PeriodBucket* best = &news.front();
                for (const auto& b : news)
                    if (b.count > best->count) best = &b;
                if (best->count > 0)
                {
                    std::wstring t = L"新歌听得最多的是 " + best->label + L"，那个月新听了 " +
                        Num(best->count) + L" 首。";
                    f.push_back({ t, { L"新歌", L"新听", L"刚听", L"发现" }, 55 });
                }
            }
        }

        return f;
    }

    std::wstring BuildLocalAnswer(const AiStatSnapshot& s, const std::wstring& question, bool allow_song_meta)
    {
        if (!s.Valid() || s.all_records == nullptr || s.all_records->empty())
            return L"这段时间里还没有可统计的播放记录。先去听几首歌，回来再问。";

        const std::vector<LocalFact> facts = BuildLocalFacts(s, allow_song_meta);
        if (facts.empty())
            return L"数据还太少，暂时没什么可说的。多听几首再来问。";

        // 按问题里的关键词打分：命中词越多分越高，同分时重要的排前面
        struct Scored
        {
            const LocalFact* f{ nullptr };
            int score{ 0 };
        };
        std::vector<Scored> hits;
        hits.reserve(facts.size());
        for (const auto& x : facts)
        {
            int sc = 0;
            for (const auto& k : x.keys)
            {
                if (question.find(k) != std::wstring::npos)
                    sc += 10;
            }
            if (sc > 0)
                sc += x.weight / 10;
            hits.push_back({ &x, sc });
        }
        std::stable_sort(hits.begin(), hits.end(),
            [](const Scored& a, const Scored& b) { return a.score > b.score; });

        if (hits[0].score > 0)
        {
            // 命中了：把最相关的几条（最多 3 条）连起来说，信息量比单条大
            std::wstring a;
            for (size_t i = 0; i < hits.size() && i < 3; ++i)
            {
                if (hits[i].score <= 0)
                    break;
                if (!a.empty())
                    a += L"\n\n";
                a += hits[i].f->text;
            }
            return a;
        }

        // 一条都没命中：别硬塞无关内容，先说清没对上，再给最有价值的几条
        std::wstring a = L"这句话我没找到能对上的角度，先把最要紧的几条给你：";
        {
            std::vector<const LocalFact*> by_weight;
            by_weight.reserve(facts.size());
            for (const auto& x : facts)
                by_weight.push_back(&x);
            std::stable_sort(by_weight.begin(), by_weight.end(),
                [](const LocalFact* x, const LocalFact* y) { return x->weight > y->weight; });
            for (size_t i = 0; i < by_weight.size() && i < 3; ++i)
                a += L"\n· " + by_weight[i]->text;
        }
        a += L"\n\n换个说法再问也行，或者直接点下面的快捷提问。";
        return a;
    }

    const std::vector<std::wstring>& QuickQuestionPool()
    {
        static const std::vector<std::wstring> pool = {
            L"我最近听得怎么样？",
            L"听得最多的歌手是谁",
            L"有什么歌我反复听却没听完",
            L"我的听歌习惯有什么变化",
            L"哪个时段我最爱听歌",
            L"这个月比上个月听得多吗",
            L"我有没有连续听歌的纪录",
            L"新歌我听了多少首",
            L"我听得最久的一天是哪天",
            L"我的完播率算高还是低",
            L"听得最多的专辑是哪张",
            L"我周末听歌多吗"
        };
        return pool;
    }
}

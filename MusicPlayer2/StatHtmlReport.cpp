#include "stdafx.h"
#include "StatHtmlReport.h"
#include "MusicPlayer2.h"
#include "StatCommon.h"
#include <shellapi.h>
#include <fstream>
#include <algorithm>

// ── 工具 ──
static std::wstring JsonStr(const std::wstring& s)
{
    std::wstring out;
    for (wchar_t ch : s)
    {
        switch (ch)
        {
        case L'"':  out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n";  break;
        case L'\r': break;
        default:
            if (static_cast<unsigned>(ch) < 0x20) { /* 跳过控制字符 */ }
            else out += ch;
        }
    }
    return out;
}

static std::wstring YmdToStr(int ymd)
{
    if (ymd <= 0) return std::wstring();
    wchar_t buf[16];
    swprintf_s(buf, L"%04d-%02d-%02d", ymd / 10000, (ymd / 100) % 100, ymd % 100);
    return buf;
}

// ── JSON 数据构建 ──
static std::wstring BuildJson(const std::vector<PlayRecord>& records,
                         const StatSummary& s,
                         const StatFilter& filter)
{
    std::wstring j;
    j += L"{";

    // summary
    j += L"\"total\":" + std::to_wstring(s.total_count) + L",";
    j += L"\"duration\":" + std::to_wstring(s.total_duration_sec) + L",";
    j += L"\"active_days\":" + std::to_wstring(s.active_days) + L",";
    j += L"\"completed_rate\":" + std::to_wstring(static_cast<int>(s.completed_rate + 0.5)) + L",";
    j += L"\"skip_rate\":" + std::to_wstring(static_cast<int>(s.skip_rate + 0.5)) + L",";
    j += L"\"avg_completion\":" + std::to_wstring(static_cast<int>(s.avg_completion + 0.5)) + L",";
    j += L"\"streak\":" + std::to_wstring(s.current_streak) + L",";
    j += L"\"longest_streak\":" + std::to_wstring(s.longest_streak) + L",";
    j += L"\"night_pct\":" + std::to_wstring(s.night_owl_percent) + L",";
    j += L"\"weekend_pct\":" + std::to_wstring(s.weekend_percent) + L",";
    j += L"\"new_songs\":" + std::to_wstring(s.new_songs_month) + L",";
    j += L"\"repeat\":" + std::to_wstring(s.repeat_depth) + L",";
    j += L"\"explore\":" + std::to_wstring(s.explore_percent) + L",";

    // top
    j += L"\"top_artist\":\"" + JsonStr(s.top_artist) + L"\",";
    j += L"\"top_artist_dur\":" + std::to_wstring(s.top_artist_sec) + L",";
    j += L"\"top_song\":\"" + JsonStr(s.top_song) + L"\",";
    j += L"\"top_song_artist\":\"" + JsonStr(s.top_song_artist) + L"\",";
    j += L"\"top_song_count\":" + std::to_wstring(s.top_song_count) + L",";

    // buckets（趋势）
    auto buckets = CStatAnalysis::ComputeBuckets(records, filter.grain);
    j += L"\"buckets\":[";
    for (size_t i = 0; i < buckets.size(); i++)
    {
        if (i) j += L",";
        j += L"{\"l\":\"" + JsonStr(buckets[i].label) + L"\",\"c\":" +
             std::to_wstring(buckets[i].count) + L",\"d\":" +
             std::to_wstring(buckets[i].duration_sec) + L"}";
    }
    j += L"],";

    // 热力图（按天）
    auto heat = CStatAnalysis::ComputeHeatmapGrid(records);
    j += L"\"heat\":[";
    for (size_t i = 0; i < heat.size(); i++)
    {
        if (i) j += L",";
        j += L"{\"d\":\"" + YmdToStr(heat[i].ymd) + L"\",\"c\":" +
             std::to_wstring(heat[i].count) + L",\"s\":" +
             std::to_wstring(heat[i].duration_sec) + L"}";
    }
    j += L"],";

    // 跳过分布
    auto skip = CStatAnalysis::ComputeSkipDistribution(records);
    j += L"\"skip\":[";
    for (size_t i = 0; i < skip.size(); i++)
    {
        if (i) j += L",";
        j += L"{\"l\":\"" + JsonStr(skip[i].label) + L"\",\"c\":" +
             std::to_wstring(skip[i].count) + L",\"p\":" +
             std::to_wstring(static_cast<int>(skip[i].percent + 0.5)) + L"}";
    }
    j += L"],";

    // 歌单贡献
    auto pl = CStatAnalysis::ComputePlaylistContribution(records);
    j += L"\"playlist\":[";
    for (size_t i = 0; i < pl.size(); i++)
    {
        if (i) j += L",";
        j += L"{\"s\":\"" + JsonStr(pl[i].source) + L"\",\"d\":" +
             std::to_wstring(pl[i].duration_sec) + L",\"p\":" +
             std::to_wstring(static_cast<int>(pl[i].percent + 0.5)) + L"}";
    }
    j += L"],";

    // 年度
    auto yearly = CStatAnalysis::ComputeYearlyReviews(records);
    j += L"\"yearly\":[";
    for (size_t i = 0; i < yearly.size(); i++)
    {
        if (i) j += L",";
        j += L"{\"y\":" + std::to_wstring(yearly[i].year) +
             L",\"c\":" + std::to_wstring(yearly[i].count) +
             L",\"d\":" + std::to_wstring(yearly[i].duration_sec) + L"}";
    }
    j += L"],";

    // 雷达
    auto radar = CStatAnalysis::ComputeRadar(s);
    j += L"\"radar\":[";
    j += L"{\"d\":\"探索\",\"v\":" + std::to_wstring(static_cast<int>(radar.explore + 0.5)) + L"},";
    j += L"{\"d\":\"专注\",\"v\":" + std::to_wstring(static_cast<int>(radar.focus + 0.5)) + L"},";
    j += L"{\"d\":\"夜行\",\"v\":" + std::to_wstring(static_cast<int>(radar.night + 0.5)) + L"},";
    j += L"{\"d\":\"专一\",\"v\":" + std::to_wstring(static_cast<int>(radar.loyalty + 0.5)) + L"},";
    j += L"{\"d\":\"新鲜\",\"v\":" + std::to_wstring(static_cast<int>(radar.fresh + 0.5)) + L"}";
    j += L"],";

    // 24 小时分布
    int hours[24] = {};
    CStatAnalysis::ComputeHourHistogram(records, hours);
    j += L"hours:[";
    for (int i = 0; i < 24; i++) { if (i) j += L","; j += std::to_wstring(hours[i]); }
    j += L"],";

    // 差点就连续
    int sm = CStatAnalysis::ComputeStreakMiss(records);
    j += L"streak_miss:" + std::to_wstring(sm) + L",";

    // 环比/同比
    auto pc = CStatAnalysis::ComputePeriodComparison(records, filter);
    j += L"comparison:{";
    j += L"has_prev:" + std::wstring(pc.has_previous ? L"true" : L"false") + L",";
    j += L"\"prev_label\":\"" + JsonStr(pc.previous.label) + L"\",";
    j += L"prev_count:" + std::to_wstring(pc.previous.count) + L",";
    j += L"delta:" + std::to_wstring(pc.count_delta) + L",";
    j += L"delta_pct:" + std::to_wstring(static_cast<int>(pc.count_delta_percent + 0.5)) + L",";
    j += L"has_ly:" + std::wstring(pc.has_last_year ? L"true" : L"false") + L",";
    j += L"\"ly_label\":\"" + JsonStr(pc.last_year.label) + L"\",";
    j += L"ly_count:" + std::to_wstring(pc.last_year.count) + L"},";

    // 新发现趋势
    auto news = CStatAnalysis::ComputeNewSongTrend(records);
    j += L"news:[";
    for (size_t i = 0; i < news.size(); i++)
    {
        if (i) j += L",";
        j += L"{\"l\":\"" + JsonStr(news[i].label) + L"\",\"c\":" + std::to_wstring(news[i].count) + L"}";
    }
    j += L"]";

    // 副标题（日期区间）
    if (filter.from_ymd > 0 && filter.to_ymd > 0)
        j += L",\"sub_title\":\"" + YmdToStr(filter.from_ymd) + L" ~ " + YmdToStr(filter.to_ymd) + L"\"";
    else
        j += L",\"sub_title\":\"全部记录\"";

    j += L"}";
    return j;
}

// ── HTML 模板（v3：拆分为多段常量，顺序拼接）──
static const wchar_t* HTML_HEAD_1 = LR"html(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>MusicPlayer2 · 播放统计报告</title>
<style>
:root{
  --bg:#fafaf8; --panel:#ffffff; --ink:#14161a; --ink-2:#4a4f57; --ink-3:#878d96;
  --hair:#d9dad5; --hair-soft:#e8e8e4; --accent:#0f6b5c; --accent-2:#128a76;
  --accent-soft:#e5efec; --app:#2f5d9e; --app-soft:#e7edf6;
  --warn:#9a6b1f; --warn-soft:#f6efdf; --bad:#94382e; --bad-soft:#f5e7e4;
  --mono:"Cascadia Mono",Consolas,"SF Mono",monospace;
  --sans:"Segoe UI","Microsoft YaHei",system-ui,sans-serif;
  --serif:"Source Han Serif SC","Noto Serif SC","SimSun",serif;
  --radius:12px; --radius-sm:8px;
  --shadow:0 1px 2px rgba(20,22,26,.04),0 8px 24px -14px rgba(20,22,26,.18);
}
*{box-sizing:border-box;margin:0;padding:0}
html{scroll-behavior:smooth}
body{background:var(--bg);color:var(--ink);font-family:var(--sans);font-size:15px;line-height:1.6;-webkit-font-smoothing:antialiased;padding:0 20px 96px}
.page{max-width:1080px;margin:0 auto}

/* Hero */
.hero{position:relative;padding:54px 0 26px;margin-bottom:6px}
.hero::before{content:"";position:absolute;left:-20px;right:-20px;top:0;height:320px;background:radial-gradient(120% 100% at 10% 0%,rgba(15,107,92,.11),rgba(15,107,92,0) 62%);pointer-events:none;z-index:-1}
.kicker{font-family:var(--mono);font-size:11.5px;letter-spacing:.16em;text-transform:uppercase;color:var(--accent);margin-bottom:12px}
.hero h1{font-family:var(--serif);font-size:40px;font-weight:600;letter-spacing:-.02em;line-height:1.15}
.hero-sub{font-family:var(--mono);font-size:14px;color:var(--ink-2);margin-top:10px}
.hero-meta{display:flex;gap:14px;align-items:center;flex-wrap:wrap;font-family:var(--mono);font-size:11.5px;color:var(--ink-3);margin-top:16px}
.dot{width:4px;height:4px;border-radius:50%;background:var(--hair)}

/* Sticky nav */
.nav{position:sticky;top:0;z-index:20;background:rgba(250,250,248,.88);backdrop-filter:blur(10px);border-bottom:1px solid var(--hair);margin:14px -20px 0;padding:0 20px}
.nav-inner{max-width:1080px;margin:0 auto;display:flex;gap:2px;overflow-x:auto;scrollbar-width:none}
.nav-inner::-webkit-scrollbar{display:none}
.nav-link{flex:none;font-size:13px;color:var(--ink-3);text-decoration:none;padding:13px 12px;border-bottom:2px solid transparent;white-space:nowrap;transition:color .15s,border-color .15s}
.nav-link:hover{color:var(--ink)}
.nav-link.active{color:var(--accent);border-bottom-color:var(--accent);font-weight:600}

/* Sections */
.section{margin-top:46px;scroll-margin-top:64px}
.sec-head{margin-bottom:16px}
.sec-head h2{font-family:var(--serif);font-size:24px;font-weight:600;letter-spacing:-.01em;display:flex;align-items:baseline;gap:10px}
.sec-head .no{font-family:var(--mono);font-size:12px;color:var(--accent);font-weight:400;letter-spacing:.08em}
.sec-desc{color:var(--ink-3);font-size:13px;margin-top:6px;max-width:72ch}

/* Cards */
.card{background:var(--panel);border:1px solid var(--hair-soft);border-radius:var(--radius);padding:18px 20px;box-shadow:var(--shadow)}
.card + .card{margin-top:14px}
.card-title{font-size:12.5px;font-weight:600;color:var(--ink-2);letter-spacing:.04em;text-transform:uppercase;margin-bottom:14px;display:flex;justify-content:space-between;align-items:center;gap:10px}
.grid-2{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:14px}
.grid-2 .card{margin-top:0}
.center{display:flex;justify-content:center}

/* KPI */
.kpi-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:14px}
.kpi{position:relative;background:var(--panel);border:1px solid var(--hair-soft);border-radius:var(--radius);padding:18px 18px 16px;box-shadow:var(--shadow);overflow:hidden}
.kpi::before{content:"";position:absolute;left:0;top:0;bottom:0;width:3px;background:var(--accent)}
.kpi.k2::before{background:var(--app)} .kpi.k3::before{background:var(--warn)} .kpi.k4::before{background:var(--bad)}
.kpi .v{font-size:30px;font-weight:700;letter-spacing:-.02em;font-variant-numeric:tabular-nums;line-height:1.1;display:flex;align-items:baseline;gap:5px}
.kpi .v small{font-size:13px;font-weight:600;color:var(--ink-3)}
.kpi .l{font-size:12.5px;color:var(--ink-2);margin-top:8px}
.kpi .h{font-size:11px;color:var(--ink-3);margin-top:3px;font-family:var(--mono)}

/* 补充指标条 */
.strip{display:grid;grid-template-columns:repeat(4,1fr);margin-top:14px;background:var(--panel);border:1px solid var(--hair-soft);border-radius:var(--radius);box-shadow:var(--shadow);overflow:hidden}
.strip .s{display:flex;align-items:baseline;gap:9px;padding:14px 18px;border-left:1px solid var(--hair-soft)}
.strip .s:first-child{border-left:0}
.strip .s b{font-size:19px;font-weight:700;font-variant-numeric:tabular-nums;letter-spacing:-.01em}
.strip .s span{font-size:12px;color:var(--ink-3)}

/* Charts */
.card svg{display:block;width:100%;height:auto;overflow:visible}

/* Heatmap */
.heat-scroll{overflow-x:auto;padding-bottom:6px}
/* 趋势图：天数多时可左右滑动 */
.trend-scroll{overflow-x:auto;overflow-y:hidden;padding-bottom:6px}
.card .trend-scroll svg{width:auto;height:auto;overflow:visible}
.heat{display:grid;grid-auto-flow:column;grid-template-rows:repeat(7,12px);grid-auto-columns:12px;gap:3px;width:max-content}
.heat i{width:12px;height:12px;border-radius:3px;background:var(--hair-soft);cursor:pointer;transition:transform .1s,outline-color .1s;outline:1px solid transparent}
.heat i:hover{transform:scale(1.28);outline:1px solid var(--ink)}
)html";
static const wchar_t* HTML_HEAD_1B = LR"html(.heat-legend{display:flex;align-items:center;gap:6px;margin-top:14px;font-size:11px;color:var(--ink-3);font-family:var(--mono)}
.heat-legend i{width:11px;height:11px;border-radius:3px;display:inline-block}
.heat-legend span{margin:0 2px}

/* Highlights */
.hl{display:flex;gap:12px;align-items:center;padding:13px 0;border-bottom:1px solid var(--hair-soft)}
.hl:last-child{border-bottom:0}
.hl .rk{font-family:var(--mono);font-size:11px;color:var(--accent);width:26px;flex:none}
.hl .tt{flex:1;min-width:0}
.hl .tt b{display:block;font-size:14px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.hl .tt span{font-size:11.5px;color:var(--ink-3)}
.hl .vv{font-family:var(--mono);font-variant-numeric:tabular-nums;font-size:13px;color:var(--ink-2);flex:none}

/* Tables */
table{width:100%;border-collapse:collapse;font-size:13px}
th{font-size:10.5px;font-weight:500;color:var(--ink-3);text-align:left;padding:8px 12px 8px 0;border-bottom:1px solid var(--hair);letter-spacing:.05em;text-transform:uppercase}
td{padding:9px 12px 9px 0;border-bottom:1px solid var(--hair-soft);color:var(--ink-2)}
tr:last-child td{border-bottom:0}
th.num,td.num{text-align:right;font-family:var(--mono);font-variant-numeric:tabular-nums}
tbody tr:hover td{background:#fcfcfb}
.empty{color:var(--ink-3);font-size:13px;padding:16px 0;text-align:center}

/* Bars */
.bar-row{display:grid;grid-template-columns:92px 1fr 92px;align-items:center;gap:12px;margin:11px 0;font-size:13px}
.bar-row .lb{color:var(--ink-2);text-align:right;font-family:var(--mono);font-size:12px}
.bar-track{display:block;background:var(--hair-soft);border-radius:6px;height:16px;overflow:hidden}
.bar-fill{display:block;height:100%;border-radius:6px;background:linear-gradient(90deg,var(--accent),var(--accent-2));min-width:2px}
.bar-fill.alt{background:linear-gradient(90deg,var(--app),#5b8fd6)}
.bar-row .vl{font-family:var(--mono);font-size:12px;color:var(--ink-3);font-variant-numeric:tabular-nums;white-space:nowrap}

/* Gauge / comparison */
.gauge{display:flex;align-items:center;gap:24px;flex-wrap:wrap}
.gauge .big{font-size:52px;font-weight:700;letter-spacing:-.03em;color:var(--accent);font-variant-numeric:tabular-nums;line-height:1}
.gauge .meta{flex:1;min-width:200px}
.gauge .meta b{font-size:14px;display:block;margin-bottom:2px}
.gauge .meta span{font-size:12px;color:var(--ink-3)}
.track{display:block;background:var(--hair-soft);border-radius:8px;height:10px;margin-top:12px;overflow:hidden}
.track i{display:block;height:100%;border-radius:8px;background:linear-gradient(90deg,var(--accent),var(--accent-2))}
.cmp-row{display:flex;align-items:center;gap:12px;padding:13px 0;border-bottom:1px solid var(--hair-soft)}
.cmp-row:last-child{border-bottom:0}
.cmp-row .k{font-size:12.5px;color:var(--ink-3);flex:none;width:104px}
.cmp-row .d{display:flex;align-items:center;gap:6px;font-size:16px;font-weight:600;font-variant-numeric:tabular-nums;flex:none;white-space:nowrap}
.cmp-row .sub{white-space:nowrap}
.up{color:var(--accent)} .down{color:var(--bad)} .flat{color:var(--ink-3)}
.cmp-row .sub{font-size:11.5px;color:var(--ink-3);font-family:var(--mono)}
.warn-note{display:flex;gap:10px;align-items:flex-start;font-size:13px;color:var(--ink-2);background:var(--warn-soft);border-radius:var(--radius-sm);padding:13px 15px;line-height:1.55}
.warn-note .ic{flex:none;margin-top:2px}

/* Tooltip */
.tip{position:fixed;background:var(--ink);color:#fff;padding:6px 10px;border-radius:6px;font-size:12px;font-family:var(--mono);pointer-events:none;opacity:0;transition:opacity .12s;z-index:60;white-space:nowrap;box-shadow:0 8px 20px -8px rgba(0,0,0,.4)}
.tip.on{opacity:1}

/* Footer */
.foot{margin-top:56px;padding-top:16px;border-top:1px solid var(--hair);font-size:11.5px;color:var(--ink-3);font-family:var(--mono);line-height:1.9}

/* Responsive */
@media (max-width:900px){
  .kpi-grid{grid-template-columns:repeat(2,1fr)}
  .grid-2{grid-template-columns:1fr}
  .hero h1{font-size:32px}
}
@media (max-width:600px){
  body{padding:0 14px 72px}
  .nav{margin:12px -14px 0;padding:0 14px}
  .hero{padding:34px 0 20px}
  .hero h1{font-size:26px}
  .hero-sub{font-size:12.5px}
  .kpi-grid{gap:10px}
  .kpi{padding:14px}
  .kpi .v{font-size:23px}
  .sec-head h2{font-size:20px}
  .bar-row{grid-template-columns:66px 1fr;row-gap:4px}
  .bar-row .vl{grid-column:2;text-align:right}
  .cmp-row{flex-wrap:wrap;gap:6px 10px}
  .strip{grid-template-columns:repeat(2,1fr)}
  .strip .s:nth-child(3){border-left:0}
  .gauge .big{font-size:40px}
}

/* Print */
@media print{
  body{padding:0;background:#fff}
  .nav{display:none}
  .hero::before{display:none}
  .card,.kpi{box-shadow:none;break-inside:avoid}
  .section{margin-top:26px}
}
</style>
)html";
static const wchar_t* HTML_HEAD_2 = LR"html(</head>
<body>
<div class="page">

<header class="hero">
  <div class="kicker">MusicPlayer2-GW · 播放统计</div>
  <h1>我的听歌报告</h1>
  <p class="hero-sub" id="sub"></p>
  <div class="hero-meta">
    <span id="gen-time"></span>
    <span class="dot"></span>
    <span>全部数据由本机统计，离线生成</span>
  </div>
</header>

<nav class="nav" id="nav">
  <div class="nav-inner">
    <a class="nav-link active" href="#sec-overview">关键指标</a>
    <a class="nav-link" href="#sec-trend">趋势</a>
    <a class="nav-link" href="#sec-hours">时段</a>
    <a class="nav-link" href="#sec-heat">热力</a>
    <a class="nav-link" href="#sec-profile">画像</a>
    <a class="nav-link" href="#sec-behavior">行为</a>
    <a class="nav-link" href="#sec-year">年度</a>
  </div>
</nav>

<section class="section" id="sec-overview">
  <div class="sec-head">
    <h2><span class="no">01</span>关键指标</h2>
    <p class="sec-desc" id="overview-desc"></p>
  </div>
  <div class="kpi-grid" id="kpi"></div>
  <div class="strip" id="kpi-extra"></div>
</section>

<section class="section" id="sec-trend">
  <div class="sec-head">
    <h2><span class="no">02</span>播放趋势</h2>
    <p class="sec-desc" id="trend-desc"></p>
  </div>
  <div class="card">
    <div class="card-title">播放次数 · 按聚合区间（左新 → 右旧）</div>
    <div class="trend-scroll" id="trend-scroll"><svg id="svg-trend" role="img" aria-label="播放趋势"></svg></div>
  </div>
  <div class="grid-2">
    <div class="card" id="comparison-box"></div>
    <div class="card">
      <div class="card-title">新发现趋势</div>
      <svg id="svg-news" role="img" aria-label="新发现趋势"></svg>
    </div>
  </div>
</section>

<section class="section" id="sec-hours">
  <div class="sec-head">
    <h2><span class="no">03</span>24 小时时段分布</h2>
    <p class="sec-desc" id="hours-desc"></p>
  </div>
  <div class="card">
    <svg id="svg-hours" role="img" aria-label="时段分布"></svg>
  </div>
</section>

<section class="section" id="sec-heat">
  <div class="sec-head">
    <h2><span class="no">04</span>每日热力图</h2>
    <p class="sec-desc" id="heat-desc"></p>
  </div>
  <div class="card">
    <div class="heat-scroll"><div class="heat" id="heatmap"></div></div>
    <div class="heat-legend" id="heat-legend"></div>
  </div>
</section>

<section class="section" id="sec-profile">
  <div class="sec-head">
    <h2><span class="no">05</span>听歌画像</h2>
    <p class="sec-desc" id="profile-desc"></p>
  </div>
  <div class="grid-2">
    <div class="card">
      <div class="card-title">五维画像</div>
      <div class="center"><svg id="svg-radar" role="img" aria-label="听歌画像雷达图"></svg></div>
    </div>
    <div class="card">
      <div class="card-title">最爱清单</div>
      <div id="profile-highlights"></div>
    </div>
    <div class="card">
      <div class="card-title">歌单 / 来源贡献</div>
      <div id="pl-table"></div>
    </div>

  </div>
</section>

<section class="section" id="sec-behavior">
  <div class="sec-head">
    <h2><span class="no">06</span>播放行为</h2>
    <p class="sec-desc" id="behavior-desc"></p>
  </div>
  <div class="card">
    <div class="card-title">跳过位置分布</div>
    <div id="skip-bars"></div>
  </div>
  <div class="card" id="streak-miss"></div>
</section>

<section class="section" id="sec-year">
  <div class="sec-head">
    <h2><span class="no">07</span>年度回顾</h2>
    <p class="sec-desc" id="year-desc"></p>
  </div>
  <div class="card">
    <svg id="svg-year" role="img" aria-label="年度回顾"></svg>
  </div>
  <div class="card">
    <div id="year-table"></div>
  </div>
</section>

<footer class="foot">
  <div>MusicPlayer2-GW · 播放统计报告 · 本页为单文件离线报告，无任何外部资源引用。</div>
  <div id="foot-note"></div>
</footer>

</div>
<div class="tip" id="tip"></div>
)html";

static const wchar_t* HTML_TAIL_1 = LR"html(<script>
"use strict";
// ══════════════════════════════════════════════════════════════════
// 数据占位符：见下方脚本首个常量赋值。运行时由 C++ GenerateAndOpen()
// 把该占位符整体替换成 BuildJson() 的输出——一个键名混用引号/裸键的
// 对象字面量（含两端花括号）。键名与 BuildJson 保持一致即可原样兼容；
// 任何字段缺失均由页面脚本兜底，不会出现 undefined / NaN。
// ══════════════════════════════════════════════════════════════════
const D=__DATA__;

// ── 通用工具（全部对缺失字段容错，不产生 undefined / NaN）──
const $ = s => (typeof document !== "undefined" && document.querySelector) ? document.querySelector(s) : null;
const $$ = s => (typeof document !== "undefined" && document.querySelectorAll) ? Array.prototype.slice.call(document.querySelectorAll(s)) : [];
function nz(v, d){ return (v === undefined || v === null || (typeof v === "number" && isNaN(v))) ? d : v; }
function num(v, d){ var n = Number(v); return isNaN(n) ? (d || 0) : n; }
function esc(s){ return String(s === undefined || s === null ? "" : s).replace(/[&<>"']/g, function(c){ return ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"})[c]; }); }
function pad2(n){ return (n < 10 ? "0" : "") + n; }
function fmt(sec){
  sec = Math.max(0, Math.round(num(sec, 0)));
  var h = Math.floor(sec / 3600), m = Math.floor(sec % 3600 / 60), s = sec % 60;
  if (h > 0) return h + " 时 " + m + " 分";
  if (m > 0) return m + " 分" + (s > 0 ? " " + s + " 秒" : "");
  return s + " 秒";
}
function fmtInt(n){ return num(n, 0).toLocaleString("en-US"); }
function clamp(v, a, b){ return Math.max(a, Math.min(b, v)); }
function isArr(v){ return Object.prototype.toString.call(v) === "[object Array]"; }
function setText(sel, t){ var el = $(sel); if (el) el.textContent = t; }
function svgW(el, pad){ var w = (el && el.parentElement && el.parentElement.clientWidth) || 760; return Math.max(w - (pad || 0), 280); }
function arrow(dir, color){
  if (dir > 0) return '<svg viewBox="0 0 10 10" width="11" height="11" aria-hidden="true"><path d="M5 1 L9 9 H1 Z" fill="' + color + '"/></svg>';
  if (dir < 0) return '<svg viewBox="0 0 10 10" width="11" height="11" aria-hidden="true"><path d="M5 9 L1 1 H9 Z" fill="' + color + '"/></svg>';
  return '<svg viewBox="0 0 10 10" width="11" height="11" aria-hidden="true"><rect x="1" y="4" width="8" height="2" rx="1" fill="' + color + '"/></svg>';
}
var tipEl = (typeof document !== "undefined" && document.getElementById) ? document.getElementById("tip") : null;
function bindTip(sel, fn){
  $$(sel).forEach(function(el){
    el.addEventListener("mousemove", function(e){
      if (!tipEl) return;
      tipEl.textContent = fn(el);
      tipEl.style.left = (e.clientX + 14) + "px";
      tipEl.style.top = (e.clientY - 34) + "px";
      tipEl.style.opacity = "1";
      if (tipEl.classList) tipEl.classList.add("on");
    });
    el.addEventListener("mouseleave", function(){ if (tipEl) { tipEl.style.opacity = "0"; if (tipEl.classList) tipEl.classList.remove("on"); } });
  });
}
// ── 图形工具：仅顶部圆角的柱形路径 ──
function roundTop(x, y, w, h, r){
  x = +x; y = +y; w = +w; h = +h; r = +r;
  h = Math.max(h, 0.5); w = Math.max(w, 0.5); r = Math.min(r, h / 2, w / 2);
  function f(n){ return (+n).toFixed(2); }
  return "M" + f(x) + "," + f(y + h) + " L" + f(x) + "," + f(y + r) + " Q" + f(x) + "," + f(y) + " " + f(x + r) + "," + f(y) +
         " L" + f(x + w - r) + "," + f(y) + " Q" + f(x + w) + "," + f(y) + " " + f(x + w) + "," + f(y + r) + " L" + f(x + w) + "," + f(y + h) + " Z";
}
function ringArc(cx, cy, R, r, a0, a1){
  var large = (a1 - a0) > Math.PI ? 1 : 0;
  var x0 = cx + R * Math.cos(a0), y0 = cy + R * Math.sin(a0);
  var x1 = cx + R * Math.cos(a1), y1 = cy + R * Math.sin(a1);
  var x2 = cx + r * Math.cos(a1), y2 = cy + r * Math.sin(a1);
  var x3 = cx + r * Math.cos(a0), y3 = cy + r * Math.sin(a0);
  return "M" + x0.toFixed(2) + "," + y0.toFixed(2) + " A" + R + "," + R + " 0 " + large + " 1 " + x1.toFixed(2) + "," + y1.toFixed(2) +
         " L" + x2.toFixed(2) + "," + y2.toFixed(2) + " A" + r + "," + r + " 0 " + large + " 0 " + x3.toFixed(2) + "," + y3.toFixed(2) + " Z";
}
var HEAT_COLORS = ["#eceee9", "#d6e9df", "#a9d5c0", "#5fb18d", "#1a7a4a"];

// ── 01 关键指标 ──
function renderKpi(){
  var el = $("#kpi"); if (!el) return;
  var cards = [
    { cls: "", v: fmtInt(D.total), u: "次", l: "播放次数", h: "区间内总播放" },
    { cls: "k2", v: fmt(D.duration), u: "", l: "播放时长", h: "累计收听" },
    { cls: "", v: fmtInt(D.active_days), u: "天", l: "活跃天数", h: "有播放记录的天数" },
    { cls: "k4", v: num(D.completed_rate) + "", u: "%", l: "完整收听率", h: "完整听完的比例" },
    { cls: "k2", v: num(D.avg_completion) + "", u: "%", l: "平均完播", h: "平均播放进度" },
    { cls: "", v: fmtInt(D.streak), u: "天", l: "当前连续", h: "最长 " + fmtInt(D.longest_streak) + " 天" },
    { cls: "k3", v: num(D.night_pct) + "", u: "%", l: "深夜占比", h: "夜间时段收听" },
    { cls: "", v: num(D.explore) + "", u: "%", l: "探索度", h: "新歌/新歌手占比" }
  ];
  el.innerHTML = cards.map(function(c){
    return '<div class="kpi ' + c.cls + '"><div class="v">' + esc(c.v) + (c.u ? '<small>' + esc(c.u) + '</small>' : '') +
      '</div><div class="l">' + esc(c.l) + '</div><div class="h">' + esc(c.h) + '</div></div>';
  }).join("");
  setText("#overview-desc", "统计区间共 " + fmtInt(D.active_days) + " 个活跃日，平均每天约 " +
    fmt(num(D.duration) / Math.max(1, num(D.active_days))) + "。");
}

function renderKpiExtra(){
  var el = $("#kpi-extra"); if (!el) return;
  var items = [
    { v: num(D.skip_rate) + "%", l: "跳过率" },
    { v: num(D.weekend_pct) + "%", l: "周末占比" },
    { v: fmtInt(D.new_songs), l: "本月新歌" },
    { v: num(D.repeat) + "x", l: "重复深度" }
  ];
  el.innerHTML = items.map(function(x){ return '<div class="s"><b>' + esc(x.v) + '</b><span>' + esc(x.l) + '</span></div>'; }).join("");
}
)html";
static const wchar_t* HTML_TAIL_2 = LR"html(
// ── 02 播放趋势（圆角柱 + 渐变面积折线）──
function renderTrend(){
  var el = $("#svg-trend"); if (!el) return;
  // 从左到右由新到旧：桶本身是按时间升序的，这里整体反过来
  var B = isArr(D.buckets) ? D.buckets.slice().reverse() : [];
  var cap = $("#trend-desc");
  if (!B.length){
    el.setAttribute("viewBox", "0 0 720 70");
    el.innerHTML = '<text x="360" y="40" text-anchor="middle" fill="#878d96" font-size="13">本期无趋势数据</text>';
    if (cap) cap.textContent = "所选时间范围内没有播放记录。";
    return;
  }
  var H = 250, ml = 46, mr = 18, mt = 22, mb = 42;
  // 桶多时给每个桶留出最小宽度，整张图横向撑开，由外层容器左右滑动
  var avail = svgW(el);
  var minSlot = 44;
  var W = Math.max(avail, Math.round(ml + mr + B.length * minSlot));
  var cw = W - ml - mr, ch = H - mt - mb;
  el.setAttribute("width", W); el.setAttribute("height", H); el.setAttribute("viewBox", "0 0 " + W + " " + H);
  el.style.width = W + "px"; el.style.height = H + "px";
  var vals = B.map(function(b){ return num(b.c); });
  var maxV = Math.max.apply(null, vals) || 1;
  var maxIdx = 0; vals.forEach(function(v, i){ if (v > vals[maxIdx]) maxIdx = i; });
  var g = "", steps = 4;
  for (var s = 0; s <= steps; s++){
    var y = mt + ch * s / steps;
    g += '<line x1="' + ml + '" y1="' + y.toFixed(1) + '" x2="' + (ml + cw) + '" y2="' + y.toFixed(1) + '" stroke="#eceae4"/>';
    g += '<text x="' + (ml - 10) + '" y="' + (y + 4).toFixed(1) + '" text-anchor="end" fill="#b3b7bd" font-size="10" font-family="Consolas,monospace">' + Math.round(maxV * (1 - s / steps)) + '</text>';
  }
  var n = B.length, slot = cw / n, bw = Math.min(slot * 0.56, 34);
  var bars = "", pts = [];
  B.forEach(function(b, i){
    var v = num(b.c), x = ml + slot * i + (slot - bw) / 2, h = v / maxV * ch, y = mt + ch - h;
    var col = (i === maxIdx) ? "#0f6b5c" : "#2f5d9e";
    bars += '<path d="' + roundTop(x.toFixed(1), y.toFixed(1), bw.toFixed(1), h.toFixed(1), 5) + '" fill="' + col + '" fill-opacity="' + (i === maxIdx ? 1 : 0.82) + '"/>';
    pts.push([ml + slot * i + slot / 2, y]);
  });
  var lineD = pts.map(function(p, i){ return (i ? "L" : "M") + p[0].toFixed(1) + "," + p[1].toFixed(1); }).join(" ");
  var areaD = lineD + " L" + pts[n - 1][0].toFixed(1) + "," + (mt + ch) + " L" + pts[0][0].toFixed(1) + "," + (mt + ch) + " Z";
  var line = '<defs><linearGradient id="grad-trend" x1="0" y1="0" x2="0" y2="1">' +
    '<stop offset="0%" stop-color="#0f6b5c" stop-opacity="0.22"/><stop offset="100%" stop-color="#0f6b5c" stop-opacity="0"/></linearGradient></defs>' +
    '<path d="' + areaD + '" fill="url(#grad-trend)"/>' +
    '<path d="' + lineD + '" fill="none" stroke="#0f6b5c" stroke-width="2" stroke-linejoin="round"/>';
  var dots = pts.map(function(p){ return '<circle cx="' + p[0].toFixed(1) + '" cy="' + p[1].toFixed(1) + '" r="2.6" fill="#fff" stroke="#0f6b5c" stroke-width="1.6"/>'; }).join("");
  var stepX = n > 10 ? Math.ceil(n / 10) : 1, xl = "";
  B.forEach(function(b, i){ if (i % stepX && i !== n - 1) return;
    xl += '<text x="' + (ml + slot * i + slot / 2).toFixed(1) + '" y="' + (mt + ch + 20) + '" text-anchor="middle" fill="#878d96" font-size="10" font-family="Consolas,monospace">' + esc(b.l) + '</text>';
  });
  var hit = "";
  B.forEach(function(b, i){
    hit += '<rect x="' + (ml + slot * i).toFixed(1) + '" y="' + mt + '" width="' + slot.toFixed(1) + '" height="' + ch + '" fill="transparent" data-tip="' + esc(b.l) + " · " + num(b.c) + " 次 · " + fmt(b.d) + '"/>';
  });
  el.innerHTML = g + line + bars + dots + xl + hit;
  // 默认停在最左侧（最新的那一段）
  var wrap = $("#trend-scroll");
  if (wrap){ wrap.scrollLeft = 0; }
  var totalDur = B.reduce(function(a, b){ return a + num(b.d); }, 0);
  if (cap) cap.textContent = "峰值出现在 " + nz(B[maxIdx].l, "") + "（" + num(B[maxIdx].c) + " 次）；区间累计时长约 " + fmt(totalDur) + "。"
    + (W > avail ? "　图表可左右滑动，最左为最新。" : "");
}

// ── 02b 新发现趋势（折线 + 渐变面积 + 数据点）──
function renderNews(){
  var el = $("#svg-news"); if (!el) return;
  var N = isArr(D.news) ? D.news : [];
  if (!N.length){ el.setAttribute("viewBox", "0 0 720 60"); el.innerHTML = '<text x="360" y="34" text-anchor="middle" fill="#878d96" font-size="12">本期无新发现数据</text>'; return; }
  var W = svgW(el), H = 200, ml = 30, mr = 16, mt = 16, mb = 34;
  var cw = W - ml - mr, ch = H - mt - mb;
  el.setAttribute("width", W); el.setAttribute("height", H); el.setAttribute("viewBox", "0 0 " + W + " " + H);
  var vals = N.map(function(x){ return num(x.c); });
  var maxV = Math.max.apply(null, vals) || 1, n = N.length;
  var g = "";
  for (var s = 0; s <= 3; s++){ var y = mt + ch * s / 3; g += '<line x1="' + ml + '" y1="' + y.toFixed(1) + '" x2="' + (ml + cw) + '" y2="' + y.toFixed(1) + '" stroke="#eceae4"/>'; }
  var pts = N.map(function(d, i){ var x = ml + cw * i / Math.max(n - 1, 1), y = mt + ch - num(d.c) / maxV * ch; return [x, y]; });
  var lineD = pts.map(function(p, i){ return (i ? "L" : "M") + p[0].toFixed(1) + "," + p[1].toFixed(1); }).join(" ");
  var areaD = lineD + " L" + pts[n - 1][0].toFixed(1) + "," + (mt + ch) + " L" + pts[0][0].toFixed(1) + "," + (mt + ch) + " Z";
  var svg = '<defs><linearGradient id="grad-news" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#2f5d9e" stop-opacity="0.2"/><stop offset="100%" stop-color="#2f5d9e" stop-opacity="0"/></linearGradient></defs>' +
    '<path d="' + areaD + '" fill="url(#grad-news)"/>' +
    '<path d="' + lineD + '" fill="none" stroke="#2f5d9e" stroke-width="2" stroke-linejoin="round"/>';
  N.forEach(function(d, i){
    svg += '<circle cx="' + pts[i][0].toFixed(1) + '" cy="' + pts[i][1].toFixed(1) + '" r="3" fill="#fff" stroke="#2f5d9e" stroke-width="1.8"/>';
    if (n <= 12 || i % Math.ceil(n / 12) === 0) svg += '<text x="' + pts[i][0].toFixed(1) + '" y="' + (mt + ch + 18) + '" text-anchor="middle" fill="#878d96" font-size="9.5" font-family="Consolas,monospace">' + esc(d.l) + '</text>';
  });
  el.innerHTML = g + svg;
}

// ── 03 24 小时时段分布 ──
function renderHours(){
  var el = $("#svg-hours"); if (!el) return;
  var Hr = isArr(D.hours) ? D.hours : [];
  var cap = $("#hours-desc");
  var allZero = !Hr.length || Hr.every(function(v){ return !num(v); });
  if (allZero){
    el.setAttribute("viewBox", "0 0 720 70");
    el.innerHTML = '<text x="360" y="40" text-anchor="middle" fill="#878d96" font-size="13">本期无时段数据</text>';
    if (cap) cap.textContent = "";
    return;
  }
  var W = svgW(el), H = 210, ml = 34, mr = 12, mt = 16, mb = 30;
  var cw = W - ml - mr, ch = H - mt - mb;
  el.setAttribute("width", W); el.setAttribute("height", H); el.setAttribute("viewBox", "0 0 " + W + " " + H);
  var maxV = Math.max.apply(null, Hr.map(function(v){ return num(v); })) || 1;
  var maxIdx = 0; Hr.forEach(function(v, i){ if (num(v) > num(Hr[maxIdx])) maxIdx = i; });
  var slot = cw / 24, bw = Math.min(slot * 0.6, 22);
  var g = '<rect x="' + (ml + slot * maxIdx).toFixed(1) + '" y="' + mt + '" width="' + slot.toFixed(1) + '" height="' + ch + '" fill="#0f6b5c" fill-opacity="0.06"/>';
  for (var s = 0; s <= 3; s++){ var y = mt + ch * s / 3; g += '<line x1="' + ml + '" y1="' + y.toFixed(1) + '" x2="' + (ml + cw) + '" y2="' + y.toFixed(1) + '" stroke="#eceae4"/>'; }
  var bars = "", xl = "", hit = "";
  for (var h = 0; h < 24; h++){
    var v = num(Hr[h]), bh = v / maxV * ch;
    var x = ml + slot * h + (slot - bw) / 2, y = mt + ch - bh;
    var col = (h === maxIdx) ? "#0f6b5c" : "#2f5d9e";
    bars += '<path d="' + roundTop(x.toFixed(1), y.toFixed(1), bw.toFixed(1), bh.toFixed(1), 4) + '" fill="' + col + '" fill-opacity="' + (h === maxIdx ? 1 : 0.72) + '"/>';
    if (h % 3 === 0) xl += '<text x="' + (ml + slot * h + slot / 2).toFixed(1) + '" y="' + (mt + ch + 16) + '" text-anchor="middle" fill="#878d96" font-size="10" font-family="Consolas,monospace">' + pad2(h) + '</text>';
    hit += '<rect x="' + (ml + slot * h).toFixed(1) + '" y="' + mt + '" width="' + slot.toFixed(1) + '" height="' + ch + '" fill="transparent" data-tip="' + pad2(h) + ":00-" + pad2(h) + ':59 · ' + v + ' 次"/>';
  }
  el.innerHTML = g + bars + xl + hit;
  if (cap) cap.textContent = "你最常在 " + pad2(maxIdx) + ":00-" + pad2(maxIdx) + ":59 听歌（" + num(Hr[maxIdx]) + " 次）。";
}
)html";
static const wchar_t* HTML_TAIL_3 = LR"html(
// ── 04 每日热力图 ──
function renderHeat(){
  var el = $("#heatmap"); if (!el) return;
  var Ht = isArr(D.heat) ? D.heat : [];
  var legend = $("#heat-legend"), cap = $("#heat-desc");
  if (!Ht.length){
    el.innerHTML = ""; if (legend) legend.innerHTML = "";
    if (cap) cap.textContent = "本期无每日数据。";
    return;
  }
  var maxS = Math.max.apply(null, Ht.map(function(x){ return num(x.s); })) || 1;
  el.innerHTML = "";
  Ht.forEach(function(d){
    var s = num(d.s);
    var idx = s <= 0 ? 0 : Math.min(4, Math.floor(s / maxS * 4.999));
    var i = document.createElement("i");
    i.style.background = HEAT_COLORS[idx];
    i.dataset.tip = nz(d.d, "") + " · " + fmt(s) + " · " + num(d.c) + " 首";
    el.appendChild(i);
  });
  if (legend){
    legend.innerHTML = "<span>少</span>" + HEAT_COLORS.map(function(c){ return '<i style="background:' + c + '"></i>'; }).join("") +
      "<span>多</span><span style=\"margin-left:auto\">" + esc(Ht[0].d) + " → " + esc(Ht[Ht.length - 1].d) + "</span>";
  }
  var active = Ht.filter(function(x){ return num(x.s) > 0; }).length;
  if (cap) cap.textContent = "共 " + Ht.length + " 天，其中 " + active + " 天有收听记录；色块越深表示当天播放时长越长。";
}

// ── 05 听歌画像（雷达）──
function renderRadar(){
  var el = $("#svg-radar"); if (!el) return;
  var R2 = isArr(D.radar) ? D.radar : [];
  var cap = $("#profile-desc");
  if (R2.length < 3){ el.setAttribute("viewBox", "0 0 360 60"); el.innerHTML = '<text x="180" y="34" text-anchor="middle" fill="#878d96" font-size="13">画像数据不足</text>'; return; }
  var W = 360, H = 320, cx = W / 2, cy = H / 2, R = 118, n = R2.length;
  el.setAttribute("width", W); el.setAttribute("height", H); el.setAttribute("viewBox", "0 0 " + W + " " + H);
  var ang = function(i){ return Math.PI * 2 * i / n - Math.PI / 2; };
  var pt = function(i, r){ var a = ang(i); return [cx + Math.cos(a) * r, cy + Math.sin(a) * r]; };
  var grid = "";
  for (var ring = 1; ring <= 4; ring++){
    var ps = [];
    for (var i = 0; i < n; i++){ var p = pt(i, R * ring / 4); ps.push(p[0].toFixed(1) + "," + p[1].toFixed(1)); }
    grid += '<polygon points="' + ps.join(" ") + '" fill="' + (ring === 4 ? "#fbfbf9" : "none") + '" stroke="#e6e6e1"/>';
  }
  for (var i2 = 0; i2 < n; i2++){ var e = pt(i2, R); grid += '<line x1="' + cx + '" y1="' + cy + '" x2="' + e[0].toFixed(1) + '" y2="' + e[1].toFixed(1) + '" stroke="#ecece7"/>'; }
  var vpts = [], dots = "", labels = "", vl = "";
  R2.forEach(function(r, i){
    var v = clamp(num(r.v), 0, 100);
    var p = pt(i, R * v / 100); vpts.push(p[0].toFixed(1) + "," + p[1].toFixed(1));
    dots += '<circle cx="' + p[0].toFixed(1) + '" cy="' + p[1].toFixed(1) + '" r="3.4" fill="#0f6b5c"/>';
    var lp = pt(i, R + 26);
    labels += '<text x="' + lp[0].toFixed(1) + '" y="' + (lp[1] + 4).toFixed(1) + '" text-anchor="middle" fill="#4a4f57" font-size="12" font-weight="600">' + esc(r.d) + '</text>';
    vl += '<text x="' + lp[0].toFixed(1) + '" y="' + (lp[1] + 19).toFixed(1) + '" text-anchor="middle" fill="#0f6b5c" font-size="10.5" font-family="Consolas,monospace">' + v + '</text>';
  });
  el.innerHTML = grid + '<polygon points="' + vpts.join(" ") + '" fill="rgba(15,107,92,0.16)" stroke="#0f6b5c" stroke-width="2" stroke-linejoin="round"/>' + dots + labels + vl;
  if (cap){
    var best = R2.slice().sort(function(a, b){ return num(b.v) - num(a.v); })[0];
    cap.textContent = "五个维度中，你的「" + nz(best.d, "") + "」得分最高（" + num(best.v) + " 分）。";
  }
}

function renderHighlights(){
  var el = $("#profile-highlights"); if (!el) return;
  var rows = [
    { rk: "01", t: nz(D.top_artist, "—"), s: "最爱歌手", v: fmt(D.top_artist_dur) },
    { rk: "02", t: nz(D.top_song, "—"), s: "最爱单曲 · " + nz(D.top_song_artist, "未知"), v: num(D.top_song_count) + " 次" }
  ];
  el.innerHTML = rows.map(function(x){
    return '<div class="hl"><div class="rk">' + x.rk + '</div><div class="tt"><b title="' + esc(x.t) + '">' + esc(x.t) + '</b><span>' + esc(x.s) + '</span></div><div class="vv">' + esc(x.v) + '</div></div>';
  }).join("");
}
)html";
static const wchar_t* HTML_TAIL_4 = LR"html(

function renderPlaylist(){
  var el = $("#pl-table"); if (!el) return;
  var P = isArr(D.playlist) ? D.playlist : [];
  if (!P.length){ el.innerHTML = '<div class="empty">本期无数据</div>'; return; }
  el.innerHTML = '<table><thead><tr><th>来源</th><th class="num">播放时长</th><th class="num">占比</th></tr></thead><tbody>' +
    P.map(function(p){ return '<tr><td>' + esc(p.s) + '</td><td class="num">' + fmt(p.d) + '</td><td class="num">' + num(p.p) + '%</td></tr>'; }).join("") +
    '</tbody></table>';
}

// ── 06 播放行为 ──
function renderSkip(){
  var el = $("#skip-bars"); if (!el) return;
  var S = isArr(D.skip) ? D.skip : [];
  var cap = $("#behavior-desc");
  if (!S.length){ el.innerHTML = '<div class="empty">本期无跳过数据</div>'; return; }
  var maxP = Math.max.apply(null, S.map(function(s){ return num(s.p); })) || 1;
  el.innerHTML = S.map(function(s, i){
    var p = num(s.p), w = p / maxP * 100;
    return '<div class="bar-row"><span class="lb">' + esc(s.l) + '</span>' +
      '<span class="bar-track"><span class="bar-fill' + (i % 2 ? " alt" : "") + '" style="width:' + w.toFixed(1) + '%"></span></span>' +
      '<span class="vl">' + p + '% · ' + num(s.c) + ' 次</span></div>';
  }).join("");
  if (cap){
    var full = S.filter(function(s){ return /听完|complete/i.test(String(s.l)); })[0];
    cap.textContent = full ? "约 " + num(full.p) + "% 的播放完整听完，其余按进度区间分布。" : "按播放进度区间统计跳过情况。";
  }
}

function renderStreak(){
  var el = $("#streak-miss"); if (!el) return;
  var m = num(D.streak_miss);
  if (m > 0){
    el.innerHTML = '<div class="warn-note"><span class="ic">' + arrow(1, "#9a6b1f") + '</span><div>差点就连续 <b>' + (m + 1) +
      '</b> 天：上一次连续收听 ' + m + ' 天后中断，再坚持一天即可刷新纪录。</div></div>';
  } else {
    el.innerHTML = '<div class="empty">没有中断记录。</div>';
  }
}
)html";
static const wchar_t* HTML_TAIL_5 = LR"html(
// ── 07 年度回顾 ──
function renderYear(){
  var el = $("#svg-year"); if (!el) return;
  var Y = isArr(D.yearly) ? D.yearly.slice() : [];
  var tbl = $("#year-table"), cap = $("#year-desc");
  if (!Y.length){
    el.setAttribute("viewBox", "0 0 720 70");
    el.innerHTML = '<text x="360" y="40" text-anchor="middle" fill="#878d96" font-size="13">本期无年度数据</text>';
    if (tbl) tbl.innerHTML = '<div class="empty">本期无数据</div>';
    return;
  }
  Y.sort(function(a, b){ return num(a.y) - num(b.y); });
  var W = svgW(el), H = 210, ml = 40, mr = 16, mt = 18, mb = 32;
  var cw = W - ml - mr, ch = H - mt - mb;
  el.setAttribute("width", W); el.setAttribute("height", H); el.setAttribute("viewBox", "0 0 " + W + " " + H);
  var maxV = Math.max.apply(null, Y.map(function(y){ return num(y.c); })) || 1;
  var n = Y.length, slot = cw / n, bw = Math.min(slot * 0.42, 72);
  var g = "";
  for (var s = 0; s <= 4; s++){
    var y = mt + ch * s / 4;
    g += '<line x1="' + ml + '" y1="' + y.toFixed(1) + '" x2="' + (ml + cw) + '" y2="' + y.toFixed(1) + '" stroke="#eceae4"/>';
    g += '<text x="' + (ml - 8) + '" y="' + (y + 4).toFixed(1) + '" text-anchor="end" fill="#b3b7bd" font-size="10" font-family="Consolas,monospace">' + Math.round(maxV * (1 - s / 4)) + '</text>';
  }
  var bars = "", xl = "";
  Y.forEach(function(yv, i){
    var v = num(yv.c), bh = v / maxV * ch, x = ml + slot * i + (slot - bw) / 2, yy = mt + ch - bh;
    var isLast = i === n - 1;
    bars += '<path d="' + roundTop(x.toFixed(1), yy.toFixed(1), bw.toFixed(1), bh.toFixed(1), 6) + '" fill="' + (isLast ? "#0f6b5c" : "#2f5d9e") + '" fill-opacity="' + (isLast ? 1 : 0.8) + '"/>';
    bars += '<text x="' + (x + bw / 2).toFixed(1) + '" y="' + (yy - 6).toFixed(1) + '" text-anchor="middle" fill="#4a4f57" font-size="10.5" font-family="Consolas,monospace">' + v + '</text>';
    xl += '<text x="' + (ml + slot * i + slot / 2).toFixed(1) + '" y="' + (mt + ch + 18) + '" text-anchor="middle" fill="#878d96" font-size="11" font-family="Consolas,monospace">' + esc(yv.y) + '</text>';
  });
  el.innerHTML = g + bars + xl;
  if (tbl){
    tbl.innerHTML = '<table><thead><tr><th>年份</th><th class="num">播放次数</th><th class="num">播放时长</th></tr></thead><tbody>' +
      Y.slice().reverse().map(function(yv){ return '<tr><td>' + esc(yv.y) + '</td><td class="num">' + fmtInt(yv.c) + '</td><td class="num">' + fmt(yv.d) + '</td></tr>'; }).join("") +
      '</tbody></table>';
  }
  var sum = Y.reduce(function(a, b){ return a + num(b.c); }, 0);
  if (cap) cap.textContent = "共 " + Y.length + " 个年度，累计 " + fmtInt(sum) + " 次播放。";
}

// ── 02c 环比 / 同比 ──
function renderComparison(){
  var el = $("#comparison-box"); if (!el) return;
  var C = (D.comparison && typeof D.comparison === "object") ? D.comparison : {};
  var html = '<div class="card-title">环比 / 同比</div>';
  if (C.has_prev){
    var d = num(C.delta), dp = num(C.delta_pct);
    var cls = d > 0 ? "up" : (d < 0 ? "down" : "flat");
    html += '<div class="cmp-row"><span class="k">环比 ' + esc(nz(C.prev_label, "上期")) + '</span>' +
      '<span class="d ' + cls + '">' + arrow(d, cls === "up" ? "#0f6b5c" : (cls === "down" ? "#94382e" : "#878d96")) + ' ' + Math.abs(d) + ' 次</span>' +
      '<span class="sub">' + (dp >= 0 ? "+" : "") + dp + '% · 上期 ' + num(C.prev_count) + ' 次</span></div>';
  } else {
    html += '<div class="cmp-row"><span class="k">环比</span><span class="d flat">' + arrow(0, "#878d96") + ' 无</span><span class="sub">无上期数据</span></div>';
  }
  if (C.has_ly){
    var ly = num(C.ly_count), delta = num(D.total) - ly;
    var cls2 = delta > 0 ? "up" : (delta < 0 ? "down" : "flat");
    html += '<div class="cmp-row"><span class="k">同比 ' + esc(nz(C.ly_label, "去年同期")) + '</span>' +
      '<span class="d ' + cls2 + '">' + arrow(delta, cls2 === "up" ? "#0f6b5c" : (cls2 === "down" ? "#94382e" : "#878d96")) + ' ' + Math.abs(delta) + ' 次</span>' +
      '<span class="sub">去年同期 ' + ly + ' 次</span></div>';
  } else {
    html += '<div class="cmp-row"><span class="k">同比</span><span class="d flat">' + arrow(0, "#878d96") + ' 无</span><span class="sub">无同期数据</span></div>';
  }
  el.innerHTML = html;
}

// ── 汇总渲染 ──
function render(){
  renderKpi();
  renderKpiExtra();
  renderTrend();
  renderNews();
  renderHours();
  renderHeat();
  renderRadar();
  renderHighlights();
  renderPlaylist();
  renderSkip();
  renderStreak();
  renderYear();
  renderComparison();
  bindTip("#heatmap i", function(el){ return el.dataset.tip; });
  bindTip("#svg-trend [data-tip]", function(el){ return el.dataset.tip; });
  bindTip("#svg-hours [data-tip]", function(el){ return el.dataset.tip; });
}
// ── 初始化：副标题 / 生成时间 / 分区导航高亮 / 尺寸自适应重绘 ──
(function(){
  setText("#sub", nz(D.sub_title, "全部记录"));
  setText("#foot-note", "统计口径：单次播放不足 15 秒不计入；后台播放按实际累计时长计时。");

  if (typeof Date !== "undefined"){
    var now = new Date();
    setText("#gen-time", "生成于 " + now.getFullYear() + "-" + pad2(now.getMonth() + 1) + "-" + pad2(now.getDate()) +
      " " + pad2(now.getHours()) + ":" + pad2(now.getMinutes()));
  }

  // 粘性导航：滚动高亮当前区块（DOM 桩中 IntersectionObserver 不存在时安全跳过）
  var links = $$(".nav-link");
  if (typeof IntersectionObserver !== "undefined" && links.length){
    var map = {};
    links.forEach(function(a){ map[a.getAttribute("href")] = a; });
    var io = new IntersectionObserver(function(entries){
      entries.forEach(function(en){
        if (!en.isIntersecting) return;
        links.forEach(function(l){ if (l.classList) l.classList.remove("active"); });
        var a = map["#" + en.target.id];
        if (a && a.classList) a.classList.add("active");
      });
    }, { rootMargin: "-45% 0px -50% 0px", threshold: 0 });
    $$("section.section").forEach(function(s){ io.observe(s); });
  }

  render();

  // 尺寸变化时重绘（防抖）
  var timer = null;
  var onResize = function(){ if (timer) clearTimeout(timer); timer = setTimeout(render, 160); };
  if (typeof window !== "undefined" && window.addEventListener) window.addEventListener("resize", onResize);
})();
</script>
</body>
</html>
)html";

// ── 主体 ──
void CStatHtmlReport::GenerateAndOpen(const std::vector<PlayRecord>& records,
                                       const StatSummary& s,
                                       const StatFilter& filter)
{
    std::wstring json = BuildJson(records, s, filter);

    // 模板由多段常量拼接而成（规避 MSVC 单字面量 65535 字节上限）：
    // HTML_HEAD_1..N 拼成 head，HTML_TAIL_1..M 拼成 tail。
    // 所有区块标记都是静态 HTML，只由页面脚本按容器 id 填充，C++ 不再动态拼 DOM。
    std::wstring head;
    head += HTML_HEAD_1;
    head += HTML_HEAD_1B;
    head += HTML_HEAD_2;

    std::wstring tail;
    tail += HTML_TAIL_1;
    tail += HTML_TAIL_2;
    tail += HTML_TAIL_3;
    tail += HTML_TAIL_4;
    tail += HTML_TAIL_5;

    // 用 BuildJson 的输出整体替换数据占位符（模板中该占位符仅出现一次）。
    // 这里只在 C++ 侧匹配前缀 "const D="，再把该语句一直替换到结尾分号，
    // 避免在源码中再次写出占位符字面量：保证整个文件中该标记恰好出现一次。
    {
        const std::wstring kPrefix{ L"const D=" };
        const auto p = tail.find(kPrefix);
        if (p != std::wstring::npos)
        {
            const auto semi = tail.find(L';', p + kPrefix.size());
            if (semi != std::wstring::npos)
            {
                std::wstring replaced{ kPrefix };
                replaced += json;
                replaced += L";";
                tail.replace(p, semi - p + 1, replaced);
            }
        }
    }

    std::wstring html{ head };
    html += tail;

    // 报告落在配置目录 statistics\reports 下，文件名带时间戳，只保留最近 kKeepReportCount 份
    std::wstring report_dir = theApp.m_config_dir + L"statistics\\reports\\";
    CCommon::CreateDir(theApp.m_config_dir + L"statistics\\");
    CCommon::CreateDir(report_dir);

    CTime now = CTime::GetCurrentTime();
    wchar_t stamp[64];
    swprintf_s(stamp, L"%04d%02d%02d_%02d%02d%02d",
        now.GetYear(), now.GetMonth(), now.GetDay(),
        now.GetHour(), now.GetMinute(), now.GetSecond());

    std::wstring path = report_dir + L"播放统计报告_" + stamp + L".html";

    // UTF-8 BOM
    std::ofstream ofs(path.c_str(), std::ios::binary);
    if (!ofs.is_open()) { AfxMessageBox(L"无法创建报告文件", MB_ICONERROR); return; }
    ofs << "\xEF\xBB\xBF";
    CStringA utf8{ CCommon::UnicodeToStr(html.c_str(), CodeType::UTF8).c_str() };
    ofs << utf8.GetString();
    ofs.close();

    // 清理旧报告：按文件名排序（时间戳天然字典序），超出份数的删掉
    {
        std::vector<std::wstring> files;
        WIN32_FIND_DATA find_data{};
        HANDLE hFind = FindFirstFile((report_dir + L"播放统计报告_*.html").c_str(), &find_data);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                files.push_back(find_data.cFileName);
            } while (FindNextFile(hFind, &find_data));
            FindClose(hFind);
        }
        if (static_cast<int>(files.size()) > kKeepReportCount)
        {
            std::sort(files.begin(), files.end());
            int to_delete = static_cast<int>(files.size()) - kKeepReportCount;
            for (int i = 0; i < to_delete; i++)
                DeleteFileW((report_dir + files[i]).c_str());
        }
    }

    ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOW);
}

// ── 独立入口 ──

// 从 statistics 目录加载全部播放记录（与播放记录写入端 PlayRecord::ToJson 的字段一一对应）
std::vector<PlayRecord> CStatHtmlReport::LoadRecords(int* broken_lines, int* failed_files)
{
    std::vector<PlayRecord> records;

    std::wstring stats_dir = theApp.m_config_dir + L"statistics\\";
    std::wstring search_pattern = stats_dir + L"playlog_*.jsonl";

    WIN32_FIND_DATA find_data;
    HANDLE hFind = FindFirstFile(search_pattern.c_str(), &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return records;

    do
    {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring file_path = stats_dir + find_data.cFileName;
        std::ifstream ifs(file_path, std::ios::binary);
        if (!ifs.is_open())
        {
            if (failed_files) (*failed_files)++;
            continue;
        }

        std::string line;
        while (std::getline(ifs, line))
        {
            if (line.empty()) continue;

            PlayRecord record;
            auto extract_string = [&line](const std::string& key, std::wstring& out) {
                std::string search = "\"" + key + "\":\"";
                size_t pos = line.find(search);
                if (pos == std::string::npos) return;
                pos += search.size();
                size_t end = pos;
                while (end < line.size())
                {
                    if (line[end] == '\\' && end + 1 < line.size()) { end += 2; continue; }
                    if (line[end] == '"') break;
                    end++;
                }
                std::string utf8_val = line.substr(pos, end - pos);
                std::string unescaped;
                for (size_t i = 0; i < utf8_val.size(); i++)
                {
                    if (utf8_val[i] == '\\' && i + 1 < utf8_val.size())
                    {
                        char next = utf8_val[i + 1];
                        if (next == '"') unescaped += '"';
                        else if (next == '\\') unescaped += '\\';
                        else if (next == 'n') unescaped += '\n';
                        else if (next == 'r') unescaped += '\r';
                        else if (next == 't') unescaped += '\t';
                        else unescaped += utf8_val[i];
                        i++;
                    }
                    else
                    {
                        unescaped += utf8_val[i];
                    }
                }
                int len = ::MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, nullptr, 0);
                if (len > 0)
                {
                    out.resize(len - 1);
                    ::MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, &out[0], len);
                }
                };

            auto extract_int = [&line](const std::string& key, int& out) {
                std::string search = "\"" + key + "\":";
                size_t pos = line.find(search);
                if (pos == std::string::npos) return;
                pos += search.size();
                if (pos >= line.size()) return;
                out = atoi(line.c_str() + pos);
                };

            auto extract_bool = [&line](const std::string& key, bool& out) {
                std::string search = "\"" + key + "\":";
                size_t pos = line.find(search);
                if (pos == std::string::npos) return;
                pos += search.size();
                if (pos >= line.size()) return;
                out = (line[pos] == 't');
                };

            extract_string("file_path", record.file_path);
            extract_string("title", record.title);
            extract_string("artist", record.artist);
            extract_string("album", record.album);
            extract_string("genre", record.genre);
            extract_string("played_at", record.played_at);
            extract_int("play_duration_sec", record.play_duration_sec);
            extract_int("song_length_sec", record.song_length_sec);
            int reason = 0;
            extract_int("finish_reason", reason);
            record.finish_reason = static_cast<PlayRecord::FinishReason>(reason);
            extract_int("volume", record.volume);
            extract_bool("was_shuffled", record.was_shuffled);
            extract_string("playlist_source", record.playlist_source);
            extract_int("bitrate", record.bitrate);
            extract_int("sample_rate", record.sample_rate);
            extract_int("channels", record.channels);

            // 完整性校验：时间字段缺失或明显越界、时长为负或超过 48 小时的行一律跳过并计数。
            // 单条脏数据不得中断整个加载流程。
            // 另外要求 played_at / play_duration_sec 两个关键字段确实出现过（extract_* 找不到会保持默认值 0，
            // 光看值会把「字段缺失」误判成「时长为 0 的合法记录」）。
            bool ok = (line.find("\"played_at\":\"") != std::string::npos)
                && (line.find("\"play_duration_sec\":") != std::string::npos)
                && (record.played_at.size() >= 19)
                && (record.play_duration_sec >= 0)
                && (record.play_duration_sec <= 48 * 3600)
                && (record.song_length_sec >= 0);
            if (!ok)
            {
                if (broken_lines) (*broken_lines)++;
                continue;
            }

            records.push_back(std::move(record));
        }
        ifs.close();
    } while (FindNextFile(hFind, &find_data));

    FindClose(hFind);

    // 按播放时间倒序（最新在前），与原统计对话框的加载顺序一致
    std::sort(records.begin(), records.end(), [](const PlayRecord& a, const PlayRecord& b) {
        return a.played_at > b.played_at;
        });

    return records;
}


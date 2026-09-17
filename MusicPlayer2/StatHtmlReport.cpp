#include "stdafx.h"
#include "StatHtmlReport.h"
#include "MusicPlayer2.h"
#include "StatCommon.h"
#include <shellapi.h>
#include <fstream>

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

static CString FormatDurationHtml(int sec)
{
    return CStatAnalysis::FormatDuration(sec).c_str();
}

static void EscapeHtml(CString& s)
{
    s.Replace(L"&", L"&amp;");
    s.Replace(L"<", L"&lt;");
    s.Replace(L">", L"&gt;");
}

// ── JSON 数据构建 ──
static std::wstring BuildJson(const std::vector<PlayRecord>& records,
                         const StatSummary& s,
                         const StatFilter& filter)
{
    std::wstring j;

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
    j += L"\"top_genre\":\"" + JsonStr(s.top_genre) + L"\",";

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

    // 流派
    auto genres = CStatAnalysis::ComputeGenreShare(records, 8);
    j += L"\"genre\":[";
    for (size_t i = 0; i < genres.size(); i++)
    {
        if (i) j += L",";
        j += L"{\"g\":\"" + JsonStr(genres[i].genre) + L"\",\"d\":" +
             std::to_wstring(genres[i].duration_sec) + L",\"p\":" +
             std::to_wstring(static_cast<int>(genres[i].percent + 0.5)) + L"}";
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
    j += L"]";

    return j;
}

// ── HTML 模板 ──
static const wchar_t* HTML_HEAD = LR"html(<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>MusicPlayer2 播放统计报告</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#f5f6f4;color:#14161a;line-height:1.65;padding:36px 20px 72px}
.w{max-width:860px;margin:0 auto}
h1{font-size:26px;font-weight:600;margin-bottom:4px}
.sub{color:#878d96;font-size:13px;margin-bottom:28px;font-family:Consolas,monospace}
h2{font-size:17px;font-weight:600;margin:34px 0 12px;padding-top:16px;border-top:1px solid #d9dad5}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:8px}
.kpi{background:#fff;border:1px solid #d9dad5;padding:12px 14px}
.kpi b{display:block;font-size:20px;font-weight:600}
.kpi span{font-size:11px;color:#878d96}
.cb{background:#fff;border:1px solid #d9dad5;padding:14px 16px;margin-bottom:6px}
.cb h3{font-size:13.5px;margin-bottom:10px}
table{width:100%;border-collapse:collapse;font-size:13px;margin:6px 0}
th{text-align:left;font-size:11px;color:#878d96;padding:5px 10px 5px 0;border-bottom:1.5px solid #14161a}
td{padding:6px 10px 6px 0;border-bottom:1px solid #e8e8e4}
.badge{display:inline-block;background:#e5efec;color:#0f6b5c;padding:2px 10px;border-radius:3px;font-size:12px;margin:2px 4px 2px 0}
.heat{display:grid;grid-template-columns:repeat(26,10px);gap:2px}
.heat i{width:10px;height:10px;border-radius:1px;cursor:pointer}
.tip{position:fixed;background:#14161a;color:#fff;padding:4px 10px;border-radius:3px;font-size:12px;pointer-events:none;display:none;z-index:9}
.foot{margin-top:48px;padding-top:14px;border-top:1px solid #d9dad5;font-size:11px;color:#878d96;font-family:Consolas,monospace}
svg text{font-family:"Segoe UI","Microsoft YaHei",sans-serif;font-size:10px;fill:#878d96}
</style></head><body><div class="w">
<h1>播放统计报告</h1><div class="sub" id="sub"></div>
)html";

static const wchar_t* HTML_TAIL = LR"html(</div>
<div class="tip" id="tip"></div>
<script>
"use strict";
const D=__DATA__;
const tip=document.getElementById("tip");
const $=s=>document.querySelector(s);
const fmt=s=>{const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=s%60;return h>0?h+"时"+m+"分"+(x>0?x+"秒":""):m>0?m+"分"+(x>0?x+"秒":""):x+"秒"};
// 悬停提示
function bindTip(sel,fn){document.querySelectorAll(sel).forEach(el=>{el.addEventListener("mousemove",e=>{tip.textContent=fn(el);tip.style.display="block";tip.style.left=(e.clientX+12)+"px";tip.style.top=(e.clientY-28)+"px"});el.addEventListener("mouseleave",()=>{tip.style.display="none"})})}
// 副标题
$("#sub").textContent=D.sub_title;
// 热力图
(function(){const el=$("#heatmap");if(!el)return;
const colors=["#e8e8e4","#cde8d5","#93cfa8","#4da875","#1a7a4a"];
D.heat.forEach(d=>{const i=document.createElement("i");
const v=Math.min(d.s/3600,4);const idx=Math.min(Math.floor(v/1),3);
i.style.background=colors[idx];i.dataset.tip=d.d+" · "+fmt(d.s)+" · "+d.c+" 次";
el.appendChild(i)});
bindTip("#heatmap i",el=>el.dataset.tip)})();
// 趋势 SVG
(function(){const el=$("#svg-trend");if(!el)return;
const W=el.parentElement.clientWidth-32,H=180,ml=30,mr=8,mt=10,mb=22;
const cw=W-ml-mr,ch=H-mt-mb;el.setAttribute("width",W);el.setAttribute("height",H);
if(!D.buckets.length)return;
const max=Math.max(...D.buckets.map(b=>b.c),1);
const n=D.buckets.length,bw=Math.max(cw/n,2);
let svg="";
// 轴
svg+=`<line x1="${ml}" y1="${mt}" x2="${ml}" y2="${mt+ch}" stroke="#c0c0c0"/>`;
svg+=`<line x1="${ml}" y1="${mt+ch}" x2="${ml+cw}" y2="${mt+ch}" stroke="#c0c0c0"/>`;
svg+=`<text x="${ml-20}" y="${mt+4}">${max}</text>`;
svg+=`<text x="${ml-14}" y="${mt+ch+4}">0</text>`;
let px=-1,py=-1;
D.buckets.forEach((b,i)=>{const x=ml+cw*i/n+bw/2;const bh=b.c/max*ch;const y=mt+ch-bh;
svg+=`<rect x="${x-bw/2}" y="${y}" width="${Math.max(bw-2,1)}" height="${bh}" fill="#2f5d9e" opacity="0.85"/>`;
if(px>=0)svg+=`<line x1="${px}" y1="${py}" x2="${x}" y2="${y}" stroke="#0f6b5c" stroke-width="2"/>`;
px=x;py=y});
// X 标签抽稀
const step=n>12?Math.ceil(n/10):1;
D.buckets.forEach((b,i)=>{if(i%step)return;const x=ml+cw*i/n+bw/2;
svg+=`<text x="${x}" y="${mt+ch+14}" text-anchor="middle">${b.label}</text>`});
el.innerHTML=svg})();
// 雷达 SVG
(function(){const el=$("#svg-radar");if(!el)return;
const W=220,H=190,cx=W/2,cy=H/2+6,R=70,n=D.radar.length;if(n<3)return;
const ang=i=>Math.PI*2*i/n-Math.PI/2;
function pt(i,r){const a=ang(i);return[cx+Math.cos(a)*r,cy+Math.sin(a)*r]}
let grid="";
for(let ring=1;ring<=3;ring++){let pts=[];
for(let i=0;i<n;i++){const[p,q]=pt(i,R*ring/3);pts.push(p.toFixed(1)+","+q.toFixed(1))}
grid+=`<polygon points="${pts.join(" ")}" fill="none" stroke="#d9dad5"/>`}
let labels="";
D.radar.forEach((r,i)=>{const[x,y]=pt(i,R+16);labels+=`<text x="${x}" y="${y+4}" text-anchor="middle" font-size="10">${r.d}</text>`});
let vals=[];
D.radar.forEach((r,i)=>{const[x,y]=pt(i,R*r.v/100);vals.push([x,y])});
let poly=vals.map(v=>v[0].toFixed(1)+","+v[1].toFixed(1)).join(" ");
let dots=vals.map(v=>`<circle cx="${v[0].toFixed(1)}" cy="${v[1].toFixed(1)}" r="3" fill="#0f6b5c"/>`).join("");
el.setAttribute("width",W);el.setAttribute("height",H);
el.innerHTML=grid+`<polygon points="${poly}" fill="rgba(15,107,92,0.15)" stroke="#0f6b5c" stroke-width="1.5"/>`+dots+labels})();
// 流派环形
(function(){const el=$("#svg-donut");if(!el||!D.genre.length)return;
const W=180,H=180,cx=W/2,cy=H/2,R=70,r=42;
let start=0,segs="";
D.genre.forEach(g=>{const sweep=g.p/100*360;
const a0=(start-90)*Math.PI/180,a1=(start+sweep-90)*Math.PI/180;
const x0=cx+R*Math.cos(a0),y0=cy+R*Math.sin(a0);
const x1=cx+R*Math.cos(a1),y1=cy+R*Math.sin(a1);
const xi=cx+r*Math.cos(a1),yi=cy+r*Math.sin(a1);
const xi2=cx+r*Math.cos(a0),yi2=cy+r*Math.sin(a0);
const large=sweep>180?1:0;
segs+=`<path d="M${x0.toFixed(1)},${y0.toFixed(1)} A${R},${R} 0 ${large} 1 ${x1.toFixed(1)},${y1.toFixed(1)} L${xi.toFixed(1)},${yi.toFixed(1)} A${r},${r} 0 ${large} 0 ${xi2.toFixed(1)},${yi2.toFixed(1)} Z" fill="hsl(${(start*7)%360},55%,45%)" opacity="0.85"/>`;
start+=sweep});
el.setAttribute("width",W);el.setAttribute("height",H);el.innerHTML=segs})();
// 环形图例
(function(){const el=$("#donut-legend");if(!el)return;
const colors=D.genre.map((_,i)=>`hsl(${(i*Math.PI*180/180)%360},55%,45%)`);
el.innerHTML=D.genre.map((g,i)=>`<span style="margin-right:12px"><i style="display:inline-block;width:8px;height:8px;border-radius:2px;margin-right:4px;background:hsl(${(i*180/8)%360},55%,45%)"></i>${g.g} ${g.p}%</span>`).join("")})();
// 跳过条
(function(){const el=$("#skip-bars");if(!el)return;
el.innerHTML=D.skip.map(s=>`<div class="skip-row"><span>${s.l}</span><div><div class="skip-bar" style="width:${s.p}%"></div></div><span>${s.c} 次</span></div>`).join("")})();
// 歌单
(function(){const el=$("#pl-table");if(!el)return;
el.innerHTML="<table><tr><th>来源</th><th>时长</th><th>占比</th></tr>"+
(D.playlist.length?D.playlist.map(p=>`<tr><td>${p.s}</td><td>${fmt(p.d)}</td><td>${p.p}%</td></tr>`).join(""):"<tr><td colspan=3>无数据</td></tr>")+"</table>"})();
// 年度
(function(){const el=$("#year-table");if(!el)return;
el.innerHTML="<table><tr><th>年份</th><th>次数</th><th>时长</th></tr>"+
(D.yearly.length?D.yearly.map(y=>`<tr><td>${y.y}</td><td>${y.c} 首</td><td>${fmt(y.d)}</td></tr>`).join(""):"<tr><td colspan=3>无记录</td></tr>")+"</table>"})();
// 遗珠
(function(){const el="#gem-list";const g=document.querySelector(el);if(!g)return})();
</script></body></html>
)html";

// ── 主体 ──
void CStatHtmlReport::GenerateAndOpen(const std::vector<PlayRecord>& records,
                                       const StatSummary& s,
                                       const StatFilter& filter)
{
    std::wstring json = BuildJson(records, s, filter);

    std::wstring sub;
    if (filter.from_ymd > 0 && filter.to_ymd > 0)
    {
        sub = YmdToStr(filter.from_ymd) + L" ~ " + YmdToStr(filter.to_ymd);
    }
    else
        sub = L"全部记录";

    std::wstring html{ HTML_HEAD };
    html += L"<h2>核心指标</h2>\n<div class=\"grid\">\n";
    auto kpi = [&](const CString& label, const CString& val) {
        html += L"<div class=\"kpi\"><b>" + val + L"</b><span>" + label + L"</span></div>\n";
    };
    kpi(L"播放次数", std::to_wstring(s.total_count).c_str());
    kpi(L"播放时长", CStatAnalysis::FormatDuration(s.total_duration_sec).c_str());
    kpi(L"活跃天数", (std::to_wstring(s.active_days) + L" 天").c_str());
    kpi(L"完整收听率", (std::to_wstring(static_cast<int>(s.completed_rate + 0.5)) + L"%").c_str());
    html += L"</div>\n";

    html += L"<h2>播放趋势</h2>\n<div class=\"cb\"><svg id=\"svg-trend\"></svg></div>\n";
    html += L"<h2>每日热力图</h2>\n<div class=\"cb\"><div class=\"heat\" id=\"heatmap\"></div></div>\n";
    html += L"<h2>听歌画像</h2>\n<div class=\"cb\" style=\"text-align:center\"><svg id=\"svg-radar\"></svg></div>\n";
    html += L"<h2>流派占比</h2>\n<div style=\"display:flex;gap:20px;align-items:center\">"
            L"<svg id=\"svg-donut\"></svg><div id=\"donut-legend\" style=\"font-size:13px\"></div></div>\n";
    html += L"<h2>跳过位置分布</h2>\n<div class=\"cb\"><div id=\"skip-bars\"></div></div>\n";
    html += L"<h2>歌单 / 来源贡献</h2>\n<div class=\"cb\"><div id=\"pl-table\"></div></div>\n";
    html += L"<h2>年度回顾</h2>\n<div class=\"cb\"><div id=\"year-table\"></div></div>\n";

    std::wstring tail{ HTML_TAIL };
    { auto p = tail.find(L"__DATA__"); if (p != std::wstring::npos) tail.replace(p, 8, json); }
    { auto p = tail.find(L"__SUBTITLE__"); if (p != std::wstring::npos) tail.replace(p, 12, sub); }
    html += tail;
    html += L"</div></body></html>\n";

    // 写文件到 %TEMP%
    wchar_t temp_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_dir);
    std::wstring path{ temp_dir };
    path += L"MusicPlayer2-统计报告.html";

    // UTF-8 BOM
    std::ofstream ofs(path.c_str(), std::ios::binary);
    if (!ofs.is_open()) { AfxMessageBox(L"无法创建报告文件", MB_ICONERROR); return; }
    ofs << "\xEF\xBB\xBF";
    CStringA utf8{ CCommon::UnicodeToStr(html.c_str(), CodeType::UTF8).c_str() };
    ofs << utf8.GetString();
    ofs.close();

    ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOW);
}

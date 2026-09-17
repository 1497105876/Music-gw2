#include "stdafx.h"
#include "StatTheme.h"
#include "MusicPlayer2.h"
#include "CPlayerUIHelper.h"
#include "BaseDialog.h"
#include <algorithm>
#include <cmath>

namespace
{
    // 构建一套调色板（dark=true 为深色模式）
    void BuildColors(StatThemeColors& c, bool dark)
    {
        UIColors ui = CPlayerUIHelper::GetUIColors(dark, false);
        const ColorTable& tc = theApp.m_app_setting_data.theme_color;

        // 容器
        c.back = ui.color_back;
        c.card_back = dark ? RGB(56, 56, 60) : RGB(246, 248, 252);
        c.card_back_alt = dark ? RGB(72, 72, 76) : RGB(238, 240, 246);
        c.panel_back = dark ? RGB(50, 50, 54) : RGB(252, 252, 255);

        // 线条与文字
        c.grid = dark ? RGB(96, 96, 100) : RGB(180, 180, 180);
        c.axis = dark ? RGB(130, 130, 134) : RGB(160, 160, 160);
        c.text_primary = ui.color_text;
        c.text_secondary = ui.color_text_lable;
        c.text_disabled = ui.color_text_disabled;

        // 强调
        c.highlight = tc.original_color;
        c.accent = tc.original_color;
        c.selected = ui.color_list_selected;

        // 系列色：浅色沿用现有固有系列色；深色向白色提亮，保证深底可辨识
        static const COLORREF base_series[8] = {
            RGB(100, 170, 230), RGB(70, 200, 130), RGB(240, 150, 90), RGB(190, 130, 210),
            RGB(235, 130, 130), RGB(90, 190, 190), RGB(230, 200, 80), RGB(140, 160, 230)
        };
        for (int i = 0; i < 8; i++)
            c.series[i] = dark ? CStatTheme::Lerp(base_series[i], RGB(255, 255, 255), 0.25) : base_series[i];

        // 热力图：由 panel_back 向主题色插值出 5 级（由浅到深）
        for (int i = 0; i < 5; i++)
            c.heat[i] = CStatTheme::Lerp(c.panel_back, c.highlight, (double)i / 4.0);

        // 语义色
        c.good = dark ? RGB(90, 210, 150) : RGB(70, 190, 120);
        c.warn = dark ? RGB(240, 190, 110) : RGB(240, 170, 70);
        c.bad = dark ? RGB(235, 110, 110) : RGB(220, 80, 80);
    }
}

const StatThemeColors& CStatTheme::Get()
{
    // 每次按当前 dark_mode 构建：主题色/深色模式变化时自动生效（构建成本可忽略）
    static StatThemeColors light;
    static StatThemeColors dark;
    if (theApp.m_app_setting_data.dark_mode)
    {
        BuildColors(dark, true);
        return dark;
    }
    BuildColors(light, false);
    return light;
}

void CStatTheme::ApplyDialog(CWnd* pDlg)
{
    if (pDlg == nullptr || pDlg->GetSafeHwnd() == nullptr) return;
    if (CBaseDialog* pBase = dynamic_cast<CBaseDialog*>(pDlg))
        pBase->SetBackgroundColor(Get().back, FALSE);
}

COLORREF CStatTheme::Lerp(COLORREF a, COLORREF b, double t)
{
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    auto mix = [t](int x, int y) -> int {
        return static_cast<int>(x + (y - x) * t + 0.5);
        };
    return RGB(mix(GetRValue(a), GetRValue(b)),
               mix(GetGValue(a), GetGValue(b)),
               mix(GetBValue(a), GetBValue(b)));
}

double CStatTheme::ContrastRatio(COLORREF fg, COLORREF bg)
{
    auto channel = [](int v) -> double {
        double s = v / 255.0;
        return (s <= 0.03928) ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
    auto luminance = [&](COLORREF c) -> double {
        return 0.2126 * channel(GetRValue(c)) + 0.7152 * channel(GetGValue(c)) + 0.0722 * channel(GetBValue(c));
        };
    double l1 = luminance(fg);
    double l2 = luminance(bg);
    if (l1 < l2) std::swap(l1, l2);
    return (l1 + 0.05) / (l2 + 0.05);
}

#pragma once
#include <windows.h>

// 统计图表语义化调色板（28 个字段）
// 取值来源复用 CPlayerUIHelper::GetUIColors 与 theApp.m_app_setting_data.theme_color，
// 深浅两套一次性按 dark_mode 提供，供后续批次的所有自绘图表统一取色。
struct StatThemeColors
{
    // 容器
    COLORREF back;              // 对话框 / 子页背景
    COLORREF card_back;         // 指标卡底
    COLORREF card_back_alt;     // 次级卡底 / 数字网格底
    COLORREF panel_back;        // 图表区背景（替代现有 RGB(252,252,255)）
    // 线条与文字
    COLORREF grid;              // 网格线（替代 RGB(180,180,180) 系）
    COLORREF axis;              // 坐标轴
    COLORREF text_primary;      // 主文字（标题/数值）
    COLORREF text_secondary;    // 次文字（轴标签/说明）
    COLORREF text_disabled;     // 禁用 / 占位（"无记录"、"—"）
    // 强调
    COLORREF highlight;         // 高亮/强调（= 主题色原始色）
    COLORREF accent;            // 区块标题竖条 / 强调条
    COLORREF selected;          // 列表选中行
    // 系列色（柱 / 折线 / 环形 / 条形 / 雷达）
    COLORREF series[8];
    // 热力图 5 级色阶（由浅到深）
    COLORREF heat[5];
    // 语义色
    COLORREF good;              // 完播
    COLORREF warn;              // 跳过
    COLORREF bad;               // 出错
};

class CStatTheme
{
public:
    // 按当前 dark_mode 返回对应调色板
    static const StatThemeColors& Get();

    // 应用主题到对话框（背景色）
    static void ApplyDialog(CWnd* pDlg);

    // 线性插值（热力图/渐变用），t 会被钳制到 [0,1]
    static COLORREF Lerp(COLORREF a, COLORREF b, double t);

    // 对比度（WCAG 相对亮度比），用于自检
    static double ContrastRatio(COLORREF fg, COLORREF bg);
};

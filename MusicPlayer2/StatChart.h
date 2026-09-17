#pragma once
#include "DrawCommon.h"
#include "CVariant.h"

//════════════════════════════════════════════════════════════════════
// 统计页共享基建：取色 / 字号 / 语言表 / 自绘图表控件基类
//
// 说明（对齐项目规范，见 Documents/statistics-optimization/03-界面层重写设计.md）：
//  1. 取色统一走 CPlayerUIHelper::GetUIColors + theApp.m_app_setting_data.theme_color，
//     与 CListCtrlEx 默认取色同源；本页不引入对话框级深色模式（项目内所有普通对话框
//     均不跟随 dark_mode，统计页单独跟随反而与全局不一致）。
//  2. 字号统一走 theApp.m_font_set.GetFontBySize(n)（n 为 8~16 的整数 pt），
//     禁止 CreatePointFont + 硬编码字体名。
//  3. 自绘控件继承 CStatChart，在 OnPaint 里用 CDrawCommon 绘制；
//     不再使用 SS_BLACKFRAME→SS_OWNERDRAW + 父窗口 OnDrawItem 的写法。
//  4. 用户可见文本统一经 StatText::T / StatText::TF 取自语言表。
//════════════════════════════════════════════════════════════════════

//取色入口：与 CPlayerUIHelper / theme_color 同源
namespace StatPalette
{
	//统计页用到的全部颜色（浅色一套，与普通对话框观感一致）
	struct StatPaletteColors
	{
		//容器
		COLORREF back;              //对话框 / 子页背景（= CTabDlg 统一背景）
		COLORREF card_back;         //指标卡底
		COLORREF card_back_alt;     //次级卡底 / 数字网格底
		COLORREF panel_back;        //图表区背景（= back，避免与页面背景形成色差）
		//线条与文字
		COLORREF grid;              //网格线
		COLORREF axis;              //坐标轴
		COLORREF text_primary;      //主文字（标题 / 数值）
		COLORREF text_secondary;    //次文字（轴标签 / 说明）
		COLORREF text_disabled;     //禁用 / 占位（“无记录”、“—”）
		//强调
		COLORREF highlight;         //高亮 / 强调（= 主题色原始色）
		COLORREF accent;            //区块标题竖条 / 强调条
		COLORREF selected;          //列表选中行
		//系列色（柱 / 折线 / 环形 / 条形 / 雷达）
		COLORREF series[8];
		//热力图 5 级色阶（由浅到深）
		COLORREF heat[5];
		//语义色
		COLORREF good;              //完播
		COLORREF warn;              //跳过
		COLORREF bad;               //出错
	};

	//构建当前调色板（浅色；主题色变化后重新调用即可生效）
	StatPaletteColors Get();

	//以下为常用单项取值的便捷接口
	COLORREF DialogBack();          //== CTabDlg 统一背景
	COLORREF PanelBack();
	COLORREF CardBack();
	COLORREF CardBackAlt();
	COLORREF Grid();
	COLORREF Axis();
	COLORREF TextPrimary();
	COLORREF TextSecondary();
	COLORREF TextDisabled();
	COLORREF Highlight();
	COLORREF Accent();
	COLORREF Selected();
	COLORREF Series(int index);     //系列色循环（index % 8）
	COLORREF Heat(int index);       //热力图色阶（index 钳制到 0~4）
	COLORREF SemGood();
	COLORREF SemWarn();
	COLORREF SemBad();

	//线性插值（热力图 / 渐变用），t 会被钳制到 [0,1]
	COLORREF Lerp(COLORREF a, COLORREF b, double t);
}

//字号档位：theApp.m_font_set 只提供 8~16 的整数 pt（CommonData.h）
namespace StatFont
{
	enum Level
	{
		Tiny = 8,       //轴标签、卡片小标题、说明、图例
		Body = 9,       //正文、指标标签
		SubHead = 10,   //区块标题、图标题、中号数值
		Heading = 11,   //页内主标题
		Title = 12,     //卡片大数值
		BigTitle = 13,  //DNA 主标题
		Hero = 14       //趋势页主标题
	};
}

//语言表取值：所有用户可见文本必须经此取得
namespace StatText
{
	std::wstring T(const wchar_t* key);
	std::wstring TFImpl(const wchar_t* key, const std::initializer_list<CVariant>& paras);
	template<typename... Args>
	std::wstring TF(const wchar_t* key, Args&&... args)
	{
		return TFImpl(key, { CVariant(std::forward<Args>(args))... });
	}
}

//统计页自绘图表控件基类
//控件自己在 OnPaint 里用 CDrawCommon 绘制，取代原先“父窗口 OnDrawItem”的做法；
//不移动控件自身（布局交给 AFX_DIALOG_LAYOUT），OnSize 仅触发重绘。
class CStatChart : public CStatic
{
public:
	CStatChart();
	~CStatChart() override;

protected:
	//派生类实现：在控件客户区绘制图表。
	//基类已铺好与子页一致的背景（StatPalette::DialogBack）。
	//draw：已 Create 好的绘图器（字体为控件默认字体，可用 SetFont 换档）
	//pDC ：需要 Pie / SelectClipRgn / 多行 DrawText 等 CDrawCommon 未覆盖的图元时直接使用
	virtual void DrawChart(CDrawCommon& draw, CDC* pDC, const CRect& rect) = 0;

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);

	DECLARE_MESSAGE_MAP()
};

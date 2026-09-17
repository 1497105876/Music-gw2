#include "stdafx.h"
#include "StatChart.h"
#include "MusicPlayer2.h"
#include "CPlayerUIHelper.h"
#include "WinVersionHelper.h"

namespace
{
	//构建一套浅色调色板（统计页不引入对话框级深色模式）
	void BuildColors(StatPalette::StatPaletteColors& c)
	{
		UIColors ui = CPlayerUIHelper::GetUIColors(false, false);
		const ColorTable& tc = theApp.m_app_setting_data.theme_color;

		//容器：背景与 CTabDlg 统一背景一致，图表区不再另设色差
		c.back = StatPalette::DialogBack();
		c.panel_back = c.back;
		c.card_back = RGB(246, 248, 252);
		c.card_back_alt = RGB(238, 240, 246);

		//线条与文字
		c.grid = RGB(180, 180, 180);
		c.axis = RGB(160, 160, 160);
		c.text_primary = ui.color_text;
		c.text_secondary = ui.color_text_lable;
		c.text_disabled = ui.color_text_disabled;

		//强调
		c.highlight = tc.original_color;
		c.accent = tc.original_color;
		c.selected = ui.color_list_selected;

		//系列色（8 色循环）
		static const COLORREF base_series[8] = {
			RGB(100, 170, 230), RGB(70, 200, 130), RGB(240, 150, 90), RGB(190, 130, 210),
			RGB(235, 130, 130), RGB(90, 190, 190), RGB(230, 200, 80), RGB(140, 160, 230)
		};
		for (int i = 0; i < 8; i++)
			c.series[i] = base_series[i];

		//热力图：由图表背景向主题色插值出 5 级（由浅到深）
		for (int i = 0; i < 5; i++)
			c.heat[i] = StatPalette::Lerp(c.panel_back, c.highlight, static_cast<double>(i) / 4.0);

		//语义色
		c.good = RGB(70, 190, 120);
		c.warn = RGB(240, 170, 70);
		c.bad = RGB(220, 80, 80);
	}
}

namespace StatPalette
{
	StatPaletteColors Get()
	{
		//主题色 / 语言设置在运行期可变，每次重建（成本可忽略，与原实现一致）
		static StatPaletteColors colors;
		BuildColors(colors);
		return colors;
	}

	COLORREF DialogBack()
	{
		//与 CTabDlg 的统一背景保持一致（TabDlg.cpp）
		return CWinVersionHelper::IsWindows11OrLater() ? RGB(249, 249, 249) : RGB(255, 255, 255);
	}

	COLORREF PanelBack() { return Get().panel_back; }
	COLORREF CardBack() { return Get().card_back; }
	COLORREF CardBackAlt() { return Get().card_back_alt; }
	COLORREF Grid() { return Get().grid; }
	COLORREF Axis() { return Get().axis; }
	COLORREF TextPrimary() { return Get().text_primary; }
	COLORREF TextSecondary() { return Get().text_secondary; }
	COLORREF TextDisabled() { return Get().text_disabled; }
	COLORREF Highlight() { return Get().highlight; }
	COLORREF Accent() { return Get().accent; }
	COLORREF Selected() { return Get().selected; }
	COLORREF Series(int index) { return Get().series[index % 8]; }
	COLORREF Heat(int index)
	{
		if (index < 0) index = 0;
		if (index > 4) index = 4;
		return Get().heat[index];
	}
	COLORREF SemGood() { return Get().good; }
	COLORREF SemWarn() { return Get().warn; }
	COLORREF SemBad() { return Get().bad; }

	COLORREF Lerp(COLORREF a, COLORREF b, double t)
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
}

namespace StatText
{
	std::wstring T(const wchar_t* key)
	{
		return theApp.m_str_table.LoadText(key);
	}

	std::wstring TFImpl(const wchar_t* key, const std::initializer_list<CVariant>& paras)
	{
		return theApp.m_str_table.LoadTextFormat(key, paras);
	}
}

BEGIN_MESSAGE_MAP(CStatChart, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
END_MESSAGE_MAP()

CStatChart::CStatChart()
{
}

CStatChart::~CStatChart()
{
}

void CStatChart::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(rect);
	if (rect.Width() <= 1 || rect.Height() <= 1)
		return;

	//双缓冲绘制，避免缩放 / 切页时闪烁（沿用项目既有 CDrawDoubleBuffer）
	CDrawDoubleBuffer buffer(&dc, rect);
	CDC* pDC = buffer.GetMemDC();

	CDrawCommon draw;
	draw.Create(pDC, GetFont());
	draw.FillRect(rect, StatPalette::DialogBack());     //背景与子页一致
	pDC->SetBkMode(TRANSPARENT);

	DrawChart(draw, pDC, rect);
}

BOOL CStatChart::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;        //背景由 OnPaint 统一铺，避免默认擦除造成闪烁
}

void CStatChart::OnSize(UINT nType, int cx, int cy)
{
	CStatic::OnSize(nType, cx, cy);
	Invalidate(FALSE);      //只重绘，绝不 MoveWindow（布局交给 AFX_DIALOG_LAYOUT）
}

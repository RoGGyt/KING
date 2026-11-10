/* KING client: settings tab. */
#include "king.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace
{
constexpr float KING_LINE = 20.0f;
constexpr float KING_HEADLINE = 20.0f;
constexpr float KING_TEXT = 13.0f;
constexpr float KING_MARGIN = 10.0f;
constexpr float KING_MARGIN_SMALL = 5.0f;
constexpr float KING_SECTION_GAP = 20.0f;

// three levels of text, so a page reads at a glance: gold headlines, white controls and
// values, dimmed descriptions and hints
const ColorRGBA KING_ACCENT(0.96f, 0.76f, 0.30f, 1.0f);
const ColorRGBA KING_TEXT_DESC(0.80f, 0.82f, 0.88f, 0.78f);
const ColorRGBA KING_TEXT_HINT(0.78f, 0.80f, 0.86f, 0.55f);

void Headline(CUi *pUi, CUIRect &View, const char *pText)
{
	CUIRect Label, Bar;
	View.HSplitTop(KING_HEADLINE, &Label, &View);
	// a short gold bar in front of the title marks where a section starts
	Label.VSplitLeft(3.0f, &Bar, &Label);
	Bar.HMargin(3.0f, &Bar);
	Bar.Draw(KING_ACCENT, IGraphics::CORNER_ALL, 1.5f);
	Label.VSplitLeft(7.0f, nullptr, &Label);
	pUi->TextRender()->TextColor(KING_ACCENT);
	pUi->DoLabel(&Label, pText, KING_HEADLINE, TEXTALIGN_ML);
	pUi->TextRender()->TextColor(pUi->TextRender()->DefaultTextColor());
	View.HSplitTop(KING_MARGIN_SMALL, nullptr, &View);
}

// a status or value line; small lines are hints and are dimmed (Muted = false keeps the
// color the caller set, for errors)
void TextLine(CUi *pUi, CUIRect &View, const char *pText, float Size = KING_TEXT, bool Muted = true)
{
	CUIRect Label;
	View.HSplitTop(Size + 4.0f, &Label, &View);
	const bool Dim = Muted && Size < KING_TEXT;
	if(Dim)
		pUi->TextRender()->TextColor(KING_TEXT_HINT);
	pUi->DoLabel(&Label, pText, Size, TEXTALIGN_ML);
	if(Dim)
		pUi->TextRender()->TextColor(pUi->TextRender()->DefaultTextColor());
}

// a description paragraph (dimmed), wrapped to the column width (translations differ in length)
void TextBlock(CUi *pUi, ITextRender *pTextRender, CUIRect &View, const char *pText, float Size = KING_TEXT)
{
	const float Height = pTextRender->TextBoundingBox(Size, pText, -1, View.w).m_H;
	CUIRect Label;
	View.HSplitTop(std::max(Height, Size) + 4.0f, &Label, &View);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_EnableWidthCheck = false;
	pTextRender->TextColor(Size < KING_TEXT ? KING_TEXT_HINT : KING_TEXT_DESC);
	pUi->DoLabel(&Label, pText, Size, TEXTALIGN_TL, Props);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
}
// Section box like the TClient settings: the box is drawn from last frame's layout.
struct SKingSections
{
	std::vector<CUIRect> m_vBoxes;
	std::vector<CUIRect> m_vNext;
	float m_PrevScrollY = 0.0f;
	void Draw(float ScrollY)
	{
		const float Shift = ScrollY - m_PrevScrollY; // the page scrolled since last frame
		m_PrevScrollY = ScrollY;
		for(CUIRect Box : m_vBoxes)
		{
			Box.y += Shift;
			const float Padding = KING_MARGIN;
			Box.x -= Padding;
			Box.y -= Padding;
			Box.w += Padding * 2.0f;
			Box.h += Padding * 2.0f;
			// a card a bit lighter than the page with a thin edge, so sections stand apart
			Box.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f), IGraphics::CORNER_ALL, 10.0f);
			CUIRect Inner;
			Box.Margin(1.0f, &Inner);
			Inner.Draw(ColorRGBA(0.105f, 0.105f, 0.125f, 0.97f), IGraphics::CORNER_ALL, 9.0f);
		}
	}
	void Begin(const CUIRect &Column) { m_vNext.push_back(Column); }
	void End(const CUIRect &Column) { m_vNext.back().h = Column.y - m_vNext.back().y; }
	void Swap()
	{
		m_vBoxes = m_vNext;
		m_vNext.clear();
	}
};

} // namespace

void CMenus::RenderSettingsKing(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	MainView.VSplitRight(10.0f, &MainView, nullptr); // room for the scrollbar

	static SKingSections s_Sections;
	s_Sections.Draw(MainView.y);

	CUIRect LeftView, RightView, Button;
	MainView.Margin(KING_MARGIN, &MainView);
	MainView.VSplitMid(&LeftView, &RightView, KING_MARGIN * 4.0f);
	LeftView.Margin(KING_MARGIN_SMALL, &LeftView);
	RightView.Margin(KING_MARGIN_SMALL, &RightView);

	// ***** Input mode ***** //
	s_Sections.Begin(LeftView);
	Headline(Ui(), LeftView, Localize("KING Input"));
	{
		// all modes in one dropdown (too many for buttons); KING first, it is the default
		static const int s_aModes[] = {CKing::INPUT_MODE_KING, CKing::INPUT_MODE_BEST, CKing::INPUT_MODE_F, CKing::INPUT_MODE_CLASSIC,
			CKing::INPUT_MODE_DELTA, CKing::INPUT_MODE_PING_COMP, CKing::INPUT_MODE_SMOOTH, CKing::INPUT_MODE_CUSTOM};
		const char *apNames[std::size(s_aModes)];
		int Selected = 0;
		for(size_t i = 0; i < std::size(s_aModes); i++)
		{
			apNames[i] = s_aModes[i] == CKing::INPUT_MODE_CUSTOM ? Localize("Custom", "KING") : CKing::InputModeName(s_aModes[i]);
			if(s_aModes[i] == g_Config.m_KgInputMode)
				Selected = (int)i;
		}

		CUIRect Row, Label, Control;
		LeftView.HSplitTop(KING_LINE, &Row, &LeftView);
		Row.VSplitLeft(std::min(120.0f, Row.w * 0.3f), &Label, &Control);
		char aLabel[64];
		str_format(aLabel, sizeof(aLabel), "%s:", Localize("Mode"));
		Ui()->DoLabel(&Label, aLabel, KING_TEXT + 1.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_ModeState;
		static CScrollRegion s_ModeScrollRegion;
		s_ModeState.m_SelectionPopupContext.m_pScrollRegion = &s_ModeScrollRegion; // the popup needs one, or it crashes
		const int NewSelected = Ui()->DoDropDown(&Control, Selected, apNames, (int)std::size(s_aModes), s_ModeState);
		if(NewSelected >= 0 && NewSelected < (int)std::size(s_aModes))
			g_Config.m_KgInputMode = s_aModes[NewSelected];
	}
	LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);

	switch(g_Config.m_KgInputMode)
	{
	case CKing::INPUT_MODE_BEST:
	{
		TextBlock(Ui(), TextRender(), LeftView, Localize("Works exactly like Best input in BestClient: your tee is shown the offset ahead. Smoothing shrinks the offset, latency comp stretches it, the curve shapes the move between ticks."));
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		char aLabel[64];
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		str_format(aLabel, sizeof(aLabel), Localize("Prediction offset %d.%02d ticks"), g_Config.m_KgBestOffset / 100, g_Config.m_KgBestOffset % 100);
		Ui()->DoScrollbarOption(&g_Config.m_KgBestOffset, &g_Config.m_KgBestOffset, &Button, aLabel, 0, 500, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgBestSmoothing, &g_Config.m_KgBestSmoothing, &Button, Localize("Smoothing"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgBestLatencyComp, &g_Config.m_KgBestLatencyComp, &Button, Localize("Latency comp"), 0, 50, &CUi::ms_LinearScrollbarScale, 0u, "%");
		static std::vector<CButtonContainer> s_vCurve = {{}, {}, {}};
		int Curve = g_Config.m_KgBestInterpolation;
		if(DoLine_RadioMenu(LeftView, Localize("Interpolation"), s_vCurve, {Localize("Linear"), Localize("Cubic"), Localize("Smooth")}, {0, 1, 2}, Curve))
			g_Config.m_KgBestInterpolation = Curve;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgBestOthers, Localize("Apply to other tees"), &g_Config.m_KgBestOthers, &LeftView, KING_LINE);
		char aNow[96];
		str_format(aNow, sizeof(aNow), Localize("= %.1f ms ahead"), GameClient()->m_King.BestFastMsExact());
		TextLine(Ui(), LeftView, aNow, 11.0f);
		break;
	}
	case CKing::INPUT_MODE_CLASSIC:
		TextBlock(Ui(), TextRender(), LeftView, Localize("Classic TClient fast input: your tee is shown this far ahead with your newest input. The margin stays manual."));
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		DoSliderWithScaledValue(&g_Config.m_KgClassicMs, &g_Config.m_KgClassicMs, &Button, Localize("Fast input"), 1, 100, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " ms");
		{
			char aInfo[256];
			str_format(aInfo, sizeof(aInfo), Localize("= %d.%02d ticks ahead (20 ms = 1 tick). More reacts earlier, but corrects harder when you change direction."), g_Config.m_KgClassicMs / 20, (g_Config.m_KgClassicMs % 20) * 5);
			TextBlock(Ui(), TextRender(), LeftView, aInfo, 11.0f);
		}
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgSmoothSelf, Localize("Smooth small corrections (30 ms glide, no jumps)"), &g_Config.m_KgSmoothSelf, &LeftView, KING_LINE);
		break;
	case CKing::INPUT_MODE_KING:
		TextBlock(Ui(), TextRender(), LeftView, Localize("One tick of fast input (20 ms) and light correction smoothing."));
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		DoSliderWithScaledValue(&g_Config.m_KgFastInputAmount, &g_Config.m_KgFastInputAmount, &Button, Localize("Fast input"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " ms");
		TextBlock(Ui(), TextRender(), LeftView, Localize("20 ms = one tick. Higher shows you further ahead, but corrects more when you change direction."), 11.0f);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgKingSmooth, Localize("Smooth small corrections (30 ms glide, no jumps)"), &g_Config.m_KgKingSmooth, &LeftView, KING_LINE);
		break;
	case CKing::INPUT_MODE_F:
	{
		TextBlock(Ui(), TextRender(), LeftView, Localize("Your tee is shown exactly this far ahead, set in thousandths of a tick (0.001 tick = 0.02 ms)."));
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		char aLabel[64];
		str_format(aLabel, sizeof(aLabel), Localize("Offset %d.%03d ticks"), g_Config.m_KgFInputAmount / 1000, g_Config.m_KgFInputAmount % 1000);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgFInputAmount, &g_Config.m_KgFInputAmount, &Button, aLabel, 0, 5000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "");

		// fine tuning: -0.01 -0.001 [exact value] +0.001 +0.01
		CUIRect Row, Minus10, Minus1, Value, Plus1, Plus10;
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Row, &LeftView);
		const float StepW = std::min(54.0f, Row.w / 6.0f);
		Row.VSplitLeft(StepW, &Minus10, &Row);
		Row.VSplitLeft(2.0f, nullptr, &Row);
		Row.VSplitLeft(StepW, &Minus1, &Row);
		Row.VSplitRight(StepW, &Row, &Plus10);
		Row.VSplitRight(2.0f, &Row, nullptr);
		Row.VSplitRight(StepW, &Row, &Plus1);
		Row.VMargin(4.0f, &Value);
		static CButtonContainer s_aStep[4];
		int Amount = g_Config.m_KgFInputAmount;
		if(DoButton_Menu(&s_aStep[0], "-0.01", 0, &Minus10))
			Amount -= 10;
		if(DoButton_Menu(&s_aStep[1], "-0.001", 0, &Minus1))
			Amount -= 1;
		if(DoButton_Menu(&s_aStep[2], "+0.001", 0, &Plus1))
			Amount += 1;
		if(DoButton_Menu(&s_aStep[3], "+0.01", 0, &Plus10))
			Amount += 10;
		static CLineInputNumber s_FValue;
		static int s_ShownAmount = -1;
		if(!s_FValue.IsActive() && s_ShownAmount != g_Config.m_KgFInputAmount)
		{
			char aValue[16];
			str_format(aValue, sizeof(aValue), "%d.%03d", g_Config.m_KgFInputAmount / 1000, g_Config.m_KgFInputAmount % 1000);
			s_FValue.Set(aValue);
			s_ShownAmount = g_Config.m_KgFInputAmount;
		}
		if(Ui()->DoEditBox(&s_FValue, &Value, KING_TEXT))
		{
			const float Ticks = s_FValue.GetFloat();
			Amount = round_to_int(Ticks * 1000.0f);
			s_ShownAmount = Amount;
		}
		g_Config.m_KgFInputAmount = std::clamp(Amount, 0, 5000);

		char aInfo[192];
		str_format(aInfo, sizeof(aInfo), Localize("= %.2f ms ahead. More than 1 tick shows you earlier, but corrects harder when you change direction."), g_Config.m_KgFInputAmount * 0.02f);
		TextBlock(Ui(), TextRender(), LeftView, aInfo, 11.0f);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgFInputSmooth, Localize("Smooth small corrections (30 ms glide, no jumps)"), &g_Config.m_KgFInputSmooth, &LeftView, KING_LINE);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgFInputOthers, Localize("Shift other tees too"), &g_Config.m_KgFInputOthers, &LeftView, KING_LINE);
		break;
	}
	case CKing::INPUT_MODE_DELTA:
		TextBlock(Ui(), TextRender(), LeftView, Localize("A fixed offset in hundredths of a tick, also between whole ticks (0.75 tick = 15 ms)."));
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		{
			char aLabel[64];
			str_format(aLabel, sizeof(aLabel), Localize("Offset %d.%02d ticks"), g_Config.m_KgDeltaAmount / 100, g_Config.m_KgDeltaAmount % 100);
			Ui()->DoScrollbarOption(&g_Config.m_KgDeltaAmount, &g_Config.m_KgDeltaAmount, &Button, aLabel, 0, 500, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "");
		}
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgDeltaOthers, Localize("Shift other tees too"), &g_Config.m_KgDeltaOthers, &LeftView, KING_LINE);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgSmoothSelf, Localize("Smooth small corrections (30 ms glide, no jumps)"), &g_Config.m_KgSmoothSelf, &LeftView, KING_LINE);
		break;
	case CKing::INPUT_MODE_PING_COMP:
		TextBlock(Ui(), TextRender(), LeftView, Localize("Fast input follows your ping: your tee is shown this share of the round trip ahead (up to 100 ms)."));
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgPingComp, &g_Config.m_KgPingComp, &Button, Localize("Share of ping"), 0, 50, &CUi::ms_LinearScrollbarScale, 0u, "%");
		{
			char aNow[64];
			str_format(aNow, sizeof(aNow), Localize("Now: %d ms ahead"), GameClient()->m_King.PingCompAmount());
			TextLine(Ui(), LeftView, aNow, 11.0f);
		}
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgSmoothSelf, Localize("Smooth small corrections (30 ms glide, no jumps)"), &g_Config.m_KgSmoothSelf, &LeftView, KING_LINE);
		break;
	case CKing::INPUT_MODE_SMOOTH:
		TextBlock(Ui(), TextRender(), LeftView, Localize("Like KING mode, but when the server corrects your tee it glides to the right spot instead of jumping. Visual only: your inputs and hitbox are not delayed."));
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		DoSliderWithScaledValue(&g_Config.m_KgFastInputAmount, &g_Config.m_KgFastInputAmount, &Button, Localize("Fast input"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " ms");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgSmoothMs, &g_Config.m_KgSmoothMs, &Button, Localize("Smoothing"), 10, 300, &CUi::ms_LinearScrollbarScale, 0u, " ms");
		break;
	default:
		TextBlock(Ui(), TextRender(), LeftView, Localize("Uses your fast input settings as they are (Settings > Input)."));
		break;
	}

	LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgSmoothOthers, Localize("Smooth corrections of other tees (30 ms, visual only)"), &g_Config.m_KgSmoothOthers, &LeftView, KING_LINE);

	s_Sections.End(LeftView);

	// ***** Prediction margin ***** //
	LeftView.HSplitTop(KING_SECTION_GAP + KING_MARGIN, nullptr, &LeftView);
	s_Sections.Begin(LeftView);
	Headline(Ui(), LeftView, Localize("Prediction Margin", "KING"));

	const bool ForcedManual = g_Config.m_KgInputMode == CKing::INPUT_MODE_CLASSIC;
	if(ForcedManual)
		TextBlock(Ui(), TextRender(), LeftView, Localize("Manual margin (Classic works exactly like TClient)"));
	else
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgAutoMargin, Localize("Auto margin (adapts to jitter)"), &g_Config.m_KgAutoMargin, &LeftView, KING_LINE);

	if(!ForcedManual && g_Config.m_KgAutoMargin)
	{
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgAutoMarginMin, &g_Config.m_KgAutoMarginMin, &Button, Localize("Min (stock 10)"), 1, 30, &CUi::ms_LinearScrollbarScale, 0u, " ms");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgAutoMarginMax, &g_Config.m_KgAutoMarginMax, &Button, Localize("Max"), 5, 100, &CUi::ms_LinearScrollbarScale, 0u, " ms");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgAutoMarginSafety, &g_Config.m_KgAutoMarginSafety, &Button, Localize("Safety"), 0, 20, &CUi::ms_LinearScrollbarScale, 0u, " ms");
		if(g_Config.m_KgAutoMarginMax < g_Config.m_KgAutoMarginMin)
			g_Config.m_KgAutoMarginMax = g_Config.m_KgAutoMarginMin;
	}
	else
	{
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, Localize("Margin"), 1, 75, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " ms");
	}

	s_Sections.End(LeftView);

	// ***** Live network ***** //
	s_Sections.Begin(RightView);
	Headline(Ui(), RightView, Localize("Live Network"));
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		const SKingNetStats Stats = Client()->KingNetStats();
		char aBuf[128];
		if(Stats.m_Valid)
		{
			str_format(aBuf, sizeof(aBuf), Localize("Ping: %d ms"), round_to_int(Stats.m_RttMs));
			TextLine(Ui(), RightView, aBuf);
			str_format(aBuf, sizeof(aBuf), Localize("Jitter: %.1f ms  (input spread %.1f ms)"), Stats.m_JitterMs, Stats.m_SpreadMs);
			TextLine(Ui(), RightView, aBuf);
		}
		else
			TextLine(Ui(), RightView, Localize("Measuring..."));

		if(Stats.m_MarginLocked)
			str_format(aBuf, sizeof(aBuf), Localize("Margin: %d ms (server forces 10 ms)"), Stats.m_MarginMs);
		else
		{
			if(Stats.m_MarginAuto)
				str_format(aBuf, sizeof(aBuf), Localize("Margin: %.1f ms (auto, heading to %.0f)"), Stats.m_MarginExactMs, Stats.m_MarginTargetMs);
			else
				str_format(aBuf, sizeof(aBuf), Localize("Margin: %.1f ms (fixed)"), Stats.m_MarginExactMs);
		}
		TextLine(Ui(), RightView, aBuf);
		str_format(aBuf, sizeof(aBuf), Localize("Late inputs this session: %d"), Stats.m_LateInputs);
		TextLine(Ui(), RightView, aBuf);
		const CKing::SLagStats &Lag = GameClient()->m_King.LagStats();
		str_format(aBuf, sizeof(aBuf), Localize("Corrections: %d (biggest %.0f units)"), Lag.m_Corrections, Lag.m_MaxCorrection);
		TextLine(Ui(), RightView, aBuf);
		str_format(aBuf, sizeof(aBuf), Localize("Stutters: %d   Snapshot gaps: %d"), Lag.m_Stutters, Lag.m_SnapGaps);
		TextLine(Ui(), RightView, aBuf);
	}
	else
		TextLine(Ui(), RightView, Localize("Join a server to see live stats."));

	RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgNetHud, Localize("Show network HUD in game"), &g_Config.m_KgNetHud, &RightView, KING_LINE);
	TextLine(Ui(), RightView, Localize("Every lag is written with its cause to"), 11.0f);
	TextLine(Ui(), RightView, "%APPDATA%\\DDNet\\king_lag.log", 11.0f);

	s_Sections.End(RightView);

	// ***** Updates ***** //
	RightView.HSplitTop(KING_SECTION_GAP + KING_MARGIN, nullptr, &RightView);
	s_Sections.Begin(RightView);
	Headline(Ui(), RightView, Localize("Updates"));
	{
		CKing &King = GameClient()->m_King;
		char aBuf[128];
		switch(King.UpdateState())
		{
		case CKing::UPDATE_CHECKING: str_copy(aBuf, Localize("Checking for updates...")); break;
		case CKing::UPDATE_LATEST: str_format(aBuf, sizeof(aBuf), Localize("You have the latest version (%s)"), KING_VERSION); break;
		case CKing::UPDATE_AVAILABLE: str_format(aBuf, sizeof(aBuf), Localize("KING %s is available (you have %s)"), King.LatestVersion(), KING_VERSION); break;
		case CKing::UPDATE_FAILED: str_copy(aBuf, Localize("Update check failed (no internet or GitHub limit)")); break;
		default: str_format(aBuf, sizeof(aBuf), Localize("Version %s"), KING_VERSION); break;
		}
		TextLine(Ui(), RightView, aBuf);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgUpdateCheck, Localize("Check for updates on start"), &g_Config.m_KgUpdateCheck, &RightView, KING_LINE);

		CUIRect CheckButton, DownloadButton;
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		RightView.HSplitTop(KING_LINE * 1.25f, &Button, &RightView);
		Button.VSplitMid(&CheckButton, &DownloadButton, KING_MARGIN_SMALL);
		static CButtonContainer s_CheckButton;
		if(DoButton_Menu(&s_CheckButton, King.UpdateState() == CKing::UPDATE_CHECKING ? Localize("Checking...") : Localize("Check now"), 0, &CheckButton))
			King.CheckForUpdates();
		static CButtonContainer s_DownloadButton;
		if(King.Installing())
			Ui()->RenderProgressBar(DownloadButton, King.DownloadProgress());
		else if(DoButton_Menu(&s_DownloadButton, King.CanInstallUpdate() ? Localize("Update now") : (King.UpdateAvailable() ? Localize("Download update") : Localize("Releases page")), 0, &DownloadButton))
		{
			if(King.UpdateAvailable())
				King.InstallUpdate();
			else
				King.OpenUpdatePage();
		}

		if(King.Installing())
		{
			str_format(aBuf, sizeof(aBuf), Localize("Downloading %s... %d%% (KING restarts by itself)"), King.LatestVersion(), round_to_int(King.DownloadProgress() * 100.0f));
			TextLine(Ui(), RightView, aBuf, 11.0f);
		}
		else if(King.InstallError()[0])
		{
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			TextLine(Ui(), RightView, King.InstallError(), 11.0f, false);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
	s_Sections.End(RightView);
	s_Sections.Swap();

	// scroll height = taller column
	CUIRect ScrollRect;
	ScrollRect.x = MainView.x;
	ScrollRect.y = std::max(LeftView.y, RightView.y) + KING_MARGIN;
	ScrollRect.w = MainView.w;
	ScrollRect.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRect);
	s_ScrollRegion.End();
}

namespace
{
// "Label ............ [control]" row; returns the control rect
CUIRect LabeledRow(CUi *pUi, CUIRect &Column, const char *pLabel, float LabelFraction = 0.32f)
{
	CUIRect Row, Label, Control;
	Column.HSplitTop(KING_LINE, &Row, &Column);
	Column.HSplitTop(KING_MARGIN_SMALL, nullptr, &Column);
	Row.VSplitLeft(Row.w * LabelFraction, &Label, &Control);
	pUi->DoLabel(&Label, pLabel, KING_TEXT + 1.0f, TEXTALIGN_ML);
	return Control;
}
} // namespace

void CMenus::RenderSettingsKingDisplay(CUIRect MainView)
{
	// scrolls, so bigger menu sizes still fit
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	MainView.VSplitRight(10.0f, &MainView, nullptr); // room for the scrollbar

	static SKingSections s_Sections;
	s_Sections.Draw(MainView.y);

	CUIRect LeftView, RightView, Button, Label;
	MainView.Margin(KING_MARGIN, &MainView);
	MainView.VSplitMid(&LeftView, &RightView, KING_MARGIN * 4.0f);
	LeftView.Margin(KING_MARGIN_SMALL, &LeftView);
	RightView.Margin(KING_MARGIN_SMALL, &RightView);

	// ***** Interface size (like Minecraft's GUI scale) ***** //
	s_Sections.Begin(LeftView);
	Headline(Ui(), LeftView, Localize("Interface Size"));
	{
		static std::vector<CButtonContainer> s_vMenuScale = {{}, {}, {}, {}, {}, {}};
		int Value = g_Config.m_KgUiScale;
		if(DoLine_RadioMenu(LeftView, Localize("Menus"), s_vMenuScale, {"80%", "90%", "100%", "110%", "120%", "130%"}, {80, 90, 100, 110, 120, 130}, Value))
			g_Config.m_KgUiScale = Value;
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgScoreboardScale, &g_Config.m_KgScoreboardScale, &Button, Localize("Scoreboard (TAB)"), 50, 150, &CUi::ms_LinearScrollbarScale, 0u, "%");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClChatFontSize, &g_Config.m_ClChatFontSize, &Button, Localize("Chat"), 10, 100);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		TextLine(Ui(), LeftView, Localize("Console: kg_ui_scale, kg_scoreboard_scale (50-150)"), 11.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	s_Sections.End(LeftView);

	// ***** Aspect ratio ***** //
	LeftView.HSplitTop(KING_SECTION_GAP + KING_MARGIN, nullptr, &LeftView);
	s_Sections.Begin(LeftView);
	Headline(Ui(), LeftView, Localize("Aspect Ratio"));
	{
		static CUi::SDropDownState s_PresetState;
		static CScrollRegion s_PresetScrollRegion;
		s_PresetState.m_SelectionPopupContext.m_pScrollRegion = &s_PresetScrollRegion; // the popup needs one, or it crashes
		const char *s_apPresets[] = {Localize("Native"), "4:3", "5:4", "3:2", "16:10", "16:9", Localize("Custom", "KING")};
		CUIRect Control = LabeledRow(Ui(), LeftView, Localize("Preset"));
		const int Preset = Ui()->DoDropDown(&Control, std::clamp(g_Config.m_KgAspectRatio, 0, 6), s_apPresets, std::size(s_apPresets), s_PresetState);
		g_Config.m_KgAspectRatio = Preset;

		static CUi::SDropDownState s_ApplyState;
		static CScrollRegion s_ApplyScrollRegion;
		s_ApplyState.m_SelectionPopupContext.m_pScrollRegion = &s_ApplyScrollRegion; // the popup needs one, or it crashes
		const char *s_apApply[] = {Localize("Full"), Localize("World only")};
		Control = LabeledRow(Ui(), LeftView, Localize("Apply"));
		g_Config.m_KgAspectApply = Ui()->DoDropDown(&Control, std::clamp(g_Config.m_KgAspectApply, 0, 1), s_apApply, std::size(s_apApply), s_ApplyState);

		if(g_Config.m_KgAspectRatio == 6)
		{
			static CLineInputNumber s_Width, s_Height;
			static bool s_Init = false;
			if(!s_Init)
			{
				s_Width.SetInteger(g_Config.m_KgAspectCustomW);
				s_Height.SetInteger(g_Config.m_KgAspectCustomH);
				s_Init = true;
			}
			Control = LabeledRow(Ui(), LeftView, Localize("Custom size"));
			CUIRect WidthBox, Colon, HeightBox;
			Control.VSplitMid(&WidthBox, &HeightBox, 16.0f);
			Colon = WidthBox;
			Colon.x = WidthBox.x + WidthBox.w;
			Colon.w = 16.0f;
			Ui()->DoEditBox(&s_Width, &WidthBox, KING_TEXT);
			Ui()->DoLabel(&Colon, ":", KING_TEXT + 1.0f, TEXTALIGN_MC);
			Ui()->DoEditBox(&s_Height, &HeightBox, KING_TEXT);

			CUIRect ApplyRow;
			LeftView.HSplitTop(KING_LINE, &ApplyRow, &LeftView);
			LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
			ApplyRow.VSplitLeft(ApplyRow.w * 0.32f, nullptr, &ApplyRow);
			static CButtonContainer s_ApplyButton;
			if(DoButton_Menu(&s_ApplyButton, Localize("Apply"), 0, &ApplyRow))
			{
				const int W = s_Width.GetInteger();
				const int H = s_Height.GetInteger();
				if(W > 0 && H > 0 && W <= 10000 && H <= 10000 && W / (float)H >= 0.5f && W / (float)H <= 4.0f)
				{
					g_Config.m_KgAspectCustomW = W;
					g_Config.m_KgAspectCustomH = H;
				}
				s_Width.SetInteger(g_Config.m_KgAspectCustomW);
				s_Height.SetInteger(g_Config.m_KgAspectCustomH);
			}
		}

		char aNow[128];
		const float Screen = Graphics()->NativeScreenAspect();
		const float Game = CKing::AspectRatioValue(g_Config.m_KgAspectRatio);
		if(Game > 0.0f)
			str_format(aNow, sizeof(aNow), Localize("Screen %.2f, game %.2f: stretched x%.2f%s"), Screen, Game, Screen / Game, g_Config.m_KgAspectApply == CKing::ASPECT_APPLY_FULL ? Localize(" (world, HUD, menus)") : Localize(" (world only)"));
		else
			str_format(aNow, sizeof(aNow), Localize("Screen %.2f, the game uses the same"), Screen);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		TextLine(Ui(), LeftView, aNow, 11.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	s_Sections.End(LeftView);

	// ***** Interface ***** //
	LeftView.HSplitTop(KING_SECTION_GAP + KING_MARGIN, nullptr, &LeftView);
	s_Sections.Begin(LeftView);
	Headline(Ui(), LeftView, Localize("Interface"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgNetHud, Localize("Network HUD in game"), &g_Config.m_KgNetHud, &LeftView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgLagLog, Localize("Lag log (king_lag.log: every lag and its cause)"), &g_Config.m_KgLagLog, &LeftView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgBadgeShow, Localize("Crown next to KING players (game and TAB)"), &g_Config.m_KgBadgeShow, &LeftView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgBadgeShare, Localize("Show my crown to other KING players"), &g_Config.m_KgBadgeShare, &LeftView, KING_LINE);
	LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_KgAssetsTile, &g_Config.m_KgAssetsTile, &Button, Localize("Asset catalog tile size"), 60, 240, &CUi::ms_LinearScrollbarScale, 0u, "");
	s_Sections.End(LeftView);

	// ***** Music player ***** //
	s_Sections.Begin(RightView);
	{
		CUIRect Header, Reset;
		RightView.HSplitTop(KING_HEADLINE, &Header, &RightView);
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		Header.VSplitRight(KING_HEADLINE * 1.4f, &Header, &Reset);
		CUIRect Bar;
		Header.VSplitLeft(3.0f, &Bar, &Header);
		Bar.HMargin(3.0f, &Bar);
		Bar.Draw(KING_ACCENT, IGraphics::CORNER_ALL, 1.5f);
		Header.VSplitLeft(7.0f, nullptr, &Header);
		TextRender()->TextColor(KING_ACCENT);
		Ui()->DoLabel(&Header, Localize("Music Player"), KING_HEADLINE, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		static CButtonContainer s_ResetButton;
		if(Ui()->DoButton_FontIcon(&s_ResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &Reset, BUTTONFLAG_LEFT))
			GameClient()->m_King.ResetMusicSettings();
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgMusicHud, Localize("Enable music player"), &g_Config.m_KgMusicHud, &RightView, KING_LINE);
	RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
	{
		static CUi::SDropDownState s_ColorState;
		static CScrollRegion s_ColorScrollRegion;
		s_ColorState.m_SelectionPopupContext.m_pScrollRegion = &s_ColorScrollRegion; // the popup needs one, or it crashes
		const char *s_apColor[] = {Localize("Cover"), "KING", Localize("White")};
		CUIRect Control = LabeledRow(Ui(), RightView, Localize("Color mode"));
		g_Config.m_KgMusicColorMode = Ui()->DoDropDown(&Control, std::clamp(g_Config.m_KgMusicColorMode, 0, 2), s_apColor, std::size(s_apColor), s_ColorState);

		static CUi::SDropDownState s_VisState;
		static CScrollRegion s_VisScrollRegion;
		s_VisState.m_SelectionPopupContext.m_pScrollRegion = &s_VisScrollRegion; // the popup needs one, or it crashes
		const char *s_apVis[] = {Localize("Off"), Localize("Bottom"), Localize("Center")};
		Control = LabeledRow(Ui(), RightView, Localize("Visualizer mode"));
		g_Config.m_KgMusicVisualizer = Ui()->DoDropDown(&Control, std::clamp(g_Config.m_KgMusicVisualizer, 0, 2), s_apVis, std::size(s_apVis), s_VisState);

		RightView.HSplitTop(KING_LINE, &Button, &RightView);
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		Ui()->DoScrollbarOption(&g_Config.m_KgMusicTextScale, &g_Config.m_KgMusicTextScale, &Button, Localize("Text scale"), 50, 200, &CUi::ms_LinearScrollbarScale, 0u, "%");
		RightView.HSplitTop(KING_LINE, &Button, &RightView);
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		Ui()->DoScrollbarOption(&g_Config.m_KgMusicColumns, &g_Config.m_KgMusicColumns, &Button, Localize("Columns"), 3, 24);

		Control = LabeledRow(Ui(), RightView, Localize("Rounding"));
		CUIRect Cube, Soft;
		Control.VSplitMid(&Cube, &Soft, 0.0f);
		static CButtonContainer s_Cube, s_Soft;
		if(DoButton_Menu(&s_Cube, Localize("Cube"), g_Config.m_KgMusicRounding == 0, &Cube, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_KgMusicRounding = 0;
		if(DoButton_Menu(&s_Soft, Localize("Soft"), g_Config.m_KgMusicRounding == 1, &Soft, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_KgMusicRounding = 1;

		CUIRect LyricsRow, Badge;
		RightView.HSplitTop(KING_LINE, &LyricsRow, &RightView);
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		LyricsRow.VSplitRight(52.0f, &LyricsRow, &Badge);
		if(DoButton_CheckBox(&g_Config.m_KgMusicLyrics, Localize("Show song lyrics"), g_Config.m_KgMusicLyrics, &LyricsRow))
			g_Config.m_KgMusicLyrics ^= 1;
		Badge.Margin(2.0f, &Badge);
		Badge.Draw(ColorRGBA(0.86f, 0.16f, 0.16f, 1.0f), IGraphics::CORNER_ALL, 4.0f);
		Ui()->DoLabel(&Badge, "BETA", 11.0f, TEXTALIGN_MC);
	}

	// live preview + now playing + controls
	RightView.HSplitTop(KING_MARGIN, nullptr, &RightView);
	CKingMedia &Media = GameClient()->m_King.m_Media;
	if(!Media.Supported())
		TextLine(Ui(), RightView, Localize("Music player is not supported on this system."), 11.0f);
	else
	{
		CUIRect Preview;
		const float Unit = 2.0f;
		const float PillH = 15.0f * Unit * std::clamp(g_Config.m_KgMusicTextScale, 50, 200) / 100.0f;
		RightView.HSplitTop(PillH, &Preview, &RightView);
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		const SKingMediaInfo Info = Media.Get();
		if(g_Config.m_KgMusicHud && Info.m_Valid && (Info.m_aTitle[0] || Info.m_aArtist[0]))
			GameClient()->m_King.RenderMusicPill(Preview.x + Preview.w / 2.0f, Preview.y, Unit, "12:34", ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);
		else
			Ui()->DoLabel(&Preview, g_Config.m_KgMusicHud ? Localize("Play something (Spotify, browser, Yandex Music...) to see it here") : Localize("Music player is off"), 11.0f, TEXTALIGN_MC);

		char aBuf[600];
		if(Info.m_Valid && (Info.m_aTitle[0] || Info.m_aArtist[0]))
			str_format(aBuf, sizeof(aBuf), "%s%s%s", Info.m_aTitle, Info.m_aArtist[0] ? " - " : "", Info.m_aArtist);
		else
			aBuf[0] = '\0';

		CUIRect Row, Prev, Toggle, Next;
		RightView.HSplitTop(KING_LINE * 1.25f, &Row, &RightView);
		Row.VSplitRight(Row.h * 1.6f, &Row, &Next);
		Row.VSplitRight(KING_MARGIN_SMALL, &Row, nullptr);
		Row.VSplitRight(Row.h * 1.6f, &Row, &Toggle);
		Row.VSplitRight(KING_MARGIN_SMALL, &Row, nullptr);
		Row.VSplitRight(Row.h * 1.6f, &Row, &Prev);
		Row.VSplitRight(KING_MARGIN, &Label, nullptr);
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, aBuf, 12.0f, TEXTALIGN_ML, Props);
		static CButtonContainer s_Prev, s_Toggle, s_Next;
		if(Ui()->DoButton_FontIcon(&s_Prev, FontIcon::BACKWARD_STEP, 0, &Prev, BUTTONFLAG_LEFT))
			Media.Command(CKingMedia::COMMAND_PREVIOUS);
		if(Ui()->DoButton_FontIcon(&s_Toggle, Info.m_Playing ? FontIcon::PAUSE : FontIcon::PLAY, 0, &Toggle, BUTTONFLAG_LEFT))
			Media.Command(CKingMedia::COMMAND_TOGGLE);
		if(Ui()->DoButton_FontIcon(&s_Next, FontIcon::FORWARD_STEP, 0, &Next, BUTTONFLAG_LEFT))
			Media.Command(CKingMedia::COMMAND_NEXT);

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
		if(g_Config.m_KgMusicVisualizer != CKing::MUSIC_VIS_OFF && Media.VisualizerCapturing())
			TextLine(Ui(), RightView, Media.VisualizerExcludesGame() ? Localize("Visualizer listens to your PC (game sounds excluded)") : Localize("Visualizer listens to your PC output"), 11.0f);
		TextLine(Ui(), RightView, Localize("Binds: kg_music_toggle, kg_music_next, kg_music_prev"), 11.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	s_Sections.End(RightView);
	s_Sections.Swap();

	// scroll height = taller column
	CUIRect ScrollRect;
	ScrollRect.x = MainView.x;
	ScrollRect.y = std::max(LeftView.y, RightView.y) + KING_MARGIN * 2.0f;
	ScrollRect.w = MainView.w;
	ScrollRect.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRect);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsKingPerformance(CUIRect MainView)
{
	static SKingSections s_Sections;
	s_Sections.Draw(MainView.y);

	CUIRect LeftView, RightView, Button;
	MainView.Margin(KING_MARGIN, &MainView);
	MainView.VSplitMid(&LeftView, &RightView, KING_MARGIN * 4.0f);
	LeftView.Margin(KING_MARGIN_SMALL, &LeftView);
	RightView.Margin(KING_MARGIN_SMALL, &RightView);

	// ***** Optimizer ***** //
	s_Sections.Begin(LeftView);
	Headline(Ui(), LeftView, Localize("Optimizer"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptimizer, Localize("Enable optimizer"), &g_Config.m_KgOptimizer, &LeftView, KING_LINE);
	{
		// the options only work while the optimizer is on
		CUIRect Options = LeftView;
		Options.VSplitLeft(16.0f, nullptr, &Options);
		const float StartY = Options.y;
		if(!g_Config.m_KgOptimizer)
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.45f);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptNoParticles, Localize("Disable all particles render"), &g_Config.m_KgOptNoParticles, &Options, KING_LINE);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptNoQuads, Localize("Disable map quads render"), &g_Config.m_KgOptNoQuads, &Options, KING_LINE);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptNoHighDetail, Localize("Disable high detail"), &g_Config.m_KgOptNoHighDetail, &Options, KING_LINE);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptFpsFog, Localize("FPS fog (cull outside limit)"), &g_Config.m_KgOptFpsFog, &Options, KING_LINE);
		if(g_Config.m_KgOptFpsFog)
		{
			Options.HSplitTop(KING_LINE, &Button, &Options);
			Options.HSplitTop(KING_MARGIN_SMALL, nullptr, &Options);
			Ui()->DoScrollbarOption(&g_Config.m_KgOptFpsFogRadius, &g_Config.m_KgOptFpsFogRadius, &Button, Localize("Fog distance"), 10, 150, &CUi::ms_LinearScrollbarScale, 0u, Localize(" tiles"));
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		LeftView.HSplitTop(Options.y - StartY, nullptr, &LeftView);
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
	TextBlock(Ui(), TextRender(), LeftView, Localize("Only what you see changes: physics, hitboxes and inputs stay exactly the same."), 11.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	s_Sections.End(LeftView);

	// ***** Performance ***** //
	s_Sections.Begin(RightView);
	Headline(Ui(), RightView, Localize("Performance"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgHighPriority, Localize("High KING Process Priority"), &g_Config.m_KgHighPriority, &RightView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgLowerDiscord, Localize("Lower Discord's Process Priority"), &g_Config.m_KgLowerDiscord, &RightView, KING_LINE);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
	TextBlock(Ui(), TextRender(), RightView, Localize("Windows gives KING more CPU time than background apps. Discord gets less while KING runs and gets its normal priority back when KING closes."), 11.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	s_Sections.End(RightView);
	s_Sections.Swap();
}

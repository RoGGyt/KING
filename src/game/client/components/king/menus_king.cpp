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

void Headline(CUi *pUi, CUIRect &View, const char *pText)
{
	CUIRect Label;
	View.HSplitTop(KING_HEADLINE, &Label, &View);
	pUi->DoLabel(&Label, pText, KING_HEADLINE, TEXTALIGN_ML);
	View.HSplitTop(KING_MARGIN_SMALL, nullptr, &View);
}

void TextLine(CUi *pUi, CUIRect &View, const char *pText, float Size = KING_TEXT)
{
	CUIRect Label;
	View.HSplitTop(Size + 4.0f, &Label, &View);
	pUi->DoLabel(&Label, pText, Size, TEXTALIGN_ML);
}
} // namespace

void CMenus::RenderSettingsKing(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	MainView.VSplitRight(10.0f, &MainView, nullptr); // room for the scrollbar

	CUIRect LeftView, RightView, Button;
	MainView.VSplitMid(&LeftView, &RightView, KING_MARGIN * 3.0f);
	LeftView.Margin(KING_MARGIN_SMALL, &LeftView);
	RightView.Margin(KING_MARGIN_SMALL, &RightView);

	// ***** Input mode ***** //
	Headline(Ui(), LeftView, "KING Input");
	{
		int Mode = g_Config.m_KgInputMode;
		static std::vector<CButtonContainer> s_vModeButtonsA = {{}, {}, {}};
		static std::vector<CButtonContainer> s_vModeButtonsB = {{}, {}, {}, {}};
		bool Changed = DoLine_RadioMenu(LeftView, "Mode",
			s_vModeButtonsA,
			{CKing::InputModeName(CKing::INPUT_MODE_BEST), CKing::InputModeName(CKing::INPUT_MODE_KING), CKing::InputModeName(CKing::INPUT_MODE_CLASSIC)},
			{CKing::INPUT_MODE_BEST, CKing::INPUT_MODE_KING, CKing::INPUT_MODE_CLASSIC},
			Mode);
		Changed |= DoLine_RadioMenu(LeftView, "",
			s_vModeButtonsB,
			{CKing::InputModeName(CKing::INPUT_MODE_DELTA), CKing::InputModeName(CKing::INPUT_MODE_PING_COMP), CKing::InputModeName(CKing::INPUT_MODE_SMOOTH), CKing::InputModeName(CKing::INPUT_MODE_CUSTOM)},
			{CKing::INPUT_MODE_DELTA, CKing::INPUT_MODE_PING_COMP, CKing::INPUT_MODE_SMOOTH, CKing::INPUT_MODE_CUSTOM},
			Mode);
		if(Changed)
			g_Config.m_KgInputMode = Mode;
	}
	LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);

	switch(g_Config.m_KgInputMode)
	{
	case CKing::INPUT_MODE_BEST:
	{
		TextLine(Ui(), LeftView, "Offset + latency comp + correction smoothing +");
		TextLine(Ui(), LeftView, "blend curve. Auto: one tick ahead, smoothing");
		TextLine(Ui(), LeftView, "follows your connection.");
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		static std::vector<CButtonContainer> s_vPreset = {{}, {}};
		int Preset = g_Config.m_KgBestPreset;
		if(DoLine_RadioMenu(LeftView, "Preset", s_vPreset, {"Auto", "Custom"}, {1, 0}, Preset))
			g_Config.m_KgBestPreset = Preset;
		if(g_Config.m_KgBestPreset)
		{
			char aNow[96];
			str_format(aNow, sizeof(aNow), "Now: %d ms ahead, %d ms smoothing", std::max(GameClient()->m_King.BestFastMs(), 0), GameClient()->m_King.BestSmoothMs());
			TextLine(Ui(), LeftView, aNow, 11.0f);
		}
		else
		{
			char aLabel[64];
			LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
			str_format(aLabel, sizeof(aLabel), "Offset %d.%02d ticks", g_Config.m_KgBestOffset / 100, g_Config.m_KgBestOffset % 100);
			Ui()->DoScrollbarOption(&g_Config.m_KgBestOffset, &g_Config.m_KgBestOffset, &Button, aLabel, 0, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "");
			LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
			Ui()->DoScrollbarOption(&g_Config.m_KgBestLatencyComp, &g_Config.m_KgBestLatencyComp, &Button, "Latency comp", 0, 50, &CUi::ms_LinearScrollbarScale, 0u, "% of ping");
			LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
			Ui()->DoScrollbarOption(&g_Config.m_KgBestSmoothing, &g_Config.m_KgBestSmoothing, &Button, "Smoothing", 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
			static std::vector<CButtonContainer> s_vCurve = {{}, {}, {}};
			int Curve = g_Config.m_KgBestInterpolation;
			if(DoLine_RadioMenu(LeftView, "Curve", s_vCurve, {"Linear", "Cubic", "Smooth"}, {0, 1, 2}, Curve))
				g_Config.m_KgBestInterpolation = Curve;
		}
		break;
	}
	case CKing::INPUT_MODE_CLASSIC:
		TextLine(Ui(), LeftView, "Classic fast input in whole ticks (1-5): your tee");
		TextLine(Ui(), LeftView, "is shown whole ticks ahead with your newest input.");
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgClassicTicks, &g_Config.m_KgClassicTicks, &Button, "Ticks", 1, 5, &CUi::ms_LinearScrollbarScale, 0u, "");
		{
			char aTicks[96];
			str_format(aTicks, sizeof(aTicks), "= %d ms ahead. 1 tick feels most honest, more = earlier", g_Config.m_KgClassicTicks * 20);
			TextLine(Ui(), LeftView, aTicks, 11.0f);
			TextLine(Ui(), LeftView, "but bigger corrections when you change direction.", 11.0f);
		}
		break;
	case CKing::INPUT_MODE_KING:
		TextLine(Ui(), LeftView, "One tick fast input + safe auto margin (never");
		TextLine(Ui(), LeftView, "below stock 10 ms, changes smoothly).");
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		DoSliderWithScaledValue(&g_Config.m_KgFastInputAmount, &g_Config.m_KgFastInputAmount, &Button, "Fast input", 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " ms");
		TextLine(Ui(), LeftView, "20 ms = one tick. Higher shows you further ahead", 11.0f);
		TextLine(Ui(), LeftView, "but corrects more when you change direction.", 11.0f);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgKingSmooth, "Smooth small corrections (40 ms glide, no jumps)", &g_Config.m_KgKingSmooth, &LeftView, KING_LINE);
		break;
	case CKing::INPUT_MODE_DELTA:
		TextLine(Ui(), LeftView, "Fixed offset in hundredths of a tick, also between");
		TextLine(Ui(), LeftView, "whole ticks (e.g. 0.75 tick = 15 ms).");
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		{
			char aLabel[64];
			str_format(aLabel, sizeof(aLabel), "Offset %d.%02d ticks", g_Config.m_KgDeltaAmount / 100, g_Config.m_KgDeltaAmount % 100);
			Ui()->DoScrollbarOption(&g_Config.m_KgDeltaAmount, &g_Config.m_KgDeltaAmount, &Button, aLabel, 0, 500, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "");
		}
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgDeltaOthers, "Shift other tees too", &g_Config.m_KgDeltaOthers, &LeftView, KING_LINE);
		break;
	case CKing::INPUT_MODE_PING_COMP:
		TextLine(Ui(), LeftView, "Fast input grows with your ping: you see your tee");
		TextLine(Ui(), LeftView, "this share of the round trip ahead (max 100 ms).");
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgPingComp, &g_Config.m_KgPingComp, &Button, "Share of ping", 0, 50, &CUi::ms_LinearScrollbarScale, 0u, "%");
		{
			char aNow[64];
			str_format(aNow, sizeof(aNow), "Now: %d ms ahead", GameClient()->m_King.PingCompAmount());
			TextLine(Ui(), LeftView, aNow, 11.0f);
		}
		break;
	case CKing::INPUT_MODE_SMOOTH:
		TextLine(Ui(), LeftView, "KING mode, but when the server corrects your tee it");
		TextLine(Ui(), LeftView, "glides to the right spot instead of jumping. Only");
		TextLine(Ui(), LeftView, "visual: your inputs and hitbox are not delayed.");
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		DoSliderWithScaledValue(&g_Config.m_KgFastInputAmount, &g_Config.m_KgFastInputAmount, &Button, "Fast input", 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " ms");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgSmoothMs, &g_Config.m_KgSmoothMs, &Button, "Smoothing", 10, 300, &CUi::ms_LinearScrollbarScale, 0u, " ms");
		break;
	default:
		TextLine(Ui(), LeftView, "Uses the fast input settings as they are");
		TextLine(Ui(), LeftView, "(Settings tab > Input).");
		break;
	}

	// ***** Input helpers ***** //
	LeftView.HSplitTop(KING_SECTION_GAP, nullptr, &LeftView);
	Headline(Ui(), LeftView, "Input Helpers");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgSnapTap, "Snap Tap (left+right: last pressed wins)", &g_Config.m_KgSnapTap, &LeftView, KING_LINE);
	if(g_Config.m_KgSnapTap)
	{
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgSnapTapDelay, &g_Config.m_KgSnapTapDelay, &Button, "Snap Tap delay", 0, 200, &CUi::ms_LinearScrollbarScale, 0u, " ms");
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgAim45, "45 degree aim (toggle)", &g_Config.m_KgAim45, &LeftView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgSmallSens, "Small sensitivity (toggle)", &g_Config.m_KgSmallSens, &LeftView, KING_LINE);
	LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_KgSmallSensFactor, &g_Config.m_KgSmallSensFactor, &Button, "Small sens", 5, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
	TextLine(Ui(), LeftView, "Hold binds: bind <key> +kg_45 / +kg_small_sens", 11.0f);

	// ***** Prediction margin ***** //
	LeftView.HSplitTop(KING_SECTION_GAP, nullptr, &LeftView);
	Headline(Ui(), LeftView, "Prediction Margin");

	const bool ForcedAuto = CKing::ModeUsesAutoMargin(g_Config.m_KgInputMode);
	const bool ForcedManual = g_Config.m_KgInputMode == CKing::INPUT_MODE_CLASSIC;
	if(ForcedManual)
		TextLine(Ui(), LeftView, "Manual margin (Classic works exactly like TClient)");
	else if(ForcedAuto)
	{
		char aForced[64];
		str_format(aForced, sizeof(aForced), "Auto margin: on (%s mode)", CKing::InputModeName(g_Config.m_KgInputMode));
		TextLine(Ui(), LeftView, aForced);
	}
	else
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgAutoMargin, "Auto margin (adapts to jitter)", &g_Config.m_KgAutoMargin, &LeftView, KING_LINE);

	if(!ForcedManual && (ForcedAuto || g_Config.m_KgAutoMargin))
	{
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgAutoMarginMin, &g_Config.m_KgAutoMarginMin, &Button, "Min (stock 10)", 1, 30, &CUi::ms_LinearScrollbarScale, 0u, " ms");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgAutoMarginMax, &g_Config.m_KgAutoMarginMax, &Button, "Max", 5, 100, &CUi::ms_LinearScrollbarScale, 0u, " ms");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgAutoMarginSafety, &g_Config.m_KgAutoMarginSafety, &Button, "Safety", 0, 20, &CUi::ms_LinearScrollbarScale, 0u, " ms");
		if(g_Config.m_KgAutoMarginMax < g_Config.m_KgAutoMarginMin)
			g_Config.m_KgAutoMarginMax = g_Config.m_KgAutoMarginMin;
	}
	else
	{
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, "Margin", 1, 75, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " ms");
	}

	// ***** Live network ***** //
	Headline(Ui(), RightView, "Live Network");
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		const SKingNetStats Stats = Client()->KingNetStats();
		char aBuf[128];
		if(Stats.m_Valid)
		{
			str_format(aBuf, sizeof(aBuf), "Ping: %d ms", round_to_int(Stats.m_RttMs));
			TextLine(Ui(), RightView, aBuf);
			str_format(aBuf, sizeof(aBuf), "Jitter: %.1f ms  (input spread %.1f ms)", Stats.m_JitterMs, Stats.m_SpreadMs);
			TextLine(Ui(), RightView, aBuf);
		}
		else
			TextLine(Ui(), RightView, "Measuring...");

		if(Stats.m_MarginLocked)
			str_format(aBuf, sizeof(aBuf), "Margin: %d ms (server forces 10 ms)", Stats.m_MarginMs);
		else
			str_format(aBuf, sizeof(aBuf), "Margin: %.1f ms (%s, heading to %.0f)", Stats.m_MarginExactMs, Stats.m_MarginAuto ? "auto" : "fixed", Stats.m_MarginAuto ? Stats.m_MarginTargetMs : Stats.m_MarginExactMs);
		TextLine(Ui(), RightView, aBuf);
		str_format(aBuf, sizeof(aBuf), "Late inputs this session: %d", Stats.m_LateInputs);
		TextLine(Ui(), RightView, aBuf);
		const CKing::SLagStats &Lag = GameClient()->m_King.LagStats();
		str_format(aBuf, sizeof(aBuf), "Corrections: %d (biggest %.0f units)", Lag.m_Corrections, Lag.m_MaxCorrection);
		TextLine(Ui(), RightView, aBuf);
		str_format(aBuf, sizeof(aBuf), "Stutters: %d   Snapshot gaps: %d", Lag.m_Stutters, Lag.m_SnapGaps);
		TextLine(Ui(), RightView, aBuf);
	}
	else
		TextLine(Ui(), RightView, "Join a server to see live stats.");

	RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgNetHud, "Show network HUD in game", &g_Config.m_KgNetHud, &RightView, KING_LINE);
	TextLine(Ui(), RightView, "Every lag is written with its cause to", 11.0f);
	TextLine(Ui(), RightView, "%APPDATA%\\DDNet\\king_lag.log", 11.0f);

	// ***** Preset ***** //
	RightView.HSplitTop(KING_SECTION_GAP, nullptr, &RightView);
	Headline(Ui(), RightView, "KoG Preset");
	TextLine(Ui(), RightView, "KING mode, antiping on, vsync off, uncapped", 11.0f);
	TextLine(Ui(), RightView, "update rate. Binds and visuals are not touched.", 11.0f);
	RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
	RightView.HSplitTop(KING_LINE * 1.25f, &Button, &RightView);
	static CButtonContainer s_PresetButton;
	if(DoButton_Menu(&s_PresetButton, "Apply KoG preset", 0, &Button))
		GameClient()->m_King.ApplyKogPreset();

	// ***** Updates ***** //
	RightView.HSplitTop(KING_SECTION_GAP, nullptr, &RightView);
	Headline(Ui(), RightView, "Updates");
	{
		CKing &King = GameClient()->m_King;
		char aBuf[128];
		switch(King.UpdateState())
		{
		case CKing::UPDATE_CHECKING: str_copy(aBuf, "Checking for updates..."); break;
		case CKing::UPDATE_LATEST: str_copy(aBuf, "You have the latest version (" KING_VERSION ")"); break;
		case CKing::UPDATE_AVAILABLE: str_format(aBuf, sizeof(aBuf), "KING %s is available (you have " KING_VERSION ")", King.LatestVersion()); break;
		case CKing::UPDATE_FAILED: str_copy(aBuf, "Update check failed (no internet or GitHub limit)"); break;
		default: str_copy(aBuf, "Version " KING_VERSION); break;
		}
		TextLine(Ui(), RightView, aBuf);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgUpdateCheck, "Check for updates on start", &g_Config.m_KgUpdateCheck, &RightView, KING_LINE);

		CUIRect CheckButton, DownloadButton;
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		RightView.HSplitTop(KING_LINE * 1.25f, &Button, &RightView);
		Button.VSplitMid(&CheckButton, &DownloadButton, KING_MARGIN_SMALL);
		static CButtonContainer s_CheckButton;
		if(DoButton_Menu(&s_CheckButton, King.UpdateState() == CKing::UPDATE_CHECKING ? "Checking..." : "Check now", 0, &CheckButton))
			King.CheckForUpdates();
		static CButtonContainer s_DownloadButton;
		if(King.Installing())
			Ui()->RenderProgressBar(DownloadButton, King.DownloadProgress());
		else if(DoButton_Menu(&s_DownloadButton, King.CanInstallUpdate() ? "Update now" : (King.UpdateAvailable() ? "Download update" : "Releases page"), 0, &DownloadButton))
		{
			if(King.UpdateAvailable())
				King.InstallUpdate();
			else
				King.OpenUpdatePage();
		}

		if(King.Installing())
		{
			str_format(aBuf, sizeof(aBuf), "Downloading %s... %d%% (KING restarts by itself)", King.LatestVersion(), round_to_int(King.DownloadProgress() * 100.0f));
			TextLine(Ui(), RightView, aBuf, 11.0f);
		}
		else if(King.InstallError()[0])
		{
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			TextLine(Ui(), RightView, King.InstallError(), 11.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}

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
			Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
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
	Headline(Ui(), LeftView, "Interface Size");
	{
		static std::vector<CButtonContainer> s_vMenuScale = {{}, {}, {}, {}, {}, {}};
		int Value = g_Config.m_KgUiScale;
		if(DoLine_RadioMenu(LeftView, "Menus", s_vMenuScale, {"80%", "90%", "100%", "110%", "120%", "130%"}, {80, 90, 100, 110, 120, 130}, Value))
			g_Config.m_KgUiScale = Value;
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgScoreboardScale, &g_Config.m_KgScoreboardScale, &Button, "Scoreboard (TAB)", 50, 150, &CUi::ms_LinearScrollbarScale, 0u, "%");
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClChatFontSize, &g_Config.m_ClChatFontSize, &Button, "Chat", 10, 100);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		TextLine(Ui(), LeftView, "Console: kg_ui_scale, kg_scoreboard_scale (50-150)", 11.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	s_Sections.End(LeftView);

	// ***** Aspect ratio ***** //
	LeftView.HSplitTop(KING_SECTION_GAP + KING_MARGIN, nullptr, &LeftView);
	s_Sections.Begin(LeftView);
	Headline(Ui(), LeftView, "Aspect Ratio");
	{
		static CUi::SDropDownState s_PresetState;
		static CScrollRegion s_PresetScrollRegion;
		s_PresetState.m_SelectionPopupContext.m_pScrollRegion = &s_PresetScrollRegion; // the popup needs one, or it crashes
		static const char *s_apPresets[] = {"Native", "4:3", "5:4", "3:2", "16:10", "16:9", "Custom"};
		CUIRect Control = LabeledRow(Ui(), LeftView, "Preset");
		const int Preset = Ui()->DoDropDown(&Control, std::clamp(g_Config.m_KgAspectRatio, 0, 6), s_apPresets, std::size(s_apPresets), s_PresetState);
		g_Config.m_KgAspectRatio = Preset;

		static CUi::SDropDownState s_ApplyState;
		static CScrollRegion s_ApplyScrollRegion;
		s_ApplyState.m_SelectionPopupContext.m_pScrollRegion = &s_ApplyScrollRegion; // the popup needs one, or it crashes
		static const char *s_apApply[] = {"Full", "World only"};
		Control = LabeledRow(Ui(), LeftView, "Apply");
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
			Control = LabeledRow(Ui(), LeftView, "Custom size");
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
			if(DoButton_Menu(&s_ApplyButton, "Apply", 0, &ApplyRow))
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
			str_format(aNow, sizeof(aNow), "Screen %.2f, game %.2f: stretched x%.2f%s", Screen, Game, Screen / Game, g_Config.m_KgAspectApply == CKing::ASPECT_APPLY_FULL ? " (world, HUD, menus)" : " (world only)");
		else
			str_format(aNow, sizeof(aNow), "Screen %.2f, the game uses the same", Screen);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		TextLine(Ui(), LeftView, aNow, 11.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	s_Sections.End(LeftView);

	// ***** Interface ***** //
	LeftView.HSplitTop(KING_SECTION_GAP + KING_MARGIN, nullptr, &LeftView);
	s_Sections.Begin(LeftView);
	Headline(Ui(), LeftView, "Interface");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgNetHud, "Network HUD in game", &g_Config.m_KgNetHud, &LeftView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgLagLog, "Lag log (king_lag.log: every lag and its cause)", &g_Config.m_KgLagLog, &LeftView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgBadgeShow, "Crown next to KING players (game and TAB)", &g_Config.m_KgBadgeShow, &LeftView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgBadgeShare, "Show my crown to other KING players", &g_Config.m_KgBadgeShare, &LeftView, KING_LINE);
	s_Sections.End(LeftView);

	// ***** Music player ***** //
	s_Sections.Begin(RightView);
	{
		CUIRect Header, Reset;
		RightView.HSplitTop(KING_HEADLINE, &Header, &RightView);
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		Header.VSplitRight(KING_HEADLINE * 1.4f, &Header, &Reset);
		Ui()->DoLabel(&Header, "Music Player", KING_HEADLINE, TEXTALIGN_ML);
		static CButtonContainer s_ResetButton;
		if(Ui()->DoButton_FontIcon(&s_ResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &Reset, BUTTONFLAG_LEFT))
			GameClient()->m_King.ResetMusicSettings();
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgMusicHud, "Enable music player", &g_Config.m_KgMusicHud, &RightView, KING_LINE);
	RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
	{
		static CUi::SDropDownState s_ColorState;
		static CScrollRegion s_ColorScrollRegion;
		s_ColorState.m_SelectionPopupContext.m_pScrollRegion = &s_ColorScrollRegion; // the popup needs one, or it crashes
		static const char *s_apColor[] = {"Cover", "KING", "White"};
		CUIRect Control = LabeledRow(Ui(), RightView, "Color mode");
		g_Config.m_KgMusicColorMode = Ui()->DoDropDown(&Control, std::clamp(g_Config.m_KgMusicColorMode, 0, 2), s_apColor, std::size(s_apColor), s_ColorState);

		static CUi::SDropDownState s_VisState;
		static CScrollRegion s_VisScrollRegion;
		s_VisState.m_SelectionPopupContext.m_pScrollRegion = &s_VisScrollRegion; // the popup needs one, or it crashes
		static const char *s_apVis[] = {"Off", "Bottom", "Center"};
		Control = LabeledRow(Ui(), RightView, "Visualizer mode");
		g_Config.m_KgMusicVisualizer = Ui()->DoDropDown(&Control, std::clamp(g_Config.m_KgMusicVisualizer, 0, 2), s_apVis, std::size(s_apVis), s_VisState);

		RightView.HSplitTop(KING_LINE, &Button, &RightView);
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		Ui()->DoScrollbarOption(&g_Config.m_KgMusicTextScale, &g_Config.m_KgMusicTextScale, &Button, "Text scale", 50, 200, &CUi::ms_LinearScrollbarScale, 0u, "%");
		RightView.HSplitTop(KING_LINE, &Button, &RightView);
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		Ui()->DoScrollbarOption(&g_Config.m_KgMusicColumns, &g_Config.m_KgMusicColumns, &Button, "Columns", 3, 24);

		Control = LabeledRow(Ui(), RightView, "Rounding");
		CUIRect Cube, Soft;
		Control.VSplitMid(&Cube, &Soft, 0.0f);
		static CButtonContainer s_Cube, s_Soft;
		if(DoButton_Menu(&s_Cube, "Cube", g_Config.m_KgMusicRounding == 0, &Cube, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_KgMusicRounding = 0;
		if(DoButton_Menu(&s_Soft, "Soft", g_Config.m_KgMusicRounding == 1, &Soft, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_KgMusicRounding = 1;

		CUIRect LyricsRow, Badge;
		RightView.HSplitTop(KING_LINE, &LyricsRow, &RightView);
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		LyricsRow.VSplitRight(52.0f, &LyricsRow, &Badge);
		if(DoButton_CheckBox(&g_Config.m_KgMusicLyrics, "Show song lyrics", g_Config.m_KgMusicLyrics, &LyricsRow))
			g_Config.m_KgMusicLyrics ^= 1;
		Badge.Margin(2.0f, &Badge);
		Badge.Draw(ColorRGBA(0.86f, 0.16f, 0.16f, 1.0f), IGraphics::CORNER_ALL, 4.0f);
		Ui()->DoLabel(&Badge, "BETA", 11.0f, TEXTALIGN_MC);
	}

	// live preview + now playing + controls
	RightView.HSplitTop(KING_MARGIN, nullptr, &RightView);
	CKingMedia &Media = GameClient()->m_King.m_Media;
	if(!Media.Supported())
		TextLine(Ui(), RightView, "Music player is not supported on this system.", 11.0f);
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
			Ui()->DoLabel(&Preview, g_Config.m_KgMusicHud ? "Play something (Spotify, browser, Yandex Music...) to see it here" : "Music player is off", 11.0f, TEXTALIGN_MC);

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
			TextLine(Ui(), RightView, Media.VisualizerExcludesGame() ? "Visualizer listens to your PC (game sounds excluded)" : "Visualizer listens to your PC output", 11.0f);
		TextLine(Ui(), RightView, "Binds: kg_music_toggle, kg_music_next, kg_music_prev", 11.0f);
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
	Headline(Ui(), LeftView, "Optimizer");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptimizer, "Enable optimizer", &g_Config.m_KgOptimizer, &LeftView, KING_LINE);
	{
		// the options only work while the optimizer is on
		CUIRect Options = LeftView;
		Options.VSplitLeft(16.0f, nullptr, &Options);
		const float StartY = Options.y;
		if(!g_Config.m_KgOptimizer)
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.45f);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptNoParticles, "Disable all particles render", &g_Config.m_KgOptNoParticles, &Options, KING_LINE);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptNoQuads, "Disable map quads render", &g_Config.m_KgOptNoQuads, &Options, KING_LINE);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptNoHighDetail, "Disable high detail", &g_Config.m_KgOptNoHighDetail, &Options, KING_LINE);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgOptFpsFog, "FPS fog (cull outside limit)", &g_Config.m_KgOptFpsFog, &Options, KING_LINE);
		if(g_Config.m_KgOptFpsFog)
		{
			Options.HSplitTop(KING_LINE, &Button, &Options);
			Options.HSplitTop(KING_MARGIN_SMALL, nullptr, &Options);
			Ui()->DoScrollbarOption(&g_Config.m_KgOptFpsFogRadius, &g_Config.m_KgOptFpsFogRadius, &Button, "Fog distance", 10, 150, &CUi::ms_LinearScrollbarScale, 0u, " tiles");
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		LeftView.HSplitTop(Options.y - StartY, nullptr, &LeftView);
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
	TextLine(Ui(), LeftView, "Only what you see changes: physics, hitboxes and", 11.0f);
	TextLine(Ui(), LeftView, "inputs stay exactly the same.", 11.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	s_Sections.End(LeftView);

	// ***** Performance ***** //
	s_Sections.Begin(RightView);
	Headline(Ui(), RightView, "Performance");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgHighPriority, "High KING Process Priority", &g_Config.m_KgHighPriority, &RightView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgLowerDiscord, "Lower Discord's Process Priority", &g_Config.m_KgLowerDiscord, &RightView, KING_LINE);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
	TextLine(Ui(), RightView, "Windows gives KING more CPU time than background", 11.0f);
	TextLine(Ui(), RightView, "apps; Discord gets less while KING runs and its", 11.0f);
	TextLine(Ui(), RightView, "normal priority back when KING closes.", 11.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	s_Sections.End(RightView);
	s_Sections.Swap();
}

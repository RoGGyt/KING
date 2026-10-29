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
		static std::vector<CButtonContainer> s_vModeButtonsB = {{}, {}, {}};
		bool Changed = DoLine_RadioMenu(LeftView, "Mode",
			s_vModeButtonsA,
			{CKing::InputModeName(CKing::INPUT_MODE_CUSTOM), CKing::InputModeName(CKing::INPUT_MODE_CLASSIC), CKing::InputModeName(CKing::INPUT_MODE_KING)},
			{CKing::INPUT_MODE_CUSTOM, CKing::INPUT_MODE_CLASSIC, CKing::INPUT_MODE_KING},
			Mode);
		Changed |= DoLine_RadioMenu(LeftView, "",
			s_vModeButtonsB,
			{CKing::InputModeName(CKing::INPUT_MODE_DELTA), CKing::InputModeName(CKing::INPUT_MODE_PING_COMP), CKing::InputModeName(CKing::INPUT_MODE_SMOOTH)},
			{CKing::INPUT_MODE_DELTA, CKing::INPUT_MODE_PING_COMP, CKing::INPUT_MODE_SMOOTH},
			Mode);
		if(Changed)
			g_Config.m_KgInputMode = Mode;
	}
	LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);

	switch(g_Config.m_KgInputMode)
	{
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
		TextLine(Ui(), LeftView, "Fast input + auto margin that follows your real");
		TextLine(Ui(), LeftView, "jitter. Best for stable wired ping.");
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		DoSliderWithScaledValue(&g_Config.m_KgFastInputAmount, &g_Config.m_KgFastInputAmount, &Button, "Fast input", 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " ms");
		TextLine(Ui(), LeftView, "20 ms = one tick. Higher shows you further ahead", 11.0f);
		TextLine(Ui(), LeftView, "but corrects more when you change direction.", 11.0f);
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
	if(ForcedAuto)
	{
		char aForced[64];
		str_format(aForced, sizeof(aForced), "Auto margin: on (%s mode)", CKing::InputModeName(g_Config.m_KgInputMode));
		TextLine(Ui(), LeftView, aForced);
	}
	else
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgAutoMargin, "Auto margin (adapts to jitter)", &g_Config.m_KgAutoMargin, &LeftView, KING_LINE);

	if(ForcedAuto || g_Config.m_KgAutoMargin)
	{
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_KgAutoMarginMin, &g_Config.m_KgAutoMarginMin, &Button, "Min", 1, 30, &CUi::ms_LinearScrollbarScale, 0u, " ms");
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
			str_format(aBuf, sizeof(aBuf), "Margin: %d ms (%s)", Stats.m_MarginMs, Stats.m_MarginAuto ? "auto" : "fixed");
		TextLine(Ui(), RightView, aBuf);
		str_format(aBuf, sizeof(aBuf), "Late inputs this session: %d", Stats.m_LateInputs);
		TextLine(Ui(), RightView, aBuf);
	}
	else
		TextLine(Ui(), RightView, "Join a server to see live stats.");

	RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgNetHud, "Show network HUD in game", &g_Config.m_KgNetHud, &RightView, KING_LINE);

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

void CMenus::RenderSettingsKingDisplay(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button;
	MainView.VSplitMid(&LeftView, &RightView, KING_MARGIN * 3.0f);
	LeftView.Margin(KING_MARGIN_SMALL, &LeftView);
	RightView.Margin(KING_MARGIN_SMALL, &RightView);

	Headline(Ui(), LeftView, "Aspect Ratio");
	TextLine(Ui(), LeftView, "Game view ratio, stretched to fill your screen", 11.0f);
	TextLine(Ui(), LeftView, "(like 4:3 stretched). Menus stay native.", 11.0f);
	LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
	{
		int Value = g_Config.m_KgAspectRatio;
		static std::vector<CButtonContainer> s_vRowA = {{}, {}, {}, {}};
		static std::vector<CButtonContainer> s_vRowB = {{}, {}, {}};
		bool Changed = DoLine_RadioMenu(LeftView, "", s_vRowA, {"Native", "4:3", "5:4", "3:2"}, {0, 1, 2, 3}, Value);
		Changed |= DoLine_RadioMenu(LeftView, "", s_vRowB, {"16:10", "16:9", "Custom"}, {4, 5, 6}, Value);
		if(Changed)
			g_Config.m_KgAspectRatio = Value;
	}
	if(g_Config.m_KgAspectRatio == 6)
	{
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		char aLabel[64];
		str_format(aLabel, sizeof(aLabel), "Ratio %d.%02d", g_Config.m_KgAspectCustom / 100, g_Config.m_KgAspectCustom % 100);
		Ui()->DoScrollbarOption(&g_Config.m_KgAspectCustom, &g_Config.m_KgAspectCustom, &Button, aLabel, 100, 240, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "");
	}
	{
		char aNow[96];
		const float Screen = Graphics()->ScreenAspect();
		const float World = CKing::AspectRatioValue(g_Config.m_KgAspectRatio);
		if(World > 0.0f)
			str_format(aNow, sizeof(aNow), "Screen %.2f, game %.2f (stretch x%.2f)", Screen, World, Screen / World);
		else
			str_format(aNow, sizeof(aNow), "Screen %.2f, game uses the same", Screen);
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		TextLine(Ui(), LeftView, aNow, 11.0f);
	}

	Headline(Ui(), RightView, "Interface");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgNetHud, "Network HUD in game", &g_Config.m_KgNetHud, &RightView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgMusicHud, "Music player in game (top, next to the timer)", &g_Config.m_KgMusicHud, &RightView, KING_LINE);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_KgMusicShowPaused, "Keep it visible while paused", &g_Config.m_KgMusicShowPaused, &RightView, KING_LINE);

	// now playing + controls
	RightView.HSplitTop(KING_SECTION_GAP, nullptr, &RightView);
	Headline(Ui(), RightView, "Music");
	CKingMedia &Media = GameClient()->m_King.m_Media;
	if(!Media.Supported())
		TextLine(Ui(), RightView, "Not supported on this system.", 11.0f);
	else
	{
		const SKingMediaInfo Info = Media.Get();
		char aBuf[600];
		if(Info.m_Valid && (Info.m_aTitle[0] || Info.m_aArtist[0]))
			str_format(aBuf, sizeof(aBuf), "%s %s%s%s", Info.m_Playing ? "Playing:" : "Paused:", Info.m_aTitle, Info.m_aArtist[0] ? " - " : "", Info.m_aArtist);
		else
			str_copy(aBuf, "Nothing is playing (Spotify, browser, Yandex Music...)");
		TextLine(Ui(), RightView, aBuf, 12.0f);

		CUIRect Row, Prev, Toggle, Next;
		RightView.HSplitTop(KING_MARGIN_SMALL, nullptr, &RightView);
		RightView.HSplitTop(KING_LINE * 1.25f, &Row, &RightView);
		Row.VSplitLeft(Row.h * 2.0f, &Prev, &Row);
		Row.VSplitLeft(KING_MARGIN_SMALL, nullptr, &Row);
		Row.VSplitLeft(Row.h * 2.0f, &Toggle, &Row);
		Row.VSplitLeft(KING_MARGIN_SMALL, nullptr, &Row);
		Row.VSplitLeft(Row.h * 2.0f, &Next, &Row);
		static CButtonContainer s_Prev, s_Toggle, s_Next;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		if(DoButton_Menu(&s_Prev, FontIcon::BACKWARD_STEP, 0, &Prev))
			Media.Command(CKingMedia::COMMAND_PREVIOUS);
		if(DoButton_Menu(&s_Toggle, Info.m_Playing ? FontIcon::PAUSE : FontIcon::PLAY, 0, &Toggle))
			Media.Command(CKingMedia::COMMAND_TOGGLE);
		if(DoButton_Menu(&s_Next, FontIcon::FORWARD_STEP, 0, &Next))
			Media.Command(CKingMedia::COMMAND_NEXT);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextLine(Ui(), RightView, "Binds: kg_music_toggle, kg_music_next, kg_music_prev", 11.0f);
	}
}

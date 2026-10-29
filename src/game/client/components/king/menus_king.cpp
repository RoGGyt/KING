/* KING client: settings tab. */
#include "king.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/version.h>

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
	CUIRect LeftView, RightView, Button;
	MainView.VSplitMid(&LeftView, &RightView, KING_MARGIN * 3.0f);
	LeftView.Margin(KING_MARGIN_SMALL, &LeftView);
	RightView.Margin(KING_MARGIN_SMALL, &RightView);

	// ***** Input ***** //
	Headline(Ui(), LeftView, "KING Input");

	{
		static std::vector<CButtonContainer> s_vModeButtons = {{}, {}, {}};
		int Mode = g_Config.m_KgInputMode;
		if(DoLine_RadioMenu(LeftView, "Mode",
			   s_vModeButtons,
			   {CKing::InputModeName(CKing::INPUT_MODE_CUSTOM), CKing::InputModeName(CKing::INPUT_MODE_CLASSIC), CKing::InputModeName(CKing::INPUT_MODE_KING)},
			   {CKing::INPUT_MODE_CUSTOM, CKing::INPUT_MODE_CLASSIC, CKing::INPUT_MODE_KING},
			   Mode))
		{
			g_Config.m_KgInputMode = Mode;
		}
	}
	LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);

	switch(g_Config.m_KgInputMode)
	{
	case CKing::INPUT_MODE_CLASSIC:
		TextLine(Ui(), LeftView, "Old TClient fast input: your tee is shown exactly");
		TextLine(Ui(), LeftView, "one tick (20 ms) ahead with your newest input.");
		TextLine(Ui(), LeftView, "Prediction margin stays manual (below).");
		break;
	case CKing::INPUT_MODE_KING:
		TextLine(Ui(), LeftView, "Classic one-tick fast input + auto margin that");
		TextLine(Ui(), LeftView, "follows your real jitter. Best for stable wired ping.");
		LeftView.HSplitTop(KING_MARGIN_SMALL, nullptr, &LeftView);
		LeftView.HSplitTop(KING_LINE, &Button, &LeftView);
		DoSliderWithScaledValue(&g_Config.m_KgFastInputAmount, &g_Config.m_KgFastInputAmount, &Button, "Fast input", 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " ms");
		TextLine(Ui(), LeftView, "20 ms = one tick. Higher shows you further ahead", 11.0f);
		TextLine(Ui(), LeftView, "but corrects more when you change direction.", 11.0f);
		break;
	default:
		TextLine(Ui(), LeftView, "Uses the TClient settings as they are");
		TextLine(Ui(), LeftView, "(Settings tab > Input).");
		break;
	}

	// ***** Prediction margin ***** //
	LeftView.HSplitTop(KING_SECTION_GAP, nullptr, &LeftView);
	Headline(Ui(), LeftView, "Prediction Margin");

	const bool ForcedAuto = g_Config.m_KgInputMode == CKing::INPUT_MODE_KING;
	if(ForcedAuto)
		TextLine(Ui(), LeftView, "Auto margin: on (KING mode)");
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
		if(DoButton_Menu(&s_DownloadButton, King.UpdateAvailable() ? "Download update" : "Releases page", 0, &DownloadButton))
			King.OpenUpdatePage();
	}
}

/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	if(g_Config.m_KgMenuStyle)
	{
		RenderKingStartMenu(MainView);
		return;
	}

	// render logo
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	// KING wordmark is 1043x385
	const float LogoH = 100.0f;
	const float LogoW = LogoH * (1043.0f / 385.0f);
	IGraphics::CQuadItem QuadItem(MainView.w / 2 - LogoW / 2, 70, LogoW, LogoH);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - (g_Config.m_KgMenuStyle ? 150.0f : 190.0f);

	CUIRect Button;
	int NewPage = -1;

	CUIRect ExtMenu;
	MainView.VSplitLeft(30.0f, nullptr, &ExtMenu);
	ExtMenu.VSplitLeft(100.0f, &ExtMenu, nullptr);

	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_DiscordButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordButton, Localize("Discord"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink(Localize("https://ddnet.org/discord"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_LearnButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_LearnButton, Localize("Learn"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink(Localize("https://wiki.ddnet.org/"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_TutorialButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_TutorialButton, Localize("Tutorial"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		GameClient()->m_Menus.JoinTutorial();
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_WebsiteButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_WebsiteButton, Localize("Website"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink("https://ddnet.org/");
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_NewsButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_NewsButton, Localize("News"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, g_Config.m_UiUnreadNews ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_N))
		NewPage = CMenus::PAGE_NEWS;

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);
	Menu.HSplitBottom(25.0f, &Menu, nullptr);

	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_QuitButton;
	bool UsedEscape = false;
	if(StartButton(&s_QuitButton, Localize("Quit"), nullptr, FontIcon::POWER_OFF, &Button, Rounding, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), KING_BUTTON_DANGER) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
		{
			GameClient()->m_Menus.ShowQuitPopup();
		}
		else
		{
			Client()->Quit();
		}
	}

	Menu.HSplitBottom(100.0f, &Menu, nullptr);
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_SettingsButton;
	if(StartButton(&s_SettingsButton, Localize("Settings"), g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, FontIcon::GEAR, &Button, Rounding, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), KING_BUTTON_NORMAL) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_LocalServerButton;

	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(StartButton(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), g_Config.m_ClShowStartMenuImages ? "local_server" : nullptr, FontIcon::NETWORK_WIRED, &Button, Rounding, LocalServerRunning ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), LocalServerRunning ? KING_BUTTON_ACTIVE : KING_BUTTON_NORMAL) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
	{
		if(LocalServerRunning)
		{
			GameClient()->m_LocalServer.KillServer();
		}
		else
		{
			GameClient()->m_LocalServer.RunServer({});
		}
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_MapEditorButton;
	if(StartButton(&s_MapEditorButton, Localize("Editor"), g_Config.m_ClShowStartMenuImages ? "editor" : nullptr, FontIcon::PEN_TO_SQUARE, &Button, Rounding, GameClient()->Editor()->HasUnsavedData() ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), GameClient()->Editor()->HasUnsavedData() ? KING_BUTTON_ACTIVE : KING_BUTTON_NORMAL) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_DemoButton;
	if(StartButton(&s_DemoButton, Localize("Demos"), g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, FontIcon::FILM, &Button, Rounding, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), KING_BUTTON_NORMAL) || CheckHotKey(KEY_D))
	{
		NewPage = CMenus::PAGE_DEMOS;
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_PlayButton;
	if(StartButton(&s_PlayButton, Localize("Play", "Start menu"), g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, FontIcon::PLAY, &Button, Rounding, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), KING_BUTTON_PRIMARY) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
	{
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	}

	// render version
	CUIRect CurVersion, ConsoleButton;
	MainView.HSplitBottom(45.0f, nullptr, &CurVersion);
	CurVersion.VSplitRight(40.0f, &CurVersion, nullptr);
	CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
	CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
	ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);
	Ui()->DoLabel(&CurVersion, GAME_RELEASE_VERSION, 14.0f, TEXTALIGN_MR);

	CUIRect TClientVersion;
	MainView.HSplitTop(15.0f, &TClientVersion, &MainView);
	TClientVersion.VSplitRight(40.0f, &TClientVersion, nullptr);
	char aTBuf[64];
	str_format(aTBuf, sizeof(aTBuf), CLIENT_NAME " %s", CLIENT_RELEASE_VERSION);
	Ui()->DoLabel(&TClientVersion, aTBuf, 14.0f, TEXTALIGN_MR);
#if defined(CONF_AUTOUPDATE)
	CUIRect UpdateToDateText;
	MainView.HSplitTop(15.0f, &UpdateToDateText, nullptr);
	UpdateToDateText.VSplitRight(40.0f, &UpdateToDateText, nullptr);
	if(!GameClient()->m_TClient.NeedUpdate() && GameClient()->m_TClient.m_FetchedTClientInfo)
	{
		Ui()->DoLabel(&UpdateToDateText, TCLocalize("(On Latest)"), 14.0f, TEXTALIGN_MR);
	}
	else
	{
		Ui()->DoLabel(&UpdateToDateText, TCLocalize("(Fetching Update Info)"), 14.0f, TEXTALIGN_MR);
	}
#endif
	static CButtonContainer s_ConsoleButton;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FontIcon::TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f)))
	{
		GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	CUIRect VersionUpdate;
	MainView.HSplitBottom(20.0f, nullptr, &VersionUpdate);
	VersionUpdate.VMargin(VMargin, &VersionUpdate);
#if defined(CONF_AUTOUPDATE)
	CUIRect UpdateButton;
	VersionUpdate.VSplitRight(100.0f, &VersionUpdate, &UpdateButton);
	VersionUpdate.VSplitRight(10.0f, &VersionUpdate, nullptr);

	char aBuf[128];
	const IUpdater::EUpdaterState State = Updater()->GetCurrentState();
	const bool NeedUpdate = GameClient()->m_TClient.NeedUpdate();

	if(State == IUpdater::CLEAN && NeedUpdate)
	{
		static CButtonContainer s_VersionUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_VersionUpdate, Localize("Update now"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Updater()->InitiateUpdate();
		}
	}
	else if(State == IUpdater::NEED_RESTART)
	{
		static CButtonContainer s_VersionUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_VersionUpdate, Localize("Restart"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Client()->Restart();
		}
	}
	else if(State >= IUpdater::GETTING_MANIFEST && State < IUpdater::NEED_RESTART)
	{
		Ui()->RenderProgressBar(UpdateButton, Updater()->GetCurrentPercent() / 100.0f);
	}

	if(State == IUpdater::CLEAN && NeedUpdate)
	{
		str_format(aBuf, sizeof(aBuf), "KING %s is out!", GameClient()->m_TClient.m_aVersionStr);
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
	}
	else if(State == IUpdater::CLEAN)
	{
		aBuf[0] = '\0';
	}
	else if(State >= IUpdater::GETTING_MANIFEST && State < IUpdater::NEED_RESTART)
	{
		char aCurrentFile[64];
		Updater()->GetCurrentFile(aCurrentFile, sizeof(aCurrentFile));
		str_format(aBuf, sizeof(aBuf), Localize("Downloading %s:"), aCurrentFile);
	}
	else if(State == IUpdater::FAIL)
	{
		str_copy(aBuf, Localize("Update failed! Check log…"));
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
	}
	else if(State == IUpdater::NEED_RESTART)
	{
		str_copy(aBuf, Localize("DDNet Client updated!"));
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
	}
	Ui()->DoLabel(&VersionUpdate, aBuf, 14.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
#elif defined(CONF_INFORM_UPDATE)
	if(str_comp(Client()->LatestVersion(), "0") != 0 && false)
	{
		CUIRect DownloadButton;
		VersionUpdate.VSplitRight(100.0f, &VersionUpdate, &DownloadButton);
		VersionUpdate.VSplitRight(10.0f, &VersionUpdate, nullptr);

		static CButtonContainer s_DownloadButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_DownloadButton, Localize("Download"), 0, &DownloadButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Client()->ViewLink("https://ddnet.org/downloads/");
		}

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("DDNet %s is out!"), Client()->LatestVersion());
		SLabelProperties UpdateLabelProps;
		UpdateLabelProps.SetColor(ColorRGBA(1.0f, 0.4f, 0.4f, 1.0f));
		Ui()->DoLabel(&VersionUpdate, aBuf, 14.0f, TEXTALIGN_ML, UpdateLabelProps);
	}
#endif

	// KING update notice
	if(GameClient()->m_King.UpdateAvailable())
	{
		CUIRect KingUpdate, KingButton;
		MainView.HSplitBottom(45.0f, nullptr, &KingUpdate);
		KingUpdate.HSplitTop(20.0f, &KingUpdate, nullptr);
		KingUpdate.VMargin(VMargin, &KingUpdate);
		KingUpdate.VSplitRight(100.0f, &KingUpdate, &KingButton);
		KingUpdate.VSplitRight(10.0f, &KingUpdate, nullptr);

		CKing &King = GameClient()->m_King;
		static CButtonContainer s_KingUpdateButton;
		if(King.Installing())
			Ui()->RenderProgressBar(KingButton, King.DownloadProgress());
		else if(GameClient()->m_Menus.DoButton_Menu(&s_KingUpdateButton, King.CanInstallUpdate() ? "Update" : "Download", 0, &KingButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			King.InstallUpdate();

		char aKingBuf[160];
		if(King.Installing())
			str_format(aKingBuf, sizeof(aKingBuf), "Downloading KING %s... %d%%", King.LatestVersion(), round_to_int(King.DownloadProgress() * 100.0f));
		else if(King.InstallError()[0])
			str_copy(aKingBuf, King.InstallError());
		else
			str_format(aKingBuf, sizeof(aKingBuf), "KING %s is out! (you have " KING_VERSION ")", King.LatestVersion());
		SLabelProperties KingLabelProps;
		KingLabelProps.SetColor(ColorRGBA(1.0f, 0.82f, 0.2f, 1.0f));
		Ui()->DoLabel(&KingUpdate, aKingBuf, 14.0f, TEXTALIGN_ML, KingLabelProps);
	}

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() && // no modifier
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}

bool CMenusStart::StartButton(CButtonContainer *pButtonContainer, const char *pText, const char *pImage, const char *pIcon, const CUIRect *pRect, float Rounding, ColorRGBA Color, int Style)
{
	if(g_Config.m_KgMenuStyle)
		return DoKingButton(pButtonContainer, pText, pIcon, pRect, Style);
	return GameClient()->m_Menus.DoButton_Menu(pButtonContainer, pText, 0, pRect, BUTTONFLAG_LEFT, pImage, IGraphics::CORNER_ALL, Rounding, 0.5f, Color);
}

bool CMenusStart::DoKingButton(CButtonContainer *pButtonContainer, const char *pText, const char *pIcon, const CUIRect *pRect, int Style)
{
	// flat, quiet buttons; the only colour is the accent on Play
	const bool Hot = Ui()->HotItem() == pButtonContainer;
	const bool Pressed = Hot && Ui()->CheckActiveItem(pButtonContainer);
	const ColorRGBA Accent(0.91f, 0.70f, 0.29f, 1.0f);

	ColorRGBA Fill, TextColor;
	switch(Style)
	{
	case KING_BUTTON_PRIMARY:
		Fill = Hot ? ColorRGBA(0.96f, 0.76f, 0.36f, 1.0f) : Accent;
		TextColor = ColorRGBA(0.08f, 0.06f, 0.04f, 1.0f);
		break;
	case KING_BUTTON_ACTIVE:
		Fill = ColorRGBA(0.3f, 0.75f, 0.4f, Hot ? 0.3f : 0.2f);
		TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f);
		break;
	case KING_BUTTON_DANGER:
		Fill = Hot ? ColorRGBA(0.85f, 0.3f, 0.28f, 0.22f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f);
		TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.9f : 0.65f);
		break;
	default:
		Fill = ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.09f : 0.04f);
		TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.95f : 0.8f);
		break;
	}
	if(Pressed)
		Fill = Fill.Multiply(ColorRGBA(0.85f, 0.85f, 0.85f, 1.0f));

	pRect->Draw(Fill, IGraphics::CORNER_ALL, 6.0f);
	TextRender()->TextColor(TextColor);
	Ui()->DoLabel(pRect, pText, pRect->h * 0.42f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	return Ui()->DoButtonLogic(pButtonContainer, 0, pRect, BUTTONFLAG_LEFT);
}

// ***** KING start menu: text menu on the left, your tee on the right ***** //

static const ColorRGBA gs_KingAccent(0.91f, 0.70f, 0.29f, 1.0f);

bool CMenusStart::DoKingMenuItem(CButtonContainer *pButtonContainer, const char *pText, float X, float Y, float FontSize, int Style)
{
	const float TextW = TextRender()->TextWidth(FontSize, pText);
	CUIRect Rect;
	Rect.x = X - 14.0f;
	Rect.y = Y - FontSize * 0.75f;
	Rect.w = TextW + 28.0f;
	Rect.h = FontSize * 1.5f;

	const bool Hot = Ui()->HotItem() == pButtonContainer;
	ColorRGBA TextColor;
	switch(Style)
	{
	case KING_BUTTON_PRIMARY: TextColor = Hot ? ColorRGBA(1.0f, 0.82f, 0.45f, 1.0f) : gs_KingAccent; break;
	case KING_BUTTON_DANGER: TextColor = Hot ? ColorRGBA(1.0f, 0.55f, 0.5f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f); break;
	case KING_BUTTON_ACTIVE: TextColor = Hot ? ColorRGBA(0.6f, 1.0f, 0.65f, 1.0f) : ColorRGBA(0.45f, 0.9f, 0.5f, 0.9f); break;
	default: TextColor = Hot ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.65f); break;
	}

	if(Hot && Style != KING_BUTTON_PRIMARY)
	{
		CUIRect Bar;
		Bar.x = X - 10.0f;
		Bar.y = Y - FontSize * 0.42f;
		Bar.w = 2.0f;
		Bar.h = FontSize * 0.84f;
		Bar.Draw(Style == KING_BUTTON_DANGER ? ColorRGBA(0.95f, 0.4f, 0.35f, 1.0f) : gs_KingAccent, IGraphics::CORNER_ALL, 1.0f);
	}

	CUIRect Label;
	Label.x = X;
	Label.y = Rect.y;
	Label.w = TextW + 2.0f;
	Label.h = Rect.h;
	TextRender()->TextColor(TextColor);
	Ui()->DoLabel(&Label, pText, FontSize, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	return Ui()->DoButtonLogic(pButtonContainer, 0, &Rect, BUTTONFLAG_LEFT);
}

void CMenusStart::RenderKingPlayer(CUIRect Area)
{
	const float Size = std::min(Area.h * 0.42f, Area.w * 0.45f);
	const vec2 Center(Area.x + Area.w / 2.0f, Area.y + Area.h * 0.55f);

	CTeeRenderInfo TeeInfo;
	const CSkin *pSkin = GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin);
	TeeInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	TeeInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	TeeInfo.m_SkinMetrics = pSkin->m_Metrics;
	TeeInfo.m_CustomColoredSkin = g_Config.m_ClPlayerUseCustomColor;
	if(TeeInfo.m_CustomColoredSkin)
	{
		TeeInfo.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClPlayerColorBody).UnclampLighting(ColorHSLA::DARKEST_LGT));
		TeeInfo.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClPlayerColorFeet).UnclampLighting(ColorHSLA::DARKEST_LGT));
	}
	else
	{
		TeeInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
		TeeInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	}
	TeeInfo.m_Size = Size;

	// soft shadow on the "floor"
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int i = 6; i >= 1; i--)
	{
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.05f);
		const float R = Size * 0.09f * i;
		IGraphics::CQuadItem Shadow(Center.x - R, Center.y + Size * 0.42f - R * 0.12f, R * 2.0f, R * 0.24f);
		Graphics()->QuadsDrawTL(&Shadow, 1);
	}
	Graphics()->QuadsEnd();

	// tee looks at the mouse
	const CAnimState *pIdle = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdle, &TeeInfo, OffsetToMid);
	const vec2 TeePos(Center.x, Center.y + OffsetToMid.y);
	vec2 Dir = Ui()->MousePos() - TeePos;
	const float Len = length(Dir);
	Dir = Len > 0.001f ? Dir / Len : vec2(1.0f, 0.0f);
	RenderTools()->RenderTee(pIdle, &TeeInfo, EMOTE_NORMAL, Dir, TeePos);

	// name above the tee
	CUIRect NameRect;
	NameRect.x = Area.x;
	NameRect.w = Area.w;
	NameRect.h = 30.0f;
	NameRect.y = Center.y - Size * 0.56f - NameRect.h;
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
	Ui()->DoLabel(&NameRect, Client()->PlayerName(), 22.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	// clicking the tee opens the tee settings
	static CButtonContainer s_TeeButton;
	CUIRect TeeRect;
	TeeRect.x = Center.x - Size * 0.5f;
	TeeRect.y = Center.y - Size * 0.5f;
	TeeRect.w = Size;
	TeeRect.h = Size;
	if(Ui()->DoButtonLogic(&s_TeeButton, 0, &TeeRect, BUTTONFLAG_LEFT))
	{
		g_Config.m_UiSettingsPage = CMenus::SETTINGS_TEE;
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(CMenus::PAGE_SETTINGS);
	}
	if(Ui()->HotItem() == &s_TeeButton)
		GameClient()->m_Tooltips.DoToolTip(&s_TeeButton, &TeeRect, "Change skin");
}

void CMenusStart::RenderKingStartMenu(CUIRect MainView)
{
	int NewPage = -1;
	const float Left = MainView.x + MainView.w * 0.1f;

	// wordmark (694x282), left aligned
	const float LogoH = 52.0f;
	const float LogoW = LogoH * (694.0f / 282.0f);
	const float LogoY = MainView.y + MainView.h * 0.17f;
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	IGraphics::CQuadItem LogoItem(Left, LogoY, LogoW, LogoH);
	Graphics()->QuadsDrawTL(&LogoItem, 1);
	Graphics()->QuadsEnd();

	// menu
	const float Row = 36.0f;
	float Y = LogoY + LogoH + 58.0f;

	static CButtonContainer s_PlayButton, s_DemoButton, s_EditorButton, s_ServerButton, s_SettingsButton, s_QuitButton;
	if(DoKingMenuItem(&s_PlayButton, Localize("Play", "Start menu"), Left, Y, 24.0f, KING_BUTTON_PRIMARY) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	Y += Row + 4.0f;

	if(DoKingMenuItem(&s_DemoButton, Localize("Demos"), Left, Y, 20.0f, KING_BUTTON_NORMAL) || CheckHotKey(KEY_D))
		NewPage = CMenus::PAGE_DEMOS;
	Y += Row;

	if(DoKingMenuItem(&s_EditorButton, Localize("Editor"), Left, Y, 20.0f, GameClient()->Editor()->HasUnsavedData() ? KING_BUTTON_ACTIVE : KING_BUTTON_NORMAL) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}
	Y += Row;

	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(DoKingMenuItem(&s_ServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), Left, Y, 20.0f, LocalServerRunning ? KING_BUTTON_ACTIVE : KING_BUTTON_NORMAL) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
	{
		if(LocalServerRunning)
			GameClient()->m_LocalServer.KillServer();
		else
			GameClient()->m_LocalServer.RunServer({});
	}
	Y += Row;

	if(DoKingMenuItem(&s_SettingsButton, Localize("Settings"), Left, Y, 20.0f, KING_BUTTON_NORMAL) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;
	Y += Row * 2.0f;

	bool UsedEscape = false;
	if(DoKingMenuItem(&s_QuitButton, Localize("Quit"), Left, Y, 20.0f, KING_BUTTON_DANGER) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			GameClient()->m_Menus.ShowQuitPopup();
		else
			Client()->Quit();
	}

	// your tee on the right
	CUIRect PlayerArea;
	PlayerArea.x = MainView.x + MainView.w * 0.5f;
	PlayerArea.y = MainView.y + MainView.h * 0.12f;
	PlayerArea.w = MainView.w * 0.4f;
	PlayerArea.h = MainView.h * 0.76f;
	RenderKingPlayer(PlayerArea);

	// version (muted) and console button, top right / bottom right
	CUIRect Version;
	MainView.VSplitRight(30.0f, &Version, nullptr);
	Version.HSplitTop(30.0f, &Version, nullptr);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.4f);
	Ui()->DoLabel(&Version, KING_VERSION, 12.0f, TEXTALIGN_MR);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	CUIRect ConsoleButton;
	MainView.HSplitBottom(40.0f, nullptr, &ConsoleButton);
	ConsoleButton.HSplitTop(20.0f, &ConsoleButton, nullptr);
	ConsoleButton.VSplitRight(30.0f, &ConsoleButton, nullptr);
	ConsoleButton.VSplitRight(30.0f, nullptr, &ConsoleButton);
	static CButtonContainer s_ConsoleButton;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FontIcon::TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f)))
		GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	// update notice, bottom left under the menu
	CKing &King = GameClient()->m_King;
	if(King.UpdateAvailable() || King.Installing() || King.InstallError()[0])
	{
		CUIRect Notice, NoticeButton;
		Notice.x = Left;
		Notice.y = MainView.y + MainView.h - 50.0f;
		Notice.w = MainView.w * 0.4f;
		Notice.h = 20.0f;
		Notice.VSplitLeft(80.0f, &NoticeButton, &Notice);
		Notice.VSplitLeft(10.0f, nullptr, &Notice);
		static CButtonContainer s_KingUpdateButton;
		if(King.Installing())
			Ui()->RenderProgressBar(NoticeButton, King.DownloadProgress());
		else if(King.UpdateAvailable() && GameClient()->m_Menus.DoButton_Menu(&s_KingUpdateButton, King.CanInstallUpdate() ? "Update" : "Download", 0, &NoticeButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.91f, 0.70f, 0.29f, 0.85f)))
			King.InstallUpdate();
		char aBuf[160];
		if(King.Installing())
			str_format(aBuf, sizeof(aBuf), "Downloading %s... %d%%", King.LatestVersion(), round_to_int(King.DownloadProgress() * 100.0f));
		else if(King.InstallError()[0])
			str_copy(aBuf, King.InstallError());
		else
			str_format(aBuf, sizeof(aBuf), "KING %s is out", King.LatestVersion());
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.7f);
		Ui()->DoLabel(&Notice, aBuf, 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

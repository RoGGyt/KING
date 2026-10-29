#include "menus.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

void CMenus::RenderSettingsCredits(CUIRect MainView)
{
	// KING: short credits. The base projects stay named (their license asks us not
	// to misrepresent where the code comes from); the full text is in license.txt.
	MainView.Margin(20.0f, &MainView);
	CUIRect Line;

	MainView.HSplitTop(40.0f, &Line, &MainView);
	Ui()->DoLabel(&Line, "KING", 32.0f, TEXTALIGN_ML);

	MainView.HSplitTop(24.0f, &Line, &MainView);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.7f);
	Ui()->DoLabel(&Line, "by RoGGyt", 16.0f, TEXTALIGN_ML);

	MainView.HSplitTop(30.0f, nullptr, &MainView);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
	MainView.HSplitTop(20.0f, &Line, &MainView);
	Ui()->DoLabel(&Line, "Built on the open source DDNet client and TClient.", 13.0f, TEXTALIGN_ML);
	MainView.HSplitTop(20.0f, &Line, &MainView);
	Ui()->DoLabel(&Line, "License: license.txt in the KING folder.", 13.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

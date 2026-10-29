/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_START_H
#define GAME_CLIENT_COMPONENTS_MENUS_START_H

#include <game/client/component.h>
#include <base/color.h>

#include <game/client/ui_rect.h>

class CMenusStart : public CComponentInterfaces
{
public:
	void RenderStartMenu(CUIRect MainView);

private:
	bool CheckHotKey(int Key) const;

	// KING main menu buttons
	enum EKingButtonStyle
	{
		KING_BUTTON_NORMAL = 0,
		KING_BUTTON_PRIMARY, // gold (Play)
		KING_BUTTON_DANGER, // red hover (Quit)
		KING_BUTTON_ACTIVE, // green accent (server running, unsaved map)
	};
	bool DoKingButton(class CButtonContainer *pButtonContainer, const char *pText, const char *pIcon, const CUIRect *pRect, int Style);
	void RenderKingStartMenu(CUIRect MainView);
	bool DoKingMenuItem(class CButtonContainer *pButtonContainer, const char *pText, float X, float Y, float FontSize, int Style);
	void RenderKingPlayer(CUIRect Area);
	bool StartButton(class CButtonContainer *pButtonContainer, const char *pText, const char *pImage, const char *pIcon, const CUIRect *pRect, float Rounding, ColorRGBA Color, int Style);
};

#endif

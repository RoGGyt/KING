/* KING: asset mixer (Settings > Assets > Editor).
 *
 * Mix gunpacks (and emoticons, particles, HUD, extras): pick a donor and a base, drag parts
 * of the donor onto the base, tint parts, take a whole weapon at once, undo, save as a new
 * asset. Loading, mixing and saving run on job threads; the menu only uploads the result
 * (at most one texture per finished mix), so editing never stalls a frame.
 */
#ifndef GAME_CLIENT_COMPONENTS_KING_KING_ASSET_EDITOR_H
#define GAME_CLIENT_COMPONENTS_KING_KING_ASSET_EDITOR_H

#include <engine/graphics.h>
#include <engine/image.h>

#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <memory>
#include <string>
#include <vector>

class IJob;

enum
{
	KING_EDITOR_BLEND_TEE = 0, // like tee colors: brightness of the part times the color
	KING_EDITOR_BLEND_SCREEN,
	KING_EDITOR_BLEND_MULTIPLY,
	KING_EDITOR_BLEND_OVERLAY,
	KING_EDITOR_NUM_BLENDS,
};

// one part of the image, in grid cells (both sides use the same layout)
struct SKingEditorPart
{
	int m_X = 0, m_Y = 0, m_W = 0, m_H = 0;
	char m_aName[48] = "";
	char m_aGroup[24] = ""; // weapon of a "weapon_<x>_..." sprite, empty otherwise
};

// what was done to one part of the result
struct SKingEditorEdit
{
	int m_From = -1; // donor part copied here, -1 = the base's own pixels
	bool m_Tint = false;
	unsigned m_Color = 0x00FF80; // packed HSL like tee colors
	int m_Blend = KING_EDITOR_BLEND_TEE;
	int m_Opacity = 100;
	bool Edited() const { return m_From >= 0 || m_Tint; }
	bool operator==(const SKingEditorEdit &Other) const
	{
		return m_From == Other.m_From && m_Tint == Other.m_Tint && m_Color == Other.m_Color && m_Blend == Other.m_Blend && m_Opacity == Other.m_Opacity;
	}
};

struct SKingEditorSide
{
	char m_aName[64] = "default";
	std::shared_ptr<CImageInfo> m_pImage; // RGBA, shared with running jobs
	IGraphics::CTextureHandle m_Texture;
	std::shared_ptr<class CKingEditorLoadJob> m_pJob;
	bool m_Failed = false;
};

struct SKingAssetEditor
{
	bool m_Open = false;
	int m_Cat = -1; // CKingAssetCatalog::CAT_*
	int m_GridX = 1, m_GridY = 1;
	std::vector<SKingEditorPart> m_vParts;
	std::vector<SKingEditorEdit> m_vEdits;
	std::vector<std::vector<SKingEditorEdit>> m_vUndo;
	int64_t m_LastCoalescedPush = 0;
	int m_LastCoalescedPart = -1;

	SKingEditorSide m_Donor;
	SKingEditorSide m_Base;
	std::vector<std::string> m_vAssetNames; // "default" first

	IGraphics::CTextureHandle m_Result;
	int m_ResultW = 0, m_ResultH = 0;
	bool m_Dirty = true;
	std::shared_ptr<class CKingEditorMixJob> m_pMixJob;
	std::shared_ptr<class CKingEditorMixJob> m_pSaveJob;

	int m_Selected = -1; // part selected on the result
	int m_Picked = -1; // donor part picked by a click (placed by clicking the result)
	int m_PressDonor = -1;
	vec2 m_PressPos = vec2(0.0f, 0.0f);
	bool m_Dragging = false;

	bool m_Unsaved = false;
	bool m_ConfirmExit = false;
	char m_aSaveName[50] = "";
	CLineInput m_SaveNameInput;
	char m_aSavedName[50] = ""; // last saved asset, for "Use"
	char m_aStatus[160] = "";
	bool m_StatusError = false;
};

#endif

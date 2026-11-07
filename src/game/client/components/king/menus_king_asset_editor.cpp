/* KING: asset mixer, see king_asset_editor.h */
#include "king_asset_editor.h"

#include <base/color.h>
#include <base/io.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/font_icons.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/components/king/king_assets.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
constexpr float ED_TOPBAR = 22.0f;
constexpr float ED_GAP = 8.0f;
constexpr float ED_TITLE = 18.0f;
constexpr float ED_STATUS = 16.0f;
constexpr float ED_ARROW = 28.0f;
constexpr float ED_LINE = 20.0f;
constexpr float ED_FONT = 12.0f;
constexpr float ED_MIN_IMAGE = 70.0f;
constexpr int ED_UNDO_MAX = 64;
const ColorRGBA ED_ACCENT(0.96f, 0.76f, 0.30f, 1.0f);

const char *const s_apWeaponGroups[] = {"hammer", "gun", "shotgun", "grenade", "laser", "ninja", "hook"};

int CatImageId(int Cat)
{
	switch(Cat)
	{
	case CKingAssetCatalog::CAT_GAME: return IMAGE_GAME;
	case CKingAssetCatalog::CAT_EMOTICONS: return IMAGE_EMOTICONS;
	case CKingAssetCatalog::CAT_PARTICLES: return IMAGE_PARTICLES;
	case CKingAssetCatalog::CAT_HUD: return IMAGE_HUD;
	case CKingAssetCatalog::CAT_EXTRAS: return IMAGE_EXTRAS;
	default: return -1; // entities are 16x16 tile sheets of up to 4096 px: not supported
	}
}

void PartGroup(const char *pName, char *pGroup, int GroupSize)
{
	pGroup[0] = '\0';
	const char *pRest = nullptr;
	if(str_startswith(pName, "weapon_"))
		pRest = pName + 7;
	else if(str_startswith(pName, "pickup_armor_"))
		pRest = pName + 13;
	else if(str_startswith(pName, "pickup_"))
		pRest = pName + 7;
	else if(str_startswith(pName, "hook_"))
	{
		str_copy(pGroup, "hook", GroupSize);
		return;
	}
	if(!pRest)
		return;
	for(const char *pWeapon : s_apWeaponGroups)
	{
		const int Length = str_length(pWeapon);
		if(str_comp_num(pRest, pWeapon, Length) == 0 && (pRest[Length] == '\0' || pRest[Length] == '_'))
		{
			str_copy(pGroup, pWeapon, GroupSize);
			return;
		}
	}
}

// every sprite of the image once (several sprites share one spot), plus the spots stock
// data does not name as sprites
void BuildParts(int Cat, std::vector<SKingEditorPart> &vParts, int &GridX, int &GridY)
{
	vParts.clear();
	GridX = GridY = 1;
	const int ImageId = CatImageId(Cat);
	if(ImageId < 0)
		return;
	const CDataImage *pImage = &g_pData->m_aImages[ImageId];
	auto Has = [&](int X, int Y, int W, int H) {
		for(const SKingEditorPart &Part : vParts)
			if(Part.m_X == X && Part.m_Y == Y && Part.m_W == W && Part.m_H == H)
				return true;
		return false;
	};
	auto Add = [&](const char *pName, int X, int Y, int W, int H) {
		if(W <= 0 || H <= 0 || Has(X, Y, W, H))
			return;
		SKingEditorPart Part;
		Part.m_X = X;
		Part.m_Y = Y;
		Part.m_W = W;
		Part.m_H = H;
		str_copy(Part.m_aName, pName);
		PartGroup(pName, Part.m_aGroup, sizeof(Part.m_aGroup));
		vParts.push_back(Part);
	};
	for(int i = 0; i < NUM_SPRITES; i++)
	{
		const CDataSprite &Sprite = g_pData->m_aSprites[i];
		if(!Sprite.m_pSet || Sprite.m_pSet->m_pImage != pImage || !Sprite.m_pName)
			continue;
		GridX = std::max(1, Sprite.m_pSet->m_Gridx);
		GridY = std::max(1, Sprite.m_pSet->m_Gridy);
		Add(Sprite.m_pName, Sprite.m_X, Sprite.m_Y, Sprite.m_W, Sprite.m_H);
	}
	if(Cat == CKingAssetCatalog::CAT_GAME)
	{
		Add("ninja_bar_full_left", 21, 4, 1, 2);
		Add("ninja_bar_full", 22, 4, 1, 2);
		Add("ninja_bar_empty", 23, 4, 1, 2);
		Add("ninja_bar_empty_right", 24, 4, 1, 2);
	}
}

/* ---------- pixel work (job threads) ---------- */

// copy a rectangle with scaling: box average when shrinking (premultiplied alpha, no dark
// edges), bilinear when growing, a plain copy when the sizes match
void Resample(CImageInfo &Dst, int DX, int DY, int DW, int DH, const CImageInfo &Src, int SX, int SY, int SW, int SH)
{
	if(DW <= 0 || DH <= 0 || SW <= 0 || SH <= 0)
		return;
	if(DX < 0 || DY < 0 || DX + DW > (int)Dst.m_Width || DY + DH > (int)Dst.m_Height)
		return;
	if(SX < 0 || SY < 0 || SX + SW > (int)Src.m_Width || SY + SH > (int)Src.m_Height)
		return;
	const size_t DStride = Dst.m_Width * 4, SStride = Src.m_Width * 4;
	if(DW == SW && DH == SH)
	{
		for(int y = 0; y < DH; y++)
			mem_copy(Dst.m_pData + (DY + y) * DStride + DX * 4, Src.m_pData + (SY + y) * SStride + SX * 4, (size_t)DW * 4);
		return;
	}
	const bool Shrink = SW > DW || SH > DH;
	for(int y = 0; y < DH; y++)
	{
		uint8_t *pRow = Dst.m_pData + (DY + y) * DStride + DX * 4;
		for(int x = 0; x < DW; x++)
		{
			float aSum[4] = {0, 0, 0, 0};
			if(Shrink)
			{
				const int X0 = SX + (int)((int64_t)x * SW / DW), X1 = std::max(X0 + 1, SX + (int)((int64_t)(x + 1) * SW / DW));
				const int Y0 = SY + (int)((int64_t)y * SH / DH), Y1 = std::max(Y0 + 1, SY + (int)((int64_t)(y + 1) * SH / DH));
				int Count = 0;
				for(int sy = Y0; sy < Y1; sy++)
					for(int sx = X0; sx < X1; sx++)
					{
						const uint8_t *p = Src.m_pData + sy * SStride + sx * 4;
						const float A = p[3] / 255.0f;
						aSum[0] += p[0] * A;
						aSum[1] += p[1] * A;
						aSum[2] += p[2] * A;
						aSum[3] += A;
						Count++;
					}
				const float Alpha = aSum[3] / Count;
				for(int c = 0; c < 3; c++)
					pRow[x * 4 + c] = (uint8_t)std::clamp(aSum[3] > 0.0f ? aSum[c] / aSum[3] : 0.0f, 0.0f, 255.0f);
				pRow[x * 4 + 3] = (uint8_t)std::clamp(Alpha * 255.0f + 0.5f, 0.0f, 255.0f);
			}
			else
			{
				const float U = std::clamp(SX + (x + 0.5f) * SW / DW - 0.5f, (float)SX, (float)(SX + SW - 1));
				const float V = std::clamp(SY + (y + 0.5f) * SH / DH - 0.5f, (float)SY, (float)(SY + SH - 1));
				const int U0 = (int)U, V0 = (int)V;
				const int U1 = std::min(U0 + 1, SX + SW - 1), V1 = std::min(V0 + 1, SY + SH - 1);
				const float FU = U - U0, FV = V - V0;
				const uint8_t *p00 = Src.m_pData + V0 * SStride + U0 * 4, *p10 = Src.m_pData + V0 * SStride + U1 * 4;
				const uint8_t *p01 = Src.m_pData + V1 * SStride + U0 * 4, *p11 = Src.m_pData + V1 * SStride + U1 * 4;
				const float aW[4] = {(1 - FU) * (1 - FV) * p00[3], FU * (1 - FV) * p10[3], (1 - FU) * FV * p01[3], FU * FV * p11[3]};
				const float WSum = aW[0] + aW[1] + aW[2] + aW[3];
				for(int c = 0; c < 3; c++)
				{
					const float Value = WSum > 0.0f ? (p00[c] * aW[0] + p10[c] * aW[1] + p01[c] * aW[2] + p11[c] * aW[3]) / WSum : 0.0f;
					pRow[x * 4 + c] = (uint8_t)std::clamp(Value + 0.5f, 0.0f, 255.0f);
				}
				const float Alpha = (1 - FU) * (1 - FV) * p00[3] + FU * (1 - FV) * p10[3] + (1 - FU) * FV * p01[3] + FU * FV * p11[3];
				pRow[x * 4 + 3] = (uint8_t)std::clamp(Alpha + 0.5f, 0.0f, 255.0f);
			}
		}
	}
}

float Overlay(float Base, float Blend)
{
	return Base < 0.5f ? 2.0f * Base * Blend : 1.0f - 2.0f * (1.0f - Base) * (1.0f - Blend);
}

void Tint(CImageInfo &Image, int X, int Y, int W, int H, const SKingEditorEdit &Edit)
{
	if(X < 0 || Y < 0 || W <= 0 || H <= 0 || X + W > (int)Image.m_Width || Y + H > (int)Image.m_Height)
		return;
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(Edit.m_Color).UnclampLighting(ColorHSLA::DARKEST_LGT));
	const float Opacity = std::clamp(Edit.m_Opacity, 0, 100) / 100.0f;
	if(Opacity <= 0.0f)
		return;
	for(int y = 0; y < H; y++)
	{
		uint8_t *p = Image.m_pData + ((Y + y) * Image.m_Width + X) * 4;
		for(int x = 0; x < W; x++, p += 4)
		{
			if(p[3] == 0)
				continue;
			const float R = p[0] / 255.0f, G = p[1] / 255.0f, B = p[2] / 255.0f;
			float OR = R, OG = G, OB = B;
			switch(Edit.m_Blend)
			{
			case KING_EDITOR_BLEND_TEE:
			{
				const float Luma = 0.2126f * R + 0.7152f * G + 0.0722f * B;
				OR = Luma * Color.r;
				OG = Luma * Color.g;
				OB = Luma * Color.b;
				break;
			}
			case KING_EDITOR_BLEND_SCREEN:
				OR = 1.0f - (1.0f - R) * (1.0f - Color.r);
				OG = 1.0f - (1.0f - G) * (1.0f - Color.g);
				OB = 1.0f - (1.0f - B) * (1.0f - Color.b);
				break;
			case KING_EDITOR_BLEND_MULTIPLY:
				OR = R * Color.r;
				OG = G * Color.g;
				OB = B * Color.b;
				break;
			case KING_EDITOR_BLEND_OVERLAY:
				OR = Overlay(R, Color.r);
				OG = Overlay(G, Color.g);
				OB = Overlay(B, Color.b);
				break;
			}
			p[0] = (uint8_t)std::clamp(round_to_int(mix(R, OR, Opacity) * 255.0f), 0, 255);
			p[1] = (uint8_t)std::clamp(round_to_int(mix(G, OG, Opacity) * 255.0f), 0, 255);
			p[2] = (uint8_t)std::clamp(round_to_int(mix(B, OB, Opacity) * 255.0f), 0, 255);
		}
	}
}

void Mix(const CImageInfo &Base, const CImageInfo *pDonor, int GridX, int GridY, const std::vector<SKingEditorPart> &vParts, const std::vector<SKingEditorEdit> &vEdits, CImageInfo &Out)
{
	Out = Base.DeepCopy();
	const int CellW = (int)Out.m_Width / GridX, CellH = (int)Out.m_Height / GridY;
	const int DonorW = pDonor ? (int)pDonor->m_Width / GridX : 0, DonorH = pDonor ? (int)pDonor->m_Height / GridY : 0;
	for(size_t i = 0; i < vParts.size() && i < vEdits.size(); i++)
	{
		const SKingEditorEdit &Edit = vEdits[i];
		if(!Edit.Edited())
			continue;
		const SKingEditorPart &Part = vParts[i];
		const int X = Part.m_X * CellW, Y = Part.m_Y * CellH, W = Part.m_W * CellW, H = Part.m_H * CellH;
		if(Edit.m_Erase)
		{
			// removed: fully transparent, the game then draws nothing there
			if(X >= 0 && Y >= 0 && X + W <= (int)Out.m_Width && Y + H <= (int)Out.m_Height)
				for(int Row = 0; Row < H; Row++)
					mem_zero(Out.m_pData + ((size_t)(Y + Row) * Out.m_Width + X) * 4, (size_t)W * 4);
			continue;
		}
		if(Edit.m_From >= 0 && Edit.m_From < (int)vParts.size() && pDonor && DonorW > 0 && DonorH > 0)
		{
			const SKingEditorPart &From = vParts[Edit.m_From];
			Resample(Out, X, Y, W, H, *pDonor, From.m_X * DonorW, From.m_Y * DonorH, From.m_W * DonorW, From.m_H * DonorH);
		}
		if(Edit.m_Tint)
			Tint(Out, X, Y, W, H, Edit);
	}
}
} // namespace

/* ---------- jobs ---------- */

class CKingEditorLoadJob : public IJob
{
public:
	CKingEditorLoadJob(IGraphics *pGraphics, const char *pPath, int GridX, int GridY) :
		m_pGraphics(pGraphics), m_GridX(GridX), m_GridY(GridY)
	{
		str_copy(m_aPath, pPath);
		Abortable(true);
	}
	std::shared_ptr<CImageInfo> m_pImage;
	bool m_Ok = false;

private:
	IGraphics *m_pGraphics;
	int m_GridX, m_GridY;
	char m_aPath[IO_MAX_PATH_LENGTH];

	void Run() override
	{
		auto pImage = std::make_shared<CImageInfo>();
		if(!m_pGraphics->LoadPng(*pImage, m_aPath, IStorage::TYPE_ALL) || pImage->m_Width == 0 || pImage->m_Height == 0)
			return;
		ConvertToRgba(*pImage);
		// the grid must divide the image, or parts would be cut between pixels
		const int W = std::max(m_GridX, (int)(pImage->m_Width / m_GridX) * m_GridX);
		const int H = std::max(m_GridY, (int)(pImage->m_Height / m_GridY) * m_GridY);
		if(W != (int)pImage->m_Width || H != (int)pImage->m_Height)
			ResizeImage(*pImage, W, H);
		m_pImage = std::move(pImage);
		m_Ok = true;
	}
};

class CKingEditorMixJob : public IJob
{
public:
	CKingEditorMixJob(std::shared_ptr<CImageInfo> pBase, std::shared_ptr<CImageInfo> pDonor, int GridX, int GridY, std::vector<SKingEditorPart> vParts, std::vector<SKingEditorEdit> vEdits) :
		m_pBase(std::move(pBase)), m_pDonor(std::move(pDonor)), m_GridX(GridX), m_GridY(GridY), m_vParts(std::move(vParts)), m_vEdits(std::move(vEdits))
	{
		Abortable(true);
	}
	// saving instead of previewing
	void SaveTo(IStorage *pStorage, int Cat, const char *pName)
	{
		m_pStorage = pStorage;
		m_Cat = Cat;
		str_copy(m_aName, pName);
		Abortable(false);
	}

	CImageInfo m_Result; // preview, or the saved image's thumbnail when saving
	bool m_Ok = false;
	int m_Cat = 0;
	char m_aName[50] = "";

private:
	std::shared_ptr<CImageInfo> m_pBase, m_pDonor;
	int m_GridX, m_GridY;
	std::vector<SKingEditorPart> m_vParts;
	std::vector<SKingEditorEdit> m_vEdits;
	IStorage *m_pStorage = nullptr;

	void Run() override
	{
		Mix(*m_pBase, m_pDonor ? m_pDonor.get() : nullptr, m_GridX, m_GridY, m_vParts, m_vEdits, m_Result);
		if(!m_pStorage)
		{
			m_Ok = m_Result.m_pData != nullptr;
			return;
		}
		char aFolder[IO_MAX_PATH_LENGTH], aPath[IO_MAX_PATH_LENGTH], aTmp[IO_MAX_PATH_LENGTH];
		str_format(aFolder, sizeof(aFolder), "assets/%s", CKingAssetCatalog::CategoryFolder(m_Cat));
		str_format(aPath, sizeof(aPath), "%s/%s.png", aFolder, m_aName);
		str_format(aTmp, sizeof(aTmp), "%s.part", aPath);
		m_pStorage->CreateFolder("assets", IStorage::TYPE_SAVE);
		m_pStorage->CreateFolder(aFolder, IStorage::TYPE_SAVE);
		if(m_pStorage->FileExists(aPath, IStorage::TYPE_SAVE))
			return;
		IOHANDLE File = m_pStorage->OpenFile(aTmp, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File || !CImageLoader::SavePng(File, aTmp, m_Result) || !m_pStorage->RenameFile(aTmp, aPath, IStorage::TYPE_SAVE))
		{
			m_pStorage->RemoveFile(aTmp, IStorage::TYPE_SAVE);
			return;
		}
		KingMakeThumbnail(m_Result); // for the menu list
		m_Ok = true;
	}
};

/* ---------- layout (pure, so every menu size can be checked) ---------- */

struct SKingEditorLayout
{
	bool m_Stack = false; // donor above result instead of side by side
	bool m_WideInspector = false; // part tools and tint side by side
	float m_ImageH = 0.0f;
	float m_InspectorH = 0.0f;
	float m_ContentH = 0.0f;
	float m_ToolsW = 0.0f, m_TintW = 0.0f;
};

static int FlowRows(int NumButtons, float MinButtonW, float Width)
{
	const int PerRow = std::max(1, (int)((Width + 4.0f) / (MinButtonW + 4.0f)));
	return (NumButtons + PerRow - 1) / PerRow;
}

static float ToolsHeight(float W, bool Game)
{
	float H = ED_TITLE + 4.0f + ED_LINE; // part name, part buttons
	if(Game)
		H += 8.0f + ED_TITLE + FlowRows(std::size(s_apWeaponGroups), 66.0f, W) * (ED_LINE + 4.0f);
	H += 8.0f + FlowRows(3, 110.0f, W) * (ED_LINE + 4.0f); // everything / reset all / undo
	return H;
}

static float TintHeight(float W)
{
	// tint checkbox, then the HSL sliders (75) with blend + opacity beside or below them
	if(W >= 380.0f)
		return ED_LINE + 4.0f + 80.0f;
	return ED_LINE + 4.0f + 80.0f + 4.0f + FlowRows(KING_EDITOR_NUM_BLENDS, 64.0f, W) * (ED_LINE + 4.0f) + ED_LINE;
}

static SKingEditorLayout KingEditorLayoutFor(float W, float H, float Aspect, bool Game, bool Wide)
{
	SKingEditorLayout L;
	L.m_WideInspector = Wide;
	if(Wide)
	{
		L.m_ToolsW = std::floor(W * 0.42f);
		L.m_TintW = W - L.m_ToolsW - ED_GAP * 2.0f;
		L.m_InspectorH = std::max(ToolsHeight(L.m_ToolsW, Game), TintHeight(L.m_TintW));
	}
	else
	{
		L.m_ToolsW = L.m_TintW = W;
		L.m_InspectorH = ToolsHeight(W, Game) + ED_GAP + TintHeight(W);
	}
	const float Fixed = ED_TOPBAR + ED_GAP + ED_GAP + L.m_InspectorH + ED_GAP + ED_STATUS;
	const float CanvasAvail = H - Fixed;
	const float PanelW = (W - ED_ARROW) / 2.0f;
	const float MinImage = ED_MIN_IMAGE * std::min(1.0f, Aspect * 2.0f);
	const float SideBySide = std::min(PanelW * Aspect, CanvasAvail - ED_TITLE);
	const float Stacked = std::min(W * Aspect, (CanvasAvail - 2.0f * ED_TITLE - ED_GAP) / 2.0f);
	// stacked only when it really gives bigger images; when nothing fits, side by side
	// needs the least scrolling
	L.m_Stack = Stacked > SideBySide + 1.0f && Stacked >= MinImage;
	L.m_ImageH = std::max(std::min(MinImage, (L.m_Stack ? W : PanelW) * Aspect), L.m_Stack ? Stacked : SideBySide);
	const float CanvasH = L.m_Stack ? 2.0f * (ED_TITLE + L.m_ImageH) + ED_GAP : ED_TITLE + L.m_ImageH;
	L.m_ContentH = std::max(H, Fixed + CanvasH);
	return L;
}

// the arrangement that shows the biggest images (and scrolls the least) for this size
static SKingEditorLayout KingEditorLayout(float W, float H, float Aspect, bool Game)
{
	const SKingEditorLayout Narrow = KingEditorLayoutFor(W, H, Aspect, Game, false);
	if(W < 560.0f)
		return Narrow;
	const SKingEditorLayout Wide = KingEditorLayoutFor(W, H, Aspect, Game, true);
	if(Wide.m_ContentH != Narrow.m_ContentH)
		return Wide.m_ContentH < Narrow.m_ContentH ? Wide : Narrow;
	return Wide.m_ImageH + 0.5f >= Narrow.m_ImageH ? Wide : Narrow;
}

/* ---------- menu ---------- */

static void SetStatus(SKingAssetEditor &E, bool Error, const char *pText)
{
	str_copy(E.m_aStatus, pText);
	E.m_StatusError = Error;
}

static void PushUndo(SKingAssetEditor &E, int CoalescePart = -1)
{
	// slider drags of one part become one undo step
	const int64_t Now = time_get();
	if(CoalescePart >= 0 && CoalescePart == E.m_LastCoalescedPart && Now - E.m_LastCoalescedPush < time_freq() * 6 / 10)
	{
		E.m_LastCoalescedPush = Now;
		return;
	}
	E.m_LastCoalescedPart = CoalescePart;
	E.m_LastCoalescedPush = Now;
	E.m_vUndo.push_back(E.m_vEdits);
	if((int)E.m_vUndo.size() > ED_UNDO_MAX)
		E.m_vUndo.erase(E.m_vUndo.begin());
	E.m_Unsaved = true;
	E.m_ConfirmExit = false;
	E.m_Dirty = true;
}

static void FreeSide(IGraphics *pGraphics, SKingEditorSide &Side)
{
	pGraphics->UnloadTexture(&Side.m_Texture);
	Side.m_pImage = nullptr;
	Side.m_pJob = nullptr; // a running job finishes on its own
	Side.m_Failed = false;
}

void CMenus::KingEditorClose()
{
	SKingAssetEditor &E = m_KingEditor;
	FreeSide(Graphics(), E.m_Donor);
	FreeSide(Graphics(), E.m_Base);
	Graphics()->UnloadTexture(&E.m_Result);
	E.m_pMixJob = nullptr;
	E.m_vParts.clear();
	E.m_vEdits.clear();
	E.m_vUndo.clear();
	E.m_Open = false;
	E.m_Cat = -1;
	E.m_Unsaved = false;
	E.m_ConfirmExit = false;
	E.m_Selected = E.m_Picked = E.m_PressDonor = -1;
	E.m_Dragging = false;
	E.m_aStatus[0] = '\0';
}

static void ScanAssetNames(IStorage *pStorage, int Cat, std::vector<std::string> &vNames)
{
	vNames.clear();
	char aFolder[IO_MAX_PATH_LENGTH];
	str_format(aFolder, sizeof(aFolder), "assets/%s", CKingAssetCatalog::CategoryFolder(Cat));
	pStorage->ListDirectory(
		IStorage::TYPE_ALL, aFolder, [](const char *pName, int IsDir, int, void *pUser) {
			auto *pNames = (std::vector<std::string> *)pUser;
			if(pName[0] == '.' || str_comp(pName, "default") == 0)
				return 0;
			if(IsDir)
				pNames->emplace_back(pName);
			else if(str_endswith(pName, ".png") && !str_endswith(pName, ".part.png"))
				pNames->emplace_back(pName, str_length(pName) - 4);
			return 0;
		},
		&vNames);
	std::sort(vNames.begin(), vNames.end(), [](const std::string &a, const std::string &b) { return str_comp_nocase(a.c_str(), b.c_str()) < 0; });
	vNames.erase(std::unique(vNames.begin(), vNames.end()), vNames.end());
	vNames.insert(vNames.begin(), "default");
}

void CMenus::KingEditorLoadSide(SKingEditorSide &Side, const char *pName)
{
	SKingAssetEditor &E = m_KingEditor;
	str_copy(Side.m_aName, pName);
	Side.m_Failed = false;
	char aPath[IO_MAX_PATH_LENGTH] = "";
	const char *pFolder = CKingAssetCatalog::CategoryFolder(E.m_Cat);
	if(str_comp(pName, "default") == 0)
		str_copy(aPath, g_pData->m_aImages[CatImageId(E.m_Cat)].m_pFilename);
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pFolder, pName);
		if(!Storage()->FileExists(aPath, IStorage::TYPE_ALL))
			str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pFolder, pName, pFolder);
	}
	Side.m_pJob = std::make_shared<CKingEditorLoadJob>(Graphics(), aPath, E.m_GridX, E.m_GridY);
	Engine()->AddJob(Side.m_pJob);
}

static bool CatSupported(int Tab)
{
	return CatImageId(Tab) >= 0;
}

bool CMenus::KingEditorSupports(int Tab) const
{
	return CatSupported(Tab);
}

void CMenus::RenderKingAssetEditor(CUIRect MainView, int &Tab, bool &Active)
{
	SKingAssetEditor &E = m_KingEditor;

	// ---- open / follow the asset tab ----
	if(E.m_Open && Tab != E.m_Cat)
	{
		if(E.m_Unsaved)
		{
			Tab = E.m_Cat; // keep the work: leave with Save or Back first
			SetStatus(E, true, Localize("Save your mix or leave the editor before switching the type."));
		}
		else
			KingEditorClose();
	}
	if(!E.m_Open)
	{
		if(!CatSupported(Tab))
		{
			Active = false;
			return;
		}
		E.m_Open = true;
		E.m_Cat = Tab;
		BuildParts(E.m_Cat, E.m_vParts, E.m_GridX, E.m_GridY);
		E.m_vEdits.assign(E.m_vParts.size(), SKingEditorEdit());
		ScanAssetNames(Storage(), E.m_Cat, E.m_vAssetNames);
		KingEditorLoadSide(E.m_Donor, "default");
		KingEditorLoadSide(E.m_Base, "default");
		E.m_SaveNameInput.SetBuffer(E.m_aSaveName, sizeof(E.m_aSaveName));
		E.m_SaveNameInput.Set("my_mix");
		E.m_aSavedName[0] = '\0';
		SetStatus(E, false, "");
		E.m_Dirty = true;
	}

	// ---- jobs ----
	for(SKingEditorSide *pSide : {&E.m_Donor, &E.m_Base})
	{
		if(pSide->m_pJob && pSide->m_pJob->Done())
		{
			if(pSide->m_pJob->m_Ok)
			{
				Graphics()->UnloadTexture(&pSide->m_Texture);
				pSide->m_pImage = pSide->m_pJob->m_pImage;
				pSide->m_Texture = Graphics()->LoadTextureRaw(*pSide->m_pImage, 0, pSide->m_aName);
				E.m_Dirty = true;
			}
			else
			{
				pSide->m_Failed = true;
				SetStatus(E, true, Localize("Could not load this asset."));
			}
			pSide->m_pJob = nullptr;
		}
	}
	if(E.m_pMixJob && E.m_pMixJob->Done())
	{
		if(E.m_pMixJob->m_Ok)
		{
			Graphics()->UnloadTexture(&E.m_Result);
			E.m_ResultW = (int)E.m_pMixJob->m_Result.m_Width;
			E.m_ResultH = (int)E.m_pMixJob->m_Result.m_Height;
			E.m_Result = Graphics()->LoadTextureRawMove(E.m_pMixJob->m_Result, 0, "king_mix");
		}
		E.m_pMixJob = nullptr;
	}
	if(E.m_pSaveJob && E.m_pSaveJob->Done())
	{
		char aBuf[160];
		if(E.m_pSaveJob->m_Ok)
		{
			str_format(aBuf, sizeof(aBuf), Localize("Saved as %s"), E.m_pSaveJob->m_aName);
			SetStatus(E, false, aBuf);
			str_copy(E.m_aSavedName, E.m_pSaveJob->m_aName);
			E.m_Unsaved = false;
			E.m_ConfirmExit = false;
			KingAddDownloadedAsset(E.m_pSaveJob->m_Cat, E.m_pSaveJob->m_aName, E.m_pSaveJob->m_Result);
			ScanAssetNames(Storage(), E.m_Cat, E.m_vAssetNames);
		}
		else
			SetStatus(E, true, Localize("Could not save: an asset with this name exists, or the folder is not writable."));
		E.m_pSaveJob = nullptr;
	}
	if(E.m_Dirty && !E.m_pMixJob && E.m_Base.m_pImage)
	{
		E.m_pMixJob = std::make_shared<CKingEditorMixJob>(E.m_Base.m_pImage, E.m_Donor.m_pImage, E.m_GridX, E.m_GridY, E.m_vParts, E.m_vEdits);
		Engine()->AddJob(E.m_pMixJob);
		E.m_Dirty = false;
	}

	// ---- keys ----
	const bool PopupOpen = Ui()->IsPopupOpen();
	if(!PopupOpen && !GameClient()->m_GameConsole.IsActive())
	{
		if(CLineInput::GetActiveInput() == nullptr && Input()->ModifierIsPressed() && Input()->KeyPress(KEY_Z) && !E.m_vUndo.empty())
		{
			E.m_vEdits = E.m_vUndo.back();
			E.m_vUndo.pop_back();
			E.m_LastCoalescedPart = -1;
			E.m_Dirty = true;
			E.m_Unsaved = true;
		}
		if(CLineInput::GetActiveInput() == nullptr && E.m_Selected >= 0 && Input()->KeyPress(KEY_DELETE) && !E.m_vEdits[E.m_Selected].m_Erase)
		{
			PushUndo(E);
			E.m_vEdits[E.m_Selected].m_Erase = true;
			SetStatus(E, false, Localize("Part removed"));
		}
		if((E.m_Picked >= 0 || E.m_Dragging) && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			E.m_Picked = -1;
			E.m_Dragging = false;
			E.m_PressDonor = -1;
		}
	}

	// ---- layout ----
	// shape of the image: the base's real one once loaded (some packs are not 2:1)
	const float Aspect = E.m_Base.m_pImage ? E.m_Base.m_pImage->m_Height / (float)std::max<size_t>(1, E.m_Base.m_pImage->m_Width) : E.m_GridY / (float)E.m_GridX;
	const bool Game = E.m_Cat == CKingAssetCatalog::CAT_GAME;
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ScrollbarMargin = 2.0f;
	const float ViewH = MainView.h;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	const SKingEditorLayout L = KingEditorLayout(MainView.w, ViewH, Aspect, Game);
	CUIRect Content = MainView;
	Content.h = L.m_ContentH;
	s_ScrollRegion.AddRect(Content);

	CUIRect TopBar, Canvas, Inspector, Status;
	Content.HSplitTop(ED_TOPBAR, &TopBar, &Content);
	Content.HSplitTop(ED_GAP, nullptr, &Content);
	Content.HSplitBottom(ED_STATUS, &Content, &Status);
	Content.HSplitBottom(ED_GAP, &Content, nullptr);
	Content.HSplitBottom(L.m_InspectorH, &Canvas, &Inspector);
	Canvas.HSplitBottom(ED_GAP, &Canvas, nullptr);

	// ---- top bar: back | name | save | use ----
	{
		CUIRect Back, Name, Save, Use;
		TopBar.VSplitLeft(std::min(150.0f, TopBar.w * 0.3f), &Back, &TopBar);
		TopBar.VSplitLeft(ED_GAP, nullptr, &TopBar);
		static CButtonContainer s_BackButton;
		const char *pBackLabel = E.m_ConfirmExit ? Localize("Leave without saving?") : Localize("My assets");
		if(DoButton_Menu(&s_BackButton, pBackLabel, 0, &Back, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, E.m_ConfirmExit ? ColorRGBA(1.0f, 0.35f, 0.35f, 0.6f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f)))
		{
			if(E.m_Unsaved && !E.m_ConfirmExit)
			{
				E.m_ConfirmExit = true;
				SetStatus(E, true, Localize("You have unsaved changes. Click again to leave without saving."));
			}
			else
			{
				KingEditorClose();
				Active = false;
				s_ScrollRegion.End();
				return;
			}
		}
		const bool HasSaved = E.m_aSavedName[0] != '\0' && !E.m_Unsaved;
		if(HasSaved)
		{
			TopBar.VSplitRight(std::min(110.0f, TopBar.w * 0.25f), &TopBar, &Use);
			TopBar.VSplitRight(ED_GAP, &TopBar, nullptr);
		}
		TopBar.VSplitRight(std::min(110.0f, TopBar.w * 0.35f), &Name, &Save);
		Name.VSplitRight(ED_GAP, &Name, nullptr);
		Ui()->DoEditBox(&E.m_SaveNameInput, &Name, ED_FONT);
		static CButtonContainer s_SaveButton;
		if(DoButton_Menu(&s_SaveButton, E.m_pSaveJob ? Localize("Saving…") : Localize("Save"), 0, &Save, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ED_ACCENT.WithAlpha(0.45f)) && !E.m_pSaveJob)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "assets/%s/%s.png", CKingAssetCatalog::CategoryFolder(E.m_Cat), E.m_aSaveName);
			if(!KingValidAssetName(E.m_aSaveName))
				SetStatus(E, true, Localize("This name cannot be used for a file."));
			else if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
				SetStatus(E, true, Localize("An asset with this name already exists."));
			else if(!E.m_Base.m_pImage)
				SetStatus(E, true, Localize("The base is still loading."));
			else
			{
				E.m_pSaveJob = std::make_shared<CKingEditorMixJob>(E.m_Base.m_pImage, E.m_Donor.m_pImage, E.m_GridX, E.m_GridY, E.m_vParts, E.m_vEdits);
				E.m_pSaveJob->SaveTo(Storage(), E.m_Cat, E.m_aSaveName);
				Engine()->AddJob(E.m_pSaveJob);
			}
		}
		if(HasSaved)
		{
			static CButtonContainer s_UseButton;
			const bool InUse = KingAssetIsCurrent(E.m_Cat, E.m_aSavedName);
			if(DoButton_Menu(&s_UseButton, InUse ? Localize("In use") : Localize("Use"), InUse, &Use) && !InUse)
				KingApplyAsset(E.m_Cat, E.m_aSavedName);
		}
	}

	// ---- canvases ----
	CUIRect DonorPanel, ResultPanel, Arrow;
	if(L.m_Stack)
	{
		Canvas.HSplitMid(&DonorPanel, &ResultPanel, ED_GAP);
		Arrow = CUIRect{0, 0, 0, 0};
	}
	else
	{
		Canvas.VSplitMid(&DonorPanel, &ResultPanel, ED_ARROW);
		Arrow = {DonorPanel.x + DonorPanel.w, Canvas.y + ED_TITLE, ED_ARROW, Canvas.h - ED_TITLE};
	}

	std::vector<const char *> vpNames;
	vpNames.reserve(E.m_vAssetNames.size());
	for(const std::string &Name : E.m_vAssetNames)
		vpNames.push_back(Name.c_str());
	auto Picker = [&](CUIRect Rect, SKingEditorSide &Side, CUi::SDropDownState &State, CScrollRegion &Region, const char *pLabel) {
		CUIRect Label, Drop;
		Rect.VSplitLeft(std::min(90.0f, Rect.w * 0.35f), &Label, &Drop);
		TextRender()->TextColor(ED_ACCENT);
		Ui()->DoLabel(&Label, pLabel, ED_FONT + 1.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		int Selected = 0;
		for(size_t i = 0; i < E.m_vAssetNames.size(); i++)
			if(E.m_vAssetNames[i] == Side.m_aName)
				Selected = (int)i;
		State.m_SelectionPopupContext.m_pScrollRegion = &Region; // the popup needs one, or it crashes
		const int New = Ui()->DoDropDown(&Drop, Selected, vpNames.data(), (int)vpNames.size(), State);
		if(New != Selected && New >= 0 && New < (int)vpNames.size())
		{
			KingEditorLoadSide(Side, vpNames[New]);
			if(&Side == &E.m_Base)
			{
				char aName[50];
				str_format(aName, sizeof(aName), "%.40s_mix", vpNames[New]);
				E.m_SaveNameInput.Set(aName);
			}
		}
	};

	auto FitImage = [&](CUIRect Area, CUIRect &Fitted) {
		float W = Area.w, H = Area.w * Aspect;
		if(H > Area.h)
		{
			H = Area.h;
			W = H / Aspect;
		}
		Fitted = {Area.x + (Area.w - W) / 2.0f, Area.y + (Area.h - H) / 2.0f, W, H};
	};
	auto PartRect = [&](const CUIRect &Fitted, const SKingEditorPart &Part) {
		return CUIRect{Fitted.x + Fitted.w * Part.m_X / E.m_GridX, Fitted.y + Fitted.h * Part.m_Y / E.m_GridY, Fitted.w * Part.m_W / E.m_GridX, Fitted.h * Part.m_H / E.m_GridY};
	};
	auto HitPart = [&](const CUIRect &Fitted, vec2 Pos, const SKingEditorPart *pPrefer) {
		int Best = -1;
		float BestArea = 1e9f;
		for(size_t i = 0; i < E.m_vParts.size(); i++)
		{
			const CUIRect R = PartRect(Fitted, E.m_vParts[i]);
			if(Pos.x < R.x || Pos.y < R.y || Pos.x >= R.x + R.w || Pos.y >= R.y + R.h)
				continue;
			float Area = R.w * R.h;
			if(pPrefer && E.m_vParts[i].m_W == pPrefer->m_W && E.m_vParts[i].m_H == pPrefer->m_H)
				Area *= 0.01f; // a dragged part lands on a spot of its own size first
			if(Area < BestArea)
			{
				BestArea = Area;
				Best = (int)i;
			}
		}
		return Best;
	};
	auto DrawTexture = [&](IGraphics::CTextureHandle Texture, const CUIRect &R) {
		Graphics()->WrapClamp();
		Graphics()->TextureSet(Texture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		IGraphics::CQuadItem Quad(R.x, R.y, R.w, R.h);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	};
	auto Outline = [&](const CUIRect &R, ColorRGBA Color) {
		Graphics()->TextureClear();
		Graphics()->LinesBegin();
		Graphics()->SetColor(Color);
		IGraphics::CLineItem aLines[4] = {
			IGraphics::CLineItem(R.x, R.y, R.x + R.w, R.y),
			IGraphics::CLineItem(R.x + R.w, R.y, R.x + R.w, R.y + R.h),
			IGraphics::CLineItem(R.x + R.w, R.y + R.h, R.x, R.y + R.h),
			IGraphics::CLineItem(R.x, R.y + R.h, R.x, R.y)};
		Graphics()->LinesDraw(aLines, 4);
		Graphics()->LinesEnd();
	};

	static CUi::SDropDownState s_DonorState, s_BaseState;
	static CScrollRegion s_DonorRegion, s_BaseRegion;
	CUIRect DonorTitle, ResultTitle, DonorArea, ResultArea;
	DonorPanel.HSplitTop(ED_TITLE, &DonorTitle, &DonorArea);
	ResultPanel.HSplitTop(ED_TITLE, &ResultTitle, &ResultArea);
	DonorArea.HSplitTop(3.0f, nullptr, &DonorArea);
	ResultArea.HSplitTop(3.0f, nullptr, &ResultArea);
	CUIRect DonorFit, ResultFit;
	FitImage(DonorArea, DonorFit);
	FitImage(ResultArea, ResultFit);
	DonorArea.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 5.0f);
	ResultArea.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 5.0f);

	// interaction (not while a dropdown or other popup is open)
	const vec2 Mouse = Ui()->MousePos();
	const bool CanInteract = !PopupOpen && Ui()->MouseInsideClip();
	const int HoverDonor = CanInteract && E.m_Donor.m_pImage ? HitPart(DonorFit, Mouse, nullptr) : -1;
	const SKingEditorPart *pDragPart = (E.m_Dragging || E.m_Picked >= 0) ? &E.m_vParts[E.m_Dragging ? E.m_PressDonor : E.m_Picked] : nullptr;
	const int HoverResult = CanInteract && E.m_Base.m_pImage ? HitPart(ResultFit, Mouse, pDragPart) : -1;
	const bool Down = Ui()->MouseButton(0), WasDown = Ui()->LastMouseButton(0);
	auto Place = [&](int Target, int From) {
		if(E.m_vEdits[Target].m_From == From && !E.m_vEdits[Target].m_Erase)
			return;
		PushUndo(E);
		E.m_vEdits[Target].m_From = From;
		E.m_vEdits[Target].m_Erase = false;
		E.m_Selected = Target;
		char aBuf[160];
		str_format(aBuf, sizeof(aBuf), Localize("%s now uses %s from %s"), E.m_vParts[Target].m_aName, E.m_vParts[From].m_aName, E.m_Donor.m_aName);
		SetStatus(E, false, aBuf);
	};
	if(CanInteract)
	{
		if(Down && !WasDown)
		{
			if(HoverDonor >= 0)
			{
				E.m_PressDonor = HoverDonor;
				E.m_PressPos = Mouse;
			}
			else if(HoverResult >= 0)
			{
				if(E.m_Picked >= 0)
					Place(HoverResult, E.m_Picked); // picked part stays picked: place it again elsewhere
				else
					E.m_Selected = HoverResult;
			}
		}
		if(Down && E.m_PressDonor >= 0 && !E.m_Dragging && length(Mouse - E.m_PressPos) > 4.0f)
			E.m_Dragging = true;
		if(!Down && WasDown && E.m_PressDonor >= 0)
		{
			if(E.m_Dragging)
			{
				if(HoverResult >= 0)
					Place(HoverResult, E.m_PressDonor);
			}
			else
				E.m_Picked = E.m_Picked == E.m_PressDonor ? -1 : E.m_PressDonor; // click: pick, click again: drop it
			E.m_Dragging = false;
			E.m_PressDonor = -1;
		}
		if(Ui()->MouseButtonClicked(1) && HoverResult >= 0 && E.m_vEdits[HoverResult].Edited())
		{
			PushUndo(E);
			E.m_vEdits[HoverResult] = SKingEditorEdit();
			SetStatus(E, false, Localize("Part reset"));
		}
	}
	else if(!Down)
	{
		E.m_Dragging = false;
		E.m_PressDonor = -1;
	}

	// draw
	Picker(DonorTitle, E.m_Donor, s_DonorState, s_DonorRegion, Localize("Donor"));
	Picker(ResultTitle, E.m_Base, s_BaseState, s_BaseRegion, Localize("Base"));
	auto Loading = [&](const CUIRect &Area, const SKingEditorSide &Side) {
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
		Ui()->DoLabel(&Area, Side.m_Failed ? Localize("Could not load this asset.") : Localize("Loading…"), ED_FONT, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};
	if(E.m_Donor.m_Texture.IsValid() && E.m_Donor.m_pImage)
	{
		DrawTexture(E.m_Donor.m_Texture, DonorFit);
		for(size_t i = 0; i < E.m_vParts.size(); i++)
			Outline(PartRect(DonorFit, E.m_vParts[i]), ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f));
		if(E.m_Picked >= 0)
			Outline(PartRect(DonorFit, E.m_vParts[E.m_Picked]), ED_ACCENT);
		if(HoverDonor >= 0 && HoverDonor != E.m_Picked)
			Outline(PartRect(DonorFit, E.m_vParts[HoverDonor]), ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f));
	}
	else
		Loading(DonorArea, E.m_Donor);
	const bool ResultReady = E.m_Result.IsValid() && E.m_ResultW > 0;
	if(ResultReady || E.m_Base.m_Texture.IsValid())
	{
		DrawTexture(ResultReady ? E.m_Result : E.m_Base.m_Texture, ResultFit);
		for(size_t i = 0; i < E.m_vParts.size(); i++)
		{
			const bool Edited = E.m_vEdits[i].Edited();
			const CUIRect R = PartRect(ResultFit, E.m_vParts[i]);
			if(E.m_vEdits[i].m_Erase)
			{
				// a removed part: faint red, so it can still be found and restored
				R.Draw(ColorRGBA(1.0f, 0.3f, 0.3f, 0.12f), IGraphics::CORNER_NONE, 0.0f);
				Outline(R, ColorRGBA(1.0f, 0.4f, 0.4f, 0.6f));
			}
			else
				Outline(R, Edited ? ColorRGBA(0.45f, 1.0f, 0.55f, 0.55f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f));
		}
		if(E.m_Selected >= 0)
			Outline(PartRect(ResultFit, E.m_vParts[E.m_Selected]), ED_ACCENT);
		if(HoverResult >= 0)
			Outline(PartRect(ResultFit, E.m_vParts[HoverResult]), pDragPart ? ColorRGBA(0.45f, 1.0f, 0.55f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f));
	}
	else
		Loading(ResultArea, E.m_Base);
	if(!L.m_Stack && Arrow.w > 0.0f)
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.35f);
		Ui()->DoLabel(&Arrow, FontIcon::CHEVRON_RIGHT, 14.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	// ---- inspector ----
	CUIRect Tools, TintBox;
	if(L.m_WideInspector)
	{
		Inspector.VSplitLeft(L.m_ToolsW, &Tools, &TintBox);
		TintBox.VSplitLeft(ED_GAP * 2.0f, nullptr, &TintBox);
	}
	else
	{
		Inspector.HSplitTop(ToolsHeight(Inspector.w, Game), &Tools, &TintBox);
		TintBox.HSplitTop(ED_GAP, nullptr, &TintBox);
	}
	auto Flow = [&](CUIRect &Area, int Num, float MinW, auto &&Fn) {
		const int PerRow = std::max(1, (int)((Area.w + 4.0f) / (MinW + 4.0f)));
		for(int Start = 0; Start < Num; Start += PerRow)
		{
			CUIRect Row;
			Area.HSplitTop(ED_LINE, &Row, &Area);
			Area.HSplitTop(4.0f, nullptr, &Area);
			const int Count = std::min(PerRow, Num - Start);
			const float W = (Row.w - 4.0f * (PerRow - 1)) / PerRow;
			for(int i = 0; i < Count; i++)
			{
				CUIRect Button;
				Row.VSplitLeft(W, &Button, &Row);
				Row.VSplitLeft(4.0f, nullptr, &Row);
				Fn(Start + i, Button);
			}
		}
	};
	{
		CUIRect Line;
		Tools.HSplitTop(ED_TITLE, &Line, &Tools);
		char aBuf[128];
		if(E.m_Selected >= 0)
		{
			const SKingEditorEdit &Edit = E.m_vEdits[E.m_Selected];
			if(Edit.m_Erase)
				str_format(aBuf, sizeof(aBuf), "%s (%s)", E.m_vParts[E.m_Selected].m_aName, Localize("removed"));
			else if(Edit.m_From >= 0)
				str_format(aBuf, sizeof(aBuf), "%s ← %s", E.m_vParts[E.m_Selected].m_aName, E.m_vParts[Edit.m_From].m_aName);
			else
				str_copy(aBuf, E.m_vParts[E.m_Selected].m_aName);
		}
		else
			str_copy(aBuf, Localize("Click a part on the base to edit it"));
		SLabelProperties Props;
		Props.m_MaxWidth = Line.w;
		Props.m_EllipsisAtEnd = true;
		TextRender()->TextColor(E.m_Selected >= 0 ? ED_ACCENT : ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f));
		Ui()->DoLabel(&Line, aBuf, ED_FONT + 1.0f, TEXTALIGN_ML, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Tools.HSplitTop(4.0f, nullptr, &Tools);

		CUIRect Row, ResetPart, SamePlace, RemovePart;
		Tools.HSplitTop(ED_LINE, &Row, &Tools);
		const float PartButtonW = (Row.w - 8.0f) / 3.0f;
		Row.VSplitLeft(PartButtonW, &ResetPart, &Row);
		Row.VSplitLeft(4.0f, nullptr, &Row);
		Row.VSplitLeft(PartButtonW, &SamePlace, &Row);
		Row.VSplitLeft(4.0f, nullptr, &RemovePart);
		static CButtonContainer s_ResetPart, s_SamePlace, s_RemovePart;
		if(DoButton_Menu(&s_RemovePart, Localize("Remove"), 0, &RemovePart, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 0.4f, 0.4f, 0.45f)) && E.m_Selected >= 0 && !E.m_vEdits[E.m_Selected].m_Erase)
		{
			PushUndo(E);
			E.m_vEdits[E.m_Selected].m_Erase = true;
			SetStatus(E, false, Localize("Part removed"));
		}
		GameClient()->m_Tooltips.DoToolTip(&s_RemovePart, &RemovePart, Localize("Make this part empty (Delete key)"));
		if(DoButton_Menu(&s_ResetPart, Localize("Reset part"), 0, &ResetPart) && E.m_Selected >= 0 && E.m_vEdits[E.m_Selected].Edited())
		{
			PushUndo(E);
			E.m_vEdits[E.m_Selected] = SKingEditorEdit();
		}
		if(DoButton_Menu(&s_SamePlace, Localize("From donor"), 0, &SamePlace) && E.m_Selected >= 0)
			Place(E.m_Selected, E.m_Selected);
		GameClient()->m_Tooltips.DoToolTip(&s_SamePlace, &SamePlace, Localize("Take the same part from the donor"));

		if(Game)
		{
			Tools.HSplitTop(8.0f, nullptr, &Tools);
			Tools.HSplitTop(ED_TITLE, &Line, &Tools);
			// one row of weapons, two jobs: take them from the donor or remove them
			static bool s_WeaponRemove = false;
			CUIRect Take, Remove;
			Line.VSplitRight(std::min(170.0f, Line.w * 0.5f), &Line, &Remove);
			Remove.VSplitMid(&Take, &Remove, 2.0f);
			static CButtonContainer s_TakeMode, s_RemoveMode;
			if(DoButton_Menu(&s_TakeMode, Localize("Take"), !s_WeaponRemove, &Take, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L, 4.0f))
				s_WeaponRemove = false;
			if(DoButton_Menu(&s_RemoveMode, Localize("Remove"), s_WeaponRemove, &Remove, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R, 4.0f, 0.0f, ColorRGBA(1.0f, 0.4f, 0.4f, 0.45f)))
				s_WeaponRemove = true;
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
			SLabelProperties WeaponProps;
			WeaponProps.m_MaxWidth = Line.w - 4.0f;
			WeaponProps.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Line, s_WeaponRemove ? Localize("Remove a whole weapon:") : Localize("Whole weapon from the donor:"), ED_FONT, TEXTALIGN_ML, WeaponProps);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			static CButtonContainer s_aWeaponButtons[std::size(s_apWeaponGroups)];
			const char *apWeaponNames[] = {Localize("Hammer"), Localize("Gun"), Localize("Shotgun"), Localize("Grenade"), Localize("Laser"), Localize("Ninja"), Localize("Hook")};
			Flow(Tools, (int)std::size(s_apWeaponGroups), 66.0f, [&](int i, CUIRect Button) {
				if(DoButton_Menu(&s_aWeaponButtons[i], apWeaponNames[i], 0, &Button))
				{
					PushUndo(E);
					for(size_t p = 0; p < E.m_vParts.size(); p++)
						if(str_comp(E.m_vParts[p].m_aGroup, s_apWeaponGroups[i]) == 0)
						{
							if(s_WeaponRemove)
								E.m_vEdits[p].m_Erase = true;
							else
							{
								E.m_vEdits[p].m_From = (int)p;
								E.m_vEdits[p].m_Erase = false;
							}
						}
					char aMsg[128];
					if(s_WeaponRemove)
						str_format(aMsg, sizeof(aMsg), Localize("Removed: %s"), apWeaponNames[i]);
					else
						str_format(aMsg, sizeof(aMsg), Localize("%s taken from %s"), apWeaponNames[i], E.m_Donor.m_aName);
					SetStatus(E, false, aMsg);
				}
			});
		}
		Tools.HSplitTop(8.0f, nullptr, &Tools);
		static CButtonContainer s_AllButton, s_ResetAllButton, s_UndoButton;
		Flow(Tools, 3, 110.0f, [&](int i, CUIRect Button) {
			if(i == 0 && DoButton_Menu(&s_AllButton, Localize("Everything from donor"), 0, &Button))
			{
				PushUndo(E);
				for(size_t p = 0; p < E.m_vParts.size(); p++)
				{
					E.m_vEdits[p].m_From = (int)p;
					E.m_vEdits[p].m_Erase = false;
				}
			}
			if(i == 1 && DoButton_Menu(&s_ResetAllButton, Localize("Reset all"), 0, &Button))
			{
				PushUndo(E);
				for(SKingEditorEdit &Edit : E.m_vEdits)
					Edit = SKingEditorEdit();
			}
			if(i == 2)
			{
				char aUndo[64];
				str_format(aUndo, sizeof(aUndo), "%s (%d)", Localize("Undo"), (int)E.m_vUndo.size());
				if(DoButton_Menu(&s_UndoButton, aUndo, 0, &Button) && !E.m_vUndo.empty())
				{
					E.m_vEdits = E.m_vUndo.back();
					E.m_vUndo.pop_back();
					E.m_LastCoalescedPart = -1;
					E.m_Dirty = true;
					E.m_Unsaved = true;
				}
				GameClient()->m_Tooltips.DoToolTip(&s_UndoButton, &Button, "Ctrl+Z");
			}
		});
	}
	{
		// tint of the selected part
		static int s_TintCheck = 0;
		CUIRect Row;
		TintBox.HSplitTop(ED_LINE, &Row, &TintBox);
		TintBox.HSplitTop(4.0f, nullptr, &TintBox);
		SKingEditorEdit Dummy;
		SKingEditorEdit &Edit = E.m_Selected >= 0 ? E.m_vEdits[E.m_Selected] : Dummy;
		if(DoButton_CheckBox(&s_TintCheck, Localize("Color this part"), Edit.m_Tint, &Row) && E.m_Selected >= 0)
		{
			PushUndo(E);
			Edit.m_Tint = !Edit.m_Tint;
		}
		CUIRect Sliders, Side;
		if(TintBox.w >= 380.0f)
		{
			TintBox.HSplitTop(80.0f, &TintBox, nullptr);
			TintBox.VSplitLeft(std::floor(TintBox.w * 0.58f), &Sliders, &Side);
			Side.VSplitLeft(ED_GAP, nullptr, &Side);
		}
		else
		{
			TintBox.HSplitTop(80.0f, &Sliders, &Side);
			Side.HSplitTop(4.0f, nullptr, &Side);
		}
		static unsigned s_Color; // the sliders take their ids from this address: it must not move
		s_Color = Edit.m_Color;
		if(RenderHslaScrollbars(&Sliders, &s_Color, false, ColorHSLA::DARKEST_LGT) && E.m_Selected >= 0 && s_Color != Edit.m_Color)
		{
			PushUndo(E, E.m_Selected);
			Edit.m_Color = s_Color;
			Edit.m_Tint = true;
		}
		static CButtonContainer s_aBlendButtons[KING_EDITOR_NUM_BLENDS];
		const char *apBlends[] = {Localize("Like tee"), Localize("Screen"), Localize("Multiply"), Localize("Overlay")};
		const int BlendPerRow = std::max(1, std::min(2, (int)((Side.w + 4.0f) / 68.0f)));
		const bool NarrowSide = TintBox.w < 380.0f;
		Flow(Side, KING_EDITOR_NUM_BLENDS, NarrowSide ? 64.0f : std::max(64.0f, (Side.w - 4.0f) / BlendPerRow - 1.0f), [&](int i, CUIRect Button) {
			if(DoButton_Menu(&s_aBlendButtons[i], apBlends[i], Edit.m_Blend == i, &Button) && E.m_Selected >= 0 && Edit.m_Blend != i)
			{
				PushUndo(E);
				Edit.m_Blend = i;
				Edit.m_Tint = true;
			}
		});
		CUIRect Opacity;
		Side.HSplitTop(ED_LINE, &Opacity, &Side);
		static int s_OpacityId;
		int OpacityValue = Edit.m_Opacity;
		Ui()->DoScrollbarOption(&s_OpacityId, &OpacityValue, &Opacity, Localize("Strength"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
		if(OpacityValue != Edit.m_Opacity && E.m_Selected >= 0)
		{
			PushUndo(E, E.m_Selected);
			Edit.m_Opacity = OpacityValue;
			Edit.m_Tint = true;
		}
	}

	// ---- status ----
	{
		CUIRect Left, Right;
		Status.VSplitMid(&Left, &Right, ED_GAP);
		SLabelProperties Props;
		Props.m_MaxWidth = Left.w;
		Props.m_EllipsisAtEnd = true;
		if(E.m_aStatus[0])
		{
			TextRender()->TextColor(E.m_StatusError ? ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f) : ColorRGBA(0.55f, 1.0f, 0.6f, 1.0f));
			Ui()->DoLabel(&Left, E.m_aStatus, ED_FONT - 1.0f, TEXTALIGN_ML, Props);
		}
		const char *pHint = E.m_Dragging ? Localize("Drop it on a part of the base") : (E.m_Picked >= 0 ? Localize("Click parts of the base to place it, Esc to stop") : Localize("Drag or click donor parts onto the base. Right click resets, Delete removes a part."));
		Props.m_MaxWidth = Right.w;
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
		Ui()->DoLabel(&Right, pHint, ED_FONT - 1.0f, TEXTALIGN_MR, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	// the dragged part follows the cursor (inside the editor, on top)
	if(E.m_Dragging && E.m_PressDonor >= 0 && E.m_Donor.m_Texture.IsValid())
	{
		const SKingEditorPart &Part = E.m_vParts[E.m_PressDonor];
		const float Size = 54.0f;
		float W = Size, H = Size * Part.m_H / (float)Part.m_W; // grid cells are square
		if(H > Size)
		{
			W *= Size / H;
			H = Size;
		}
		CUIRect Ghost = {Mouse.x + 10.0f, Mouse.y + 10.0f, W + 8.0f, H + 8.0f};
		Ghost.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 4.0f);
		Graphics()->WrapClamp();
		Graphics()->TextureSet(E.m_Donor.m_Texture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.95f);
		Graphics()->QuadsSetSubset(Part.m_X / (float)E.m_GridX, Part.m_Y / (float)E.m_GridY, (Part.m_X + Part.m_W) / (float)E.m_GridX, (Part.m_Y + Part.m_H) / (float)E.m_GridY);
		IGraphics::CQuadItem Quad(Ghost.x + 4.0f, Ghost.y + 4.0f, W, H);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	}
	s_ScrollRegion.End();
}

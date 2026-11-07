/* KING: the community asset catalog page (Settings > Assets > Download assets).
 * Data, downloads and previews live in CKingAssetCatalog (king_assets.cpp); this file only
 * lays out the grid. Only visible tiles are laid out and ask for previews. */
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <vector>

void CMenus::RenderKingAssetCatalog(CUIRect MainView, int Tab, bool &Active)
{
	const int Cat = std::clamp(Tab, 0, (int)CKingAssetCatalog::NUM_CATS - 1);
	m_KingAssets.Update();

	// finished downloads go into the normal asset list right away
	CKingAssetCatalog::SInstalled Installed;
	while(m_KingAssets.PopInstalled(Installed))
		KingAddDownloadedAsset(Installed.m_Cat, Installed.m_aName, Installed.m_Image);

	const ColorRGBA Accent(0.96f, 0.76f, 0.30f, 1.0f);
	const float FontSize = 12.0f;
	const float Pad = 5.0f;
	const float Gap = 8.0f;

	CUIRect Header, Grid, Bottom;
	MainView.HSplitTop(20.0f, &Header, &MainView);
	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitBottom(ms_ButtonHeight + 5.0f, &Grid, &Bottom);
	Bottom.HSplitTop(5.0f, nullptr, &Bottom);

	// ---- bottom row: search | back to my assets ----
	static CLineInputBuffered<64> s_aSearch[CKingAssetCatalog::NUM_CATS];
	CUIRect Search, Back;
	Bottom.VSplitLeft(std::min(220.0f, Bottom.w * 0.45f), &Search, &Bottom);
	Bottom.VSplitRight(std::min(175.0f, Bottom.w), nullptr, &Back);
	Ui()->DoEditBox_Search(&s_aSearch[Cat], &Search, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
	static CButtonContainer s_BackButton;
	if(DoButton_Menu(&s_BackButton, Localize("My assets"), 0, &Back))
		Active = false;

	// ---- header: what this is, refresh ----
	{
		CUIRect Refresh;
		Header.VSplitRight(20.0f, &Header, &Refresh);
		Header.VSplitRight(5.0f, &Header, nullptr);
		char aHeader[128];
		const int State = m_KingAssets.IndexState();
		if(State == CKingAssetCatalog::INDEX_READY)
		{
			if(m_KingAssets.IndexStale())
				str_format(aHeader, sizeof(aHeader), "%s · %d (%s)", Localize("Community catalog"), (int)m_KingAssets.Items(Cat).size(), Localize("offline copy"));
			else
				str_format(aHeader, sizeof(aHeader), "%s · %d", Localize("Community catalog"), (int)m_KingAssets.Items(Cat).size());
		}
		else
			str_copy(aHeader, Localize("Community catalog"));
		SLabelProperties Props;
		Props.m_MaxWidth = Header.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Header, aHeader, FontSize + 1.0f, TEXTALIGN_ML, Props);

		static CButtonContainer s_RefreshButton;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		if(DoButton_Menu(&s_RefreshButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &Refresh))
			m_KingAssets.Refresh();
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		GameClient()->m_Tooltips.DoToolTip(&s_RefreshButton, &Refresh, Localize("Check the catalog for new assets"));
	}

	Grid.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_ALL, 6.0f);

	auto CenterText = [&](const char *pText, float Alpha) {
		CUIRect Line = Grid;
		Line.HSplitTop(Grid.h / 2.0f - 20.0f, nullptr, &Line);
		Line.HSplitTop(20.0f, &Line, nullptr);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
		Ui()->DoLabel(&Line, pText, FontSize + 1.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return Line;
	};

	const int State = m_KingAssets.IndexState();
	if(State == CKingAssetCatalog::INDEX_NONE || State == CKingAssetCatalog::INDEX_LOADING)
	{
		CenterText(Localize("Loading the catalog…"), 0.7f);
		return;
	}
	if(State == CKingAssetCatalog::INDEX_FAILED)
	{
		CUIRect Line = CenterText(Localize("Could not load the catalog. Check your internet connection."), 0.8f);
		CUIRect Retry;
		Line.HSplitTop(Line.h + 10.0f, nullptr, &Retry);
		Retry.HSplitTop(ms_ButtonHeight, &Retry, nullptr);
		Retry.VMargin(std::max(0.0f, (Retry.w - 140.0f) / 2.0f), &Retry);
		static CButtonContainer s_RetryButton;
		if(DoButton_Menu(&s_RetryButton, Localize("Try again"), 0, &Retry))
			m_KingAssets.Refresh();
		return;
	}

	// ---- filtered list (rebuilt only when the filter or the catalog changes) ----
	static std::vector<CKingAssetCatalog::SItem *> s_vpFiltered;
	static char s_aLastFilter[64] = "\x01";
	static int s_LastCat = -1;
	static int s_LastGeneration = -1;
	const std::vector<CKingAssetCatalog::SItem *> &vpItems = m_KingAssets.Items(Cat);
	const char *pFilter = s_aSearch[Cat].GetString();
	if(s_LastCat != Cat || str_comp(s_aLastFilter, pFilter) != 0 || s_LastGeneration != m_KingAssets.Generation())
	{
		s_vpFiltered.clear();
		for(CKingAssetCatalog::SItem *pItem : vpItems)
			if(!pFilter[0] || str_utf8_find_nocase(pItem->m_aName, pFilter))
				s_vpFiltered.push_back(pItem);
		s_LastCat = Cat;
		str_copy(s_aLastFilter, pFilter);
		s_LastGeneration = m_KingAssets.Generation();
	}
	if(s_vpFiltered.empty())
	{
		CenterText(vpItems.empty() ? Localize("This catalog has nothing of this type yet.") : Localize("Nothing found"), 0.7f);
		return;
	}

	// ---- grid: as many columns as fit, the tile size is a KING setting ----
	CUIRect Content = Grid;
	Content.Margin(Pad, &Content);
	static CScrollRegion s_aScrollRegion[CKingAssetCatalog::NUM_CATS];
	CScrollRegion &ScrollRegion = s_aScrollRegion[Cat];
	const float Aspect = CKingAssetCatalog::CategoryAspect(Cat);
	const float BaseW = g_Config.m_KgAssetsTile * (Aspect < 1.0f ? 1.6f : 1.0f);
	const float NameH = 14.0f;
	const float ButtonH = 18.0f;
	auto CellHeight = [&](float W) { return Pad + (W - 2.0f * Pad) * Aspect + 4.0f + NameH + 4.0f + ButtonH + Pad; };

	CScrollRegionParams Params;
	Params.m_ScrollUnit = CellHeight(BaseW) + Gap;
	Params.m_ScrollbarMargin = 2.0f;
	ScrollRegion.Begin(&Content, &Params); // takes the scrollbar off Content when it is shown

	// as many columns as fit the width that is left, stretched to fill the row
	const int Cols = std::max(1, (int)((Content.w + Gap) / (BaseW + Gap)));
	const float CellW = (Content.w - Gap * (Cols - 1)) / Cols;
	const float ImageH = (CellW - 2.0f * Pad) * Aspect;
	const float CellH = CellHeight(CellW);

	const int NumRows = ((int)s_vpFiltered.size() + Cols - 1) / Cols;
	for(int Row = 0; Row < NumRows; Row++)
	{
		CUIRect RowRect;
		Content.HSplitTop(CellH, &RowRect, &Content);
		if(Row + 1 < NumRows)
			Content.HSplitTop(Gap, nullptr, &Content);
		if(!ScrollRegion.AddRect(RowRect))
			continue; // off screen: no layout, no preview request

		for(int Col = 0; Col < Cols; Col++)
		{
			const int Index = Row * Cols + Col;
			if(Index >= (int)s_vpFiltered.size())
				break;
			CKingAssetCatalog::SItem *pItem = s_vpFiltered[Index];
			CUIRect Cell;
			RowRect.VSplitLeft(CellW, &Cell, &RowRect);
			RowRect.VSplitLeft(Gap, nullptr, &RowRect);

			const bool Current = pItem->m_Installed && KingAssetIsCurrent(Tab, pItem->m_aName);
			const bool Hovered = Ui()->MouseHovered(&Cell);
			if(Current)
			{
				Cell.Draw(Accent.WithAlpha(0.55f), IGraphics::CORNER_ALL, 6.0f);
				CUIRect Inner;
				Cell.Margin(1.5f, &Inner);
				Inner.Draw(ColorRGBA(0.10f, 0.10f, 0.12f, 1.0f), IGraphics::CORNER_ALL, 5.0f);
			}
			else
				Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Hovered ? 0.12f : 0.07f), IGraphics::CORNER_ALL, 6.0f);

			CUIRect Body, Image, Name, Button;
			Cell.Margin(Pad, &Body);
			Body.HSplitTop(ImageH, &Image, &Body);
			Body.HSplitTop(4.0f, nullptr, &Body);
			Body.HSplitTop(NameH, &Name, &Body);
			Body.HSplitTop(4.0f, nullptr, &Body);
			Body.HSplitTop(ButtonH, &Button, nullptr);

			// preview
			Image.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, 4.0f);
			m_KingAssets.WantThumb(pItem);
			if(pItem->m_ThumbState == CKingAssetCatalog::THUMB_READY && pItem->m_Thumb.IsValid())
			{
				float W = Image.w, H = Image.w * pItem->m_ThumbAspect;
				if(H > Image.h)
				{
					H = Image.h;
					W = H / std::max(0.01f, pItem->m_ThumbAspect);
				}
				Graphics()->WrapClamp();
				Graphics()->TextureSet(pItem->m_Thumb);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				IGraphics::CQuadItem Quad(Image.x + (Image.w - W) / 2.0f, Image.y + (Image.h - H) / 2.0f, W, H);
				Graphics()->QuadsDrawTL(&Quad, 1);
				Graphics()->QuadsEnd();
				Graphics()->WrapNormal();
			}
			else
			{
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.35f);
				Ui()->DoLabel(&Image, pItem->m_ThumbState == CKingAssetCatalog::THUMB_FAILED ? Localize("No preview") : "…", FontSize, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}

			// name
			SLabelProperties NameProps;
			NameProps.m_MaxWidth = Name.w;
			NameProps.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Name, pItem->m_aName, FontSize - 1.0f, TEXTALIGN_MC, NameProps);

			// action
			if(pItem->m_InstallState == CKingAssetCatalog::INSTALL_RUNNING)
			{
				Button.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f), IGraphics::CORNER_ALL, 5.0f);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.7f);
				Ui()->DoLabel(&Button, Localize("Downloading…"), FontSize - 1.0f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			else if(Current)
			{
				Button.Draw(Accent.WithAlpha(0.25f), IGraphics::CORNER_ALL, 5.0f);
				Ui()->DoLabel(&Button, Localize("In use"), FontSize - 1.0f, TEXTALIGN_MC);
			}
			else if(pItem->m_Installed)
			{
				if(DoButton_Menu(&pItem->m_Button, Localize("Use"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, Accent.WithAlpha(0.35f)))
					KingApplyAsset(Tab, pItem->m_aName);
			}
			else
			{
				const char *pLabel = pItem->m_InstallState == CKingAssetCatalog::INSTALL_FAILED ? Localize("Failed, retry") : Localize("Download");
				if(DoButton_Menu(&pItem->m_Button, pLabel, 0, &Button))
					m_KingAssets.Install(pItem);
			}
		}
	}
	ScrollRegion.End();
}

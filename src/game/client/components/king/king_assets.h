/* KING: community asset catalog (game skins, emoticons, entities, particles, HUD, extras).
 *
 * The catalog is the GitHub repository Zytsen/ddnet-assets (one folder per asset type,
 * flat PNG files). Everything here is built so it never costs anything when the catalog
 * is not on screen, and never blocks a frame when it is:
 *  - the index is one request (GitHub tree API, jsDelivr as fallback), cached on disk for
 *    12 hours and parsed on a job thread;
 *  - previews are requested only for tiles that are visible, at most 4 downloads at once;
 *    PNG decoding and scaling to a small preview runs on job threads, the preview is
 *    cached on disk (king/assets/thumbs), and at most 2 previews are uploaded per frame;
 *  - downloading an asset streams it into memory on the HTTP thread, a job checks that it
 *    is a valid PNG and writes it to assets/<type>/, the decoded image is handed to the
 *    menu list as a preview so nothing is loaded again;
 *  - when the catalog has not been drawn for a second, every preview texture is freed and
 *    queued work is dropped.
 */
#ifndef GAME_CLIENT_COMPONENTS_KING_KING_ASSETS_H
#define GAME_CLIENT_COMPONENTS_KING_KING_ASSETS_H

#include <engine/graphics.h>
#include <engine/image.h>

#include <game/client/ui.h>

#include <deque>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class IEngine;
class IHttp;
class IHttpRequest;
class IStorage;
class IJob;

// a plain asset file name (no separators, not hidden, < 50 bytes, not "default")
bool KingValidAssetName(const char *pName);
// RGBA preview, longest side at most 320 px (premultiplied 2x2 halving, then bicubic)
void KingMakeThumbnail(CImageInfo &Image);

class CKingAssetCatalog
{
public:
	enum
	{
		CAT_ENTITIES = 0, // same order as the asset tabs in the menu
		CAT_GAME,
		CAT_EMOTICONS,
		CAT_PARTICLES,
		CAT_HUD,
		CAT_EXTRAS,
		NUM_CATS,
	};
	static const char *CategoryFolder(int Cat);
	// height / width of a preview cell for this type (game skins are 2:1)
	static float CategoryAspect(int Cat) { return Cat == CAT_GAME ? 0.5f : 1.0f; }

	enum
	{
		INDEX_NONE = 0,
		INDEX_LOADING,
		INDEX_READY,
		INDEX_FAILED,
	};
	enum
	{
		THUMB_NONE = 0,
		THUMB_QUEUED,
		THUMB_LOADING,
		THUMB_READY,
		THUMB_FAILED,
	};
	enum
	{
		INSTALL_NONE = 0,
		INSTALL_RUNNING,
		INSTALL_FAILED,
	};

	struct SItem
	{
		char m_aName[50] = ""; // same limit as the menu asset list
		int m_Cat = 0;
		int m_Size = 0;
		IGraphics::CTextureHandle m_Thumb;
		float m_ThumbAspect = 1.0f; // height / width of the original image
		int m_ThumbState = THUMB_NONE;
		int m_ThumbTries = 0;
		int64_t m_LastWanted = 0;
		bool m_Installed = false;
		int m_InstallState = INSTALL_NONE;
		CButtonContainer m_Button;
	};

	// a finished download, for the menu list (with its preview, so nothing is loaded again)
	struct SInstalled
	{
		int m_Cat = 0;
		char m_aName[50] = "";
		CImageInfo m_Image;
	};

	CKingAssetCatalog();
	~CKingAssetCatalog();

	void Init(IGraphics *pGraphics, IStorage *pStorage, IHttp *pHttp, IEngine *pEngine);

	// called by the menu every frame the catalog is drawn
	void Update();
	// called every frame by the menu component: frees everything once the catalog is gone
	void ReleaseIfHidden();
	// downloads keep going after the catalog is closed; nothing to do when there are none
	void UpdateInstallsIfAny()
	{
		if(!m_vpInstallTasks.empty())
			UpdateInstalls();
	}
	void Refresh();

	int IndexState() const { return m_IndexState; }
	bool IndexStale() const { return m_IndexStale; }
	const std::vector<SItem *> &Items(int Cat) const { return m_avpItems[Cat]; }
	int NumItems() const { return (int)m_vpAllItems.size(); }
	int Generation() const { return m_Generation; } // changes whenever the item list is replaced

	void WantThumb(SItem *pItem);
	void Install(SItem *pItem);
	bool PopInstalled(SInstalled &Out);
	void RescanInstalled(int Cat);

private:
	struct SThumbTask;
	struct SInstallTask;
	class CIndexJob;
	class CThumbJob;
	class CInstallJob;

	IGraphics *m_pGraphics = nullptr;
	IStorage *m_pStorage = nullptr;
	IHttp *m_pHttp = nullptr;
	IEngine *m_pEngine = nullptr;

	int m_IndexState = INDEX_NONE;
	bool m_IndexStale = false;
	int m_IndexStep = 0; // 0 cache, 1 github, 2 jsdelivr
	std::shared_ptr<CIndexJob> m_pIndexJob;
	std::shared_ptr<IHttpRequest> m_pIndexRequest;
	std::vector<std::unique_ptr<SItem>> m_vpAllItems;
	std::vector<SItem *> m_avpItems[NUM_CATS];
	std::vector<std::unique_ptr<SItem>> m_vpStaleItems; // stale cache, used if the network fails

	std::vector<SItem *> m_vpQueue;
	std::vector<std::unique_ptr<SThumbTask>> m_vpThumbTasks;
	std::vector<std::unique_ptr<SInstallTask>> m_vpInstallTasks;
	std::deque<SInstalled> m_InstalledQueue;
	int m_NumThumbs = 0;
	bool m_UseMirror = false; // raw.githubusercontent.com failed: use jsDelivr

	int m_Generation = 0;
	bool m_Shown = false;
	int64_t m_LastShown = 0;
	bool m_aInstalledScanned[NUM_CATS] = {};

	void StartIndex(bool Force);
	void StartIndexRequest();
	void FinishIndex(std::vector<std::unique_ptr<SItem>> &&vpItems);
	void UpdateIndex();
	void UpdateThumbs();
	void UpdateInstalls();
	void FreeThumb(SItem *pItem);
	void ReleaseAll();
	void FileUrl(const SItem *pItem, char *pBuf, int BufSize) const;
};

#endif

/* KING: community asset catalog, see king_assets.h */
#include "king_assets.h"

#include <base/log.h>
#include <base/io.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/version.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

static constexpr const char *KING_ASSETS_REPO = "Zytsen/ddnet-assets";
static constexpr const char *KING_ASSETS_BRANCH = "main";
static constexpr const char *KING_ASSETS_INDEX_FILE = "king/assets/index.txt";
static constexpr int64_t KING_ASSETS_INDEX_TTL = 12 * 60 * 60; // seconds
static constexpr int KING_THUMB_MAX = 320; // preview size in pixels (longest side)
static constexpr int KING_MAX_THUMB_DOWNLOADS = 4;
static constexpr int KING_MAX_THUMB_JOBS = 3;
static constexpr int KING_MAX_UPLOADS_PER_FRAME = 2;
static constexpr int KING_MAX_THUMB_TEXTURES = 120;

static const char *const s_apCategoryFolders[CKingAssetCatalog::NUM_CATS] = {"entities", "game", "emoticons", "particles", "hud", "extras"};

const char *CKingAssetCatalog::CategoryFolder(int Cat)
{
	return s_apCategoryFolders[std::clamp(Cat, 0, (int)NUM_CATS - 1)];
}

static int CategoryFromFolder(const char *pFolder, int Length)
{
	for(int i = 0; i < CKingAssetCatalog::NUM_CATS; i++)
		if(str_length(s_apCategoryFolders[i]) == Length && str_comp_num(s_apCategoryFolders[i], pFolder, Length) == 0)
			return i;
	return -1;
}

// only plain file names: no paths, no hidden files, fits the menu list, not the reserved "default"
static bool ValidAssetName(const char *pName)
{
	const int Length = str_length(pName);
	if(Length <= 0 || Length >= 50 || pName[0] == '.' || str_comp(pName, "default") == 0)
		return false;
	for(const char *p = pName; *p; p++)
	{
		const unsigned char c = (unsigned char)*p;
		if(c < 32 || c == 127 || (c < 128 && std::strchr("/\\:*?\"<>|", (int)c) != nullptr))
			return false;
	}
	return true; // no separators, so no way out of the asset folder
}

static std::unique_ptr<IHttpRequest> KingAssetGet(const char *pUrl, int64_t MaxSize)
{
	std::unique_ptr<IHttpRequest> pRequest = HttpGet(pUrl);
	pRequest->HeaderString("User-Agent", "KING/" KING_VERSION);
	pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	pRequest->MaxResponseSize(MaxSize);
	pRequest->LogProgress(HTTPLOG::NONE);
	pRequest->FailOnErrorStatus(false);
	pRequest->IpResolve(IPRESOLVE::V4);
	return pRequest;
}

static bool RequestOk(const std::shared_ptr<IHttpRequest> &pRequest)
{
	return pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200;
}

// the request could not reach the host at all (not a 404): worth trying the mirror
static bool RequestUnreachable(const std::shared_ptr<IHttpRequest> &pRequest)
{
	return pRequest->State() == EHttpState::ERROR || (pRequest->State() == EHttpState::DONE && pRequest->StatusCode() >= 500);
}

// downscale to fit KING_THUMB_MAX: halving with 2x2 averages (premultiplied alpha, so
// transparent pixels do not darken the edges), then one bicubic step for the rest
static void MakeThumbnail(CImageInfo &Image)
{
	ConvertToRgba(Image);
	auto Halve = [](CImageInfo &Img) {
		const size_t W = Img.m_Width / 2, H = Img.m_Height / 2;
		uint8_t *pNew = (uint8_t *)malloc(W * H * 4);
		const size_t Stride = Img.m_Width * 4;
		for(size_t y = 0; y < H; y++)
			for(size_t x = 0; x < W; x++)
			{
				const uint8_t *p0 = Img.m_pData + (y * 2) * Stride + x * 2 * 4;
				const uint8_t *p1 = p0 + Stride;
				const int aA[4] = {p0[3], p0[7], p1[3], p1[7]};
				const int SumA = aA[0] + aA[1] + aA[2] + aA[3];
				uint8_t *pDst = pNew + (y * W + x) * 4;
				for(int c = 0; c < 3; c++)
				{
					const int Sum = p0[c] * aA[0] + p0[4 + c] * aA[1] + p1[c] * aA[2] + p1[4 + c] * aA[3];
					pDst[c] = SumA > 0 ? (uint8_t)std::clamp(Sum / SumA, 0, 255) : (uint8_t)((p0[c] + p0[4 + c] + p1[c] + p1[4 + c]) / 4);
				}
				pDst[3] = (uint8_t)(SumA / 4);
			}
		free(Img.m_pData);
		Img.m_pData = pNew;
		Img.m_Width = W;
		Img.m_Height = H;
	};
	while(Image.m_Width >= 2 * KING_THUMB_MAX + 2 || Image.m_Height >= 2 * KING_THUMB_MAX + 2)
	{
		if(Image.m_Width < 4 || Image.m_Height < 4)
			break;
		Halve(Image);
	}
	const float Scale = std::min(1.0f, std::min(KING_THUMB_MAX / (float)Image.m_Width, KING_THUMB_MAX / (float)Image.m_Height));
	if(Scale < 1.0f)
	{
		const int W = std::max(1, (int)(Image.m_Width * Scale + 0.5f));
		const int H = std::max(1, (int)(Image.m_Height * Scale + 0.5f));
		if(W >= 2 && H >= 2)
			ResizeImage(Image, W, H);
	}
}

/* ---------- jobs (never touch the catalog: they only own their own data) ---------- */

struct SIndexEntry
{
	int m_Cat;
	char m_aName[50];
	int m_Size;
};

class CKingAssetCatalog::CIndexJob : public IJob
{
public:
	enum
	{
		MODE_CACHE,
		MODE_PARSE,
	};
	CIndexJob(IStorage *pStorage, int Mode, std::shared_ptr<IHttpRequest> pRequest) :
		m_Mode(Mode), m_pStorage(pStorage), m_pRequest(std::move(pRequest))
	{
		Abortable(true);
	}

	int m_Mode;
	bool m_Ok = false;
	bool m_Fresh = false;
	std::vector<SIndexEntry> m_vEntries;

private:
	IStorage *m_pStorage;
	std::shared_ptr<IHttpRequest> m_pRequest;

	void Add(const char *pPath, int Size)
	{
		// "cat/name.png", exactly one level deep
		if(pPath[0] == '/')
			pPath++;
		const char *pSlash = str_find(pPath, "/");
		if(!pSlash || str_find(pSlash + 1, "/") || !str_endswith(pSlash + 1, ".png"))
			return;
		const int Cat = CategoryFromFolder(pPath, (int)(pSlash - pPath));
		if(Cat < 0)
			return;
		SIndexEntry Entry;
		Entry.m_Cat = Cat;
		Entry.m_Size = Size;
		const int NameLength = str_length(pSlash + 1) - 4;
		if(NameLength <= 0 || NameLength >= (int)sizeof(Entry.m_aName))
			return;
		str_truncate(Entry.m_aName, sizeof(Entry.m_aName), pSlash + 1, NameLength);
		if(!ValidAssetName(Entry.m_aName))
			return;
		m_vEntries.push_back(Entry);
	}

	void ReadCache()
	{
		char *pText = m_pStorage->ReadFileStr(KING_ASSETS_INDEX_FILE, IStorage::TYPE_SAVE);
		if(!pText)
			return;
		int64_t Stamp = 0;
		char *pLine = pText;
		bool First = true;
		while(pLine && *pLine)
		{
			char *pEnd = (char *)str_find(pLine, "\n");
			if(pEnd)
				*pEnd = 0;
			if(First)
			{
				if(str_startswith(pLine, "KINGASSETS 1 "))
					Stamp = str_toint64_base(pLine + 13);
				else
					break;
				First = false;
			}
			else if(pLine[0])
			{
				// size \t cat/name.png
				const char *pTab = str_find(pLine, "\t");
				if(pTab)
					Add(pTab + 1, str_toint(pLine));
			}
			pLine = pEnd ? pEnd + 1 : nullptr;
		}
		free(pText);
		m_Ok = !First && !m_vEntries.empty();
		m_Fresh = m_Ok && time_timestamp() - Stamp < KING_ASSETS_INDEX_TTL && Stamp <= time_timestamp();
	}

	void Parse()
	{
		unsigned char *pData = nullptr;
		size_t DataSize = 0;
		m_pRequest->Result(&pData, &DataSize);
		if(!pData || !DataSize)
			return;
		json_value *pJson = JsonParse((const char *)pData, DataSize);
		if(!pJson)
			return;
		const json_value &Tree = (*pJson)["tree"]; // GitHub tree API
		const json_value &Files = (*pJson)["files"]; // jsDelivr flat listing
		if(Tree.type == json_array)
		{
			for(unsigned i = 0; i < Tree.u.array.length; i++)
			{
				const json_value &Node = Tree[(int)i];
				const json_value &Type = Node["type"];
				const json_value &Path = Node["path"];
				const json_value &Size = Node["size"];
				if(Type.type == json_string && str_comp(Type.u.string.ptr, "blob") == 0 && Path.type == json_string)
					Add(Path.u.string.ptr, Size.type == json_integer ? (int)Size.u.integer : 0);
			}
		}
		else if(Files.type == json_array)
		{
			for(unsigned i = 0; i < Files.u.array.length; i++)
			{
				const json_value &Node = Files[(int)i];
				const json_value &Name = Node["name"];
				const json_value &Size = Node["size"];
				if(Name.type == json_string)
					Add(Name.u.string.ptr, Size.type == json_integer ? (int)Size.u.integer : 0);
			}
		}
		json_value_free(pJson);
		m_Ok = !m_vEntries.empty();
		if(!m_Ok || State() == IJob::STATE_ABORTED)
			return;

		// cache it, normalized
		m_pStorage->CreateFolder("king", IStorage::TYPE_SAVE);
		m_pStorage->CreateFolder("king/assets", IStorage::TYPE_SAVE);
		IOHANDLE File = m_pStorage->OpenFile(KING_ASSETS_INDEX_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(File)
		{
			char aLine[128];
			str_format(aLine, sizeof(aLine), "KINGASSETS 1 %lld\n", (long long)time_timestamp());
			io_write(File, aLine, str_length(aLine));
			for(const SIndexEntry &Entry : m_vEntries)
			{
				str_format(aLine, sizeof(aLine), "%d\t%s/%s.png\n", Entry.m_Size, s_apCategoryFolders[Entry.m_Cat], Entry.m_aName);
				io_write(File, aLine, str_length(aLine));
			}
			io_close(File);
		}
	}

	void Run() override
	{
		if(m_Mode == MODE_CACHE)
			ReadCache();
		else
			Parse();
		std::sort(m_vEntries.begin(), m_vEntries.end(), [](const SIndexEntry &a, const SIndexEntry &b) {
			if(a.m_Cat != b.m_Cat)
				return a.m_Cat < b.m_Cat;
			return str_comp_nocase(a.m_aName, b.m_aName) < 0;
		});
		m_vEntries.erase(std::unique(m_vEntries.begin(), m_vEntries.end(), [](const SIndexEntry &a, const SIndexEntry &b) {
			return a.m_Cat == b.m_Cat && str_comp(a.m_aName, b.m_aName) == 0;
		}),
			m_vEntries.end());
	}
};

class CKingAssetCatalog::CThumbJob : public IJob
{
public:
	CThumbJob(IGraphics *pGraphics, IStorage *pStorage, const char *pCachePath, std::shared_ptr<IHttpRequest> pRequest) :
		m_pGraphics(pGraphics), m_pStorage(pStorage), m_pRequest(std::move(pRequest))
	{
		str_copy(m_aCachePath, pCachePath);
		Abortable(true);
	}

	bool m_Ok = false;
	bool m_CacheBroken = false;
	float m_Aspect = 1.0f;
	CImageInfo m_Image;

private:
	IGraphics *m_pGraphics;
	IStorage *m_pStorage;
	std::shared_ptr<IHttpRequest> m_pRequest;
	char m_aCachePath[IO_MAX_PATH_LENGTH];

	void Run() override
	{
		if(m_pRequest)
		{
			unsigned char *pData = nullptr;
			size_t DataSize = 0;
			m_pRequest->Result(&pData, &DataSize);
			if(!pData || !m_pGraphics->LoadPng(m_Image, pData, DataSize, m_aCachePath))
				return;
			m_pRequest = nullptr; // the downloaded file is not needed any more
			m_Aspect = m_Image.m_Height / (float)std::max<size_t>(1, m_Image.m_Width);
			if(State() == IJob::STATE_ABORTED)
				return;
			MakeThumbnail(m_Image);
			// cache the preview (it keeps the aspect of the original)
			IOHANDLE File = m_pStorage->OpenFile(m_aCachePath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(File)
				CImageLoader::SavePng(File, m_aCachePath, m_Image);
			m_Ok = true;
		}
		else
		{
			void *pData = nullptr;
			unsigned DataSize = 0;
			if(!m_pStorage->ReadFile(m_aCachePath, IStorage::TYPE_SAVE, &pData, &DataSize))
			{
				m_CacheBroken = true;
				return;
			}
			const bool Loaded = m_pGraphics->LoadPng(m_Image, (const uint8_t *)pData, DataSize, m_aCachePath);
			free(pData);
			if(!Loaded || m_Image.m_Width == 0 || m_Image.m_Height == 0)
			{
				m_pStorage->RemoveFile(m_aCachePath, IStorage::TYPE_SAVE);
				m_CacheBroken = true;
				return;
			}
			ConvertToRgba(m_Image);
			m_Aspect = m_Image.m_Height / (float)m_Image.m_Width;
			m_Ok = true;
		}
	}
};

class CKingAssetCatalog::CInstallJob : public IJob
{
public:
	CInstallJob(IGraphics *pGraphics, IStorage *pStorage, int Cat, const char *pName, std::shared_ptr<IHttpRequest> pRequest) :
		m_pGraphics(pGraphics), m_pStorage(pStorage), m_Cat(Cat), m_pRequest(std::move(pRequest))
	{
		str_copy(m_aName, pName);
		Abortable(false); // a half written asset would be worse than a finished one
	}

	bool m_Ok = false;
	CImageInfo m_Image;

private:
	IGraphics *m_pGraphics;
	IStorage *m_pStorage;
	int m_Cat;
	char m_aName[50];
	std::shared_ptr<IHttpRequest> m_pRequest;

	void Run() override
	{
		unsigned char *pData = nullptr;
		size_t DataSize = 0;
		m_pRequest->Result(&pData, &DataSize);
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", s_apCategoryFolders[m_Cat], m_aName);
		if(!pData || !m_pGraphics->LoadPng(m_Image, pData, DataSize, aPath))
			return; // not a valid PNG: nothing is written
		// the menu list only shows a preview of it (using it loads the real file), so a
		// 4096x4096 entities image does not become a 64 MB texture upload on the main thread
		MakeThumbnail(m_Image);

		char aFolder[IO_MAX_PATH_LENGTH];
		str_format(aFolder, sizeof(aFolder), "assets/%s", s_apCategoryFolders[m_Cat]);
		m_pStorage->CreateFolder("assets", IStorage::TYPE_SAVE);
		m_pStorage->CreateFolder(aFolder, IStorage::TYPE_SAVE);
		char aTmpPath[IO_MAX_PATH_LENGTH];
		str_format(aTmpPath, sizeof(aTmpPath), "%s.part", aPath);
		IOHANDLE File = m_pStorage->OpenFile(aTmpPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return;
		const bool Written = io_write(File, pData, DataSize) == DataSize;
		io_close(File);
		m_pRequest = nullptr;
		if(!Written || !m_pStorage->RenameFile(aTmpPath, aPath, IStorage::TYPE_SAVE))
		{
			m_pStorage->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
			return;
		}
		m_Ok = true;
	}
};

/* ---------- catalog ---------- */

struct CKingAssetCatalog::SThumbTask
{
	SItem *m_pItem = nullptr;
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::shared_ptr<CThumbJob> m_pJob;
};

struct CKingAssetCatalog::SInstallTask
{
	int m_Cat = 0;
	char m_aName[50] = "";
	bool m_Retried = false;
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::shared_ptr<CInstallJob> m_pJob;
};

CKingAssetCatalog::CKingAssetCatalog() = default;

CKingAssetCatalog::~CKingAssetCatalog()
{
	// textures go with the graphics backend; only stop network work
	for(auto &pTask : m_vpThumbTasks)
		if(pTask->m_pRequest)
			pTask->m_pRequest->Abort();
	if(m_pIndexRequest)
		m_pIndexRequest->Abort();
}

void CKingAssetCatalog::Init(IGraphics *pGraphics, IStorage *pStorage, IHttp *pHttp, IEngine *pEngine)
{
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	m_pHttp = pHttp;
	m_pEngine = pEngine;
}

void CKingAssetCatalog::FileUrl(const SItem *pItem, char *pBuf, int BufSize) const
{
	char aEscaped[256];
	EscapeUrl(aEscaped, pItem->m_aName);
	if(m_UseMirror)
		str_format(pBuf, BufSize, "https://cdn.jsdelivr.net/gh/%s@%s/%s/%s.png", KING_ASSETS_REPO, KING_ASSETS_BRANCH, CategoryFolder(pItem->m_Cat), aEscaped);
	else
		str_format(pBuf, BufSize, "https://raw.githubusercontent.com/%s/%s/%s/%s.png", KING_ASSETS_REPO, KING_ASSETS_BRANCH, CategoryFolder(pItem->m_Cat), aEscaped);
}

void CKingAssetCatalog::StartIndex(bool Force)
{
	m_IndexState = INDEX_LOADING;
	m_IndexStale = false;
	if(Force)
	{
		m_IndexStep = 1;
		StartIndexRequest();
		return;
	}
	m_IndexStep = 0;
	m_pIndexJob = std::make_shared<CIndexJob>(m_pStorage, CIndexJob::MODE_CACHE, nullptr);
	m_pEngine->AddJob(m_pIndexJob);
}

void CKingAssetCatalog::StartIndexRequest()
{
	char aUrl[256];
	if(m_IndexStep == 1)
		str_format(aUrl, sizeof(aUrl), "https://api.github.com/repos/%s/git/trees/%s?recursive=1", KING_ASSETS_REPO, KING_ASSETS_BRANCH);
	else
		str_format(aUrl, sizeof(aUrl), "https://data.jsdelivr.com/v1/packages/gh/%s@%s?structure=flat", KING_ASSETS_REPO, KING_ASSETS_BRANCH);
	std::shared_ptr<IHttpRequest> pRequest = KingAssetGet(aUrl, 8 * 1024 * 1024);
	if(m_IndexStep == 1)
		pRequest->HeaderString("Accept", "application/vnd.github+json");
	m_pIndexRequest = pRequest;
	m_pHttp->Run(pRequest);
}

void CKingAssetCatalog::Refresh()
{
	if(m_IndexState == INDEX_LOADING || !m_vpInstallTasks.empty())
		return;
	StartIndex(true);
}

void CKingAssetCatalog::FinishIndex(std::vector<std::unique_ptr<SItem>> &&vpItems)
{
	ReleaseAll(); // tasks point into the old items
	m_vpAllItems = std::move(vpItems);
	m_Generation++;
	for(auto &vpCat : m_avpItems)
		vpCat.clear();
	for(auto &pItem : m_vpAllItems)
		m_avpItems[pItem->m_Cat].push_back(pItem.get());
	for(bool &Scanned : m_aInstalledScanned)
		Scanned = false;
	m_IndexState = INDEX_READY;
}

void CKingAssetCatalog::UpdateIndex()
{
	auto MakeItems = [](const std::vector<SIndexEntry> &vEntries) {
		std::vector<std::unique_ptr<SItem>> vpItems;
		vpItems.reserve(vEntries.size());
		for(const SIndexEntry &Entry : vEntries)
		{
			auto pItem = std::make_unique<SItem>();
			pItem->m_Cat = Entry.m_Cat;
			pItem->m_Size = Entry.m_Size;
			str_copy(pItem->m_aName, Entry.m_aName);
			vpItems.push_back(std::move(pItem));
		}
		return vpItems;
	};
	auto NextStepOrFail = [&]() {
		if(m_IndexStep == 1)
		{
			m_IndexStep = 2;
			StartIndexRequest();
		}
		else if(!m_vpStaleItems.empty())
		{
			FinishIndex(std::move(m_vpStaleItems));
			m_vpStaleItems.clear();
			m_IndexStale = true;
		}
		else
			m_IndexState = INDEX_FAILED;
	};

	if(m_pIndexJob && m_pIndexJob->Done())
	{
		std::shared_ptr<CIndexJob> pJob = std::move(m_pIndexJob);
		m_pIndexJob = nullptr;
		if(pJob->m_Mode == CIndexJob::MODE_CACHE)
		{
			if(pJob->m_Ok && pJob->m_Fresh)
				FinishIndex(MakeItems(pJob->m_vEntries));
			else
			{
				if(pJob->m_Ok)
					m_vpStaleItems = MakeItems(pJob->m_vEntries);
				m_IndexStep = 1;
				StartIndexRequest();
			}
		}
		else if(pJob->m_Ok)
		{
			m_vpStaleItems.clear();
			FinishIndex(MakeItems(pJob->m_vEntries));
		}
		else
			NextStepOrFail();
	}
	if(m_pIndexRequest && m_pIndexRequest->Done())
	{
		std::shared_ptr<IHttpRequest> pRequest = std::move(m_pIndexRequest);
		m_pIndexRequest = nullptr;
		if(RequestOk(pRequest))
		{
			m_pIndexJob = std::make_shared<CIndexJob>(m_pStorage, CIndexJob::MODE_PARSE, pRequest);
			m_pEngine->AddJob(m_pIndexJob);
		}
		else
		{
			log_info("king", "asset catalog index request failed (state %d, status %d)", (int)pRequest->State(), pRequest->State() == EHttpState::DONE ? pRequest->StatusCode() : 0);
			NextStepOrFail();
		}
	}
}

void CKingAssetCatalog::WantThumb(SItem *pItem)
{
	pItem->m_LastWanted = time_get();
	if(pItem->m_ThumbState == THUMB_NONE)
	{
		pItem->m_ThumbState = THUMB_QUEUED;
		m_vpQueue.push_back(pItem);
	}
}

void CKingAssetCatalog::FreeThumb(SItem *pItem)
{
	if(pItem->m_Thumb.IsValid())
	{
		m_pGraphics->UnloadTexture(&pItem->m_Thumb);
		m_NumThumbs--;
	}
	pItem->m_Thumb = IGraphics::CTextureHandle();
}

void CKingAssetCatalog::UpdateThumbs()
{
	const int64_t Now = time_get();
	const int64_t Freq = time_freq();

	// scrolled away before it started: forget it
	m_vpQueue.erase(std::remove_if(m_vpQueue.begin(), m_vpQueue.end(), [&](SItem *pItem) {
		if(Now - pItem->m_LastWanted > Freq / 2)
		{
			pItem->m_ThumbState = THUMB_NONE;
			return true;
		}
		return false;
	}),
		m_vpQueue.end());

	int Downloads = 0, Jobs = 0;
	for(auto &pTask : m_vpThumbTasks)
	{
		Downloads += pTask->m_pRequest != nullptr;
		Jobs += pTask->m_pJob != nullptr;
	}

	// start: the most recently wanted first (what is on screen right now)
	std::stable_sort(m_vpQueue.begin(), m_vpQueue.end(), [](const SItem *a, const SItem *b) { return a->m_LastWanted > b->m_LastWanted; });
	size_t Next = 0;
	while(Next < m_vpQueue.size() && Downloads < KING_MAX_THUMB_DOWNLOADS && Jobs < KING_MAX_THUMB_JOBS)
	{
		SItem *pItem = m_vpQueue[Next++];
		char aCache[IO_MAX_PATH_LENGTH];
		str_format(aCache, sizeof(aCache), "king/assets/thumbs/%s/%s.png", CategoryFolder(pItem->m_Cat), pItem->m_aName);
		auto pTask = std::make_unique<SThumbTask>();
		pTask->m_pItem = pItem;
		if(m_pStorage->FileExists(aCache, IStorage::TYPE_SAVE))
		{
			pTask->m_pJob = std::make_shared<CThumbJob>(m_pGraphics, m_pStorage, aCache, nullptr);
			m_pEngine->AddJob(pTask->m_pJob);
			Jobs++;
		}
		else
		{
			char aFolder[IO_MAX_PATH_LENGTH];
			m_pStorage->CreateFolder("king", IStorage::TYPE_SAVE);
			m_pStorage->CreateFolder("king/assets", IStorage::TYPE_SAVE);
			m_pStorage->CreateFolder("king/assets/thumbs", IStorage::TYPE_SAVE);
			str_format(aFolder, sizeof(aFolder), "king/assets/thumbs/%s", CategoryFolder(pItem->m_Cat));
			m_pStorage->CreateFolder(aFolder, IStorage::TYPE_SAVE);
			char aUrl[512];
			FileUrl(pItem, aUrl, sizeof(aUrl));
			pTask->m_pRequest = KingAssetGet(aUrl, 16 * 1024 * 1024);
			m_pHttp->Run(pTask->m_pRequest);
			Downloads++;
		}
		pItem->m_ThumbState = THUMB_LOADING;
		m_vpThumbTasks.push_back(std::move(pTask));
	}
	m_vpQueue.erase(m_vpQueue.begin(), m_vpQueue.begin() + Next);

	// progress
	int Uploads = 0;
	for(size_t i = 0; i < m_vpThumbTasks.size();)
	{
		SThumbTask &Task = *m_vpThumbTasks[i];
		SItem *pItem = Task.m_pItem;
		bool Remove = false;
		if(Task.m_pRequest && Task.m_pRequest->Done())
		{
			std::shared_ptr<IHttpRequest> pRequest = std::move(Task.m_pRequest);
			Task.m_pRequest = nullptr;
			if(RequestOk(pRequest))
			{
				char aCache[IO_MAX_PATH_LENGTH];
				str_format(aCache, sizeof(aCache), "king/assets/thumbs/%s/%s.png", CategoryFolder(pItem->m_Cat), pItem->m_aName);
				Task.m_pJob = std::make_shared<CThumbJob>(m_pGraphics, m_pStorage, aCache, pRequest);
				m_pEngine->AddJob(Task.m_pJob);
			}
			else
			{
				if(RequestUnreachable(pRequest) && !m_UseMirror)
					m_UseMirror = true;
				pItem->m_ThumbTries++;
				pItem->m_ThumbState = pItem->m_ThumbTries < 2 ? THUMB_NONE : THUMB_FAILED;
				Remove = true;
			}
		}
		else if(Task.m_pJob && Task.m_pJob->Done())
		{
			if(Task.m_pJob->m_Ok && Task.m_pJob->m_Image.m_pData)
			{
				if(Uploads >= KING_MAX_UPLOADS_PER_FRAME)
				{
					i++;
					continue; // next frame
				}
				FreeThumb(pItem);
				pItem->m_ThumbAspect = Task.m_pJob->m_Aspect;
				pItem->m_Thumb = m_pGraphics->LoadTextureRawMove(Task.m_pJob->m_Image, 0, pItem->m_aName);
				m_NumThumbs++;
				Uploads++;
				pItem->m_ThumbState = THUMB_READY;
			}
			else if(Task.m_pJob->m_CacheBroken && pItem->m_ThumbTries < 2)
			{
				pItem->m_ThumbTries++;
				pItem->m_ThumbState = THUMB_NONE; // download it again
			}
			else
				pItem->m_ThumbState = THUMB_FAILED;
			Remove = true;
		}
		if(Remove)
			m_vpThumbTasks.erase(m_vpThumbTasks.begin() + i);
		else
			i++;
	}

	// keep video memory small: free previews that have been off screen the longest
	if(m_NumThumbs > KING_MAX_THUMB_TEXTURES)
	{
		std::vector<SItem *> vpLoaded;
		for(auto &pItem : m_vpAllItems)
			if(pItem->m_ThumbState == THUMB_READY && Now - pItem->m_LastWanted > Freq)
				vpLoaded.push_back(pItem.get());
		std::sort(vpLoaded.begin(), vpLoaded.end(), [](const SItem *a, const SItem *b) { return a->m_LastWanted < b->m_LastWanted; });
		for(SItem *pItem : vpLoaded)
		{
			if(m_NumThumbs <= KING_MAX_THUMB_TEXTURES * 3 / 4)
				break;
			FreeThumb(pItem);
			pItem->m_ThumbState = THUMB_NONE;
		}
	}
}

void CKingAssetCatalog::Install(SItem *pItem)
{
	if(pItem->m_Installed || pItem->m_InstallState == INSTALL_RUNNING)
		return;
	auto pTask = std::make_unique<SInstallTask>();
	pTask->m_Cat = pItem->m_Cat;
	str_copy(pTask->m_aName, pItem->m_aName);
	char aUrl[512];
	FileUrl(pItem, aUrl, sizeof(aUrl));
	pTask->m_pRequest = KingAssetGet(aUrl, 32 * 1024 * 1024);
	m_pHttp->Run(pTask->m_pRequest);
	pItem->m_InstallState = INSTALL_RUNNING;
	m_vpInstallTasks.push_back(std::move(pTask));
}

void CKingAssetCatalog::UpdateInstalls()
{
	auto FindItem = [&](int Cat, const char *pName) -> SItem * {
		for(SItem *pItem : m_avpItems[Cat])
			if(str_comp(pItem->m_aName, pName) == 0)
				return pItem;
		return nullptr;
	};
	for(size_t i = 0; i < m_vpInstallTasks.size();)
	{
		SInstallTask &Task = *m_vpInstallTasks[i];
		SItem *pItem = FindItem(Task.m_Cat, Task.m_aName);
		bool Remove = false;
		if(Task.m_pRequest && Task.m_pRequest->Done())
		{
			std::shared_ptr<IHttpRequest> pRequest = std::move(Task.m_pRequest);
			Task.m_pRequest = nullptr;
			if(RequestOk(pRequest))
			{
				Task.m_pJob = std::make_shared<CInstallJob>(m_pGraphics, m_pStorage, Task.m_Cat, Task.m_aName, pRequest);
				m_pEngine->AddJob(Task.m_pJob);
			}
			else if(RequestUnreachable(pRequest) && !Task.m_Retried && pItem)
			{
				// the main host is blocked or down: once more through the mirror
				m_UseMirror = true;
				Task.m_Retried = true;
				char aUrl[512];
				FileUrl(pItem, aUrl, sizeof(aUrl));
				Task.m_pRequest = KingAssetGet(aUrl, 32 * 1024 * 1024);
				m_pHttp->Run(Task.m_pRequest);
			}
			else
			{
				if(pItem)
					pItem->m_InstallState = INSTALL_FAILED;
				Remove = true;
			}
		}
		else if(Task.m_pJob && Task.m_pJob->Done())
		{
			if(Task.m_pJob->m_Ok)
			{
				if(pItem)
				{
					pItem->m_Installed = true;
					pItem->m_InstallState = INSTALL_NONE;
				}
				SInstalled Installed;
				Installed.m_Cat = Task.m_Cat;
				str_copy(Installed.m_aName, Task.m_aName);
				Installed.m_Image = std::move(Task.m_pJob->m_Image);
				m_InstalledQueue.push_back(std::move(Installed));
			}
			else if(pItem)
				pItem->m_InstallState = INSTALL_FAILED;
			Remove = true;
		}
		if(Remove)
			m_vpInstallTasks.erase(m_vpInstallTasks.begin() + i);
		else
			i++;
	}
}

bool CKingAssetCatalog::PopInstalled(SInstalled &Out)
{
	if(m_InstalledQueue.empty())
		return false;
	Out = std::move(m_InstalledQueue.front());
	m_InstalledQueue.pop_front();
	return true;
}

void CKingAssetCatalog::RescanInstalled(int Cat)
{
	if(m_IndexState != INDEX_READY)
		return;
	m_aInstalledScanned[Cat] = true;
	struct SScan
	{
		std::unordered_set<std::string> m_Names;
	} Scan;
	char aFolder[IO_MAX_PATH_LENGTH];
	str_format(aFolder, sizeof(aFolder), "assets/%s", CategoryFolder(Cat));
	m_pStorage->ListDirectory(
		IStorage::TYPE_ALL, aFolder, [](const char *pName, int IsDir, int, void *pUser) {
			SScan *pScan = (SScan *)pUser;
			if(pName[0] == '.')
				return 0;
			if(IsDir)
				pScan->m_Names.emplace(pName);
			else if(str_endswith(pName, ".png"))
				pScan->m_Names.emplace(pName, str_length(pName) - 4);
			return 0;
		},
		&Scan);
	for(SItem *pItem : m_avpItems[Cat])
		pItem->m_Installed = Scan.m_Names.count(pItem->m_aName) > 0;
}

void CKingAssetCatalog::Update()
{
	m_Shown = true;
	m_LastShown = time_get();
	if(m_IndexState == INDEX_NONE)
		StartIndex(false);
	UpdateIndex();
	UpdateInstalls();
	if(m_IndexState == INDEX_READY)
	{
		for(int Cat = 0; Cat < NUM_CATS; Cat++)
			if(!m_aInstalledScanned[Cat])
				RescanInstalled(Cat);
		UpdateThumbs();
	}
}

void CKingAssetCatalog::ReleaseAll()
{
	for(auto &pTask : m_vpThumbTasks)
		if(pTask->m_pRequest)
			pTask->m_pRequest->Abort(); // jobs finish on their own and free their data
	m_vpThumbTasks.clear();
	m_vpQueue.clear();
	for(auto &pItem : m_vpAllItems)
	{
		FreeThumb(pItem.get());
		pItem->m_ThumbState = THUMB_NONE;
		pItem->m_ThumbTries = 0;
	}
	m_NumThumbs = 0;
}

void CKingAssetCatalog::ReleaseIfHidden()
{
	if(m_Shown && time_get() - m_LastShown > time_freq())
	{
		m_Shown = false;
		ReleaseAll();
	}
}

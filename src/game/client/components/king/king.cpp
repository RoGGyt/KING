/* KING client: input modes, KoG preset, network HUD, lag log, music player and update check. */
#include "king.h"
#include "king_install.h"
#include "king_system.h"

#include <base/color.h>
#include <base/fs.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/net.h>
#include <base/io.h>
#include <base/log.h>
#include <base/process.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/version.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <tuple>

static constexpr const char *KING_EXE_NAME = "KING.exe";

const char *CKing::InputModeName(int Mode)
{
	switch(Mode)
	{
	case INPUT_MODE_CUSTOM: return "Custom";
	case INPUT_MODE_CLASSIC: return "Classic";
	case INPUT_MODE_KING: return "KING";
	case INPUT_MODE_DELTA: return "Delta";
	case INPUT_MODE_PING_COMP: return "Ping Comp";
	case INPUT_MODE_SMOOTH: return "Smooth";
	case INPUT_MODE_BEST: return "Best";
	}
	return "?";
}

void CKing::OnConsoleInit()
{
	Console()->Register("kg_kog_preset", "", CFGFLAG_CLIENT, ConKogPreset, this, "Apply KING recommended settings for KoG (antiping, low latency rendering, KING input)");
	Console()->Register("kg_check_update", "", CFGFLAG_CLIENT, ConCheckUpdate, this, "Check GitHub for a new KING release");
	Console()->Register("kg_music_toggle", "", CFGFLAG_CLIENT, ConMusicToggle, this, "Music player: play/pause what plays on your PC");
	Console()->Register("kg_music_next", "", CFGFLAG_CLIENT, ConMusicNext, this, "Music player: next track");
	Console()->Register("kg_music_prev", "", CFGFLAG_CLIENT, ConMusicPrev, this, "Music player: previous track");
	Console()->Register("kg_lag_stats", "", CFGFLAG_CLIENT, ConLagStats, this, "Show what caused lag this session (late inputs, corrections, stutters)");
}

void CKing::ConMusicToggle(IConsole::IResult *pResult, void *pUserData) { static_cast<CKing *>(pUserData)->m_Media.Command(CKingMedia::COMMAND_TOGGLE); }
void CKing::ConMusicNext(IConsole::IResult *pResult, void *pUserData) { static_cast<CKing *>(pUserData)->m_Media.Command(CKingMedia::COMMAND_NEXT); }
void CKing::ConMusicPrev(IConsole::IResult *pResult, void *pUserData) { static_cast<CKing *>(pUserData)->m_Media.Command(CKingMedia::COMMAND_PREVIOUS); }

void CKing::ConCheckUpdate(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CKing *>(pUserData)->CheckForUpdates();
}

void CKing::ConKogPreset(IConsole::IResult *pResult, void *pUserData)
{
	CKing *pSelf = static_cast<CKing *>(pUserData);
	pSelf->ApplyKogPreset();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "king", "KoG preset applied");
}

void CKing::OnInit()
{
	if(!g_Config.m_KgPresetApplied)
	{
		ApplyKogPreset();
		g_Config.m_KgPresetApplied = 1;
	}
	if(!g_Config.m_KgThemeApplied)
	{
		// KING look: dark gold interface color (HSLA packed as A H S L)
		g_Config.m_UiColor = (0xE4u << 24) | (0x1Eu << 16) | (0x8Cu << 8) | 0x46u;
		g_Config.m_KgMenuStyle = 1;
		g_Config.m_KgThemeApplied = 1;
	}
	if(g_Config.m_KgDefaultsVersion < 2)
	{
		// 1.0.8: safer auto margin (no late inputs) and no automatic chat translation
		g_Config.m_KgAutoMarginMin = std::max(g_Config.m_KgAutoMarginMin, 6);
		g_Config.m_KgAutoMarginSafety = std::max(g_Config.m_KgAutoMarginSafety, 3);
		g_Config.m_TcTranslateAuto = 0;
		g_Config.m_KgDefaultsVersion = 2;
	}
	if(g_Config.m_KgDefaultsVersion < 3)
	{
		// 1.0.9: auto margin never below the stock 10 ms, light correction smoothing in
		// KING mode, smooth margin changes, custom aspect ratio as width:height
		g_Config.m_KgAutoMarginMin = std::max(g_Config.m_KgAutoMarginMin, 10);
		g_Config.m_KgAutoMarginSafety = std::max(g_Config.m_KgAutoMarginSafety, 3);
		g_Config.m_KgKingSmooth = 1;
		g_Config.m_TcSmoothPredictionMargin = 0;
		if(g_Config.m_KgAspectCustom != 133)
		{
			g_Config.m_KgAspectCustomW = g_Config.m_KgAspectCustom;
			g_Config.m_KgAspectCustomH = 100;
		}
		g_Config.m_KgDefaultsVersion = 3;
	}
	if(g_Config.m_KgDefaultsVersion < 5)
	{
		// 1.0.16: Classic is set in ms instead of whole ticks
		if(g_Config.m_KgClassicTicks != 1)
			g_Config.m_KgClassicMs = std::clamp(g_Config.m_KgClassicTicks, 1, 5) * 20;
	}
	if(g_Config.m_KgDefaultsVersion < 4)
	{
		// 1.0.13: automatic chat translation is off by default now; switch it off once
		// (another client sharing %APPDATA%\DDNet could have turned it back on)
		g_Config.m_TcTranslateAuto = 0;
		g_Config.m_KgDefaultsVersion = 4;
	}
	g_Config.m_KgDefaultsVersion = std::max(g_Config.m_KgDefaultsVersion, 5);
	m_CrownTexture = Graphics()->LoadTexture("tclient/king_crown.png", IStorage::TYPE_ALL);
	m_Media.Start();
	ReadLastInstallResult();
	{
		// files the last update moved out of the way (they were in use)
		char aExe[IO_MAX_PATH_LENGTH];
		Storage()->GetBinaryPathAbsolute(KING_EXE_NAME, aExe, sizeof(aExe));
		if(fs_parent_dir(aExe) == 0)
			KingCleanupOldFiles(aExe);
	}
	if(g_Config.m_KgUpdateCheck)
		CheckForUpdates();
}

void CKing::OnShutdown()
{
	LagLogClose();
	if(m_AppliedLowerDiscord == 1)
		KingSetDiscordLowPriority(false); // give Discord its normal priority back
	m_Media.Stop();
	if(m_pLyricsTask)
	{
		m_pLyricsTask->Abort();
		m_pLyricsTask = nullptr;
	}
	for(auto *ppTask : {&m_pLookupStats, &m_pLookupDDNet})
	{
		if(*ppTask)
		{
			(*ppTask)->Abort();
			*ppTask = nullptr;
		}
	}
	if(m_HasCoverTexture)
	{
		Graphics()->UnloadTexture(&m_CoverTexture);
		m_HasCoverTexture = false;
	}
	if(m_pUpdateTask)
	{
		m_pUpdateTask->Abort();
		m_pUpdateTask = nullptr;
	}
	if(m_pDownloadTask)
	{
		m_pDownloadTask->Abort();
		m_pDownloadTask = nullptr;
	}
}

// ***** Updates ***** //

using TKingVersion = std::tuple<int, int, int>;

// Accepts "1.2.3", "v1.2.3", "1.2" (missing parts are 0). Returns false for anything else.
static bool ParseKingVersion(const char *pStr, TKingVersion &Out)
{
	if(pStr[0] == 'v' || pStr[0] == 'V')
		pStr++;
	int aParts[3] = {0, 0, 0};
	int Part = 0;
	bool Digit = false;
	for(; *pStr; pStr++)
	{
		if(*pStr >= '0' && *pStr <= '9')
		{
			aParts[Part] = aParts[Part] * 10 + (*pStr - '0');
			Digit = true;
			if(aParts[Part] > 100000)
				return false;
		}
		else if(*pStr == '.' && Digit && Part < 2)
		{
			Part++;
			Digit = false;
		}
		else if(*pStr == '-' || *pStr == '+')
			break; // ignore suffixes like -beta
		else
			return false;
	}
	if(!Digit)
		return false;
	Out = std::make_tuple(aParts[0], aParts[1], aParts[2]);
	return true;
}

void CKing::CheckForUpdates()
{
	if(m_pUpdateTask && !m_pUpdateTask->Done())
		return;
	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "https://api.github.com/repos/%s/releases/latest", KING_UPDATE_REPO);
	m_pUpdateTask = HttpGet(aUrl);
	m_pUpdateTask->HeaderString("Accept", "application/vnd.github+json");
	m_pUpdateTask->HeaderString("User-Agent", "KING/" KING_VERSION);
	m_pUpdateTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pUpdateTask->IpResolve(IPRESOLVE::V4);
	m_pUpdateTask->FailOnErrorStatus(false);
	m_pUpdateTask->MaxResponseSize(1024 * 1024);
	m_UpdateState = UPDATE_CHECKING;
	Http()->Run(m_pUpdateTask);
}

void CKing::FinishUpdateCheck()
{
	m_UpdateState = UPDATE_FAILED;
	if(m_pUpdateTask->State() != EHttpState::DONE)
		return;
	if(m_pUpdateTask->StatusCode() == 404)
	{
		// repository has no releases yet
		m_UpdateState = UPDATE_LATEST;
		return;
	}
	if(m_pUpdateTask->StatusCode() != 200)
		return;

	json_value *pJson = m_pUpdateTask->ResultJson();
	if(!pJson)
		return;
	const json_value &Json = *pJson;
	const json_value &Tag = Json["tag_name"];
	const json_value &Page = Json["html_url"];
	const json_value &Draft = Json["draft"];
	const json_value &Prerelease = Json["prerelease"];

	TKingVersion Latest, Current;
	if(Tag.type == json_string && ParseKingVersion(Tag, Latest) && ParseKingVersion(KING_VERSION, Current) &&
		!(Draft.type == json_boolean && Draft.u.boolean) && !(Prerelease.type == json_boolean && Prerelease.u.boolean))
	{
		str_copy(m_aLatestVersion, Tag);
		m_aUpdateUrl[0] = '\0';
		m_aAssetUrl[0] = '\0';
#if defined(CONF_FAMILY_WINDOWS)
		// Prefer the direct download of the Windows zip
		const json_value &Assets = Json["assets"];
		if(Assets.type == json_array)
		{
			for(int i = 0; i < json_array_length(&Assets); i++)
			{
				const json_value &Asset = *json_array_get(&Assets, i);
				const json_value &Name = Asset["name"];
				const json_value &Download = Asset["browser_download_url"];
				if(Name.type == json_string && Download.type == json_string && str_endswith(Name, "-win64.zip") && str_startswith(Download, "https://"))
				{
					str_copy(m_aUpdateUrl, Download);
					str_copy(m_aAssetUrl, Download);
					break;
				}
			}
		}
#endif
		if(!m_aUpdateUrl[0] && Page.type == json_string && str_startswith(Page, "https://"))
			str_copy(m_aUpdateUrl, Page);
		if(!m_aUpdateUrl[0])
			str_format(m_aUpdateUrl, sizeof(m_aUpdateUrl), "https://github.com/%s/releases/latest", KING_UPDATE_REPO);

		m_UpdateState = Latest > Current ? UPDATE_AVAILABLE : UPDATE_LATEST;
		if(m_UpdateState == UPDATE_AVAILABLE)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "KING %s is available (you have %s)", m_aLatestVersion, KING_VERSION);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "king", aBuf);
		}
	}
	json_value_free(pJson);
}

void CKing::OpenUpdatePage()
{
	if(m_aUpdateUrl[0])
		Client()->ViewLink(m_aUpdateUrl);
	else
	{
		char aUrl[256];
		str_format(aUrl, sizeof(aUrl), "https://github.com/%s/releases/latest", KING_UPDATE_REPO);
		Client()->ViewLink(aUrl);
	}
}

// ***** One-click install ***** //

static constexpr const char *KING_UPDATE_DIR = "king_update";
static constexpr const char *KING_UPDATE_ZIP = "king_update/KING-update.zip";
static constexpr const char *KING_UPDATE_LOG = "king_update/update.log"; // written by the old (script) updater

bool CKing::CanInstallUpdate() const
{
#if defined(CONF_FAMILY_WINDOWS)
	return UpdateAvailable() && m_aAssetUrl[0] != '\0';
#else
	return false;
#endif
}

float CKing::DownloadProgress() const
{
	if(!m_pDownloadTask)
		return 0.0f;
	const double Size = m_pDownloadTask->Size();
	if(Size <= 0.0)
		return 0.0f;
	return std::clamp((float)(m_pDownloadTask->Current() / Size), 0.0f, 1.0f);
}

void CKing::InstallUpdate()
{
	if(!CanInstallUpdate())
	{
		OpenUpdatePage();
		return;
	}
	if(m_pDownloadTask)
		return;
	m_aInstallError[0] = '\0';
	Storage()->CreateFolder(KING_UPDATE_DIR, IStorage::TYPE_SAVE);
	Storage()->RemoveFile(KING_UPDATE_ZIP, IStorage::TYPE_SAVE);

	m_pDownloadTask = HttpGet(m_aAssetUrl);
	m_pDownloadTask->HeaderString("User-Agent", "KING/" KING_VERSION);
	m_pDownloadTask->HeaderString("Accept", "application/octet-stream");
	// no total timeout for a ~60 MB file, but give up if it stalls for 30 s
	m_pDownloadTask->Timeout(CTimeout{10000, 0, 1024, 30});
	m_pDownloadTask->IpResolve(IPRESOLVE::V4);
	m_pDownloadTask->WriteToFile(Storage(), KING_UPDATE_ZIP, IStorage::TYPE_SAVE);
	Http()->Run(m_pDownloadTask);
	log_info("king", "downloading update %s", m_aLatestVersion);
}

void CKing::FinishDownload()
{
	const bool Ok = m_pDownloadTask->State() == EHttpState::DONE && m_pDownloadTask->StatusCode() == 200;
	const int Status = m_pDownloadTask->StatusCode();
	m_pDownloadTask = nullptr;
	if(!Ok)
	{
		str_format(m_aInstallError, sizeof(m_aInstallError), "Download failed (HTTP %d). Try again or use the releases page.", Status);
		Storage()->RemoveFile(KING_UPDATE_ZIP, IStorage::TYPE_SAVE);
		return;
	}
	if(!LaunchInstaller())
		return;
	Client()->Quit();
}

// Unpacks the release zip over the install folder right here (no scripts or helper
// programs: those look like malware to antivirus heuristics), starts the new KING
// and quits. Config, binds and skins live in %APPDATA%\DDNet and are not touched.
bool CKing::LaunchInstaller()
{
#if defined(CONF_FAMILY_WINDOWS)
	char aZip[IO_MAX_PATH_LENGTH];
	char aExe[IO_MAX_PATH_LENGTH];
	char aDest[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, KING_UPDATE_ZIP, aZip, sizeof(aZip));
	Storage()->GetBinaryPathAbsolute(KING_EXE_NAME, aExe, sizeof(aExe));
	str_copy(aDest, aExe);
	if(fs_parent_dir(aDest) != 0)
	{
		str_copy(m_aInstallError, "Could not find the KING install folder.");
		return false;
	}
	if(!KingInstallZip(aZip, aDest, m_aInstallError, sizeof(m_aInstallError)))
		return false;
	Storage()->RemoveFile(KING_UPDATE_ZIP, IStorage::TYPE_SAVE);
	if(process_execute(aExe, EShellExecuteWindowState::FOREGROUND) == INVALID_PROCESS)
	{
		str_copy(m_aInstallError, "Updated. Restart KING to use the new version.");
		return false;
	}
	log_info("king", "update installed into %s, restarting", aDest);
	return true;
#else
	str_copy(m_aInstallError, "Automatic install is only supported on Windows.");
	return false;
#endif
}

void CKing::ReadLastInstallResult()
{
	char *pLog = Storage()->ReadFileStr(KING_UPDATE_LOG, IStorage::TYPE_SAVE);
	if(!pLog)
		return;
	const char *pText = pLog;
	if(str_startswith(pText, "\xEF\xBB\xBF"))
		pText += 3;
	if(const char *pReason = str_startswith(pText, "fail "))
	{
		char aReason[200];
		str_copy(aReason, pReason);
		str_utf8_trim_right(aReason);
		str_format(m_aInstallError, sizeof(m_aInstallError), "Last update failed: %s", aReason);
	}
	free(pLog);
	Storage()->RemoveFile(KING_UPDATE_LOG, IStorage::TYPE_SAVE);
}

void CKing::ApplyKogPreset()
{
	// Input: KING mode = one tick fast input + safe auto margin + light correction smoothing
	g_Config.m_KgInputMode = INPUT_MODE_KING;
	g_Config.m_KgFastInputAmount = 20;
	g_Config.m_KgKingSmooth = 1;
	g_Config.m_KgAutoMargin = 1;
	g_Config.m_KgAutoMarginMin = 10;
	g_Config.m_KgAutoMarginMax = 40;
	g_Config.m_KgAutoMarginSafety = 3;

	// Antiping: predict others so hooks/collisions with other tees line up
	g_Config.m_ClPredict = 1;
	g_Config.m_ClAntiPing = 1;
	g_Config.m_ClAntiPingPlayers = 1;
	g_Config.m_ClAntiPingPreInput = 1;
	g_Config.m_ClAntiPingSmooth = 0;
	g_Config.m_TcAntiPingImproved = 0;
	g_Config.m_TcSmoothPredictionMargin = 0;
	g_Config.m_ClPredictionMargin = 10; // stock DDNet value, also the floor of auto margin

	// Rendering latency: no vsync, uncapped update rate
	g_Config.m_GfxVsync = 0;
	g_Config.m_ClRefreshRate = 0;

	g_Config.m_KgNetHud = 1;
}

// ***** Input modes ***** //

void CKing::SetFastInput(float WantedMs, bool Ramp, bool Others)
{
	const int64_t Now = time_get();
	const int Mode = g_Config.m_KgInputMode;
	WantedMs = std::clamp(WantedMs, 0.0f, 100.0f);
	if(!Ramp || m_AppliedFastMs < 0.0f || m_LastAppliedMode != Mode)
		m_AppliedFastMs = WantedMs;
	else
	{
		// automatic changes shift your tee on screen: at most 5 ms per second, i.e.
		// 1 ms (1/20 tick) at a time, which is too small to see
		const float Dt = std::clamp((Now - m_LastFastUpdate) / (float)time_freq(), 0.0f, 0.1f);
		const float Step = 5.0f * Dt;
		if(WantedMs > m_AppliedFastMs)
			m_AppliedFastMs = std::min(WantedMs, m_AppliedFastMs + Step);
		else
			m_AppliedFastMs = std::max(WantedMs, m_AppliedFastMs - Step);
	}
	m_LastFastUpdate = Now;
	m_LastAppliedMode = Mode;

	const int Amount = round_to_int(m_AppliedFastMs);
	g_Config.m_TcFastInput = Amount > 0;
	g_Config.m_TcFastInputAmount = std::clamp(Amount, 1, 100);
	g_Config.m_TcFastInputOthers = Others;
	g_Config.m_TcFastInputRepredictHook = 0;
}

void CKing::UpdateBestInput()
{
	const SKingNetStats Stats = Client()->KingNetStats();
	if(g_Config.m_KgBestPreset)
	{
		// auto: always exactly one tick ahead (moving the offset would move your tee),
		// only how long corrections are blended follows the connection
		const float Jitter = Stats.m_Valid ? Stats.m_JitterMs : 0.0f;
		m_BestTarget = 20;
		m_BestSmoothMs = Jitter < 3.0f ? 50 : (Jitter < 8.0f ? 70 : 100);
		m_BestCurve = SMOOTH_CURVE_SMOOTH;
	}
	else
	{
		const float Rtt = Stats.m_Valid ? Stats.m_RttMs : 0.0f;
		const int Wanted = std::clamp(round_to_int(g_Config.m_KgBestOffset * 0.2f + Rtt * g_Config.m_KgBestLatencyComp / 100.0f), 0, 100);
		// hysteresis: ping noise must not make the offset flicker by 1 ms
		if(g_Config.m_KgBestLatencyComp == 0 || absolute(Wanted - m_BestTarget) >= 2)
			m_BestTarget = Wanted;
		m_BestSmoothMs = round_to_int(g_Config.m_KgBestSmoothing * 1.5f);
		m_BestCurve = std::clamp(g_Config.m_KgBestInterpolation, 0, 2);
	}
	m_BestFastMs = m_BestTarget;
}

void CKing::ApplyInputMode()
{
	switch(g_Config.m_KgInputMode)
	{
	case INPUT_MODE_CLASSIC:
		// TClient fast input as it worked before the Feb 2026 "adjustable fast input"
		// rewrite: old tc_fast_input 1-5 = whole ticks ahead. A multiple of 20 ms
		// makes the new code take the exact same whole-tick path (no sub-tick part).
		// Prediction margin stays manual (cl_prediction_margin), exactly like TClient.
		SetFastInput((float)std::clamp(g_Config.m_KgClassicMs, 1, 100), false, false);
		break;
	case INPUT_MODE_KING:
	case INPUT_MODE_SMOOTH: // smoothing itself is applied when rendering our tee (gameclient.cpp)
		SetFastInput((float)g_Config.m_KgFastInputAmount, false, false);
		break;
	case INPUT_MODE_DELTA:
		// 0.01 tick units -> ms (1 tick = 20 ms). 0 turns fast input off.
		SetFastInput(g_Config.m_KgDeltaAmount * 0.2f, false, g_Config.m_KgDeltaOthers);
		break;
	case INPUT_MODE_PING_COMP:
	{
		// show our tee a share of the round trip ahead; follows the ping slowly
		const SKingNetStats Stats = Client()->KingNetStats();
		if(Stats.m_Valid)
		{
			const int Wanted = std::clamp(round_to_int(Stats.m_RttMs * g_Config.m_KgPingComp / 100.0f), 0, 100);
			if(absolute(Wanted - m_PingCompTarget) >= 2)
				m_PingCompTarget = Wanted;
		}
		SetFastInput((float)m_PingCompTarget, true, false);
		m_PingCompAmount = round_to_int(m_AppliedFastMs);
		break;
	}
	case INPUT_MODE_BEST:
		UpdateBestInput();
		SetFastInput((float)m_BestTarget, !g_Config.m_KgBestPreset && g_Config.m_KgBestLatencyComp > 0, false);
		break;
	default:
		m_LastAppliedMode = g_Config.m_KgInputMode;
		m_AppliedFastMs = -1.0f;
		break;
	}
}

float CKing::SmoothDurationMs() const
{
	switch(g_Config.m_KgInputMode)
	{
	case INPUT_MODE_KING: return g_Config.m_KgKingSmooth ? KING_SMOOTH_MS : 0.0f;
	case INPUT_MODE_SMOOTH: return (float)g_Config.m_KgSmoothMs;
	case INPUT_MODE_BEST: return (float)m_BestSmoothMs;
	}
	return 0.0f;
}

int CKing::SmoothCurve() const
{
	return g_Config.m_KgInputMode == INPUT_MODE_BEST ? m_BestCurve : (int)SMOOTH_CURVE_SMOOTH;
}

float CKing::SmoothEase(float T, int Curve)
{
	T = std::clamp(T, 0.0f, 1.0f);
	switch(Curve)
	{
	case SMOOTH_CURVE_LINEAR: return T;
	case SMOOTH_CURVE_CUBIC: return 1.0f - (1.0f - T) * (1.0f - T) * (1.0f - T); // fast start, soft landing
	default: return T * T * (3.0f - 2.0f * T); // smoothstep
	}
}

float CKing::AspectRatioValue(int Setting)
{
	switch(Setting)
	{
	case 1: return 4.0f / 3.0f;
	case 2: return 5.0f / 4.0f;
	case 3: return 3.0f / 2.0f;
	case 4: return 16.0f / 10.0f;
	case 5: return 16.0f / 9.0f;
	case 6:
		if(g_Config.m_KgAspectCustomW > 0 && g_Config.m_KgAspectCustomH > 0)
			return std::clamp(g_Config.m_KgAspectCustomW / (float)g_Config.m_KgAspectCustomH, 0.5f, 4.0f);
		return 0.0f;
	}
	return 0.0f; // native
}

void CKing::OnUpdate()
{
	ApplyInputMode();
	ApplyPriorities(false);
	AutoSaveConfig();

	// stretched aspect ratio while in a game: "Full" = everything (world, HUD,
	// in-game menus), "World" = only the game world
	const bool InGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const float Ratio = InGame ? AspectRatioValue(g_Config.m_KgAspectRatio) : 0.0f;
	if(g_Config.m_KgAspectApply == ASPECT_APPLY_FULL)
	{
		Graphics()->SetScreenAspectOverride(Ratio);
		Graphics()->SetWorldAspect(0.0f);
	}
	else
	{
		Graphics()->SetScreenAspectOverride(0.0f);
		Graphics()->SetWorldAspect(Ratio);
	}

	// visualizer: capture what plays only while the bars can be seen
	const bool VisOn = g_Config.m_KgMusicHud && g_Config.m_KgMusicVisualizer != MUSIC_VIS_OFF && m_Media.Supported();
	const bool Seen = InGame || GameClient()->m_Menus.IsActive();
	m_Media.SetVisualizerWanted(VisOn && Seen && m_Media.Get().m_Playing);
}

void CKing::OnRender()
{
	if(m_pUpdateTask && m_pUpdateTask->Done())
	{
		FinishUpdateCheck();
		m_pUpdateTask = nullptr;
	}
	if(m_pDownloadTask && m_pDownloadTask->Done())
		FinishDownload();
	if(m_pLyricsTask && m_pLyricsTask->Done())
		FinishLyrics();
	if((m_pLookupStats || m_pLookupDDNet) && (!m_pLookupStats || m_pLookupStats->Done()) && (!m_pLookupDDNet || m_pLookupDDNet->Done()))
		FinishLookup();
	if(g_Config.m_KgMusicHud && g_Config.m_KgMusicLyrics)
		UpdateLyrics(m_Media.Get());

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_PillRenderedThisFrame = false;
		return;
	}
	UpdateLagMonitor();
	if(g_Config.m_KgNetHud)
		RenderNetHud();
	if(g_Config.m_KgMusicHud && g_Config.m_ClShowhud)
		RenderLyrics();
	m_PillRenderedThisFrame = false;
}

void CKing::RenderNetHud()
{
	const SKingNetStats Stats = Client()->KingNetStats();

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, Height);

	const float FontSize = 6.0f;
	const float LineHeight = FontSize + 1.5f;
	const float Pad = 3.0f;

	char aLine1[128];
	char aLine2[128];
	char aLine3[128];
	char aLine4[128];
	if(Stats.m_Valid)
		str_format(aLine1, sizeof(aLine1), "ping %d ms   jitter %.1f ms", round_to_int(Stats.m_RttMs), Stats.m_JitterMs);
	else
		str_copy(aLine1, "ping -- ms   measuring...");

	if(Stats.m_MarginLocked)
		str_format(aLine2, sizeof(aLine2), "margin %d ms (locked by server)", Stats.m_MarginMs);
	else if(Stats.m_MarginAuto && absolute(Stats.m_MarginTargetMs - Stats.m_MarginExactMs) >= 0.5f)
		str_format(aLine2, sizeof(aLine2), "margin %.0f -> %.0f ms auto", Stats.m_MarginExactMs, Stats.m_MarginTargetMs);
	else
		str_format(aLine2, sizeof(aLine2), "margin %d ms %s", Stats.m_MarginMs, Stats.m_MarginAuto ? "auto" : "fixed");

	if(g_Config.m_TcFastInput && g_Config.m_KgInputMode == INPUT_MODE_CLASSIC)
		str_format(aLine3, sizeof(aLine3), "%s input   fast %d ms", InputModeName(g_Config.m_KgInputMode), g_Config.m_TcFastInputAmount);
	else if(g_Config.m_TcFastInput && g_Config.m_KgInputMode == INPUT_MODE_DELTA)
		str_format(aLine3, sizeof(aLine3), "%s input   %d.%02d ticks (%d ms)", InputModeName(g_Config.m_KgInputMode), g_Config.m_KgDeltaAmount / 100, g_Config.m_KgDeltaAmount % 100, g_Config.m_TcFastInputAmount);
	else if(SmoothDurationMs() > 0.0f)
		str_format(aLine3, sizeof(aLine3), "%s input   fast %d ms, smooth %d ms", InputModeName(g_Config.m_KgInputMode), g_Config.m_TcFastInput ? g_Config.m_TcFastInputAmount : 0, round_to_int(SmoothDurationMs()));
	else if(g_Config.m_TcFastInput)
		str_format(aLine3, sizeof(aLine3), "%s input   fast %d ms", InputModeName(g_Config.m_KgInputMode), g_Config.m_TcFastInputAmount);
	else
		str_format(aLine3, sizeof(aLine3), "%s input   fast off", InputModeName(g_Config.m_KgInputMode));

	// what went wrong this session (also written to king_lag.log)
	str_format(aLine4, sizeof(aLine4), "late %d   corr %d   stutter %d   gaps %d", m_LagStats.m_LateInputs, m_LagStats.m_Corrections, m_LagStats.m_Stutters, m_LagStats.m_SnapGaps);

	char aLine5[96] = "";
	{
		const CControls &Controls = GameClient()->m_Controls;
		if(g_Config.m_KgSnapTap)
			str_append(aLine5, "SnapTap  ");
		if(Controls.King45Active())
			str_append(aLine5, "45deg  ");
		if(Controls.KingSmallSensActive())
			str_append(aLine5, "SmallSens");
	}
	const bool HasLine5 = aLine5[0] != '\0';
	const float TextW = std::max({TextRender()->TextWidth(FontSize, aLine5), TextRender()->TextWidth(FontSize, aLine1), TextRender()->TextWidth(FontSize, aLine2), TextRender()->TextWidth(FontSize, aLine3), TextRender()->TextWidth(FontSize, aLine4), TextRender()->TextWidth(FontSize, "KING")});
	const float BoxW = TextW + Pad * 2.0f;
	const float BoxH = LineHeight * (HasLine5 ? 6.0f : 5.0f) + Pad * 2.0f - 1.5f;

	float y = 5.0f;
	if(g_Config.m_ClShowfps)
		y += 15.0f;
	if(g_Config.m_ClShowpred)
		y += 15.0f;
	const float x = Width - 10.0f - BoxW;

	Graphics()->DrawRect(x, y, BoxW, BoxH, ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 3.0f);

	float Ty = y + Pad;
	TextRender()->TextColor(1.0f, 0.82f, 0.2f, 1.0f); // gold
	TextRender()->Text(x + Pad, Ty, FontSize, "KING");
	Ty += LineHeight;

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	TextRender()->Text(x + Pad, Ty, FontSize, aLine1);
	Ty += LineHeight;

	if(Stats.m_RecentLate)
		TextRender()->TextColor(1.0f, 0.35f, 0.35f, 1.0f);
	TextRender()->Text(x + Pad, Ty, FontSize, aLine2);
	Ty += LineHeight;

	TextRender()->TextColor(0.75f, 0.75f, 0.75f, 1.0f);
	TextRender()->Text(x + Pad, Ty, FontSize, aLine3);
	Ty += LineHeight;

	const int64_t Now = time_get();
	const bool RecentTrouble = (m_LastStutterTime && Now - m_LastStutterTime < 2 * time_freq()) || (m_LastGapTime && Now - m_LastGapTime < 2 * time_freq()) || Stats.m_RecentLate;
	if(RecentTrouble)
		TextRender()->TextColor(1.0f, 0.45f, 0.35f, 1.0f);
	else
		TextRender()->TextColor(0.6f, 0.6f, 0.6f, 1.0f);
	TextRender()->Text(x + Pad, Ty, FontSize, aLine4);
	if(HasLine5)
	{
		Ty += LineHeight;
		TextRender()->TextColor(0.55f, 0.85f, 1.0f, 1.0f);
		TextRender()->Text(x + Pad, Ty, FontSize, aLine5);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

// ***** Lag monitor ***** //

static constexpr const char *KING_LAG_LOG = "king_lag.log";
static constexpr const char *KING_LAG_LOG_OLD = "king_lag.old.log";

void CKing::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE && OldState != IClient::STATE_ONLINE)
	{
		m_LagStats = SLagStats();
		m_OnlineSince = time_get();
		m_LastSnapshotTime = 0;
		m_LastStutterTime = 0;
		m_LastGapTime = 0;
		m_LastCorrectionLog = 0;
		m_CorrectionLogBudget = 10.0f;
		m_CorrectionsNotLogged = 0;
		m_LastLogFlush = time_get();
		m_SeenLateInputs = 0;
		m_SeenPredResets = 0;
		m_LastSummary = time_get();
		m_MinFrameFps = 0.0f;
		LagLogOpen();
		ApplyPriorities(true); // Discord may have been restarted meanwhile
	}
	else if(NewState != IClient::STATE_ONLINE && OldState == IClient::STATE_ONLINE)
	{
		LagLog("left the server: late %d, corrections %d (big %d, max %.0f units), stutters %d, snapshot gaps %d, prediction resets %d, teleports %d",
			m_LagStats.m_LateInputs, m_LagStats.m_Corrections, m_LagStats.m_BigCorrections, m_LagStats.m_MaxCorrection, m_LagStats.m_Stutters, m_LagStats.m_SnapGaps, m_LagStats.m_PredResets, m_LagStats.m_Teleports);
		LagLogClose();
		m_OnlineSince = 0;
	}
}

void CKing::LagLogOpen()
{
	LagLogClose();
	if(!g_Config.m_KgLagLog)
		return;
	// keep it small: start a new file when it grows past 1 MB
	IOHANDLE Existing = Storage()->OpenFile(KING_LAG_LOG, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(Existing)
	{
		const int64_t Length = io_length(Existing);
		io_close(Existing);
		if(Length > 1024 * 1024)
		{
			Storage()->RemoveFile(KING_LAG_LOG_OLD, IStorage::TYPE_SAVE);
			Storage()->RenameFile(KING_LAG_LOG, KING_LAG_LOG_OLD, IStorage::TYPE_SAVE);
		}
	}
	m_LagLogFile = Storage()->OpenFile(KING_LAG_LOG, IOFLAG_APPEND, IStorage::TYPE_SAVE);
	if(!m_LagLogFile)
		return;

	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
	char aDate[64];
	str_timestamp_format(aDate, sizeof(aDate), TimestampFormat::SPACE);
	char aLine[512];
	str_format(aLine, sizeof(aLine), "\n==== %s | KING %s | server %s | map %s\n", aDate, KING_VERSION, aAddr, GameClient()->Map()->IsLoaded() ? GameClient()->Map()->BaseName() : "?");
	io_write(m_LagLogFile, aLine, str_length(aLine));
	LagLogSettings("settings");
	m_LoggedSettings = SettingsSignature();
	m_SettingsChangedSince = 0;
}

void CKing::LagLogClose()
{
	if(m_LagLogFile)
	{
		io_close(m_LagLogFile);
		m_LagLogFile = nullptr;
	}
}

void CKing::LagLog(const char *pFormat, ...)
{
	if(!m_LagLogFile)
		return;
	char aMsg[768];
	va_list Args;
	va_start(Args, pFormat);
	str_format_v(aMsg, sizeof(aMsg), pFormat, Args);
	va_end(Args);
	char aTime[32];
	str_timestamp_format(aTime, sizeof(aTime), TimestampFormat::TIME);
	char aLine[832];
	str_format(aLine, sizeof(aLine), "%s  %s\n", aTime, aMsg);
	io_write(m_LagLogFile, aLine, str_length(aLine)); // buffered, flushed every few seconds
}

void CKing::OnNewSnapshot()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	const int64_t Now = time_get();
	const bool Settled = m_OnlineSince && Now - m_OnlineSince > 3 * time_freq();
	if(Settled && m_LastSnapshotTime)
	{
		const float GapMs = (Now - m_LastSnapshotTime) * 1000.0f / (float)time_freq();
		if(GapMs > 150.0f)
		{
			m_LagStats.m_SnapGaps++;
			m_LastGapTime = Now;
			const SKingNetStats Stats = Client()->KingNetStats();
			LagLog("snapshot gap: %.0f ms without server data (ping %.0f ms)", GapMs, Stats.m_RttMs);
		}
	}
	m_LastSnapshotTime = Now;
}

// the input settings that matter for lag, to notice when they change during a session
std::string CKing::SettingsSignature() const
{
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%d %d %d %d %d %d %d %d %d %d %d %d %d %d", g_Config.m_KgInputMode, g_Config.m_TcFastInput, g_Config.m_TcFastInputAmount, g_Config.m_KgClassicMs,
		g_Config.m_KgKingSmooth, g_Config.m_KgSmoothMs, g_Config.m_KgAutoMargin, g_Config.m_KgAutoMarginMin, g_Config.m_KgAutoMarginMax, g_Config.m_KgAutoMarginSafety,
		g_Config.m_ClPredictionMargin, g_Config.m_ClAntiPing, g_Config.m_ClAntiPingPlayers, g_Config.m_KgFastInputAmount);
	return aBuf;
}

void CKing::LagLogSettings(const char *pPrefix)
{
	LagLog("%s: mode %s, fast input %s %d ms, smoothing %d ms, margin %s (min %d max %d safety %d, manual %d), antiping %d players %d, fps cap %d, vsync %d, refresh %d",
		pPrefix, InputModeName(g_Config.m_KgInputMode), g_Config.m_TcFastInput ? "on" : "off", g_Config.m_TcFastInputAmount, round_to_int(SmoothDurationMs()),
		ModeUsesAutoMargin(g_Config.m_KgInputMode) || (g_Config.m_KgAutoMargin && g_Config.m_KgInputMode != INPUT_MODE_CLASSIC) ? "auto" : "manual",
		g_Config.m_KgAutoMarginMin, g_Config.m_KgAutoMarginMax, g_Config.m_KgAutoMarginSafety, g_Config.m_ClPredictionMargin,
		g_Config.m_ClAntiPing, g_Config.m_ClAntiPingPlayers, g_Config.m_GfxRefreshRate, g_Config.m_GfxVsync, g_Config.m_ClRefreshRate);
}

void CKing::OnLocalCorrection(float Distance, int Tick)
{
	if(Client()->State() != IClient::STATE_ONLINE || Distance < 4.0f)
		return;
	if(Distance >= TELEPORT_DISTANCE)
	{
		// tele tiles, kill, spawn, rescue: the server moved you on purpose, not lag
		m_LagStats.m_Teleports++;
		if(m_LagLogFile)
			LagLog("teleport: %.0f units (tele, kill or spawn, not lag)", Distance);
		return;
	}
	m_LagStats.m_Corrections++;
	if(Distance >= 24.0f)
		m_LagStats.m_BigCorrections++;
	m_LagStats.m_MaxCorrection = std::max(m_LagStats.m_MaxCorrection, Distance);
	if(!m_LagLogFile)
		return;

	const int64_t Now = time_get();
	const int64_t Freq = time_freq();
	if(Distance < 8.0f && m_LastCorrectionLog && Now - m_LastCorrectionLog < Freq / 5)
		return;
	// at most ~10 lines per second, busy KoG maps can correct you a lot
	m_CorrectionLogBudget = std::min(10.0f, m_CorrectionLogBudget + (Now - m_LastCorrectionLog) / (float)Freq * 10.0f);
	m_LastCorrectionLog = Now;
	if(m_CorrectionLogBudget < 1.0f)
	{
		m_CorrectionsNotLogged++;
		return;
	}
	m_CorrectionLogBudget -= 1.0f;

	// what could have caused it: anything that happened within one round trip
	const SKingNetStats Stats = Client()->KingNetStats();
	const int64_t Window = (int64_t)((Stats.m_RttMs + 250.0f) / 1000.0f * Freq);
	char aCause[256] = "";
	if(Stats.m_LastLateTime && Now - Stats.m_LastLateTime < Window)
		str_format(aCause + str_length(aCause), sizeof(aCause) - str_length(aCause), "late input (%.0f ms late), ", Stats.m_LastLateMs);
	if(m_LastStutterTime && Now - m_LastStutterTime < Window)
		str_append(aCause, "stutter, ");
	if(m_LastGapTime && Now - m_LastGapTime < Window)
		str_append(aCause, "snapshot gap, ");
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId >= 0 && GameClient()->m_aClients[LocalId].m_Predicted.HookedPlayer() >= 0)
		str_append(aCause, "hooking a player, ");
	if(LocalId >= 0)
	{
		// someone hooking us from further away: the server knows their input, we don't
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i != LocalId && GameClient()->m_Snap.m_aCharacters[i].m_Active && GameClient()->m_Snap.m_aCharacters[i].m_Cur.m_HookedPlayer == LocalId)
			{
				str_append(aCause, "hooked by a player, ");
				break;
			}
		}
	}
	if(LocalId >= 0)
	{
		const vec2 LocalPos = GameClient()->m_LocalCharacterPos;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i == LocalId || !GameClient()->m_Snap.m_aCharacters[i].m_Active)
				continue;
			const vec2 Pos(GameClient()->m_Snap.m_aCharacters[i].m_Cur.m_X, GameClient()->m_Snap.m_aCharacters[i].m_Cur.m_Y);
			if(distance(Pos, LocalPos) < 160.0f)
			{
				str_append(aCause, "player nearby, ");
				break;
			}
		}
	}
	if(aCause[0])
		aCause[str_length(aCause) - 2] = '\0'; // drop the last ", "
	else
		str_copy(aCause, "no local cause (server side)");
	LagLog("correction: %.1f units at tick %d | %s | margin %.1f ms, ping %.0f ms%s", Distance, Tick, aCause, Stats.m_MarginExactMs, Stats.m_RttMs, m_CorrectionsNotLogged ? " (more were not logged)" : "");
	m_CorrectionsNotLogged = 0;
}

void CKing::UpdateLagMonitor()
{
	const int64_t Now = time_get();
	const int64_t Freq = time_freq();
	const bool Settled = m_OnlineSince && Now - m_OnlineSince > 3 * Freq;
	const SKingNetStats Stats = Client()->KingNetStats();

	// input settings changed while playing: log them once they stop changing (sliders)
	if(m_LagLogFile)
	{
		const std::string Signature = SettingsSignature();
		if(Signature == m_LoggedSettings)
			m_SettingsChangedSince = 0;
		else if(!m_SettingsChangedSince)
			m_SettingsChangedSince = Now;
		else if(Now - m_SettingsChangedSince > 2 * Freq)
		{
			LagLogSettings("settings changed");
			m_LoggedSettings = Signature;
			m_SettingsChangedSince = 0;
		}
	}

	const float FrameMs = Client()->RenderFrameTime() * 1000.0f;
	if(FrameMs > 0.0f)
	{
		const float Fps = 1000.0f / FrameMs;
		m_MinFrameFps = m_MinFrameFps <= 0.0f ? Fps : std::min(m_MinFrameFps, Fps);
	}
	// a hitch longer than the prediction margin can make inputs late, even if you don't see it
	const float AverageMs = Client()->FrameTimeAverage() * 1000.0f;
	if(Settled && FrameMs > std::max(25.0f, AverageMs * 2.5f) && FrameMs < 1000.0f) // > 1 s: minimized / alt-tab
	{
		m_LagStats.m_Stutters++;
		m_LastStutterTime = Now;
		LagLog("stutter: a frame took %.0f ms (usually %.1f ms), inputs may have been sent late", FrameMs, AverageMs);
	}

	if(Stats.m_LateInputs < m_SeenLateInputs)
		m_SeenLateInputs = Stats.m_LateInputs; // counters were reset on (re)connect
	if(Stats.m_LateInputs > m_SeenLateInputs)
	{
		const int New = Stats.m_LateInputs - m_SeenLateInputs;
		m_SeenLateInputs = Stats.m_LateInputs;
		if(Settled)
		{
			m_LagStats.m_LateInputs += New;
			LagLog("late input%s: %d (last %.0f ms late) | margin %.1f ms, auto target %.0f ms, ping %.0f ms, jitter %.1f ms",
				New > 1 ? "s" : "", New, Stats.m_LastLateMs, Stats.m_MarginExactMs, Stats.m_MarginTargetMs, Stats.m_RttMs, Stats.m_JitterMs);
		}
	}

	if(Stats.m_PredictionResets < m_SeenPredResets)
		m_SeenPredResets = Stats.m_PredictionResets;
	if(Stats.m_PredictionResets > m_SeenPredResets)
	{
		m_LagStats.m_PredResets += Stats.m_PredictionResets - m_SeenPredResets;
		m_SeenPredResets = Stats.m_PredictionResets;
		if(Settled)
			LagLog("prediction time reset (connection hiccup, the client resynced its clock)");
	}

	if(m_LagLogFile && Now - m_LastLogFlush > 5 * Freq)
	{
		io_flush(m_LagLogFile);
		m_LastLogFlush = Now;
	}

	if(Now - m_LastSummary > 60 * Freq)
	{
		m_LastSummary = Now;
		LagLog("minute: ping %.0f ms, jitter %.1f ms, margin %.1f ms (target %.0f) | %s | late %d, corrections %d (max %.0f units), stutters %d, gaps %d | lowest fps %.0f",
			Stats.m_RttMs, Stats.m_JitterMs, Stats.m_MarginExactMs, Stats.m_MarginTargetMs, InputModeName(g_Config.m_KgInputMode), m_LagStats.m_LateInputs, m_LagStats.m_Corrections, m_LagStats.m_MaxCorrection, m_LagStats.m_Stutters, m_LagStats.m_SnapGaps, m_MinFrameFps);
		m_MinFrameFps = 0.0f;
	}
}

void CKing::ConLagStats(IConsole::IResult *pResult, void *pUserData)
{
	CKing *pSelf = static_cast<CKing *>(pUserData);
	const SLagStats &Lag = pSelf->m_LagStats;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "late inputs %d, corrections %d (big %d, max %.0f units), stutters %d, snapshot gaps %d, prediction resets %d",
		Lag.m_LateInputs, Lag.m_Corrections, Lag.m_BigCorrections, Lag.m_MaxCorrection, Lag.m_Stutters, Lag.m_SnapGaps, Lag.m_PredResets);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "king", aBuf);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "king", "details: king_lag.log in your KING settings folder (%APPDATA%\\DDNet)");
}

// ***** Music player ***** //

static const ColorRGBA s_KingGold(0.91f, 0.70f, 0.29f, 1.0f);

void CKing::ResetMusicSettings()
{
	g_Config.m_KgMusicHud = 1;
	g_Config.m_KgMusicColorMode = MUSIC_COLOR_COVER;
	g_Config.m_KgMusicVisualizer = MUSIC_VIS_CENTER;
	g_Config.m_KgMusicTextScale = 100;
	g_Config.m_KgMusicColumns = 5;
	g_Config.m_KgMusicRounding = 1;
	g_Config.m_KgMusicLyrics = 0;
	g_Config.m_KgMusicShowPaused = 1;
}

bool CKing::MusicPillVisible()
{
	if(!g_Config.m_KgMusicHud || !m_Media.Supported())
		return false;
	const SKingMediaInfo Info = m_Media.Get();
	return Info.m_Valid && (Info.m_Playing || g_Config.m_KgMusicShowPaused) && (Info.m_aTitle[0] || Info.m_aArtist[0]);
}

ColorRGBA CKing::MusicAccent(const SKingMediaInfo &Info) const
{
	switch(g_Config.m_KgMusicColorMode)
	{
	case MUSIC_COLOR_KING: return s_KingGold;
	case MUSIC_COLOR_WHITE: return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	default:
	{
		if(!Info.m_HasCover)
			return s_KingGold;
		// cover color, made bright enough to read on the dark pill
		ColorHSLA Hsl = color_cast<ColorHSLA>(ColorRGBA(Info.m_aCoverColor[0], Info.m_aCoverColor[1], Info.m_aCoverColor[2], 1.0f));
		Hsl.s = std::min(Hsl.s * 1.1f, 0.85f);
		Hsl.l = std::clamp(Hsl.l, 0.62f, 0.8f);
		return color_cast<ColorRGBA>(Hsl);
	}
	}
}

void CKing::UpdateCoverTexture()
{
	const SKingMediaInfo Info = m_Media.Get();
	if(Info.m_CoverSerial == m_CoverSerial)
		return;
	std::vector<uint8_t> vPixels;
	int Serial = 0;
	const bool Have = m_Media.GetCover(vPixels, Serial);
	m_CoverSerial = Serial;
	if(m_HasCoverTexture)
	{
		Graphics()->UnloadTexture(&m_CoverTexture);
		m_HasCoverTexture = false;
	}
	const size_t Bytes = (size_t)CKingMedia::COVER_SIZE * CKingMedia::COVER_SIZE * 4;
	if(!Have || vPixels.size() < Bytes)
		return;
	CImageInfo Image;
	Image.m_Width = CKingMedia::COVER_SIZE;
	Image.m_Height = CKingMedia::COVER_SIZE;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.Allocate();
	mem_copy(Image.m_pData, vPixels.data(), Bytes);
	m_CoverTexture = Graphics()->LoadTextureRaw(Image, 0, "king_cover");
	m_HasCoverTexture = m_CoverTexture.IsValid();
}

// Textured quad with rounded corners (the cover art).
static void KingDrawRoundedImage(IGraphics *pGraphics, float X, float Y, float W, float H, float Rounding, ColorRGBA Color)
{
	Rounding = std::clamp(Rounding, 0.0f, std::min(W, H) * 0.5f);
	const auto Quad = [&](vec2 A, vec2 B, vec2 C, vec2 D) {
		const auto Uv = [&](vec2 P) { return vec2((P.x - X) / W, (P.y - Y) / H); };
		const vec2 Ua = Uv(A), Ub = Uv(B), Uc = Uv(C), Ud = Uv(D);
		pGraphics->QuadsSetSubsetFree(Ua.x, Ua.y, Ub.x, Ub.y, Uc.x, Uc.y, Ud.x, Ud.y);
		const IGraphics::CFreeformItem Item(A, B, C, D);
		pGraphics->QuadsDrawFreeform(&Item, 1);
	};

	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color);
	const float R = Rounding;
	if(R < 0.05f)
		Quad(vec2(X, Y), vec2(X + W, Y), vec2(X, Y + H), vec2(X + W, Y + H));
	else
	{
		Quad(vec2(X + R, Y), vec2(X + W - R, Y), vec2(X + R, Y + H), vec2(X + W - R, Y + H));
		Quad(vec2(X, Y + R), vec2(X + R, Y + R), vec2(X, Y + H - R), vec2(X + R, Y + H - R));
		Quad(vec2(X + W - R, Y + R), vec2(X + W, Y + R), vec2(X + W - R, Y + H - R), vec2(X + W, Y + H - R));
		const vec2 aCenter[4] = {vec2(X + R, Y + R), vec2(X + W - R, Y + R), vec2(X + W - R, Y + H - R), vec2(X + R, Y + H - R)};
		const float aStart[4] = {pi, pi * 1.5f, 0.0f, pi * 0.5f};
		constexpr int SEGMENTS = 6;
		for(int c = 0; c < 4; c++)
		{
			for(int s = 0; s < SEGMENTS; s++)
			{
				const float A0 = aStart[c] + (pi * 0.5f) * s / SEGMENTS;
				const float A1 = aStart[c] + (pi * 0.5f) * (s + 1) / SEGMENTS;
				const vec2 P0 = aCenter[c] + vec2(std::cos(A0), std::sin(A0)) * R;
				const vec2 P1 = aCenter[c] + vec2(std::cos(A1), std::sin(A1)) * R;
				Quad(aCenter[c], P0, aCenter[c], P1);
			}
		}
	}
	pGraphics->QuadsEnd();
}

float CKing::RenderMusicPill(float CenterX, float TopY, float Unit, const char *pTime, ColorRGBA TimeColor, bool Hud)
{
	const SKingMediaInfo Info = m_Media.Get();
	UpdateCoverTexture();

	const float Scale = Unit * std::clamp(g_Config.m_KgMusicTextScale, 50, 200) / 100.0f;
	const bool Soft = g_Config.m_KgMusicRounding != 0;
	const float FontSize = 10.0f * Scale;
	const float H = 15.0f * Scale;
	const float Pad = 1.7f * Scale;
	const float Gap = 3.0f * Scale;
	const float CoverSize = H - 2.0f * Pad;
	const int Vis = g_Config.m_KgMusicVisualizer;
	const int Columns = std::clamp(g_Config.m_KgMusicColumns, 3, 24);
	const float BarW = 1.7f * Scale;
	const float BarGap = 1.1f * Scale;
	const float VisW = Vis != MUSIC_VIS_OFF ? Columns * BarW + (Columns - 1) * BarGap : 0.0f;

	// width from a digit template, so the pill does not wobble every second
	char aTemplate[64];
	str_copy(aTemplate, pTime);
	for(char *p = aTemplate; *p; p++)
		if(*p >= '0' && *p <= '9')
			*p = '0';
	const float TimeW = TextRender()->TextWidth(FontSize, aTemplate);
	const float LeftW = Pad + CoverSize + Gap;
	const float RightW = Vis != MUSIC_VIS_OFF ? Gap + VisW + Pad + 0.6f * Scale : Pad;
	const float Side = std::max(LeftW, RightW);
	const float W = TimeW + 2.0f * Side;
	const float X = CenterX - W / 2.0f;
	const ColorRGBA Accent = MusicAccent(Info);
	const float PausedAlpha = Info.m_Playing ? 1.0f : 0.55f;

	// pill
	Graphics()->DrawRect(X, TopY, W, H, ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f), IGraphics::CORNER_ALL, Soft ? H * 0.32f : 1.2f * Scale);

	// cover
	const float CoverX = X + Pad;
	const float CoverY = TopY + Pad;
	const float CoverRounding = Soft ? CoverSize * 0.22f : 0.6f * Scale;
	if(m_HasCoverTexture)
	{
		Graphics()->TextureSet(m_CoverTexture);
		KingDrawRoundedImage(Graphics(), CoverX, CoverY, CoverSize, CoverSize, CoverRounding, ColorRGBA(1.0f, 1.0f, 1.0f, PausedAlpha));
		Graphics()->TextureClear();
	}
	else
	{
		Graphics()->DrawRect(CoverX, CoverY, CoverSize, CoverSize, ColorRGBA(Accent.r * 0.35f, Accent.g * 0.35f, Accent.b * 0.35f, 0.9f), IGraphics::CORNER_ALL, CoverRounding);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(Accent.WithAlpha(0.9f * PausedAlpha));
		const float IconSize = CoverSize * 0.55f;
		const float IconW = TextRender()->TextWidth(IconSize, FontIcon::MUSIC);
		TextRender()->Text(CoverX + (CoverSize - IconW) / 2.0f, CoverY + (CoverSize - IconSize) / 2.0f - 0.2f * IconSize, IconSize, FontIcon::MUSIC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	if(!Info.m_Playing)
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
		const float IconSize = CoverSize * 0.45f;
		const float IconW = TextRender()->TextWidth(IconSize, FontIcon::PAUSE);
		TextRender()->Text(CoverX + (CoverSize - IconW) / 2.0f, CoverY + (CoverSize - IconSize) / 2.0f - 0.2f * IconSize, IconSize, FontIcon::PAUSE);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	// timer in the middle
	const STextBoundingBox Box = TextRender()->TextBoundingBox(FontSize, pTime);
	TextRender()->TextColor(TimeColor);
	TextRender()->Text(CenterX - Box.m_W / 2.0f, TopY + (H - Box.m_H) / 2.0f, FontSize, pTime, -1.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	// visualizer on the right
	if(Vis != MUSIC_VIS_OFF)
	{
		std::array<float, CKingMedia::NUM_BANDS> aBands{};
		const bool Real = m_Media.VisualizerCapturing();
		if(Real)
			m_Media.GetBands(aBands);
		const float Dt = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
		const double Time = time_get() / (double)time_freq();
		const float VisX = X + W - Pad - 0.6f * Scale - VisW;
		const float MinH = BarW;
		const float MaxH = H - 2.0f * Pad - 0.4f * Scale;
		for(int i = 0; i < Columns; i++)
		{
			float Target = 0.0f;
			if(Info.m_Playing)
			{
				if(Real)
				{
					const int B0 = i * CKingMedia::NUM_BANDS / Columns;
					const int B1 = std::max(B0 + 1, (i + 1) * CKingMedia::NUM_BANDS / Columns);
					for(int b = B0; b < B1 && b < CKingMedia::NUM_BANDS; b++)
						Target = std::max(Target, aBands[b]);
				}
				else
				{
					// no access to the audio output: a calm decorative motion
					Target = 0.35f + 0.25f * std::sin(Time * (3.1 + i * 0.7) + i * 1.3) + 0.15f * std::sin(Time * (5.3 + i * 0.4));
				}
			}
			float &Level = m_aBarLevels[i];
			Level += (Target - Level) * std::min(1.0f, Dt * 30.0f);
			Level = std::clamp(Level, 0.0f, 1.0f);

			const float BarH = MinH + (MaxH - MinH) * Level;
			const float BarX = VisX + i * (BarW + BarGap);
			const float BarY = Vis == MUSIC_VIS_CENTER ? TopY + (H - BarH) / 2.0f : TopY + H - Pad - 0.2f * Scale - BarH;
			Graphics()->DrawRect(BarX, BarY, BarW, BarH, Accent.WithAlpha((0.55f + 0.45f * Level) * PausedAlpha), IGraphics::CORNER_ALL, Soft ? BarW * 0.5f : 0.0f);
		}
	}

	if(Hud)
	{
		m_PillRenderedThisFrame = true;
		m_PillBottom = TopY + H;
		m_PillRight = X + W;
		m_LyricsTop = m_PillBottom;
		m_PillUnit = Unit;
	}
	return H;
}

void CKing::RenderLyrics()
{
	if(!m_PillRenderedThisFrame || !g_Config.m_KgMusicLyrics)
		return;
	const SKingMediaInfo Info = m_Media.Get();
	if(!Info.m_Valid)
		return;

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, Height);

	const float Scale = m_PillUnit * std::clamp(g_Config.m_KgMusicTextScale, 50, 200) / 100.0f;
	const float Dt = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);

	// synced lyrics (opt-in); the song name is not shown, the cover is enough
	const std::string Track = std::string(Info.m_aTitle) + "\n" + Info.m_aArtist;
	char aText[512] = "";
	float Alpha = 0.0f;
	if(g_Config.m_KgMusicLyrics && Info.m_HasTimeline && Track == m_LyricsTrack && !m_vLyrics.empty())
	{
		const float Pos = Info.m_PositionSec + 0.25f; // show a line a moment early, like karaoke
		int Line = -1;
		for(int i = 0; i < (int)m_vLyrics.size() && m_vLyrics[i].m_Time <= Pos; i++)
			Line = i;
		if(Line != m_CurrentLyric)
		{
			m_CurrentLyric = Line;
			m_LyricAlpha = 0.0f;
		}
		if(Line >= 0 && !m_vLyrics[Line].m_Text.empty())
		{
			str_copy(aText, m_vLyrics[Line].m_Text.c_str());
			m_LyricAlpha = std::min(1.0f, m_LyricAlpha + Dt / 0.15f);
			Alpha = m_LyricAlpha * (Info.m_Playing ? 1.0f : 0.6f);
		}
	}
	if(!aText[0] || Alpha <= 0.0f)
		return;

	float FontSize = 6.5f * Scale;
	const float MaxW = Width * 0.6f;
	const float TextW = TextRender()->TextWidth(FontSize, aText);
	if(TextW > MaxW)
		FontSize = std::max(FontSize * 0.7f, FontSize * MaxW / TextW);
	const float FinalW = TextRender()->TextWidth(FontSize, aText);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.92f * Alpha);
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.45f * Alpha);
	TextRender()->Text(Width / 2.0f - FinalW / 2.0f, m_LyricsTop + 1.5f * Scale, FontSize, aText, -1.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
}

// ***** Lyrics (lrclib.net) ***** //

// "Song (Official Video) [HD]" -> "Song"
static void KingCleanTitle(char *pBuf, int Size, const char *pTitle)
{
	std::string Out;
	int Depth = 0;
	for(const char *p = pTitle; *p; p++)
	{
		if(*p == '(' || *p == '[')
			Depth++;
		else if((*p == ')' || *p == ']') && Depth > 0)
			Depth--;
		else if(Depth == 0)
			Out += *p;
	}
	str_copy(pBuf, Out.c_str(), Size);
	str_utf8_trim_right(pBuf);
	// leading spaces
	const char *pStart = str_utf8_skip_whitespaces(pBuf);
	if(pStart != pBuf)
		mem_move(pBuf, pStart, str_length(pStart) + 1);
}

// "Artist - Topic" / "ArtistVEVO" (YouTube channel names) -> "Artist"
static void KingCleanArtist(char *pBuf, int Size, const char *pArtist)
{
	str_copy(pBuf, pArtist, Size);
	if(str_endswith(pBuf, " - Topic"))
		pBuf[str_length(pBuf) - 8] = '\0';
	if(str_endswith(pBuf, "VEVO"))
		pBuf[str_length(pBuf) - 4] = '\0';
	str_utf8_trim_right(pBuf);
}

void CKing::UpdateLyrics(const SKingMediaInfo &Info)
{
	if(!Info.m_Valid || !Info.m_aTitle[0])
		return;
	const std::string Track = std::string(Info.m_aTitle) + "\n" + Info.m_aArtist;
	if(Track == m_LyricsTrack || Track == m_LyricsWanted)
		return;

	if(m_pLyricsTask)
	{
		m_pLyricsTask->Abort();
		m_pLyricsTask = nullptr;
	}
	m_vLyrics.clear();
	m_CurrentLyric = -1;
	m_LyricsTrack.clear();
	m_LyricsWanted = Track;
	m_LyricsAttempt = 0;

	char aTitle[256], aArtist[256];
	KingCleanTitle(aTitle, sizeof(aTitle), Info.m_aTitle);
	KingCleanArtist(aArtist, sizeof(aArtist), Info.m_aArtist);
	if(!aTitle[0])
		str_copy(aTitle, Info.m_aTitle);

	char aEscTitle[768], aEscArtist[768], aUrl[2048];
	EscapeUrl(aEscTitle, aTitle);
	EscapeUrl(aEscArtist, aArtist);
	if(aArtist[0])
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?track_name=%s&artist_name=%s", aEscTitle, aEscArtist);
	else
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?q=%s", aEscTitle);

	m_pLyricsTask = HttpGet(aUrl);
	m_pLyricsTask->HeaderString("User-Agent", "KING/" KING_VERSION " (https://github.com/" KING_UPDATE_REPO ")");
	m_pLyricsTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pLyricsTask->FailOnErrorStatus(false);
	m_pLyricsTask->MaxResponseSize(4 * 1024 * 1024);
	m_pLyricsTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pLyricsTask);
}

static bool KingParseLrcTime(const char *&p, float &Out)
{
	// [mm:ss.xx] or [mm:ss] or [mm:ss.xxx]
	if(*p != '[')
		return false;
	const char *q = p + 1;
	int Min = 0, Digits = 0;
	while(*q >= '0' && *q <= '9')
	{
		Min = Min * 10 + (*q - '0');
		q++;
		Digits++;
	}
	if(!Digits || *q != ':')
		return false;
	q++;
	int Sec = 0;
	Digits = 0;
	while(*q >= '0' && *q <= '9')
	{
		Sec = Sec * 10 + (*q - '0');
		q++;
		Digits++;
	}
	if(!Digits)
		return false;
	float Frac = 0.0f;
	if(*q == '.' || *q == ':')
	{
		q++;
		float Div = 10.0f;
		while(*q >= '0' && *q <= '9')
		{
			Frac += (*q - '0') / Div;
			Div *= 10.0f;
			q++;
		}
	}
	if(*q != ']')
		return false;
	Out = Min * 60.0f + Sec + Frac;
	p = q + 1;
	return true;
}

void CKing::FinishLyrics()
{
	std::shared_ptr<IHttpRequest> pTask = m_pLyricsTask;
	m_pLyricsTask = nullptr;
	const SKingMediaInfo Info = m_Media.Get();
	const std::string Track = std::string(Info.m_aTitle) + "\n" + Info.m_aArtist;
	if(Track != m_LyricsWanted)
		return; // the song changed meanwhile; UpdateLyrics() starts over

	const char *pSynced = nullptr;
	json_value *pJson = nullptr;
	if(pTask->State() == EHttpState::DONE && pTask->StatusCode() == 200)
		pJson = pTask->ResultJson();
	if(pJson && pJson->type == json_array)
	{
		// the result whose length is closest to what plays (if the player tells us)
		float BestDiff = 1e9f;
		for(int i = 0; i < json_array_length(pJson); i++)
		{
			const json_value &Entry = *json_array_get(pJson, i);
			const json_value &Synced = Entry["syncedLyrics"];
			if(Synced.type != json_string || !Synced.u.string.length)
				continue;
			const json_value &Duration = Entry["duration"];
			float Diff = 0.0f;
			if(Info.m_DurationSec > 0.0f && (Duration.type == json_double || Duration.type == json_integer))
				Diff = absolute((Duration.type == json_double ? (float)Duration.u.dbl : (float)Duration.u.integer) - Info.m_DurationSec);
			if(Diff < BestDiff)
			{
				BestDiff = Diff;
				pSynced = Synced.u.string.ptr;
			}
		}
		if(BestDiff > 10.0f)
			pSynced = nullptr; // a different version of the song, timings would be off
	}

	if(pSynced)
	{
		const char *p = pSynced;
		while(*p)
		{
			const char *pEnd = p;
			while(*pEnd && *pEnd != '\n')
				pEnd++;
			std::string Line(p, pEnd - p);
			if(!Line.empty() && Line.back() == '\r')
				Line.pop_back();

			const char *pLine = Line.c_str();
			std::vector<float> vTimes;
			float Time;
			while(KingParseLrcTime(pLine, Time))
				vTimes.push_back(Time);
			while(*pLine == ' ')
				pLine++;
			for(float T : vTimes)
				m_vLyrics.push_back({T, std::string(pLine)});
			p = *pEnd ? pEnd + 1 : pEnd;
		}
		std::stable_sort(m_vLyrics.begin(), m_vLyrics.end(), [](const SLyricLine &a, const SLyricLine &b) { return a.m_Time < b.m_Time; });
	}
	if(pJson)
		json_value_free(pJson);

	if(!m_vLyrics.empty() || m_LyricsAttempt >= 1)
	{
		m_LyricsTrack = m_LyricsWanted; // done (with or without lyrics)
		m_CurrentLyric = -1;
		return;
	}

	// second try: YouTube style "Artist - Title" in the title
	m_LyricsAttempt++;
	char aTitle[256];
	KingCleanTitle(aTitle, sizeof(aTitle), Info.m_aTitle);
	const char *pDash = str_find(aTitle, " - ");
	char aEsc[1024], aUrl[2048];
	if(pDash)
	{
		char aArtist[256];
		str_truncate(aArtist, sizeof(aArtist), aTitle, pDash - aTitle);
		char aEscArtist[768];
		EscapeUrl(aEscArtist, aArtist);
		EscapeUrl(aEsc, pDash + 3);
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?track_name=%s&artist_name=%s", aEsc, aEscArtist);
	}
	else
	{
		char aQuery[600];
		str_format(aQuery, sizeof(aQuery), "%s %s", aTitle, Info.m_aArtist);
		EscapeUrl(aEsc, aQuery);
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?q=%s", aEsc);
	}
	m_pLyricsTask = HttpGet(aUrl);
	m_pLyricsTask->HeaderString("User-Agent", "KING/" KING_VERSION " (https://github.com/" KING_UPDATE_REPO ")");
	m_pLyricsTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pLyricsTask->FailOnErrorStatus(false);
	m_pLyricsTask->MaxResponseSize(4 * 1024 * 1024);
	m_pLyricsTask->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pLyricsTask);
}

// ***** Player lookup ("Find Hours") ***** //

void CKing::LookupPlayer(const char *pName)
{
	if(!pName[0])
		return;
	for(auto *ppTask : {&m_pLookupStats, &m_pLookupDDNet})
	{
		if(*ppTask)
		{
			(*ppTask)->Abort();
			*ppTask = nullptr;
		}
	}
	str_copy(m_aLookupName, pName);

	char aEscaped[256], aUrl[512];
	EscapeUrl(aEscaped, pName);
	const auto Start = [&](const char *pUrl) {
		std::shared_ptr<IHttpRequest> pTask = HttpGet(pUrl);
		pTask->HeaderString("User-Agent", "KING/" KING_VERSION " (https://github.com/" KING_UPDATE_REPO ")");
		pTask->HeaderString("Accept", "application/json");
		pTask->Timeout(CTimeout{10000, 0, 500, 10});
		pTask->FailOnErrorStatus(false);
		pTask->MaxResponseSize(8 * 1024 * 1024);
		pTask->LogProgress(HTTPLOG::FAILURE);
		Http()->Run(pTask);
		return pTask;
	};
	str_format(aUrl, sizeof(aUrl), "https://ddstats.tw/player/json?player=%s", aEscaped);
	m_pLookupStats = Start(aUrl);
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEscaped);
	m_pLookupDDNet = Start(aUrl);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Looking up %s...", pName);
	GameClient()->m_Chat.Echo(aBuf);
}

static json_value *KingJsonResult(const std::shared_ptr<IHttpRequest> &pTask)
{
	if(!pTask || pTask->State() != EHttpState::DONE || pTask->StatusCode() != 200)
		return nullptr;
	json_value *pJson = pTask->ResultJson(); // nullptr for an html challenge page etc.
	if(pJson && pJson->type != json_object)
	{
		json_value_free(pJson);
		return nullptr;
	}
	return pJson;
}

static bool KingJsonNumber(const json_value &Value, double &Out)
{
	if(Value.type == json_integer)
		Out = (double)Value.u.integer;
	else if(Value.type == json_double)
		Out = Value.u.dbl;
	else
		return false;
	return true;
}

void CKing::FinishLookup()
{
	json_value *pStats = KingJsonResult(m_pLookupStats);
	json_value *pDDNet = KingJsonResult(m_pLookupDDNet);
	m_pLookupStats = nullptr;
	m_pLookupDDNet = nullptr;

	// total playtime: ddstats.tw follows every server, ddnet.org only knows the last year
	char aHours[128] = "";
	double Value;
	if(pStats && KingJsonNumber((*pStats)["general_activity"]["total_seconds_played"], Value) && Value > 0.0)
	{
		const json_value &Since = (*pStats)["general_activity"]["start_of_playtime"];
		if(Since.type == json_string && str_length(Since.u.string.ptr) >= 4)
		{
			char aYear[8];
			str_truncate(aYear, sizeof(aYear), Since.u.string.ptr, 4);
			str_format(aHours, sizeof(aHours), "%d hours played (since %s)", round_to_int(Value / 3600.0), aYear);
		}
		else
			str_format(aHours, sizeof(aHours), "%d hours played", round_to_int(Value / 3600.0));
	}
	else if(pDDNet && KingJsonNumber((*pDDNet)["hours_played_past_365_days"], Value))
		str_format(aHours, sizeof(aHours), "%d hours played in the last year (total unknown)", round_to_int(Value));

	// points: ddnet.org is the official source
	char aPoints[128] = "";
	const json_value *pPoints = pDDNet ? &(*pDDNet)["points"] : nullptr;
	double Points, Total, Rank;
	if(pPoints && pPoints->type == json_object && KingJsonNumber((*pPoints)["points"], Points))
	{
		const bool HaveTotal = KingJsonNumber((*pPoints)["total"], Total);
		const bool HaveRank = KingJsonNumber((*pPoints)["rank"], Rank);
		if(HaveRank && HaveTotal)
			str_format(aPoints, sizeof(aPoints), "%d / %d points (rank #%d)", round_to_int(Points), round_to_int(Total), round_to_int(Rank));
		else
			str_format(aPoints, sizeof(aPoints), "%d points", round_to_int(Points));
	}
	else if(pStats && KingJsonNumber((*pStats)["profile"]["points"], Points))
		str_format(aPoints, sizeof(aPoints), "%d points", round_to_int(Points));

	char aBuf[512];
	if(aHours[0] && aPoints[0])
		str_format(aBuf, sizeof(aBuf), "%s: %s, %s", m_aLookupName, aHours, aPoints);
	else if(aHours[0] || aPoints[0])
		str_format(aBuf, sizeof(aBuf), "%s: %s", m_aLookupName, aHours[0] ? aHours : aPoints);
	else if(pStats || pDDNet)
		str_format(aBuf, sizeof(aBuf), "%s: no stats found (new player or a different name)", m_aLookupName);
	else
		str_format(aBuf, sizeof(aBuf), "%s: stats sites did not answer, try again later", m_aLookupName);
	GameClient()->m_Chat.Echo(aBuf);

	if(pStats)
		json_value_free(pStats);
	if(pDDNet)
		json_value_free(pDDNet);
}

// ***** Settings autosave ***** //

uint64_t CKing::ConfigHash() const
{
	// FNV-1a over all settings, without the values KING itself changes every frame
	CConfig &Config = g_Config;
	const int aSaved[4] = {Config.m_TcFastInput, Config.m_TcFastInputAmount, Config.m_TcFastInputOthers, Config.m_TcFastInputRepredictHook};
	Config.m_TcFastInput = Config.m_TcFastInputAmount = Config.m_TcFastInputOthers = Config.m_TcFastInputRepredictHook = 0;
	uint64_t Hash = 14695981039346656037ull;
	const unsigned char *pBytes = reinterpret_cast<const unsigned char *>(&Config);
	for(size_t i = 0; i < sizeof(CConfig); i++)
	{
		Hash ^= pBytes[i];
		Hash *= 1099511628211ull;
	}
	Config.m_TcFastInput = aSaved[0];
	Config.m_TcFastInputAmount = aSaved[1];
	Config.m_TcFastInputOthers = aSaved[2];
	Config.m_TcFastInputRepredictHook = aSaved[3];
	return Hash;
}

void CKing::AutoSaveConfig()
{
	// The client normally writes settings only when it closes cleanly; a crash or a
	// killed process lost everything changed in that session. Save shortly after a
	// change instead, but never while you play (writing the file can take a moment).
	const int64_t Now = time_get();
	const int64_t Freq = time_freq();
	if(Now - m_LastConfigCheck < Freq)
		return;
	m_LastConfigCheck = Now;

	const uint64_t Hash = ConfigHash();
	if(!m_ConfigHashValid)
	{
		m_ConfigHash = Hash;
		m_ConfigHashValid = true;
		return;
	}
	if(Hash != m_ConfigHash)
	{
		m_ConfigHash = Hash;
		m_ConfigDirty = true;
		m_ConfigDirtySince = Now;
	}
	const bool Playing = Client()->State() == IClient::STATE_ONLINE && !GameClient()->m_Menus.IsActive();
	if(m_ConfigDirty && !Playing && Now - m_ConfigDirtySince > 2 * Freq && Now - m_LastConfigSave > 10 * Freq)
	{
		m_ConfigDirty = false;
		m_LastConfigSave = Now;
		if(!ConfigManager()->Save())
			log_error("king", "saving settings failed");
	}
}

// ***** Process priorities ***** //

void CKing::ApplyPriorities(bool Force)
{
	if(Force || g_Config.m_KgHighPriority != m_AppliedHighPriority)
	{
		// only touch the priority once the option was used, so a default install changes nothing
		if(g_Config.m_KgHighPriority || m_AppliedHighPriority == 1)
			KingSetHighPriority(g_Config.m_KgHighPriority != 0);
		m_AppliedHighPriority = g_Config.m_KgHighPriority;
	}
	if(Force || g_Config.m_KgLowerDiscord != m_AppliedLowerDiscord)
	{
		if(g_Config.m_KgLowerDiscord || m_AppliedLowerDiscord == 1)
			KingSetDiscordLowPriority(g_Config.m_KgLowerDiscord != 0);
		m_AppliedLowerDiscord = g_Config.m_KgLowerDiscord;
	}
}

// ***** Optimizer: FPS fog ***** //

void CKing::RenderFpsFog()
{
	if(!g_Config.m_KgOptimizer || !g_Config.m_KgOptFpsFog)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	// the map renderer culls tiles beyond the fog distance (whole tiles around the camera);
	// fog covers everything outside a box that always lies inside the rendered tiles
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const CScreenRect World = Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->WorldAspect(), GameClient()->m_Camera.m_Zoom);
	const float Radius = g_Config.m_KgOptFpsFogRadius * 32.0f;
	const float Band = std::min(6.0f * 32.0f, Radius * 0.5f); // soft edge
	const float L = Center.x - Radius, R = Center.x + Radius, T = Center.y - Radius, B = Center.y + Radius;
	const float X0 = World.m_TopLeft.x, Y0 = World.m_TopLeft.y, X1 = World.m_BottomRight.x, Y1 = World.m_BottomRight.y;
	if(L + Band <= X0 && R - Band >= X1 && T + Band <= Y0 && B - Band >= Y1)
		return; // the fog starts outside the screen on every side: nothing to draw

	Graphics()->MapScreen(World);
	Graphics()->TextureClear();
	const ColorRGBA Fog(0.035f, 0.035f, 0.045f, 1.0f);
	const ColorRGBA Clear = Fog.WithAlpha(0.0f);
	const int None = IGraphics::CORNER_NONE;
	// solid outside
	if(T > Y0)
		Graphics()->DrawRect(X0, Y0, X1 - X0, T - Y0, Fog, None, 0.0f);
	if(B < Y1)
		Graphics()->DrawRect(X0, B, X1 - X0, Y1 - B, Fog, None, 0.0f);
	if(L > X0)
		Graphics()->DrawRect(X0, T, L - X0, B - T, Fog, None, 0.0f);
	if(R < X1)
		Graphics()->DrawRect(R, T, X1 - R, B - T, Fog, None, 0.0f);
	// soft edges inside the box
	Graphics()->DrawRect4(L, T, R - L, Band, Fog, Fog, Clear, Clear, None, 0.0f);
	Graphics()->DrawRect4(L, B - Band, R - L, Band, Clear, Clear, Fog, Fog, None, 0.0f);
	Graphics()->DrawRect4(L, T, Band, B - T, Fog, Clear, Fog, Clear, None, 0.0f);
	Graphics()->DrawRect4(R - Band, T, Band, B - T, Clear, Fog, Clear, Fog, None, 0.0f);
}

// ***** KING crown ***** //

static int KingBadgeApply(int Color, uint32_t Bits)
{
	return (Color & ~CKing::BADGE_COLOR_MASK) | (int)((Bits & 3) << 16) | (int)(((Bits >> 2) & 7) << 8) | (int)((Bits >> 5) & 7);
}

static uint32_t KingBadgeRead(int Color)
{
	return (uint32_t)((Color >> 16) & 3) | (uint32_t)(((Color >> 8) & 7) << 2) | (uint32_t)((Color & 7) << 5);
}

uint32_t CKing::BadgeSignature(const char *pName, int Body, int Feet)
{
	// FNV-1a; the name without whitespace/control bytes (servers clean those up)
	uint32_t Hash = 2166136261u;
	auto Add = [&Hash](uint8_t Byte) {
		Hash ^= Byte;
		Hash *= 16777619u;
	};
	for(const char *p = "KING-crown-1"; *p; p++)
		Add((uint8_t)*p);
	for(const char *p = pName; *p; p++)
		if((uint8_t)*p > 32)
			Add((uint8_t)*p);
	for(int Color : {Body & ~BADGE_COLOR_MASK, Feet & ~BADGE_COLOR_MASK})
		for(int i = 0; i < 4; i++)
			Add((uint8_t)(Color >> (i * 8)));
	return (Hash ^ (Hash >> 16)) & 0xFFFF;
}

int CKing::MarkColorBody(int Color, int Feet, const char *pName)
{
	if(!g_Config.m_KgBadgeShare)
		return Color;
	return KingBadgeApply(Color, BadgeSignature(pName, Color, Feet) & 0xFF);
}

int CKing::MarkColorFeet(int Body, int Color, const char *pName)
{
	if(!g_Config.m_KgBadgeShare)
		return Color;
	return KingBadgeApply(Color, BadgeSignature(pName, Body, Color) >> 8);
}

bool CKing::IsKingPlayer(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
	if(!Client.m_Active || !Client.m_aName[0])
		return false;
	const uint32_t Signature = BadgeSignature(Client.m_aName, Client.m_ColorBody, Client.m_ColorFeet);
	return KingBadgeRead(Client.m_ColorBody) == (Signature & 0xFF) && KingBadgeRead(Client.m_ColorFeet) == (Signature >> 8);
}

void CKing::RenderCrown(float X, float CenterY, float Size, float Alpha) const
{
	if(!m_CrownTexture.IsValid())
		return;
	Graphics()->TextureSet(m_CrownTexture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(CrownColor().WithAlpha(Alpha));
	const IGraphics::CQuadItem Item(X, CenterY - Size / 2.0f, Size, Size);
	Graphics()->QuadsDrawTL(&Item, 1);
	Graphics()->QuadsEnd();
	Graphics()->TextureClear();
}

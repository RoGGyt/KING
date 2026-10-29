/* KING client: input modes, KoG preset, network HUD and update check. */
#include "king.h"

#include <base/math.h>
#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/process.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/version.h>

#include <algorithm>
#include <cstdio>
#include <tuple>

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
	}
	return "?";
}

void CKing::OnConsoleInit()
{
	Console()->Register("kg_kog_preset", "", CFGFLAG_CLIENT, ConKogPreset, this, "Apply KING recommended settings for KoG (antiping, low latency rendering, KING input)");
	Console()->Register("kg_check_update", "", CFGFLAG_CLIENT, ConCheckUpdate, this, "Check GitHub for a new KING release");
}

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
	ReadLastInstallResult();
	if(g_Config.m_KgUpdateCheck)
		CheckForUpdates();
}

void CKing::OnShutdown()
{
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
static constexpr const char *KING_UPDATE_SCRIPT = "king_update/update.ps1";
static constexpr const char *KING_UPDATE_LOG = "king_update/update.log";
static constexpr const char *KING_EXE_NAME = "KING.exe";

// Runs after KING has quit: waits for the process to exit, unpacks the release
// zip over the install folder (robocopy retries locked files), starts KING again.
// Config, binds and skins live in %APPDATA%\DDNet and are not touched.
static const char gs_aUpdateScript[] = R"PS(param([int]$ProcId, [string]$Zip, [string]$Dest, [string]$Exe, [string]$Log)
$ErrorActionPreference = 'Stop'
$ok = $false
try {
    Wait-Process -Id $ProcId -Timeout 60 -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
    $tmp = Join-Path (Split-Path -Parent $Zip) 'extract'
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
    Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force
    $src = $tmp
    $items = @(Get-ChildItem -LiteralPath $tmp)
    if ($items.Count -eq 1 -and $items[0].PSIsContainer) { $src = $items[0].FullName }
    if (-not (Test-Path -LiteralPath (Join-Path $src 'KING.exe'))) { throw 'KING.exe not found in update zip' }
    & robocopy.exe $src $Dest /E /R:30 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "copy failed (robocopy $LASTEXITCODE), is the folder write protected?" }
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue
    $ok = $true
    Set-Content -LiteralPath $Log -Value ('ok ' + (Get-Date -Format s)) -Encoding ASCII
} catch {
    Set-Content -LiteralPath $Log -Value ('fail ' + $_.Exception.Message) -Encoding ASCII
}
Start-Process -FilePath $Exe -WorkingDirectory $Dest
)PS";

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

bool CKing::LaunchInstaller()
{
#if defined(CONF_FAMILY_WINDOWS)
	IOHANDLE File = Storage()->OpenFile(KING_UPDATE_SCRIPT, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		str_copy(m_aInstallError, "Could not write the update script.");
		return false;
	}
	io_write(File, gs_aUpdateScript, sizeof(gs_aUpdateScript) - 1);
	io_close(File);

	char aScript[IO_MAX_PATH_LENGTH];
	char aZip[IO_MAX_PATH_LENGTH];
	char aLog[IO_MAX_PATH_LENGTH];
	char aExe[IO_MAX_PATH_LENGTH];
	char aDest[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, KING_UPDATE_SCRIPT, aScript, sizeof(aScript));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, KING_UPDATE_ZIP, aZip, sizeof(aZip));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, KING_UPDATE_LOG, aLog, sizeof(aLog));
	Storage()->GetBinaryPathAbsolute(KING_EXE_NAME, aExe, sizeof(aExe));
	str_copy(aDest, aExe);
	if(fs_parent_dir(aDest) != 0)
	{
		str_copy(m_aInstallError, "Could not find the KING install folder.");
		return false;
	}

	char aPid[16];
	str_format(aPid, sizeof(aPid), "%d", process_id());
	const char *apArgs[] = {
		"-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden",
		"-File", aScript,
		"-ProcId", aPid,
		"-Zip", aZip,
		"-Dest", aDest,
		"-Exe", aExe,
		"-Log", aLog};
	PROCESS Process = process_execute("powershell.exe", EShellExecuteWindowState::BACKGROUND, apArgs, std::size(apArgs));
	if(Process == INVALID_PROCESS)
	{
		str_copy(m_aInstallError, "Could not start PowerShell for the update.");
		return false;
	}
	log_info("king", "update downloaded, installing into %s", aDest);
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
	// Input: KING mode = classic one tick fast input + adaptive margin
	g_Config.m_KgInputMode = INPUT_MODE_KING;
	g_Config.m_KgFastInputAmount = 20;
	g_Config.m_KgAutoMargin = 1;
	g_Config.m_KgAutoMarginMin = 4;
	g_Config.m_KgAutoMarginMax = 40;
	g_Config.m_KgAutoMarginSafety = 2;

	// Antiping: predict others so hooks/collisions with other tees line up
	g_Config.m_ClPredict = 1;
	g_Config.m_ClAntiPing = 1;
	g_Config.m_ClAntiPingPlayers = 1;
	g_Config.m_ClAntiPingPreInput = 1;
	g_Config.m_ClAntiPingSmooth = 0;
	g_Config.m_TcAntiPingImproved = 0;
	g_Config.m_ClPredictionMargin = 10; // fallback when auto margin has no data yet

	// Rendering latency: no vsync, uncapped update rate
	g_Config.m_GfxVsync = 0;
	g_Config.m_ClRefreshRate = 0;

	g_Config.m_KgNetHud = 1;
}

void CKing::ApplyInputMode()
{
	switch(g_Config.m_KgInputMode)
	{
	case INPUT_MODE_CLASSIC:
		// TClient fast input as it worked before the Feb 2026 "adjustable fast input"
		// rewrite: old tc_fast_input 1-5 = whole ticks ahead. A multiple of 20 ms
		// makes the new code take the exact same whole-tick path (no sub-tick part).
		g_Config.m_TcFastInput = 1;
		g_Config.m_TcFastInputAmount = std::clamp(g_Config.m_KgClassicTicks, 1, 5) * 20;
		g_Config.m_TcFastInputOthers = 0;
		g_Config.m_TcFastInputRepredictHook = 0;
		break;
	case INPUT_MODE_KING:
	case INPUT_MODE_SMOOTH: // smoothing itself is applied when rendering our tee (gameclient.cpp)
		g_Config.m_TcFastInput = 1;
		g_Config.m_TcFastInputAmount = g_Config.m_KgFastInputAmount;
		g_Config.m_TcFastInputOthers = 0;
		g_Config.m_TcFastInputRepredictHook = 0;
		g_Config.m_KgAutoMargin = 1;
		break;
	case INPUT_MODE_DELTA:
		// 0.01 tick units -> ms (1 tick = 20 ms). 0 turns fast input off.
		g_Config.m_TcFastInput = g_Config.m_KgDeltaAmount > 0;
		g_Config.m_TcFastInputAmount = std::clamp(round_to_int(g_Config.m_KgDeltaAmount * 0.2f), 1, 100);
		g_Config.m_TcFastInputOthers = g_Config.m_KgDeltaOthers;
		g_Config.m_TcFastInputRepredictHook = 0;
		break;
	case INPUT_MODE_PING_COMP:
	{
		// show our tee a share of the round trip ahead; only move in 2 ms steps so it does not flicker with jitter
		const SKingNetStats Stats = Client()->KingNetStats();
		if(Stats.m_Valid)
		{
			const int Wanted = std::clamp(round_to_int(Stats.m_RttMs * g_Config.m_KgPingComp / 100.0f), 1, 100);
			if(absolute(Wanted - m_PingCompAmount) >= 2)
				m_PingCompAmount = Wanted;
		}
		g_Config.m_TcFastInput = 1;
		g_Config.m_TcFastInputAmount = m_PingCompAmount;
		g_Config.m_TcFastInputOthers = 0;
		g_Config.m_TcFastInputRepredictHook = 0;
		g_Config.m_KgAutoMargin = 1;
		break;
	}
	default:
		break;
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
	case 6: return g_Config.m_KgAspectCustom / 100.0f;
	}
	return 0.0f; // native
}

void CKing::OnUpdate()
{
	ApplyInputMode();

	// stretched aspect ratio only for the game world, menus stay native
	const bool InGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	Graphics()->SetWorldAspect(InGame ? AspectRatioValue(g_Config.m_KgAspectRatio) : 0.0f);
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

	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(g_Config.m_KgNetHud)
		RenderNetHud();
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
	if(Stats.m_Valid)
		str_format(aLine1, sizeof(aLine1), "ping %d ms   jitter %.1f ms", round_to_int(Stats.m_RttMs), Stats.m_JitterMs);
	else
		str_copy(aLine1, "ping -- ms   measuring...");

	if(Stats.m_MarginLocked)
		str_format(aLine2, sizeof(aLine2), "margin %d ms (locked by server)", Stats.m_MarginMs);
	else
		str_format(aLine2, sizeof(aLine2), "margin %d ms %s   late %d", Stats.m_MarginMs, Stats.m_MarginAuto ? "auto" : "fixed", Stats.m_LateInputs);

	if(g_Config.m_TcFastInput && g_Config.m_KgInputMode == INPUT_MODE_CLASSIC)
		str_format(aLine3, sizeof(aLine3), "%s input   %d tick%s (%d ms)", InputModeName(g_Config.m_KgInputMode), g_Config.m_KgClassicTicks, g_Config.m_KgClassicTicks == 1 ? "" : "s", g_Config.m_TcFastInputAmount);
	else if(g_Config.m_TcFastInput && g_Config.m_KgInputMode == INPUT_MODE_DELTA)
		str_format(aLine3, sizeof(aLine3), "%s input   %d.%02d ticks (%d ms)", InputModeName(g_Config.m_KgInputMode), g_Config.m_KgDeltaAmount / 100, g_Config.m_KgDeltaAmount % 100, g_Config.m_TcFastInputAmount);
	else if(g_Config.m_TcFastInput && g_Config.m_KgInputMode == INPUT_MODE_SMOOTH)
		str_format(aLine3, sizeof(aLine3), "%s input   fast %d ms, smooth %d ms", InputModeName(g_Config.m_KgInputMode), g_Config.m_TcFastInputAmount, g_Config.m_KgSmoothMs);
	else if(g_Config.m_TcFastInput)
		str_format(aLine3, sizeof(aLine3), "%s input   fast %d ms", InputModeName(g_Config.m_KgInputMode), g_Config.m_TcFastInputAmount);
	else
		str_format(aLine3, sizeof(aLine3), "%s input   fast off", InputModeName(g_Config.m_KgInputMode));

	char aLine4[96] = "";
	{
		const CControls &Controls = GameClient()->m_Controls;
		if(g_Config.m_KgSnapTap)
			str_append(aLine4, "SnapTap  ");
		if(Controls.King45Active())
			str_append(aLine4, "45deg  ");
		if(Controls.KingSmallSensActive())
			str_append(aLine4, "SmallSens");
	}
	const bool HasLine4 = aLine4[0] != '\0';
	const float TextW = std::max({TextRender()->TextWidth(FontSize, aLine4), TextRender()->TextWidth(FontSize, aLine1), TextRender()->TextWidth(FontSize, aLine2), TextRender()->TextWidth(FontSize, aLine3), TextRender()->TextWidth(FontSize, "KING")});
	const float BoxW = TextW + Pad * 2.0f;
	const float BoxH = LineHeight * (HasLine4 ? 5.0f : 4.0f) + Pad * 2.0f - 1.5f;

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
	if(HasLine4)
	{
		Ty += LineHeight;
		TextRender()->TextColor(0.55f, 0.85f, 1.0f, 1.0f);
		TextRender()->Text(x + Pad, Ty, FontSize, aLine4);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

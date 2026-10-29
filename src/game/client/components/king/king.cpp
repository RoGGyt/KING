/* KING client: input modes, KoG preset, network HUD and update check. */
#include "king.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
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
	case INPUT_MODE_CLASSIC: return "Classic TClient";
	case INPUT_MODE_KING: return "KING";
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
		g_Config.m_TcFastInput = 1;
		g_Config.m_TcFastInputAmount = g_Config.m_KgFastInputAmount;
		g_Config.m_TcFastInputOthers = 0;
		g_Config.m_TcFastInputRepredictHook = 0;
		g_Config.m_KgAutoMargin = 1;
		break;
	default:
		break;
	}
}

void CKing::OnUpdate()
{
	ApplyInputMode();
}

void CKing::OnRender()
{
	if(m_pUpdateTask && m_pUpdateTask->Done())
	{
		FinishUpdateCheck();
		m_pUpdateTask = nullptr;
	}

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
	else if(g_Config.m_TcFastInput)
		str_format(aLine3, sizeof(aLine3), "%s input   fast %d ms", InputModeName(g_Config.m_KgInputMode), g_Config.m_TcFastInputAmount);
	else
		str_format(aLine3, sizeof(aLine3), "%s input   fast off", InputModeName(g_Config.m_KgInputMode));

	const float TextW = std::max({TextRender()->TextWidth(FontSize, aLine1), TextRender()->TextWidth(FontSize, aLine2), TextRender()->TextWidth(FontSize, aLine3), TextRender()->TextWidth(FontSize, "KING")});
	const float BoxW = TextW + Pad * 2.0f;
	const float BoxH = LineHeight * 4.0f + Pad * 2.0f - 1.5f;

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

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

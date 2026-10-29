/* KING client: input modes, KoG preset, network HUD, lag log, music player and update check. */
#ifndef GAME_CLIENT_COMPONENTS_KING_KING_H
#define GAME_CLIENT_COMPONENTS_KING_KING_H

#include <base/color.h>
#include <base/types.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/http.h>

#include "king_media.h"

#include <game/client/component.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

class CKing : public CComponent
{
public:
	enum EInputMode
	{
		INPUT_MODE_CUSTOM = 0, // leave TClient settings alone
		INPUT_MODE_CLASSIC, // old TClient fast input: whole ticks ahead, manual margin
		INPUT_MODE_KING, // one tick fast input + safe auto margin + light correction smoothing
		INPUT_MODE_DELTA, // fixed sub-tick offset in 0.01 ticks
		INPUT_MODE_PING_COMP, // offset follows a share of the measured ping
		INPUT_MODE_SMOOTH, // KING + longer, adjustable correction smoothing
		INPUT_MODE_BEST, // offset + latency comp + smoothing + interpolation, auto or custom
		NUM_INPUT_MODES,
	};

	enum ESmoothCurve
	{
		SMOOTH_CURVE_LINEAR = 0,
		SMOOTH_CURVE_CUBIC,
		SMOOTH_CURVE_SMOOTH,
	};

	enum EUpdateState
	{
		UPDATE_IDLE = 0,
		UPDATE_CHECKING,
		UPDATE_LATEST,
		UPDATE_AVAILABLE,
		UPDATE_FAILED,
	};

	enum EAspectApply
	{
		ASPECT_APPLY_FULL = 0, // world, HUD and in-game menus
		ASPECT_APPLY_WORLD, // only the game world
	};

	enum EMusicColor
	{
		MUSIC_COLOR_COVER = 0,
		MUSIC_COLOR_KING,
		MUSIC_COLOR_WHITE,
	};

	enum EMusicVisualizer
	{
		MUSIC_VIS_OFF = 0,
		MUSIC_VIS_BOTTOM,
		MUSIC_VIS_CENTER,
	};

	// corrections bigger than this are real teleports (tele tiles, rescue, spawn): never smoothed
	static constexpr float SMOOTH_SNAP_DISTANCE = 64.0f;
	static constexpr float KING_SMOOTH_MS = 40.0f;

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnShutdown() override;
	void OnNewSnapshot() override;
	void OnStateChange(int NewState, int OldState) override;

	// Sets antiping/render options that matter for KoG. Does not touch binds or visuals.
	void ApplyKogPreset();

	static const char *InputModeName(int Mode);
	static float AspectRatioValue(int Setting); // 0 = native
	static bool ModeUsesAutoMargin(int Mode) { return Mode == INPUT_MODE_KING || Mode == INPUT_MODE_PING_COMP || Mode == INPUT_MODE_SMOOTH || Mode == INPUT_MODE_BEST; }
	// correction smoothing of our own tee; 0 = off
	float SmoothDurationMs() const;
	int SmoothCurve() const;
	static float SmoothEase(float T, int Curve);
	int BestFastMs() const { return m_BestFastMs; }
	int BestSmoothMs() const { return m_BestSmoothMs; }
	int PingCompAmount() const { return m_PingCompAmount; }

	// Lag monitor: gameclient reports every server correction of our tee
	void OnLocalCorrection(float Distance, int Tick);
	struct SLagStats
	{
		int m_Corrections = 0; // >= 4 units
		int m_BigCorrections = 0; // >= 24 units
		float m_MaxCorrection = 0.0f;
		int m_Stutters = 0; // frames > 50 ms
		int m_SnapGaps = 0; // no snapshot for > 150 ms
		int m_LateInputs = 0;
		int m_PredResets = 0;
	};
	const SLagStats &LagStats() const { return m_LagStats; }

	// Optimizer FPS fog: darkens everything beyond the culled area (called before the HUD)
	void RenderFpsFog();

	// "Find Hours" in the player menu: total playtime (ddstats.tw) and points (ddnet.org), printed to chat
	void LookupPlayer(const char *pName);

	// Updates (GitHub releases of KING_UPDATE_REPO)
	void CheckForUpdates();
	EUpdateState UpdateState() const { return m_UpdateState; }
	bool UpdateAvailable() const { return m_UpdateState == UPDATE_AVAILABLE; }
	const char *LatestVersion() const { return m_aLatestVersion; }
	const char *UpdateUrl() const { return m_aUpdateUrl; }
	void OpenUpdatePage();

	// One-click install: download the release zip, quit, a helper script replaces
	// the files and starts KING again. Windows only, needs a -win64.zip asset.
	bool CanInstallUpdate() const;
	void InstallUpdate();
	bool Installing() const { return m_pDownloadTask != nullptr; }
	float DownloadProgress() const;
	const char *InstallError() const { return m_aInstallError; }

	// Music player (system "now playing"): the race timer sits in a pill with the
	// cover on the left and a visualizer on the right.
	CKingMedia m_Media;
	bool MusicPillVisible();
	// Draws the pill centered on CenterX with its top at TopY. Unit scales everything
	// (1 = HUD units of a 300 high screen). Returns the pill height.
	float RenderMusicPill(float CenterX, float TopY, float Unit, const char *pTime, ColorRGBA TimeColor, bool Hud);
	// HUD layout around the pill (HUD units, valid for the current frame only)
	bool PillRenderedThisFrame() const { return m_PillRenderedThisFrame; }
	float PillBottom() const { return m_PillBottom; }
	float PillRight() const { return m_PillRight; }
	// HUD text placed under the pill: lyrics go below it
	void ReserveTopCenter(float Bottom)
	{
		if(m_PillRenderedThisFrame)
			m_LyricsTop = std::max(m_LyricsTop, Bottom);
	}
	void ResetMusicSettings();

private:
	void ApplyInputMode();
	void UpdateBestInput();
	void SetFastInput(float WantedMs, bool Ramp, bool Others);
	void RenderNetHud();
	void RenderLyrics();
	void FinishUpdateCheck();
	void FinishDownload();
	bool LaunchInstaller();
	void ReadLastInstallResult();

	// lag log
	void LagLogOpen();
	void LagLogClose();
	void LagLog(const char *pFormat, ...)
#if defined(__GNUC__) || defined(__clang__)
		__attribute__((format(printf, 2, 3)))
#endif
		;
	void UpdateLagMonitor();

	// music
	void UpdateCoverTexture();
	void UpdateLyrics(const SKingMediaInfo &Info);
	void FinishLyrics();
	ColorRGBA MusicAccent(const SKingMediaInfo &Info) const;

	int m_PingCompAmount = 20;
	int m_PingCompTarget = 20;
	int m_BestFastMs = 20;
	int m_BestTarget = 20;
	int m_BestSmoothMs = 50;
	int m_BestCurve = SMOOTH_CURVE_SMOOTH;
	float m_AppliedFastMs = -1.0f;
	int64_t m_LastFastUpdate = 0;
	int m_LastAppliedMode = -1;

	std::shared_ptr<IHttpRequest> m_pUpdateTask = nullptr;
	EUpdateState m_UpdateState = UPDATE_IDLE;
	char m_aLatestVersion[32] = "";
	char m_aUpdateUrl[512] = "";
	char m_aAssetUrl[512] = ""; // direct -win64.zip download
	std::shared_ptr<IHttpRequest> m_pDownloadTask = nullptr;
	char m_aInstallError[256] = "";

	// lag monitor
	SLagStats m_LagStats;
	IOHANDLE m_LagLogFile = nullptr;
	int64_t m_OnlineSince = 0;
	int64_t m_LastSnapshotTime = 0;
	int64_t m_LastStutterTime = 0;
	int64_t m_LastGapTime = 0;
	int64_t m_LastCorrectionLog = 0;
	float m_CorrectionLogBudget = 10.0f;
	int m_CorrectionsNotLogged = 0;
	int64_t m_LastLogFlush = 0;
	int m_SeenLateInputs = 0;
	int m_SeenPredResets = 0;
	int64_t m_LastSummary = 0;
	float m_MinFrameFps = 0.0f;

	// music pill
	IGraphics::CTextureHandle m_CoverTexture;
	int m_CoverSerial = -1;
	bool m_HasCoverTexture = false;
	std::array<float, 24> m_aBarLevels{};
	float m_PillAlpha = 0.0f;
	bool m_PillRenderedThisFrame = false;
	float m_PillBottom = 0.0f;
	float m_PillRight = 0.0f;
	float m_LyricsTop = 0.0f;
	float m_PillUnit = 1.0f;

	// lyrics (lrclib.net, synced lyrics)
	struct SLyricLine
	{
		float m_Time;
		std::string m_Text;
	};
	std::vector<SLyricLine> m_vLyrics;
	std::string m_LyricsTrack; // "title\nartist" the lyrics belong to
	std::string m_LyricsWanted; // track we fetch for
	std::shared_ptr<IHttpRequest> m_pLyricsTask = nullptr;
	int m_LyricsAttempt = 0;
	int m_CurrentLyric = -1;
	float m_LyricAlpha = 0.0f;
	int64_t m_TrackChangedTime = 0;
	std::string m_LastTrack;

	// settings are saved right after they change (not only on a clean exit)
	void AutoSaveConfig();
	uint64_t ConfigHash() const;
	int64_t m_LastConfigCheck = 0;
	int64_t m_ConfigDirtySince = 0;
	int64_t m_LastConfigSave = 0;
	uint64_t m_ConfigHash = 0;
	bool m_ConfigHashValid = false;
	bool m_ConfigDirty = false;

	// process priorities (Windows)
	void ApplyPriorities(bool Force);
	int m_AppliedHighPriority = -1;
	int m_AppliedLowerDiscord = -1;

	// player lookup
	void FinishLookup();
	std::shared_ptr<IHttpRequest> m_pLookupStats = nullptr; // ddstats.tw
	std::shared_ptr<IHttpRequest> m_pLookupDDNet = nullptr; // ddnet.org
	char m_aLookupName[64] = "";

	static void ConKogPreset(IConsole::IResult *pResult, void *pUserData);
	static void ConCheckUpdate(IConsole::IResult *pResult, void *pUserData);
	static void ConMusicToggle(IConsole::IResult *pResult, void *pUserData);
	static void ConMusicNext(IConsole::IResult *pResult, void *pUserData);
	static void ConMusicPrev(IConsole::IResult *pResult, void *pUserData);
	static void ConLagStats(IConsole::IResult *pResult, void *pUserData);
};

#endif

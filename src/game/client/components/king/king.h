/* KING client: input modes, KoG preset, network HUD and update check. */
#ifndef GAME_CLIENT_COMPONENTS_KING_KING_H
#define GAME_CLIENT_COMPONENTS_KING_KING_H

#include <engine/console.h>
#include <engine/http.h>

#include "king_media.h"

#include <game/client/component.h>

#include <memory>

class CKing : public CComponent
{
public:
	enum EInputMode
	{
		INPUT_MODE_CUSTOM = 0, // leave TClient settings alone
		INPUT_MODE_CLASSIC, // old TClient fast input: exactly one tick ahead
		INPUT_MODE_KING, // classic input + adaptive prediction margin
		INPUT_MODE_DELTA, // fixed sub-tick offset in 0.01 ticks
		INPUT_MODE_PING_COMP, // offset follows a share of the measured ping
		INPUT_MODE_SMOOTH, // KING + blend out server corrections of the own tee
		NUM_INPUT_MODES,
	};

	enum EUpdateState
	{
		UPDATE_IDLE = 0,
		UPDATE_CHECKING,
		UPDATE_LATEST,
		UPDATE_AVAILABLE,
		UPDATE_FAILED,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnShutdown() override;

	// Sets antiping/render options that matter for KoG. Does not touch binds or visuals.
	void ApplyKogPreset();

	static const char *InputModeName(int Mode);
	static float AspectRatioValue(int Setting); // 0 = native
	static bool ModeUsesAutoMargin(int Mode) { return Mode == INPUT_MODE_KING || Mode == INPUT_MODE_PING_COMP || Mode == INPUT_MODE_SMOOTH; }
	int PingCompAmount() const { return m_PingCompAmount; }

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

	// Music player (system "now playing")
	CKingMedia m_Media;

private:
	void ApplyInputMode();
	void RenderNetHud();
	void RenderMusicHud();
	void FinishUpdateCheck();
	void FinishDownload();
	bool LaunchInstaller();
	void ReadLastInstallResult();

	int m_PingCompAmount = 20;
	int64_t m_PingCompChanged = 0;
	std::shared_ptr<IHttpRequest> m_pUpdateTask = nullptr;
	EUpdateState m_UpdateState = UPDATE_IDLE;
	char m_aLatestVersion[32] = "";
	char m_aUpdateUrl[512] = "";
	char m_aAssetUrl[512] = ""; // direct -win64.zip download
	std::shared_ptr<IHttpRequest> m_pDownloadTask = nullptr;
	char m_aInstallError[256] = "";

	static void ConKogPreset(IConsole::IResult *pResult, void *pUserData);
	static void ConCheckUpdate(IConsole::IResult *pResult, void *pUserData);
	static void ConMusicToggle(IConsole::IResult *pResult, void *pUserData);
	static void ConMusicNext(IConsole::IResult *pResult, void *pUserData);
	static void ConMusicPrev(IConsole::IResult *pResult, void *pUserData);
};

#endif

/* KING client: input modes, KoG preset, network HUD and update check. */
#ifndef GAME_CLIENT_COMPONENTS_KING_KING_H
#define GAME_CLIENT_COMPONENTS_KING_KING_H

#include <engine/console.h>
#include <engine/http.h>

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

	// Updates (GitHub releases of KING_UPDATE_REPO)
	void CheckForUpdates();
	EUpdateState UpdateState() const { return m_UpdateState; }
	bool UpdateAvailable() const { return m_UpdateState == UPDATE_AVAILABLE; }
	const char *LatestVersion() const { return m_aLatestVersion; }
	const char *UpdateUrl() const { return m_aUpdateUrl; }
	void OpenUpdatePage();

private:
	void ApplyInputMode();
	void RenderNetHud();
	void FinishUpdateCheck();

	std::shared_ptr<IHttpRequest> m_pUpdateTask = nullptr;
	EUpdateState m_UpdateState = UPDATE_IDLE;
	char m_aLatestVersion[32] = "";
	char m_aUpdateUrl[512] = "";

	static void ConKogPreset(IConsole::IResult *pResult, void *pUserData);
	static void ConCheckUpdate(IConsole::IResult *pResult, void *pUserData);
};

#endif

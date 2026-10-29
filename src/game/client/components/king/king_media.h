/* KING client: "now playing" from the operating system's media controls (Windows). */
#ifndef GAME_CLIENT_COMPONENTS_KING_KING_MEDIA_H
#define GAME_CLIENT_COMPONENTS_KING_KING_MEDIA_H

#include <atomic>
#include <mutex>
#include <thread>

struct SKingMediaInfo
{
	bool m_Valid = false; // a media session exists
	bool m_Playing = false;
	char m_aTitle[256] = "";
	char m_aArtist[256] = "";
	float m_PositionSec = 0.0f; // already advanced to "now" while playing
	float m_DurationSec = 0.0f;
};

// Polls the system media session (Spotify, browsers, Yandex Music, ...) on a
// background thread. On platforms/compilers without support it stays invalid.
class CKingMedia
{
public:
	enum ECommand
	{
		COMMAND_NONE = 0,
		COMMAND_TOGGLE,
		COMMAND_NEXT,
		COMMAND_PREVIOUS,
	};

	~CKingMedia();
	void Start();
	void Stop();
	bool Supported() const;
	SKingMediaInfo Get() const;
	void Command(ECommand Command) { m_PendingCommand.store(Command); }

private:
	void Loop();

	std::thread m_Thread;
	std::atomic<bool> m_Running{false};
	std::atomic<int> m_PendingCommand{COMMAND_NONE};
	mutable std::mutex m_Mutex;
	SKingMediaInfo m_Info;
	double m_InfoTime = 0.0; // steady clock seconds when m_Info.m_PositionSec was measured
};

#endif

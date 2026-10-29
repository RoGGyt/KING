/* KING client: "now playing", cover art and an audio visualizer from the operating
   system's media controls and audio output (Windows). */
#ifndef GAME_CLIENT_COMPONENTS_KING_KING_MEDIA_H
#define GAME_CLIENT_COMPONENTS_KING_KING_MEDIA_H

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

struct SKingMediaInfo
{
	bool m_Valid = false; // a media session exists
	bool m_Playing = false;
	char m_aTitle[256] = "";
	char m_aArtist[256] = "";
	float m_PositionSec = 0.0f; // already advanced to "now" while playing
	float m_DurationSec = 0.0f;
	bool m_HasTimeline = false; // the player reports a position (needed for lyrics)
	int m_CoverSerial = 0; // changes whenever a new cover was decoded
	bool m_HasCover = false;
	float m_aCoverColor[3] = {0.91f, 0.70f, 0.29f}; // vivid average of the cover (RGB 0..1)
};

// Polls the system media session (Spotify, browsers, Yandex Music, ...) on a
// background thread, decodes the cover art, and (when asked) captures what the
// PC plays for the visualizer. On platforms/compilers without support it stays
// invalid and the visualizer stays flat.
class CKingMedia
{
public:
	static constexpr int COVER_SIZE = 96; // cover is cropped to a square of this size
	static constexpr int NUM_BANDS = 32; // log spaced 45 Hz .. 16 kHz

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

	// RGBA8, COVER_SIZE x COVER_SIZE. Returns false if there is no cover.
	bool GetCover(std::vector<uint8_t> &vPixels, int &Serial) const;

	// Visualizer: capture only runs while wanted (cheap to call every frame).
	void SetVisualizerWanted(bool Wanted) { m_AudioWanted.store(Wanted); }
	bool VisualizerCapturing() const { return m_AudioCapturing.load(); }
	bool VisualizerExcludesGame() const { return m_AudioExcludesGame.load(); }
	void GetBands(std::array<float, NUM_BANDS> &aBands) const;

private:
	void Loop();
	void AudioLoop();

	std::thread m_Thread;
	std::atomic<bool> m_Running{false};
	std::atomic<int> m_PendingCommand{COMMAND_NONE};
	mutable std::mutex m_Mutex;
	SKingMediaInfo m_Info;
	double m_InfoTime = 0.0; // steady clock seconds when m_Info.m_PositionSec was measured
	std::vector<uint8_t> m_vCover;

	std::thread m_AudioThread;
	std::atomic<bool> m_AudioWanted{false};
	std::atomic<bool> m_AudioCapturing{false};
	std::atomic<bool> m_AudioExcludesGame{false};
	mutable std::mutex m_AudioMutex;
	std::array<float, NUM_BANDS> m_aBands{};
	double m_BandsTime = 0.0;
};

#endif

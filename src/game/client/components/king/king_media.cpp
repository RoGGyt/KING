/* KING client: "now playing" from the operating system's media controls (Windows). */
#include "king_media.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

#if defined(_MSC_VER) && defined(__has_include)
#if __has_include(<winrt/Windows.Media.Control.h>)
#define KING_MEDIA_WINRT 1
#endif
#endif

#if defined(KING_MEDIA_WINRT)
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#pragma comment(lib, "windowsapp")
#pragma message("KING: system media player support ENABLED (C++/WinRT)")
#elif defined(_MSC_VER)
#pragma message("KING: system media player support DISABLED (no C++/WinRT headers)")
#endif

static double KingSteadySeconds()
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

#if defined(KING_MEDIA_WINRT)
static void KingCopy(char *pDst, size_t DstSize, const std::string &Src)
{
	const size_t Len = std::min(Src.size(), DstSize - 1);
	std::memcpy(pDst, Src.data(), Len);
	pDst[Len] = '\0';
}
#endif

CKingMedia::~CKingMedia()
{
	Stop();
}

bool CKingMedia::Supported() const
{
#if defined(KING_MEDIA_WINRT)
	return true;
#else
	return false;
#endif
}

void CKingMedia::Start()
{
	if(!Supported() || m_Running.load())
		return;
	m_Running.store(true);
	m_Thread = std::thread([this]() { Loop(); });
}

void CKingMedia::Stop()
{
	m_Running.store(false);
	if(m_Thread.joinable())
		m_Thread.join();
}

SKingMediaInfo CKingMedia::Get() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	SKingMediaInfo Info = m_Info;
	if(Info.m_Valid && Info.m_Playing)
	{
		// advance the last known position to "now"
		Info.m_PositionSec += (float)(KingSteadySeconds() - m_InfoTime);
		if(Info.m_DurationSec > 0.0f)
			Info.m_PositionSec = std::min(Info.m_PositionSec, Info.m_DurationSec);
	}
	return Info;
}

void CKingMedia::Loop()
{
#if defined(KING_MEDIA_WINRT)
	using namespace winrt::Windows::Media::Control;
	try
	{
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
	}
	catch(...)
	{
	}

	GlobalSystemMediaTransportControlsSessionManager Manager{nullptr};
	double NextPoll = 0.0;
	while(m_Running.load())
	{
		try
		{
			if(!Manager)
				Manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

			GlobalSystemMediaTransportControlsSession Session{nullptr};
			if(Manager)
				Session = Manager.GetCurrentSession();
			const int Cmd = m_PendingCommand.exchange(COMMAND_NONE);
			if(Session && Cmd != COMMAND_NONE)
			{
				if(Cmd == COMMAND_TOGGLE)
					Session.TryTogglePlayPauseAsync().get();
				else if(Cmd == COMMAND_NEXT)
					Session.TrySkipNextAsync().get();
				else if(Cmd == COMMAND_PREVIOUS)
					Session.TrySkipPreviousAsync().get();
				NextPoll = 0.0; // refresh right away
			}

			if(KingSteadySeconds() >= NextPoll)
			{
				NextPoll = KingSteadySeconds() + 0.5;
				SKingMediaInfo Info;
				double InfoTime = KingSteadySeconds();
				if(Session)
				{
					auto Props = Session.TryGetMediaPropertiesAsync().get();
					auto Playback = Session.GetPlaybackInfo();
					auto Timeline = Session.GetTimelineProperties();
					Info.m_Valid = true;
					Info.m_Playing = Playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
					KingCopy(Info.m_aTitle, sizeof(Info.m_aTitle), winrt::to_string(Props.Title()));
					KingCopy(Info.m_aArtist, sizeof(Info.m_aArtist), winrt::to_string(Props.Artist()));
					using Seconds = std::chrono::duration<float>;
					Info.m_DurationSec = std::chrono::duration_cast<Seconds>(Timeline.EndTime() - Timeline.StartTime()).count();
					Info.m_PositionSec = std::chrono::duration_cast<Seconds>(Timeline.Position() - Timeline.StartTime()).count();
					if(Info.m_Playing)
					{
						// the timeline is only updated now and then; account for the time since
						const auto Since = winrt::clock::now() - Timeline.LastUpdatedTime();
						const float SinceSec = std::chrono::duration_cast<Seconds>(Since).count();
						if(SinceSec > 0.0f && SinceSec < 3600.0f)
							Info.m_PositionSec += SinceSec;
					}
					if(Info.m_DurationSec > 0.0f)
						Info.m_PositionSec = std::clamp(Info.m_PositionSec, 0.0f, Info.m_DurationSec);
				}
				std::lock_guard<std::mutex> Lock(m_Mutex);
				m_Info = Info;
				m_InfoTime = InfoTime;
			}
		}
		catch(...)
		{
			// session vanished or the API is unavailable: show nothing, try again later
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Info = SKingMediaInfo();
			NextPoll = KingSteadySeconds() + 2.0;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	try
	{
		winrt::uninit_apartment();
	}
	catch(...)
	{
	}
#endif
}

/* KING client: "now playing", cover art and an audio visualizer from the operating
   system's media controls and audio output (Windows). */

#if defined(_MSC_VER) && defined(__has_include)
#if __has_include(<winrt/Windows.Media.Control.h>)
#define KING_MEDIA_WINRT 1
#endif
#endif

#if defined(KING_MEDIA_WINRT)
// This file alone talks to Windows 10 APIs (media session, imaging, WASAPI process
// loopback), so it may see their declarations. The rest of the client keeps the
// minimum OS version set by CMake.
#undef NTDDI_VERSION
#undef _WIN32_WINNT
#define NTDDI_VERSION 0x0A00000C
#define _WIN32_WINNT 0x0A00
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// the build defines WIN32_LEAN_AND_MEAN, so COM basics have to be pulled in by hand
#include <objbase.h>
#include <propidl.h>

#include <unknwn.h> // before C++/WinRT: lets winrt::implements take classic COM interfaces

#include <mmsystem.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 1
#endif
#if __has_include(<audioclientactivationparams.h>)
#include <audioclientactivationparams.h>
#define KING_PROCESS_LOOPBACK 1
#endif

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma comment(lib, "windowsapp")
#pragma comment(lib, "ole32")
#pragma message("KING: system media player support ENABLED (C++/WinRT)")
#elif defined(_MSC_VER)
#pragma message("KING: system media player support DISABLED (no C++/WinRT headers)")
#endif

#if defined(KING_REQUIRE_WINRT) && !defined(KING_MEDIA_WINRT)
#error "KING: C++/WinRT headers not found, the music player would be disabled"
#endif

#include "king_media.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <string>

static double KingSteadySeconds()
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

#if defined(KING_MEDIA_WINRT)
namespace
{
void KingCopy(char *pDst, size_t DstSize, const std::string &Src)
{
	const size_t Len = std::min(Src.size(), DstSize - 1);
	std::memcpy(pDst, Src.data(), Len);
	pDst[Len] = '\0';
}

// Decodes the session thumbnail (any format Windows knows: jpeg, png, ...), crops the
// middle square and scales it to COVER_SIZE. Also finds a vivid average color.
bool KingDecodeCover(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference &Ref, std::vector<uint8_t> &vOut, float *pColor)
{
	using namespace winrt::Windows::Graphics::Imaging;
	if(!Ref)
		return false;
	auto Stream = Ref.OpenReadAsync().get();
	if(!Stream || Stream.Size() == 0)
		return false;
	auto Decoder = BitmapDecoder::CreateAsync(Stream).get();
	const uint32_t W = Decoder.PixelWidth();
	const uint32_t H = Decoder.PixelHeight();
	if(W == 0 || H == 0)
		return false;

	const uint32_t Size = CKingMedia::COVER_SIZE;
	const float Scale = Size / (float)std::min(W, H);
	const uint32_t ScaledW = std::max<uint32_t>(Size, (uint32_t)std::lround(W * Scale));
	const uint32_t ScaledH = std::max<uint32_t>(Size, (uint32_t)std::lround(H * Scale));
	BitmapTransform Transform;
	Transform.ScaledWidth(ScaledW);
	Transform.ScaledHeight(ScaledH);
	Transform.InterpolationMode(BitmapInterpolationMode::Fant);
	Transform.Bounds(BitmapBounds{(ScaledW - Size) / 2, (ScaledH - Size) / 2, Size, Size});
	auto Provider = Decoder.GetPixelDataAsync(BitmapPixelFormat::Rgba8, BitmapAlphaMode::Straight, Transform, ExifOrientationMode::IgnoreExifOrientation, ColorManagementMode::DoNotColorManage).get();
	const winrt::com_array<uint8_t> Pixels = Provider.DetachPixelData();
	const size_t Bytes = (size_t)Size * Size * 4;
	if(Pixels.size() < Bytes)
		return false;
	vOut.assign(Pixels.begin(), Pixels.begin() + Bytes);

	// colorful, bright pixels count most, grey/dark ones barely
	double aSum[3] = {0.0, 0.0, 0.0};
	double WeightSum = 0.0;
	for(size_t i = 0; i < Bytes; i += 4)
	{
		const float R = vOut[i] / 255.0f, G = vOut[i + 1] / 255.0f, B = vOut[i + 2] / 255.0f;
		const float Max = std::max({R, G, B});
		const float Min = std::min({R, G, B});
		const float Sat = Max > 0.0f ? (Max - Min) / Max : 0.0f;
		const double Weight = Sat * Sat * Max + 0.01;
		aSum[0] += R * Weight;
		aSum[1] += G * Weight;
		aSum[2] += B * Weight;
		WeightSum += Weight;
	}
	for(int c = 0; c < 3; c++)
		pColor[c] = (float)(aSum[c] / WeightSum);
	return true;
}

// ***** Visualizer: audio capture + spectrum ***** //

constexpr int FFT_SIZE = 2048;

void KingFft(std::vector<std::complex<float>> &vData)
{
	const size_t N = vData.size();
	for(size_t i = 1, j = 0; i < N; i++)
	{
		size_t Bit = N >> 1;
		for(; j & Bit; Bit >>= 1)
			j ^= Bit;
		j ^= Bit;
		if(i < j)
			std::swap(vData[i], vData[j]);
	}
	for(size_t Len = 2; Len <= N; Len <<= 1)
	{
		const float Angle = -2.0f * 3.14159265f / (float)Len;
		const std::complex<float> Step(std::cos(Angle), std::sin(Angle));
		for(size_t i = 0; i < N; i += Len)
		{
			std::complex<float> W(1.0f, 0.0f);
			for(size_t k = 0; k < Len / 2; k++)
			{
				const std::complex<float> U = vData[i + k];
				const std::complex<float> V = vData[i + k + Len / 2] * W;
				vData[i + k] = U + V;
				vData[i + k + Len / 2] = U - V;
				W *= Step;
			}
		}
	}
}

#if defined(KING_PROCESS_LOOPBACK)
// ActivateAudioInterfaceAsync calls this back on a worker thread. C++/WinRT objects
// are agile (IAgileObject), which this API requires.
struct CKingActivateHandler : winrt::implements<CKingActivateHandler, IActivateAudioInterfaceCompletionHandler>
{
	HANDLE m_Done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	HRESULT m_Result = E_FAIL;
	winrt::com_ptr<IAudioClient> m_Client;

	~CKingActivateHandler()
	{
		if(m_Done)
			CloseHandle(m_Done);
	}

	HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *pOperation) noexcept final
	{
		HRESULT ActivateResult = E_FAIL;
		winrt::com_ptr<IUnknown> Unknown;
		const HRESULT Hr = pOperation->GetActivateResult(&ActivateResult, Unknown.put());
		if(SUCCEEDED(Hr) && SUCCEEDED(ActivateResult) && Unknown)
			m_Client = Unknown.try_as<IAudioClient>();
		m_Result = FAILED(Hr) ? Hr : (FAILED(ActivateResult) ? ActivateResult : (m_Client ? S_OK : E_NOINTERFACE));
		SetEvent(m_Done);
		return S_OK;
	}
};
#endif

// Captures what the PC plays. Prefers "everything except KING" (Windows 10 2004+
// process loopback), so hooks and hammers don't move the bars; falls back to the
// whole default output device.
class CKingAudioCapture
{
public:
	~CKingAudioCapture() { Close(); }

	bool Active() const { return m_pClient != nullptr; }
	bool ExcludesGame() const { return m_ProcessLoopback; }
	int SampleRate() const { return m_Rate; }

	bool Open()
	{
		Close();
		if(OpenProcessLoopback())
			return true;
		Close();
		if(OpenDeviceLoopback())
			return true;
		Close();
		return false;
	}

	void Close()
	{
		if(m_pClient)
			m_pClient->Stop();
		m_pCapture = nullptr;
		m_pClient = nullptr;
		if(m_Event)
		{
			CloseHandle(m_Event);
			m_Event = nullptr;
		}
		m_ProcessLoopback = false;
	}

	// Appends mono samples. Returns false when the device is gone.
	bool Read(std::vector<float> &vMono)
	{
		if(m_Event)
			WaitForSingleObject(m_Event, 30);
		else
			std::this_thread::sleep_for(std::chrono::milliseconds(10));

		UINT32 Packet = 0;
		if(FAILED(m_pCapture->GetNextPacketSize(&Packet)))
			return false;
		while(Packet > 0)
		{
			BYTE *pData = nullptr;
			UINT32 Frames = 0;
			DWORD Flags = 0;
			const HRESULT Hr = m_pCapture->GetBuffer(&pData, &Frames, &Flags, nullptr, nullptr);
			if(FAILED(Hr))
				return false;
			if(Hr == AUDCLNT_S_BUFFER_EMPTY)
				break;
			Convert(pData, Frames, (Flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0, vMono);
			m_pCapture->ReleaseBuffer(Frames);
			if(FAILED(m_pCapture->GetNextPacketSize(&Packet)))
				return false;
		}
		return true;
	}

private:
	bool SetFormat(const WAVEFORMATEX *pFormat)
	{
		m_Channels = pFormat->nChannels;
		m_BlockAlign = pFormat->nBlockAlign;
		m_Bits = pFormat->wBitsPerSample;
		m_Rate = (int)pFormat->nSamplesPerSec;
		DWORD Tag = pFormat->wFormatTag;
		if(Tag == WAVE_FORMAT_EXTENSIBLE && pFormat->cbSize >= 22)
			Tag = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(pFormat)->SubFormat.Data1;
		m_Float = Tag == WAVE_FORMAT_IEEE_FLOAT;
		if(!m_Float && Tag != WAVE_FORMAT_PCM)
			return false;
		if(m_Float ? m_Bits != 32 : (m_Bits != 16 && m_Bits != 24 && m_Bits != 32))
			return false;
		return m_Channels > 0 && m_Rate > 0 && m_BlockAlign >= m_Channels * (m_Bits / 8);
	}

	void Convert(const BYTE *pData, UINT32 Frames, bool Silent, std::vector<float> &vMono) const
	{
		const size_t Start = vMono.size();
		vMono.resize(Start + Frames, 0.0f);
		if(Silent || !pData)
			return;
		const int Bytes = m_Bits / 8;
		for(UINT32 f = 0; f < Frames; f++)
		{
			const BYTE *pFrame = pData + (size_t)f * m_BlockAlign;
			float Sum = 0.0f;
			for(int c = 0; c < m_Channels; c++)
			{
				const BYTE *p = pFrame + c * Bytes;
				float Value;
				if(m_Float)
				{
					std::memcpy(&Value, p, sizeof(float));
				}
				else if(m_Bits == 16)
				{
					int16_t v;
					std::memcpy(&v, p, 2);
					Value = v / 32768.0f;
				}
				else if(m_Bits == 24)
				{
					const int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
					Value = v / 8388608.0f;
				}
				else
				{
					int32_t v;
					std::memcpy(&v, p, 4);
					Value = v / 2147483648.0f;
				}
				Sum += Value;
			}
			vMono[Start + f] = Sum / (float)m_Channels;
		}
	}

	bool OpenProcessLoopback()
	{
#if defined(KING_PROCESS_LOOPBACK)
		using FActivate = HRESULT(WINAPI *)(LPCWSTR, REFIID, PROPVARIANT *, IActivateAudioInterfaceCompletionHandler *, IActivateAudioInterfaceAsyncOperation **);
		static HMODULE s_Module = LoadLibraryW(L"Mmdevapi.dll"); // stays loaded
		if(!s_Module)
			return false;
		const FActivate pfnActivate = reinterpret_cast<FActivate>(reinterpret_cast<void *>(GetProcAddress(s_Module, "ActivateAudioInterfaceAsync")));
		if(!pfnActivate)
			return false;

		AUDIOCLIENT_ACTIVATION_PARAMS Params = {};
		Params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
		Params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
		Params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
		PROPVARIANT Prop = {};
		Prop.vt = VT_BLOB;
		Prop.blob.cbSize = sizeof(Params);
		Prop.blob.pBlobData = reinterpret_cast<BYTE *>(&Params);

		auto pHandler = winrt::make_self<CKingActivateHandler>();
		if(!pHandler->m_Done)
			return false;
		winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> pOperation;
		if(FAILED(pfnActivate(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &Prop, pHandler.get(), pOperation.put())))
			return false;
		if(WaitForSingleObject(pHandler->m_Done, 3000) != WAIT_OBJECT_0 || FAILED(pHandler->m_Result) || !pHandler->m_Client)
			return false;
		m_pClient = pHandler->m_Client;

		// process loopback has no mix format of its own: ask for plain 16 bit stereo
		WAVEFORMATEX Format = {};
		Format.wFormatTag = WAVE_FORMAT_PCM;
		Format.nChannels = 2;
		Format.nSamplesPerSec = 48000;
		Format.wBitsPerSample = 16;
		Format.nBlockAlign = Format.nChannels * Format.wBitsPerSample / 8;
		Format.nAvgBytesPerSec = Format.nSamplesPerSec * Format.nBlockAlign;
		if(!SetFormat(&Format))
			return false;
		if(FAILED(m_pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 2000000, 0, &Format, nullptr)))
			return false;
		m_Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if(!m_Event || FAILED(m_pClient->SetEventHandle(m_Event)))
			return false;
		if(FAILED(m_pClient->GetService(__uuidof(IAudioCaptureClient), m_pCapture.put_void())) || !m_pCapture)
			return false;
		if(FAILED(m_pClient->Start()))
			return false;
		m_ProcessLoopback = true;
		return true;
#else
		return false;
#endif
	}

	bool OpenDeviceLoopback()
	{
		winrt::com_ptr<IMMDeviceEnumerator> pEnumerator;
		if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), pEnumerator.put_void())) || !pEnumerator)
			return false;
		winrt::com_ptr<IMMDevice> pDevice;
		if(FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, pDevice.put())) || !pDevice)
			return false;
		if(FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, m_pClient.put_void())) || !m_pClient)
			return false;
		WAVEFORMATEX *pMix = nullptr;
		if(FAILED(m_pClient->GetMixFormat(&pMix)) || !pMix)
			return false;
		const bool FormatOk = SetFormat(pMix);
		const HRESULT Hr = FormatOk ? m_pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, pMix, nullptr) : E_FAIL;
		CoTaskMemFree(pMix);
		if(FAILED(Hr))
			return false;
		if(FAILED(m_pClient->GetService(__uuidof(IAudioCaptureClient), m_pCapture.put_void())) || !m_pCapture)
			return false;
		return SUCCEEDED(m_pClient->Start());
	}

	winrt::com_ptr<IAudioClient> m_pClient;
	winrt::com_ptr<IAudioCaptureClient> m_pCapture;
	HANDLE m_Event = nullptr;
	bool m_ProcessLoopback = false;
	int m_Channels = 0;
	int m_BlockAlign = 0;
	int m_Bits = 0;
	int m_Rate = 48000;
	bool m_Float = false;
};

// Turns the newest FFT_SIZE samples into NUM_BANDS levels (0..1).
class CKingSpectrum
{
public:
	CKingSpectrum()
	{
		m_vWindow.resize(FFT_SIZE);
		for(int i = 0; i < FFT_SIZE; i++)
			m_vWindow[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (FFT_SIZE - 1));
		m_vRing.assign(FFT_SIZE, 0.0f);
		m_vFft.resize(FFT_SIZE);
	}

	void Push(const std::vector<float> &vSamples)
	{
		for(float Sample : vSamples)
		{
			m_vRing[m_RingPos] = Sample;
			m_RingPos = (m_RingPos + 1) % FFT_SIZE;
		}
	}

	void Analyze(int SampleRate, std::array<float, CKingMedia::NUM_BANDS> &aOut, float Dt)
	{
		for(int i = 0; i < FFT_SIZE; i++)
			m_vFft[i] = std::complex<float>(m_vRing[(m_RingPos + i) % FFT_SIZE] * m_vWindow[i], 0.0f);
		KingFft(m_vFft);

		const float BinHz = SampleRate / (float)FFT_SIZE;
		const float LowHz = 45.0f, HighHz = std::min(16000.0f, SampleRate * 0.48f);
		const float FullScale = FFT_SIZE / 4.0f; // a full scale sine with the Hann window
		float aDb[CKingMedia::NUM_BANDS];
		float MaxDb = -120.0f;
		for(int b = 0; b < CKingMedia::NUM_BANDS; b++)
		{
			const float F0 = LowHz * std::pow(HighHz / LowHz, b / (float)CKingMedia::NUM_BANDS);
			const float F1 = LowHz * std::pow(HighHz / LowHz, (b + 1) / (float)CKingMedia::NUM_BANDS);
			const int K0 = std::clamp((int)(F0 / BinHz), 1, FFT_SIZE / 2 - 1);
			const int K1 = std::clamp((int)(F1 / BinHz), K0, FFT_SIZE / 2 - 1);
			float Mag = 0.0f;
			for(int k = K0; k <= K1; k++)
				Mag = std::max(Mag, std::abs(m_vFft[k]));
			// music has most energy in the bass: tilt +3 dB per octave so highs show too
			const float Center = std::sqrt(F0 * F1);
			aDb[b] = 20.0f * std::log10(Mag / FullScale + 1e-9f) + 3.0f * std::log2(Center / 1000.0f);
			MaxDb = std::max(MaxDb, aDb[b]);
		}

		// automatic gain: follow the loudest band quickly up, slowly down
		if(MaxDb > m_PeakDb)
			m_PeakDb = MaxDb;
		else
			m_PeakDb = std::max(MaxDb, m_PeakDb - 6.0f * Dt);
		const float Top = std::max(m_PeakDb, -45.0f); // quieter than that is "silence"
		const float Range = 42.0f;

		for(int b = 0; b < CKingMedia::NUM_BANDS; b++)
		{
			const float Level = std::clamp((aDb[b] - (Top - Range)) / Range, 0.0f, 1.0f);
			const float Shaped = Level * Level; // keeps quiet bands low, peaks punchy
			if(Shaped > aOut[b])
				aOut[b] += (Shaped - aOut[b]) * 0.65f;
			else
				aOut[b] = std::max(Shaped, aOut[b] - 2.2f * Dt);
		}
	}

private:
	std::vector<float> m_vWindow;
	std::vector<float> m_vRing;
	std::vector<std::complex<float>> m_vFft;
	int m_RingPos = 0;
	float m_PeakDb = -45.0f;
};
} // namespace
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
	m_AudioThread = std::thread([this]() { AudioLoop(); });
}

void CKingMedia::Stop()
{
	m_Running.store(false);
	if(m_Thread.joinable())
		m_Thread.join();
	if(m_AudioThread.joinable())
		m_AudioThread.join();
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

bool CKingMedia::GetCover(std::vector<uint8_t> &vPixels, int &Serial) const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	Serial = m_Info.m_CoverSerial;
	if(!m_Info.m_HasCover || m_vCover.empty())
		return false;
	vPixels = m_vCover;
	return true;
}

void CKingMedia::GetBands(std::array<float, NUM_BANDS> &aBands) const
{
	std::lock_guard<std::mutex> Lock(m_AudioMutex);
	aBands = m_aBands;
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
	std::string LastTrack;
	double TrackChanged = 0.0;
	int CoverFetches = 0; // fetches done for the current track
	bool CoverOk = false;
	int CoverSerial = 0;
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

			const double Now = KingSteadySeconds();
			if(Now >= NextPoll)
			{
				NextPoll = Now + 0.5;
				SKingMediaInfo Info;
				const double InfoTime = KingSteadySeconds();
				winrt::Windows::Storage::Streams::IRandomAccessStreamReference Thumbnail{nullptr};
				if(Session)
				{
					auto Props = Session.TryGetMediaPropertiesAsync().get();
					auto Playback = Session.GetPlaybackInfo();
					auto Timeline = Session.GetTimelineProperties();
					Info.m_Valid = true;
					Info.m_Playing = Playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
					if(Props)
					{
						KingCopy(Info.m_aTitle, sizeof(Info.m_aTitle), winrt::to_string(Props.Title()));
						KingCopy(Info.m_aArtist, sizeof(Info.m_aArtist), winrt::to_string(Props.Artist()));
						Thumbnail = Props.Thumbnail();
					}
					using Seconds = std::chrono::duration<float>;
					Info.m_DurationSec = std::chrono::duration_cast<Seconds>(Timeline.EndTime() - Timeline.StartTime()).count();
					Info.m_PositionSec = std::chrono::duration_cast<Seconds>(Timeline.Position() - Timeline.StartTime()).count();
					Info.m_HasTimeline = Info.m_DurationSec > 0.0f;
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

				// cover art: fetch on track change, again a bit later (some players
				// update the picture after the title), retry while there is none
				const std::string Track = std::string(Info.m_aTitle) + '\n' + Info.m_aArtist;
				if(Track != LastTrack)
				{
					LastTrack = Track;
					TrackChanged = Now;
					CoverFetches = 0;
					CoverOk = false;
					std::lock_guard<std::mutex> Lock(m_Mutex);
					m_vCover.clear();
					m_Info.m_HasCover = false;
					m_Info.m_CoverSerial = ++CoverSerial;
				}
				const bool WantFetch = Thumbnail && Info.m_Valid &&
						       ((CoverFetches == 0) || (CoverFetches == 1 && Now - TrackChanged > 2.5) || (!CoverOk && CoverFetches < 6));
				std::vector<uint8_t> vCover;
				float aColor[3];
				bool NewCover = false;
				if(WantFetch)
				{
					CoverFetches++;
					try
					{
						NewCover = KingDecodeCover(Thumbnail, vCover, aColor);
					}
					catch(...)
					{
						NewCover = false;
					}
					CoverOk = CoverOk || NewCover;
				}

				std::lock_guard<std::mutex> Lock(m_Mutex);
				Info.m_CoverSerial = m_Info.m_CoverSerial;
				Info.m_HasCover = m_Info.m_HasCover;
				std::copy(std::begin(m_Info.m_aCoverColor), std::end(m_Info.m_aCoverColor), Info.m_aCoverColor);
				if(NewCover && vCover != m_vCover)
				{
					m_vCover = std::move(vCover);
					Info.m_HasCover = true;
					Info.m_CoverSerial = ++CoverSerial;
					std::copy(std::begin(aColor), std::end(aColor), Info.m_aCoverColor);
				}
				m_Info = Info;
				m_InfoTime = InfoTime;
			}
		}
		catch(...)
		{
			// session vanished or the API is unavailable: show nothing, try again later
			std::lock_guard<std::mutex> Lock(m_Mutex);
			const int Serial = m_Info.m_CoverSerial;
			m_Info = SKingMediaInfo();
			m_Info.m_CoverSerial = Serial;
			m_vCover.clear();
			LastTrack.clear();
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

void CKingMedia::AudioLoop()
{
#if defined(KING_MEDIA_WINRT)
	const HRESULT CoResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	{
		CKingAudioCapture Capture;
		CKingSpectrum Spectrum;
		std::array<float, NUM_BANDS> aLevels{};
		std::vector<float> vSamples;
		double NextRetry = 0.0;
		double LastAnalyze = KingSteadySeconds();
		double LastSound = 0.0;

		while(m_Running.load())
		{
			const double Now = KingSteadySeconds();
			const float Dt = (float)std::clamp(Now - LastAnalyze, 0.0, 0.25);

			if(!m_AudioWanted.load())
			{
				if(Capture.Active())
					Capture.Close();
				m_AudioCapturing.store(false);
				aLevels.fill(0.0f);
				{
					std::lock_guard<std::mutex> Lock(m_AudioMutex);
					m_aBands = aLevels;
				}
				LastAnalyze = Now;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			if(!Capture.Active())
			{
				bool Opened = false;
				if(Now >= NextRetry)
				{
					try
					{
						Opened = Capture.Open();
					}
					catch(...)
					{
						Capture.Close();
						Opened = false;
					}
				}
				if(!Opened)
				{
					if(Now >= NextRetry)
						NextRetry = Now + 5.0;
					m_AudioCapturing.store(false);
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					continue;
				}
				m_AudioExcludesGame.store(Capture.ExcludesGame());
				m_AudioCapturing.store(true);
			}

			vSamples.clear();
			bool Ok = true;
			try
			{
				Ok = Capture.Read(vSamples);
			}
			catch(...)
			{
				Ok = false;
			}
			if(!Ok)
			{
				// default device changed or was unplugged: reopen soon
				Capture.Close();
				m_AudioCapturing.store(false);
				NextRetry = Now + 1.0;
				continue;
			}
			if(!vSamples.empty())
			{
				Spectrum.Push(vSamples);
				LastSound = Now;
			}

			if(Now - LastAnalyze >= 1.0 / 60.0)
			{
				LastAnalyze = Now;
				if(Now - LastSound > 0.3)
				{
					// nothing is being played: let the bars fall
					for(float &Level : aLevels)
						Level = std::max(0.0f, Level - 2.2f * Dt);
				}
				else
					Spectrum.Analyze(Capture.SampleRate(), aLevels, Dt);
				std::lock_guard<std::mutex> Lock(m_AudioMutex);
				m_aBands = aLevels;
				m_BandsTime = Now;
			}
		}
		Capture.Close();
	}
	m_AudioCapturing.store(false);
	if(SUCCEEDED(CoResult))
		CoUninitialize();
#endif
}

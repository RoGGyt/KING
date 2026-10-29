/* KING client: network quality monitor and adaptive prediction margin. */
#ifndef ENGINE_CLIENT_KING_NET_H
#define ENGINE_CLIENT_KING_NET_H

#include <base/time.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Fed from NETMSG_INPUTTIMING. For every input the server tells us how many ms
// before its tick the input arrived (TimeLeft). The client aims for
// TimeLeft == prediction margin, so (margin - TimeLeft) is how much of the
// margin the network ate. Its spread over the last few seconds is the jitter we
// have to cover; anything above that is pure added latency.
class CKingNetMonitor
{
public:
	static constexpr int WINDOW = 150; // ~3 s of inputs at 50 Hz
	static constexpr int MIN_SAMPLES = 25;

	void Reset(float StartMargin)
	{
		m_Count = 0;
		m_Index = 0;
		m_RttMs = 0.0f;
		m_JitterMs = 0.0f;
		m_LastRttMs = -1.0f;
		m_TimeLeftMs = 0.0f;
		m_SpreadMs = 0.0f;
		m_MarginMs = StartMargin;
		m_LateTotal = 0;
		m_LastLate = 0;
		m_LastUpdate = 0;
	}

	void OnInputTiming(float RttMs, float TimeLeftMs, float SentMarginMs, int MinMargin, int MaxMargin, int Safety, int64_t Now)
	{
		const float Freq = (float)time_freq();
		MaxMargin = std::max(MaxMargin, MinMargin);

		// RTT and RFC 3550 style inter-arrival jitter
		if(m_LastRttMs < 0.0f)
			m_RttMs = RttMs;
		else
		{
			m_RttMs += (RttMs - m_RttMs) * 0.1f;
			m_JitterMs += (std::fabs(RttMs - m_LastRttMs) - m_JitterMs) / 16.0f;
		}
		m_LastRttMs = RttMs;
		m_TimeLeftMs = TimeLeftMs;

		m_aDev[m_Index] = SentMarginMs - TimeLeftMs;
		m_Index = (m_Index + 1) % WINDOW;
		m_Count = std::min(m_Count + 1, WINDOW);

		const float Dt = m_LastUpdate ? (Now - m_LastUpdate) / Freq : 0.0f;
		m_LastUpdate = Now;

		if(TimeLeftMs < 0.0f)
		{
			// Input arrived after the server tick started -> server reused the
			// old input -> misprediction. React immediately.
			m_LateTotal++;
			m_LastLate = Now;
			// Single spikes are cheap (one tick corrected), so nudge instead of jumping.
			m_MarginMs = std::min((float)MaxMargin, m_MarginMs + 2.0f);
			return;
		}

		if(m_Count < MIN_SAMPLES)
			return;

		std::array<float, WINDOW> aSorted;
		std::copy(m_aDev.begin(), m_aDev.begin() + m_Count, aSorted.begin());
		const int Med = m_Count / 2;
		const int P95 = std::min(m_Count - 1, (int)(m_Count * 0.95f));
		std::nth_element(aSorted.begin(), aSorted.begin() + Med, aSorted.begin() + m_Count);
		const float Median = aSorted[Med];
		std::nth_element(aSorted.begin(), aSorted.begin() + P95, aSorted.begin() + m_Count);
		m_SpreadMs = std::max(0.0f, aSorted[P95] - Median);

		const float Target = std::clamp(m_SpreadMs + Safety, (float)MinMargin, (float)MaxMargin);
		const bool Hold = m_LastLate && (Now - m_LastLate) < (int64_t)(3 * Freq);
		if(Target > m_MarginMs)
			m_MarginMs += (Target - m_MarginMs) * 0.25f; // go up fast
		else if(!Hold)
			m_MarginMs = std::max(Target, m_MarginMs - 1.0f * Dt); // go down slowly: 1 ms per second
		m_MarginMs = std::clamp(m_MarginMs, (float)MinMargin, (float)MaxMargin);
	}

	int Margin(int MinMargin, int MaxMargin) const
	{
		return std::clamp((int)std::ceil(m_MarginMs), MinMargin, std::max(MinMargin, MaxMargin));
	}

	bool Valid() const { return m_Count >= MIN_SAMPLES; }
	float RttMs() const { return m_RttMs; }
	float JitterMs() const { return m_JitterMs; }
	float SpreadMs() const { return m_SpreadMs; }
	float TimeLeftMs() const { return m_TimeLeftMs; }
	int LateTotal() const { return m_LateTotal; }
	bool RecentLate(int64_t Now) const { return m_LastLate && (Now - m_LastLate) < 3 * time_freq(); }

private:
	std::array<float, WINDOW> m_aDev{};
	int m_Count = 0;
	int m_Index = 0;
	float m_RttMs = 0.0f;
	float m_JitterMs = 0.0f;
	float m_LastRttMs = -1.0f;
	float m_TimeLeftMs = 0.0f;
	float m_SpreadMs = 0.0f;
	float m_MarginMs = 10.0f;
	int m_LateTotal = 0;
	int64_t m_LastLate = 0;
	int64_t m_LastUpdate = 0;
};

#endif

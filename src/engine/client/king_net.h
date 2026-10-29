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
	static constexpr int WINDOW = 500; // ~10 s of inputs at 50 Hz
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
		m_Margin = (int)std::ceil(StartMargin);
		m_LateTotal = 0;
		m_LastLate = 0;
		m_LastChange = 0;
	}

	// The margin is deliberately sticky: every change shifts the prediction
	// timeline a little, and every late input costs a tick of your input (a
	// visible correction). So: go up at once when needed, come down only after
	// a long quiet period, one millisecond at a time.
	void OnInputTiming(float RttMs, float TimeLeftMs, float SentMarginMs, int MinMargin, int MaxMargin, int Safety, int64_t Now)
	{
		const int64_t Freq = time_freq();
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

		if(TimeLeftMs < 0.0f)
		{
			// late input: the server had to reuse the previous input
			m_LateTotal++;
			m_LastLate = Now;
			m_Margin = std::min(MaxMargin, m_Margin + 4);
			m_LastChange = Now;
			return;
		}

		if(m_Count < MIN_SAMPLES)
			return;

		// worst delay spike of the last ~10 s relative to the median
		std::array<float, WINDOW> aSorted;
		std::copy(m_aDev.begin(), m_aDev.begin() + m_Count, aSorted.begin());
		const int Med = m_Count / 2;
		std::nth_element(aSorted.begin(), aSorted.begin() + Med, aSorted.begin() + m_Count);
		const float Median = aSorted[Med];
		const float Worst = *std::max_element(aSorted.begin(), aSorted.begin() + m_Count);
		m_SpreadMs = std::max(0.0f, Worst - Median);

		const int Target = std::clamp((int)std::ceil(m_SpreadMs) + Safety, MinMargin, MaxMargin);
		if(Target > m_Margin)
		{
			m_Margin = Target;
			m_LastChange = Now;
		}
		else if(Target < m_Margin && Now - m_LastChange > 20 * Freq && (!m_LastLate || Now - m_LastLate > 60 * Freq))
		{
			m_Margin--;
			m_LastChange = Now;
		}
		m_Margin = std::clamp(m_Margin, MinMargin, MaxMargin);
	}

	int Margin(int MinMargin, int MaxMargin) const
	{
		return std::clamp(m_Margin, MinMargin, std::max(MinMargin, MaxMargin));
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
	int m_Margin = 10;
	int m_LateTotal = 0;
	int64_t m_LastLate = 0;
	int64_t m_LastChange = 0;
};

#endif

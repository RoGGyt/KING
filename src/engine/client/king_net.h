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
// margin the network (and our own frame timing) ate. Its spread is what the
// margin has to cover; anything above that is pure added latency.
//
// Rules (learned the hard way, every one of them was a visible "teleport"):
//  - never go below the base margin (stock DDNet: 10 ms). Saving 3 ms of input
//    delay is not worth a single late input.
//  - a late input means the server used your previous input for that tick, so
//    the margin only grows when the network shows it needs it, and shrinks back
//    very slowly.
//  - the margin is part of the prediction clock: changing it at once makes your
//    tee jump. The client ramps the applied margin (see Advance()).
//  - the first seconds after joining (or a prediction clock reset) the clock is still
//    settling: those timings say nothing about the network. Counting them pushed the
//    margin to the maximum on every join and it took half a minute to come back.
class CKingNetMonitor
{
public:
	static constexpr int WINDOW = 500; // ~10 s of inputs at 50 Hz
	static constexpr int MIN_SAMPLES = 25;
	static constexpr float HOLD_SECONDS = 8.0f; // keep a spike in mind this long
	static constexpr float DECAY_SECONDS = 15.0f; // then forget it (every ms of margin is input delay)
	static constexpr float WARMUP_SECONDS = 3.0f; // clock settling after a join / clock reset

	void Reset(float BaseMargin)
	{
		m_Count = 0;
		m_Index = 0;
		m_RttMs = 0.0f;
		m_JitterMs = 0.0f;
		m_LastRttMs = -1.0f;
		m_TimeLeftMs = 0.0f;
		m_SpreadMs = 0.0f;
		m_PeakMs = 0.0f;
		m_PeakTime = 0;
		m_LastSample = 0;
		m_LateTotal = 0;
		m_LastLate = 0;
		m_LastLateMs = 0.0f;
		m_aLateTimes.fill(0);
		m_CurrentMs = BaseMargin;
		m_LastAdvance = 0;
		m_WarmupUntil = 0;
	}

	// the prediction clock was reset: ignore timings until it settled again
	void Warmup(int64_t Now) { m_WarmupUntil = Now + (int64_t)(WARMUP_SECONDS * time_freq()); }
	bool WarmingUp(int64_t Now) const { return !m_WarmupUntil || Now < m_WarmupUntil; }

	void OnInputTiming(float RttMs, float TimeLeftMs, float SentMarginMs, int64_t Now)
	{
		const int64_t Freq = time_freq();

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

		if(!m_WarmupUntil)
			Warmup(Now); // first timing after a join
		if(Now < m_WarmupUntil)
			return;

		m_aDev[m_Index] = SentMarginMs - TimeLeftMs;
		m_Index = (m_Index + 1) % WINDOW;
		m_Count = std::min(m_Count + 1, WINDOW);

		if(TimeLeftMs < 0.0f)
		{
			// late input: the server had to reuse the previous input. A burst of late
			// inputs (one ping spike) counts as one event for the "repeated" rule below.
			m_LateTotal++;
			const bool NewEvent = !m_LastLate || Now - m_LastLate > Freq / 2;
			m_LastLate = Now;
			m_LastLateMs = -TimeLeftMs;
			if(NewEvent)
			{
				m_aLateTimes[m_LateIndex] = Now;
				m_LateIndex = (m_LateIndex + 1) % (int)m_aLateTimes.size();
			}
		}

		if(m_Count < MIN_SAMPLES)
			return;

		// typical value and a robust "bad case" of the last ~10 s: the 2nd percentile
		// from the top ignores single freak packets (a margin cannot fix those anyway)
		std::array<float, WINDOW> aSorted;
		std::copy(m_aDev.begin(), m_aDev.begin() + m_Count, aSorted.begin());
		const int Med = m_Count / 2;
		std::nth_element(aSorted.begin(), aSorted.begin() + Med, aSorted.begin() + m_Count);
		const float Median = aSorted[Med];
		// ping spikes far beyond what the highest margin could cover (a lag spike of
		// +100 ms) are late whatever we do: they must not hold the margin at its maximum
		// for the next 20 s, that is only input delay
		int Num = 0;
		for(int i = 0; i < m_Count; i++)
			if(aSorted[i] - Median <= m_CoverMs)
				aSorted[Num++] = aSorted[i];
		const int FMed = std::min(Med, Num - 1);
		const int High = std::clamp((int)(Num * 0.98f), FMed, Num - 1);
		std::nth_element(aSorted.begin(), aSorted.begin() + High, aSorted.begin() + Num);
		float Spread = std::max(0.0f, aSorted[High] - Median);

		// repeated late inputs: cover the worst one too (if it can be covered at all)
		if(RecentLateCount(Now, 10 * Freq) >= 2)
		{
			const float Worst = *std::max_element(aSorted.begin(), aSorted.begin() + Num);
			Spread = std::max(Spread, Worst - Median);
		}
		m_SpreadMs = Spread;

		// peak hold with slow exponential release
		const float Dt = m_LastSample ? (Now - m_LastSample) / (float)Freq : 0.0f;
		m_LastSample = Now;
		if(Spread >= m_PeakMs)
		{
			m_PeakMs = Spread;
			m_PeakTime = Now;
		}
		else if(Now - m_PeakTime > (int64_t)(HOLD_SECONDS * Freq) && Dt > 0.0f)
		{
			m_PeakMs = Spread + (m_PeakMs - Spread) * std::exp(-Dt / DECAY_SECONDS);
		}
	}

	// Margin the network asks for right now (ms), never below Base.
	float TargetMs(int Base, int Max, int Safety) const
	{
		Max = std::max(Max, Base);
		if(!Valid())
			return (float)Base;
		return std::clamp(std::ceil(m_PeakMs) + (float)Safety, (float)Base, (float)Max);
	}

	// Moves the applied margin towards Target: fast up (+25 ms/s, so a needed
	// increase is there within a few ticks), down at -5 ms/s (1/4 tick per second,
	// too small to see, but extra input delay is gone within seconds).
	void Advance(float TargetMs, int64_t Now)
	{
		if(!m_LastAdvance)
		{
			m_LastAdvance = Now;
			return;
		}
		const float Dt = std::clamp((Now - m_LastAdvance) / (float)time_freq(), 0.0f, 0.1f);
		m_LastAdvance = Now;
		if(TargetMs > m_CurrentMs)
			m_CurrentMs = std::min(TargetMs, m_CurrentMs + 25.0f * Dt);
		else if(TargetMs < m_CurrentMs)
			m_CurrentMs = std::max(TargetMs, m_CurrentMs - 5.0f * Dt);
	}
	void SetCurrent(float Ms) { m_CurrentMs = Ms; }
	// deviations above this are spikes no margin up to Max can absorb
	void SetLimits(int Max, int Safety) { m_CoverMs = std::max(5.0f, (std::max(Max, 5) - Safety) * 1.5f); }
	float CurrentMs() const { return m_CurrentMs; }

	bool Valid() const { return m_Count >= MIN_SAMPLES; }
	float RttMs() const { return m_RttMs; }
	float JitterMs() const { return m_JitterMs; }
	float SpreadMs() const { return m_SpreadMs; }
	float TimeLeftMs() const { return m_TimeLeftMs; }
	int LateTotal() const { return m_LateTotal; }
	float LastLateMs() const { return m_LastLateMs; }
	int64_t LastLateTime() const { return m_LastLate; }
	bool RecentLate(int64_t Now) const { return m_LastLate && (Now - m_LastLate) < 3 * time_freq(); }

private:
	int RecentLateCount(int64_t Now, int64_t Within) const
	{
		int Num = 0;
		for(int64_t Time : m_aLateTimes)
			if(Time && Now - Time < Within)
				Num++;
		return Num;
	}

	std::array<float, WINDOW> m_aDev{};
	int m_Count = 0;
	int m_Index = 0;
	float m_RttMs = 0.0f;
	float m_JitterMs = 0.0f;
	float m_LastRttMs = -1.0f;
	float m_TimeLeftMs = 0.0f;
	float m_SpreadMs = 0.0f;
	float m_PeakMs = 0.0f;
	int64_t m_PeakTime = 0;
	int64_t m_LastSample = 0;
	int m_LateTotal = 0;
	int64_t m_LastLate = 0;
	float m_LastLateMs = 0.0f;
	std::array<int64_t, 8> m_aLateTimes{};
	int m_LateIndex = 0;
	float m_CurrentMs = 10.0f;
	int64_t m_LastAdvance = 0;
	int64_t m_WarmupUntil = 0;
	float m_CoverMs = 1e9f;
};

#endif

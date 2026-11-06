/* KING client: the crown signature hidden in skin colors. Shared by the game
   (crown in game and TAB) and the engine (server browser: KING players on
   every server, straight from the server list, no extra server needed). */
#ifndef ENGINE_SHARED_KING_BADGE_H
#define ENGINE_SHARED_KING_BADGE_H

#include <cstdint>
#include <initializer_list>

// 16 bits: hue 2, saturation 3, lightness 3 low bits of body and feet colors
inline constexpr int KING_BADGE_COLOR_MASK = 0x030707;

inline int KingBadgeApply(int Color, uint32_t Bits)
{
	return (Color & ~KING_BADGE_COLOR_MASK) | (int)((Bits & 3) << 16) | (int)(((Bits >> 2) & 7) << 8) | (int)((Bits >> 5) & 7);
}

inline uint32_t KingBadgeRead(int Color)
{
	return (uint32_t)((Color >> 16) & 3) | (uint32_t)(((Color >> 8) & 7) << 2) | (uint32_t)((Color & 7) << 5);
}

// FNV-1a over a tag, the name without whitespace/control bytes (servers clean
// those up) and the colors without the signature bits
inline uint32_t KingBadgeSignature(const char *pName, int Body, int Feet)
{
	uint32_t Hash = 2166136261u;
	auto Add = [&Hash](uint8_t Byte) {
		Hash ^= Byte;
		Hash *= 16777619u;
	};
	for(const char *p = "KING-crown-1"; *p; p++)
		Add((uint8_t)*p);
	for(const char *p = pName; *p; p++)
		if((uint8_t)*p > 32)
			Add((uint8_t)*p);
	for(int Color : {Body & ~KING_BADGE_COLOR_MASK, Feet & ~KING_BADGE_COLOR_MASK})
		for(int i = 0; i < 4; i++)
			Add((uint8_t)(Color >> (i * 8)));
	return (Hash ^ (Hash >> 16)) & 0xFFFF;
}

inline bool KingBadgeMarked(const char *pName, int Body, int Feet)
{
	if(!pName[0])
		return false;
	const uint32_t Signature = KingBadgeSignature(pName, Body, Feet);
	return KingBadgeRead(Body) == (Signature & 0xFF) && KingBadgeRead(Feet) == (Signature >> 8);
}

#endif

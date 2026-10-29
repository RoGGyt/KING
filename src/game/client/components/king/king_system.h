/* KING client: operating system tweaks (process priorities). Windows only, no-ops elsewhere. */
#ifndef GAME_CLIENT_COMPONENTS_KING_KING_SYSTEM_H
#define GAME_CLIENT_COMPONENTS_KING_KING_SYSTEM_H

// High (true) or normal (false) priority for KING itself. Returns false if it failed.
bool KingSetHighPriority(bool High);

// Below normal (true) or normal (false) priority for every running Discord process
// (Discord, Discord PTB, Discord Canary). Returns how many processes were changed.
int KingSetDiscordLowPriority(bool Low);

#endif

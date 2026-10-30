/* KING client: installs a downloaded release zip over the running client, in process
   (no scripts, no helper programs). */
#ifndef GAME_CLIENT_COMPONENTS_KING_KING_INSTALL_H
#define GAME_CLIENT_COMPONENTS_KING_KING_INSTALL_H

#include <cstddef>

// Unpacks pZipPath (a KING-x.y.z-win64.zip, optionally with one top folder) into
// pDestDir. Everything is unpacked to a staging folder first and checked (CRC);
// only then are the files moved into place. Files that are in use (the running
// KING.exe, its DLLs) are renamed to *.king_old first, which Windows allows.
// Returns false and fills pError if nothing could be installed.
bool KingInstallZip(const char *pZipPath, const char *pDestDir, char *pError, size_t ErrorSize);

// Removes *.king_old files left over by the previous update. Returns how many.
int KingCleanupOldFiles(const char *pDir);

#endif

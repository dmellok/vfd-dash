#pragma once

#include "models.h"

// Persistent cache for the slow / network-dependent stats so the very first
// frame after a reboot can show known-last values instead of "waiting...".
// Files live alongside settings.bin in LittleFS.
void cacheLoadCats   (Cats& out);
void cacheLoadClaude (ClaudeUsage& out);
void cacheSaveCats   (const Cats& cats);
void cacheSaveClaude (const ClaudeUsage& claude);

// Last printing-state wall-clock timestamp (UTC seconds). Used by the Prusa
// page to fall back to a "waiting for print" message after extended idle.
// Returns 0 if never recorded.
time_t cacheLoadLastPrint();
void   cacheSaveLastPrint(time_t epoch);

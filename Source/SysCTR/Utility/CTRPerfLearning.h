#pragma once

#include "Utility/DaedalusTypes.h"

namespace CTRPerfLearning
{
// Start a per-cartridge profile session using the stable N64 ROM identity.
void BeginGame(u32 crc1, u32 crc2, u8 country, u32 romSize);
void EndGame();

// Applies a conservative learned policy. New 3DS uses the existing full-ROM
// path when size permits; Old 3DS only tries it after measured cache I/O stalls.
bool ShouldPreloadROM(u32 romSize, bool isNew3DS);

void RecordRomLoad(bool preloadAttempted, bool fullRomLoaded,
                   bool succeeded, u64 elapsedTicks);
void RecordCacheRead(u32 bytes, u64 elapsedTicks, bool succeeded);
void RecordFrame(u64 elapsedTicks, u64 targetFrameTicks);
}

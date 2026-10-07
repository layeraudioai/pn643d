#pragma once

// Logical PICA viewport used by the game renderer. Nintenstation643D renders
// into a smaller centered viewport to reduce pixel work while preserving the
// normal 400x240 UI/screen target.
#ifdef DAEDALUS_NINTENSTATION643D
#define CTR_GAME_VIEW_WIDTH 360
#define CTR_GAME_VIEW_HEIGHT 216
#define CTR_GAME_VIEW_X 20
#define CTR_GAME_VIEW_Y 12
#else
#define CTR_GAME_VIEW_WIDTH 400
#define CTR_GAME_VIEW_HEIGHT 240
#define CTR_GAME_VIEW_X 0
#define CTR_GAME_VIEW_Y 0
#endif

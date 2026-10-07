#pragma once

// The physical 3DS top-screen target is 400x240. CTRGetRenderWidth/Height
// return the logical canvas dimensions; the game viewport and scissor are
// mapped to this physical output. A larger logical canvas is not SSAA by itself.
#define CTR_OUTPUT_WIDTH 400
#define CTR_OUTPUT_HEIGHT 240

#ifdef DAEDALUS_NINTENSTATION643D
#define CTR_GAME_VIEW_WIDTH 360
#define CTR_GAME_VIEW_HEIGHT 216
#else
#define CTR_GAME_VIEW_WIDTH CTR_OUTPUT_WIDTH
#define CTR_GAME_VIEW_HEIGHT CTR_OUTPUT_HEIGHT
#endif

u32 CTRGetRenderWidth();
u32 CTRGetRenderHeight();

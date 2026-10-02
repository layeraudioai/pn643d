/*
 * Compile-time hooks for an optional single-game build profile.
 *
 * A game ID of zero keeps the normal all-ROM build. Profile headers are
 * selected by CMake for dedicated builds and may provide
 * DAEDALUS_GAME_PROFILE_APPLY() for compatibility changes to g_ROM.
 */
#ifndef DAEDALUS_GAME_BUILD_CONFIG_H
#define DAEDALUS_GAME_BUILD_CONFIG_H

#ifndef DAEDALUS_TARGET_GAME_ID
#define DAEDALUS_TARGET_GAME_ID 0
#endif

#if DAEDALUS_TARGET_GAME_ID != 0 && defined(DAEDALUS_GAME_PROFILE_HEADER)
#include DAEDALUS_GAME_PROFILE_HEADER
#endif

#ifndef DAEDALUS_GAME_PROFILE_APPLY
#define DAEDALUS_GAME_PROFILE_APPLY() ((void)0)
#endif

#define DAEDALUS_GAME_BUILD_IS(cart_id) \
    (DAEDALUS_TARGET_GAME_ID == (cart_id))

#endif /* DAEDALUS_GAME_BUILD_CONFIG_H */

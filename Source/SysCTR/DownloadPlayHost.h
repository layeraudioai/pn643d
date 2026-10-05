#pragma once

namespace CTRDownloadPlayHost
{
	enum State
	{
		STATE_UNAVAILABLE,
		STATE_OFF,
		STATE_ACCEPTING,
		STATE_DISTRIBUTING,
		STATE_FINISHED,
		STATE_ERROR
	};

	// Host a bundled, ROM-specific Download Play child. This API is compiled
	// into the host title only when CMake was given DAEDALUS_DOWNLOADPLAY_CHILD_CIA.
	bool Start();
	bool BeginDistribution();
	void Tick();
	void Stop();
	State GetState();
	unsigned GetClientCount();
	unsigned GetProgressPercent();
	const char *GetStatus();
}

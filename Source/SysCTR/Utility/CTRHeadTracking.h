#pragma once

namespace CTRHeadTracking
{
	// Initializes QTM head tracking when this libctru / console combination supports it.
	void Initialize();
	void Shutdown();
	void SetEnabled(bool enabled);
	void Update();
	bool IsAvailable();
}

#include "stdafx.h"
#include "CTRHeadTracking.h"

#include <3ds.h>
#include <math.h>
#include <GL/picaGL.h>

#if defined(DAEDALUS_CTR_HAS_QTM) && !defined(DAEDALUS_MINIMAL_EMULATOR) && !defined(DAEDALUS_DOWNLOADPLAY)
#include <3ds/services/qtm.h>
#endif

namespace CTRHeadTracking
{
#if defined(DAEDALUS_CTR_HAS_QTM) && !defined(DAEDALUS_MINIMAL_EMULATOR) && !defined(DAEDALUS_DOWNLOADPLAY)
static bool sAvailable = false;
static bool sEnabled = false;
static bool sHaveCenter = false;
static float sCenterX = 0.0f;
static float sCenterY = 0.0f;
static float sOffsetX = 0.0f;
static float sOffsetY = 0.0f;

static float ClampOffset(float value)
{
	if (value < -0.20f) return -0.20f;
	if (value >  0.20f) return  0.20f;
	return value;
}
#endif

void Initialize()
{
#if defined(DAEDALUS_CTR_HAS_QTM) && !defined(DAEDALUS_MINIMAL_EMULATOR) && !defined(DAEDALUS_DOWNLOADPLAY)
	// QTM owns the New 3DS inner camera and IR emitter. Do not open CAMU or
	// capture camera frames here: doing so would prevent the system tracker
	// from operating. QTM is absent on O3DS and may be unavailable on N2DSXL.
	if (qtmCheckServicesRegistered() && R_SUCCEEDED(qtmInit(QTM_SERVICE_USER)))
	{
		QtmTrackingData probe;
		if (R_SUCCEEDED(QTMU_GetTrackingData(&probe)))
			sAvailable = true;
		else
			qtmExit();
	}
#endif
}

void Shutdown()
{
#if defined(DAEDALUS_CTR_HAS_QTM) && !defined(DAEDALUS_MINIMAL_EMULATOR) && !defined(DAEDALUS_DOWNLOADPLAY)
	if (sAvailable)
		qtmExit();
	sAvailable = false;
	sEnabled = false;
	sHaveCenter = false;
	sOffsetX = sOffsetY = 0.0f;
	pglSetStereoHeadOffset(0.0f, 0.0f);
#endif
}

void SetEnabled(bool enabled)
{
#if defined(DAEDALUS_CTR_HAS_QTM) && !defined(DAEDALUS_MINIMAL_EMULATOR) && !defined(DAEDALUS_DOWNLOADPLAY)
	if (enabled == sEnabled)
		return;
	sEnabled = enabled && sAvailable;
	sHaveCenter = false;
	sOffsetX = sOffsetY = 0.0f;
	pglSetStereoHeadOffset(0.0f, 0.0f);
#else
	(void)enabled;
#endif
}

void Update()
{
#if defined(DAEDALUS_CTR_HAS_QTM) && !defined(DAEDALUS_MINIMAL_EMULATOR) && !defined(DAEDALUS_DOWNLOADPLAY)
	if (!sAvailable || !sEnabled)
		return;

	QtmTrackingData data;
	const Result result = QTMU_GetTrackingData(&data);
	if (R_FAILED(result) || !data.headTracked || data.confidenceLevel < 0.20f)
	{
		// Re-center after reacquisition rather than applying a stale offset.
		sHaveCenter = false;
		sOffsetX *= 0.75f;
		sOffsetY *= 0.75f;
		pglSetStereoHeadOffset(sOffsetX, sOffsetY);
		return;
	}

	const float eyeX = 0.5f * (data.eyeWorldCoordinates[QTM_EYE_LEFT][0] +
		data.eyeWorldCoordinates[QTM_EYE_RIGHT][0]);
	const float eyeY = 0.5f * (data.eyeWorldCoordinates[QTM_EYE_LEFT][1] +
		data.eyeWorldCoordinates[QTM_EYE_RIGHT][1]);
	if (!isfinite(eyeX) || !isfinite(eyeY))
		return;

	if (!sHaveCenter)
	{
		sCenterX = eyeX;
		sCenterY = eyeY;
		sHaveCenter = true;
	}

	// QTM world coordinates are tangent-of-angle values. A gentle gain and
	// hard bound give a useful motion-parallax cue without large camera jumps.
	float targetX = ClampOffset((eyeX - sCenterX) * 0.30f);
	float targetY = ClampOffset((eyeY - sCenterY) * 0.30f);
	if (fabsf(targetX) < 0.006f) targetX = 0.0f;
	if (fabsf(targetY) < 0.006f) targetY = 0.0f;

	// Low-pass filter QTM updates to suppress camera/gyro jitter on the ARM11.
	sOffsetX += (targetX - sOffsetX) * 0.25f;
	sOffsetY += (targetY - sOffsetY) * 0.25f;
	pglSetStereoHeadOffset(sOffsetX, sOffsetY);
#endif
}

bool IsAvailable()
{
#if defined(DAEDALUS_CTR_HAS_QTM) && !defined(DAEDALUS_MINIMAL_EMULATOR) && !defined(DAEDALUS_DOWNLOADPLAY)
	return sAvailable;
#else
	return false;
#endif
}
}

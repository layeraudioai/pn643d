#include <stdio.h>
#include "internal.h"

static aptHookCookie _hookCookie;

static void _AptEventHook(APT_HookType type, void* param)
{
	switch (type)
	{
		case APTHOOK_ONSUSPEND:
			/* Submit queued draws before the system applet takes over the GPU. */
			glFinish();
			break;
		case APTHOOK_ONRESTORE:
			GX_BindQueue(&pglState->gxQueue);
			gxCmdQueueRun(&pglState->gxQueue);
			/* Applets may leave PICA registers and the active command buffer in
			 * an unknown state. Start a fresh list, rebind both render targets,
			 * and re-emit picaGL's baseline before the emulator draws again. */
			_pglResetCommandBuffer();
			pglState->stereoRightEye = GL_FALSE;
			gfxSet3D(pglState->stereoEnabled != GL_FALSE);
			_picaRenderBuffer(pglState->colorBuffer, pglState->depthBuffer);
			_picaAttribBuffersLocation((void*)__ctru_linear_heap);
			shaderProgramUse(&pglState->basicShader);
			_stateDefault();
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			pglState->textureChanged = GL_TRUE;
			pglState->changes = STATE_ALL_CHANGE;
			break;
		default:
			break;
	}
}

void pglInit()
{
	static int pgl_initialized = 0;
	if (pgl_initialized)
		return;

	pglState = malloc(sizeof(picaGLState));
	if (!pglState)
		return;
	memset(pglState, 0, sizeof(picaGLState));
	_stateInitialize();
	_stateDefault();
	aptHook(&_hookCookie, _AptEventHook, NULL);
	pgl_initialized = 1;
}

void pglExit()
{
	if (!pglState)
		return;
	aptUnhook(&_hookCookie);
	_queueWaitAndClear();
	GX_BindQueue(NULL);
	// Existing picaGL resources are retained for the application lifetime.
}

bool _pglStereoIsActive(void)
{
	if (!pglState || !pglState->stereoEnabled ||
		pglState->display != GFX_TOP || !gfxIs3D())
		return false;
	return osGet3DSliderState() > 0.0f;
}

void _pglSetStereoRenderTarget(bool right_eye)
{
	if (!pglState)
		return;
	GLboolean next_eye = right_eye ? GL_TRUE : GL_FALSE;
	if (pglState->stereoRightEye == next_eye)
		return;

	/* PICA keeps color/depth writes in an internal framebuffer cache. Flush
	 * that cache before changing render targets, otherwise alternating eye
	 * targets can lose or corrupt draws. */
	_picaFinalize(true);
	pglState->stereoRightEye = next_eye;
	if (right_eye)
		_picaRenderBuffer(pglState->stereoColorBuffer, pglState->stereoDepthBuffer);
	else
		_picaRenderBuffer(pglState->colorBuffer, pglState->depthBuffer);
}

static void _pglTransferToFramebuffer(uint32_t *source_buffer,
		uint32_t *output_framebuffer, uint8_t output_format)
{
	if (pglState->display == GFX_TOP)
	{
		if (pglState->renderWidth > 400 || pglState->renderHeight > 240)
			GX_DisplayTransfer((u32*)source_buffer,
				GX_BUFFER_DIM(pglState->renderHeight, pglState->renderWidth),
				output_framebuffer, GX_BUFFER_DIM(240, 400),
				GX_TRANSFER_OUT_FORMAT(output_format) | GX_TRANSFER_SCALE_XY);
		else
			GX_DisplayTransfer((u32*)source_buffer, GX_BUFFER_DIM(240, 400),
				output_framebuffer, GX_BUFFER_DIM(240, 400),
				GX_TRANSFER_OUT_FORMAT(output_format));
	}
	else
	{
		GX_DisplayTransfer((u32*)source_buffer + (240 * 80), GX_BUFFER_DIM(240, 320),
			output_framebuffer, GX_BUFFER_DIM(240, 320),
			GX_TRANSFER_OUT_FORMAT(output_format));
	}
}

void pglSwapBuffers()
{
	/* The eye buffers are read immediately below. Wait for both queued eye
	 * renders to finish before starting display transfers, or the transfer can
	 * race the GPU and expose partially rendered textures/depth surfaces. */
	glFinish();
	uint8_t output_format = gfxGetScreenFormat(pglState->display);
	bool has_stereo = _pglStereoIsActive();
	/* Keep the 3DS top screen in dual-framebuffer (800-pixel combined) mode
	 * whenever stereo is enabled. At the bottom of the hardware 3D slider,
	 * render once and mirror that image to both eyes; raising the slider switches
	 * to the independently rendered stereoscopic image automatically. */
	bool stereo_output = pglState->stereoEnabled &&
		pglState->display == GFX_TOP && gfxIs3D();
	pglState->stereoActive = has_stereo ? GL_TRUE : GL_FALSE;

	if (stereo_output)
	{
		uint32_t *left_framebuffer = (uint32_t*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
		uint32_t *right_framebuffer = (uint32_t*)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
		_pglTransferToFramebuffer(pglState->colorBuffer, left_framebuffer, output_format);
		_pglTransferToFramebuffer(has_stereo ? pglState->stereoColorBuffer : pglState->colorBuffer,
			right_framebuffer, output_format);
	}
	else
	{
		uint32_t *output_framebuffer = (uint32_t*)gfxGetFramebuffer(pglState->display, pglState->display_side, NULL, NULL);
		_pglTransferToFramebuffer(pglState->colorBuffer, output_framebuffer, output_format);
	}

	_queueRun(false);
	gfxScreenSwapBuffers(pglState->display, stereo_output);
}

void pglSelectScreen(unsigned display, unsigned side)
{
	pglState->display = display;
	pglState->display_side = side;
}

void pglSetStereo(int enabled, float separation)
{
	if (!pglState)
		return;
	pglState->stereoEnabled = enabled ? GL_TRUE : GL_FALSE;
	/* Keep parallax strong enough to read, but bounded to avoid excessive
	 * eye-to-eye divergence and edge discomfort. */
	if (separation < 0.0f) separation = 0.0f;
	if (separation > 0.20f) separation = 0.20f;
	pglState->stereoSeparation = separation;
	gfxSet3D(enabled != 0);
}

void pglSetStereoParallax(int enabled)
{
	if (pglState)
		pglState->stereoParallax = enabled ? GL_TRUE : GL_FALSE;
}

void pglSetStereoPopout(int enabled)
{
	if (pglState)
		pglState->stereoPopout = enabled ? GL_TRUE : GL_FALSE;
}

void pglSetStereoHeadOffset(float x, float y)
{
	if (!pglState)
		return;
	/* Keep external tracking values bounded even if a platform API glitches. */
	if (x < -0.20f) x = -0.20f;
	if (x >  0.20f) x =  0.20f;
	if (y < -0.20f) y = -0.20f;
	if (y >  0.20f) y =  0.20f;
	pglState->stereoHeadOffsetX = x;
	pglState->stereoHeadOffsetY = y;
}

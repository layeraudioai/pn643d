#include <stdio.h>
#include "internal.h"

static aptHookCookie _hookCookie;

static void _AptEventHook(APT_HookType type, void* param)
{
	switch (type)
	{
		case APTHOOK_ONSUSPEND:
			_queueWaitAndClear();
			break;
		case APTHOOK_ONRESTORE:
			GX_BindQueue(&pglState->gxQueue);
			gxCmdQueueRun(&pglState->gxQueue);
			_picaRenderBuffer(pglState->colorBuffer, pglState->depthBuffer);
			_picaAttribBuffersLocation((void*)__ctru_linear_heap);
			for (int i = 1; i < 6; i++)
				_picaTextureEnvSet(i, &pglState->texenv[PGL_TEXENV_DUMMY]);
			shaderProgramUse(&pglState->basicShader);
			pglState->changes |= 0xFFFFFFFF;
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
	pglState->stereoRightEye = right_eye ? GL_TRUE : GL_FALSE;
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
	glFlush();
	uint8_t output_format = gfxGetScreenFormat(pglState->display);
	bool has_stereo = _pglStereoIsActive();
	pglState->stereoActive = has_stereo ? GL_TRUE : GL_FALSE;

	if (has_stereo)
	{
		uint32_t *left_framebuffer = (uint32_t*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
		uint32_t *right_framebuffer = (uint32_t*)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
		_pglTransferToFramebuffer(pglState->colorBuffer, left_framebuffer, output_format);
		_pglTransferToFramebuffer(pglState->stereoColorBuffer, right_framebuffer, output_format);
	}
	else
	{
		uint32_t *output_framebuffer = (uint32_t*)gfxGetFramebuffer(pglState->display, pglState->display_side, NULL, NULL);
		_pglTransferToFramebuffer(pglState->colorBuffer, output_framebuffer, output_format);
	}

	_queueRun(false);
	gfxScreenSwapBuffers(pglState->display, has_stereo);
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
	pglState->stereoSeparation = separation < 0.0f ? 0.0f : separation;
	gfxSet3D(enabled != 0);
}

void pglSetStereoParallax(int enabled)
{
	if (pglState)
		pglState->stereoParallax = enabled ? GL_TRUE : GL_FALSE;
}

#include <stdio.h>
#include "internal.h"

static aptHookCookie _hookCookie;
static int g_stereo_enabled = 1;
static float g_stereo_separation = 0.02f;
static int g_stereo_parallax = 1;

static void _AptEventHook(APT_HookType type, void* param)
{

	switch (type)
	{
		case APTHOOK_ONSUSPEND:
		{
			_queueWaitAndClear();
			break;
		}
		case APTHOOK_ONRESTORE:
		{
			GX_BindQueue(&pglState->gxQueue);
			gxCmdQueueRun(&pglState->gxQueue);

			_picaRenderBuffer(pglState->colorBuffer, pglState->depthBuffer);
			pglState->stereoEye = 0;
			_picaAttribBuffersLocation((void*)__ctru_linear_heap);

			for(int i = 1; i < 6; i++)
				_picaTextureEnvSet(i, &pglState->texenv[PGL_TEXENV_DUMMY]);

			shaderProgramUse(&pglState->basicShader);
			pglState->changes |= 0xFFFFFFFF;
			break;
		}
		default:
			break;
	}
}

void pglInit()
{
	static int pgl_initialized = 0;

	if(pgl_initialized)
		return;

	pglState = malloc(sizeof(picaGLState));
	memset(pglState, 0, sizeof(picaGLState));

	_stateInitialize();
	_stateDefault();

	aptHook(&_hookCookie, _AptEventHook, NULL);
}

void pglExit()
{
	aptUnhook(&_hookCookie);

	_queueWaitAndClear();
	GX_BindQueue(NULL);

	if (pglState->stereoColorBuffer)
		vramFree(pglState->stereoColorBuffer);
	if (pglState->stereoDepthBuffer)
		vramFree(pglState->stereoDepthBuffer);
	pglState->stereoColorBuffer = NULL;
	pglState->stereoDepthBuffer = NULL;

	//TODO: Clear remaining picaGL resources
}

static void _pglTransferToFramebuffer(uint32_t *output_framebuffer, uint8_t output_format)
{
	if(pglState->display == GFX_TOP)
	{
		GX_DisplayTransfer(
			(u32*)pglState->colorBuffer, GX_BUFFER_DIM(240, 400),
			output_framebuffer, GX_BUFFER_DIM(240, 400),
			GX_TRANSFER_OUT_FORMAT(output_format));
	}
	else
	{
		GX_DisplayTransfer(
			(u32*)pglState->colorBuffer + (240*80), GX_BUFFER_DIM(240, 320),
			output_framebuffer, GX_BUFFER_DIM(240, 320),
			GX_TRANSFER_OUT_FORMAT(output_format));
	}
}

static void _pglTransferToFramebufferStereo(uint32_t *left_fb, uint32_t *right_fb, uint8_t output_format)
{
	if (pglState->display != GFX_TOP)
		return;

	GX_DisplayTransfer(
		pglState->colorBuffer, GX_BUFFER_DIM(240, 400),
		left_fb, GX_BUFFER_DIM(240, 400),
		GX_TRANSFER_OUT_FORMAT(output_format));

	if (pglState->stereoColorBuffer)
	{
		GX_DisplayTransfer(
			pglState->stereoColorBuffer, GX_BUFFER_DIM(240, 400),
			right_fb, GX_BUFFER_DIM(240, 400),
			GX_TRANSFER_OUT_FORMAT(output_format));
	}
	else
	{
		/* Preserve a usable mono image if the right-eye VRAM allocation failed. */
		GX_DisplayTransfer(
			pglState->colorBuffer, GX_BUFFER_DIM(240, 400),
			right_fb, GX_BUFFER_DIM(240, 400),
			GX_TRANSFER_OUT_FORMAT(output_format));
	}
}

bool _pglStereoActive(void)
{
	return g_stereo_enabled && pglState &&
		pglState->display == GFX_TOP && gfxIs3D() &&
		pglState->stereoColorBuffer && pglState->stereoDepthBuffer;
}

void _pglSelectStereoTarget(int right_eye)
{
	if (right_eye && _pglStereoActive())
	{
		if (pglState->stereoEye != 1)
		{
			_picaRenderBuffer(pglState->stereoColorBuffer, pglState->stereoDepthBuffer);
			pglState->stereoEye = 1;
		}
	}
	else if (pglState->stereoEye != 0)
	{
		_picaRenderBuffer(pglState->colorBuffer, pglState->depthBuffer);
		pglState->stereoEye = 0;
	}
}

void _pglSetStereoProjection(int right_eye)
{
	matrix4x4 projection;
	matrix4x4 mvp;
	float slider;
	float eye_offset;

	if (!_pglStereoActive())
		return;

	slider = osGet3DSliderState();
	if (slider < 0.0f) slider = 0.0f;
	if (slider > 1.0f) slider = 1.0f;
	eye_offset = g_stereo_parallax ?
		g_stereo_separation * slider * (right_eye ? 0.5f : -0.5f) : 0.0f;
	matrix4x4_copy(&projection, &pglState->matrix_projection);

	/* Off-axis projection: the z-dependent term makes disparity depend on
	 * scene depth, unlike a screen-space image shift. Orthographic views use
	 * a small clip-space offset instead. */
	if (projection.row[3].w == 0.0f)
		projection.row[0].z += eye_offset * projection.row[0].x;
	else
		projection.row[0].w += eye_offset;

	matrix4x4_multiply(&mvp, &projection, &pglState->matrix_modelview);
	_picaUniformFloat(GPU_VERTEX_SHADER, 0, (float*)&mvp, 4);
}

void pglSetStereo(int enabled, float separation)
{
	g_stereo_enabled = enabled != 0;
	/* Separation is the maximum normalized off-axis camera baseline. */
	if (separation < 0.0f) separation = -separation;
	if (separation > 0.10f) separation = 0.10f;
	g_stereo_separation = separation;
	gfxSet3D(g_stereo_enabled);
	gfxSetWide(!g_stereo_enabled);
}

void pglSetStereoParallax(int enabled)
{
	g_stereo_parallax = enabled != 0;
}

void pglSwapBuffers()
{
	glFlush();

	uint8_t output_format = gfxGetScreenFormat(pglState->display);
	bool has_stereo = _pglStereoActive();

	if(has_stereo)
	{
		uint32_t *left_framebuffer = (uint32_t*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
		uint32_t *right_framebuffer = (uint32_t*)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
		_pglTransferToFramebufferStereo(left_framebuffer, right_framebuffer, output_format);
	}
	else
	{
		uint32_t *output_framebuffer = (uint32_t*)gfxGetFramebuffer(pglState->display, pglState->display_side, NULL, NULL);
		_pglTransferToFramebuffer(output_framebuffer, output_format);
	}

	_queueRun(false);

	gfxScreenSwapBuffers(pglState->display, has_stereo);
}

void pglSelectScreen(unsigned display, unsigned side)
{
	pglState->display = display;
	pglState->display_side = side;
}

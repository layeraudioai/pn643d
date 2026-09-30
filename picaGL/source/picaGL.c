#include <stdio.h>
#include "internal.h"

static aptHookCookie _hookCookie;

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

	//TODO: Clear memory
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
	if(pglState->display == GFX_TOP)
	{
		GX_DisplayTransfer(
			(u32*)pglState->colorBuffer, GX_BUFFER_DIM(240, 400),
			left_fb, GX_BUFFER_DIM(240, 400),
			GX_TRANSFER_OUT_FORMAT(output_format));

		float slider = osGet3DSliderState();
		int shift = (int)(slider * 4.0f); // Parallax disparity shift for true 3D stereo

		if (shift != 0)
		{
			static uint32_t *right_buf = NULL;
			if (!right_buf)
			{
				right_buf = (uint32_t*)linearAlloc(400 * 240 * 4);
			}
			if (right_buf)
			{
				memset(right_buf, 0, 400 * 240 * 4);
				uint32_t *src = (uint32_t*)pglState->colorBuffer;
				uint32_t *dst = right_buf;
				
				for (int y = 0; y < 240; y++)
				{
					for (int x = 0; x < 400; x++)
					{
						int src_x = x - shift;
						if (src_x >= 0 && src_x < 400)
						{
							dst[y * 400 + x] = src[y * 400 + src_x];
						}
					}
				}
				GSPGPU_FlushDataCache(right_buf, 400 * 240 * 4);
				GX_DisplayTransfer(
					right_buf, GX_BUFFER_DIM(240, 400),
					right_fb, GX_BUFFER_DIM(240, 400),
					GX_TRANSFER_OUT_FORMAT(output_format));
				return;
			}
		}

		GX_DisplayTransfer(
			(u32*)pglState->colorBuffer, GX_BUFFER_DIM(240, 400),
			right_fb, GX_BUFFER_DIM(240, 400),
			GX_TRANSFER_OUT_FORMAT(output_format));
	}
}

void pglSwapBuffers()
{
	glFlush();

	uint8_t output_format = gfxGetScreenFormat(pglState->display);
	bool has_stereo = pglState->display == GFX_TOP && gfxIs3D();

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

static int g_stereo_enabled = 1;
static float g_stereo_separation = 0.02f;
static int g_stereo_parallax = 1;

void pglSetStereo(int enabled, float separation)
{
	g_stereo_enabled = enabled;
	g_stereo_separation = separation;
	gfxSet3D(enabled);
	gfxSetWide(!enabled);
}

void pglSetStereoParallax(int enabled)
{
	g_stereo_parallax = enabled;
}
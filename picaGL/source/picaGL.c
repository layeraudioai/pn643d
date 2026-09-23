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

void pglSwapBuffers()
{
	glFlush();

	uint8_t output_format = gfxGetScreenFormat(pglState->display);
	bool has_stereo = pglState->display == GFX_TOP && gfxIs3D();

	if(has_stereo)
	{
		// In 3D mode libctru presents the right-eye image from the second half
		// of the top framebuffer only when hasStereo is true. picaGL renders to
		// a single color buffer, so copy that image to both eye buffers. This
		// keeps the stereo framebuffer valid and avoids an uninitialized right
		// eye; producing real parallax still requires rendering the scene twice
		// with different eye projections.
		uint32_t *left_framebuffer = (uint32_t*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
		uint32_t *right_framebuffer = (uint32_t*)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
		_pglTransferToFramebuffer(left_framebuffer, output_format);
		_pglTransferToFramebuffer(right_framebuffer, output_format);
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
#ifndef __PICAGL_H__
#define __PICAGL_H__

#include <GL/gl.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the render target before pglInit. Requests above 400x240 enable
 * a 2x2 off-screen target, resolved to the display with GX anti-aliasing. */
void pglSetRenderSize(unsigned width, unsigned height);
unsigned pglGetRenderWidth(void);
unsigned pglGetRenderHeight(void);
void pglInit();
void pglExit();
void pglSwapBuffers();
void pglSelectScreen(unsigned display, unsigned side);

/* Nintendo 3DS stereoscopic rendering support. Each scene draw is rendered
 * into separate left/right color and depth buffers. `separation` controls the
 * symmetric off-axis eye projection; the hardware 3D slider scales it. */
void pglSetStereo(int enabled, float separation);
/* Keep stereo output but suppress the per-eye projection offset when disabled. */
void pglSetStereoParallax(int enabled);
/* Reverse the eye disparity so the scene appears in front of the screen. */
void pglSetStereoPopout(int enabled);
/* Apply a small normalized viewer-position offset to both stereo eyes. */
void pglSetStereoHeadOffset(float x, float y);

/* Transferable picaGL shader cache support. Import before pglInit(); export
 * after pglInit(). The supplied buffers must remain valid until pglInit(). */
void pglSetShaderCache(const void *vertex_shader, size_t vertex_size,
                       const void *clear_shader, size_t clear_size);
void pglGetShaderCache(const void **vertex_shader, size_t *vertex_size,
                       const void **clear_shader, size_t *clear_size);

#ifdef __cplusplus
}
#endif

#endif
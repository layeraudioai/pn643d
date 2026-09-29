#ifndef __PICAGL_H__
#define __PICAGL_H__

#include <GL/gl.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void pglInit();
void pglExit();
void pglSwapBuffers();
void pglSelectScreen(unsigned display, unsigned side);

/* Nintendo 3DS stereoscopic rendering support. */
void pglSetStereo(int enabled, float separation);
void pglSetStereoParallax(int enabled);

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
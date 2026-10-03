/* Minimal LZ4 block codec used by the seekable CTR ROM container. */
#ifndef DAEDALUS_LZ4_BLOCK_H
#define DAEDALUS_LZ4_BLOCK_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int DaedalusLZ4_decompress_safe(const unsigned char *src, int src_size,
                                unsigned char *dst, int dst_capacity);
int DaedalusLZ4_compress(const unsigned char *src, int src_size,
                         unsigned char *dst, int dst_capacity);
#ifdef __cplusplus
}
#endif
#endif

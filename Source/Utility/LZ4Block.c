/* Compact, bounds-checked LZ4 block codec. LZ4 block format, BSD-2-Clause style. */
#include "LZ4Block.h"
#include <stdint.h>
#include <string.h>

int DaedalusLZ4_decompress_safe(const unsigned char *src, int src_size,
                                unsigned char *dst, int dst_capacity)
{
    const unsigned char *ip = src, *iend = src + src_size;
    unsigned char *op = dst, *oend = dst + dst_capacity;
    if (src_size < 0 || dst_capacity < 0) return -1;
    while (ip < iend) {
        unsigned token = *ip++, lit = token >> 4, match = token & 15;
        if (lit == 15) { unsigned x; do { if (ip >= iend) return -1; x=*ip++; if (lit > (unsigned)-1-x) return -1; lit += x; } while (x == 255); }
        if ((size_t)(iend-ip) < lit || (size_t)(oend-op) < lit) return -1;
        memcpy(op, ip, lit); ip += lit; op += lit;
        if (ip == iend) return (int)(op-dst);
        if (iend-ip < 2) return -1;
        unsigned offset = (unsigned)ip[0] | ((unsigned)ip[1] << 8); ip += 2;
        if (!offset || offset > (unsigned)(op-dst)) return -1;
        if (match == 15) { unsigned x; do { if (ip >= iend) return -1; x=*ip++; if (match > (unsigned)-1-x) return -1; match += x; } while (x == 255); }
        match += 4;
        if ((size_t)(oend-op) < match) return -1;
        unsigned char *ref = op-offset;
        while (match--) *op++ = *ref++;
    }
    return (int)(op-dst);
}

static int emit_len(unsigned char **op, unsigned char *end, int n)
{
    while (n >= 255) { if (*op >= end) return 0; *(*op)++=255; n-=255; }
    if (*op >= end) return 0;
    *(*op)++=(unsigned char)n;
    return 1;
}

int DaedalusLZ4_compress(const unsigned char *src, int src_size,
                         unsigned char *dst, int dst_capacity)
{
    int table[1 << 16];
    const unsigned char *ip=src, *anchor=src, *iend=src+src_size;
    unsigned char *op=dst, *oend=dst+dst_capacity;
    if (src_size < 0 || dst_capacity < 0) return 0;
    for (int i=0;i<(1<<16);++i) table[i]=-1;
    while ((size_t)(iend - ip) >= 12) {
        uint32_t v; memcpy(&v,ip,4);
        unsigned h=(unsigned)((v * 2654435761u) >> 16);
        int prev=table[h]; table[h]=(int)(ip-src);
        if (prev < 0 || (int)(ip-src)-prev > 65535 || memcmp(src+prev,ip,4)!=0) { ++ip; continue; }
        const unsigned char *match=src+prev;
        while (ip>anchor && match>src && ip[-1]==match[-1]) { --ip; --match; }
        int lit=(int)(ip-anchor), mlen=4;
        while (ip+mlen < iend-5 && match[mlen]==ip[mlen]) ++mlen;
        if (op >= oend) return 0;
        unsigned char *token=op++; *token=(unsigned char)((lit<15?lit:15)<<4) | (unsigned char)((mlen-4<15)?(mlen-4):15);
        if (lit>=15 && !emit_len(&op,oend,lit-15)) return 0;
        if (oend-op < lit+2) return 0;
        memcpy(op,anchor,(size_t)lit); op+=lit;
        unsigned offset=(unsigned)(ip-match); *op++=(unsigned char)offset; *op++=(unsigned char)(offset>>8);
        if (mlen-4>=15 && !emit_len(&op,oend,mlen-4-15)) return 0;
        ip += mlen; anchor=ip;
        /* Seed nearby positions to improve compression while keeping the search bounded. */
        if ((size_t)(iend-ip) < 12) break;
        const unsigned char *p=ip-2;
        uint32_t q; memcpy(&q,p,4); table[(unsigned)((q*2654435761u)>>16)]=(int)(p-src);
    }
    int lit=(int)(iend-anchor);
    if (op>=oend) return 0;
    unsigned char *token=op++; *token=(unsigned char)((lit<15?lit:15)<<4);
    if (lit>=15 && !emit_len(&op,oend,lit-15)) return 0;
    if (oend-op < lit) return 0;
    memcpy(op,anchor,(size_t)lit); op+=lit;
    return (int)(op-dst);
}

.syntax unified
.arm
.align 2

/*
 * ARM11 / ARMv6K bulk word copy for memcpy_byteswap's aligned fast path.
 *
 * void memcpy_words_arm(void *dst, const void *src, size_t size)
 * Preconditions: dst and src are 4-byte aligned; size is a multiple of 32.
 * Copies words verbatim. The caller handles byte-lane swizzling for any
 * unaligned/prefix/tail bytes through the portable implementation.
 */
.global memcpy_words_arm
.type memcpy_words_arm, %function
memcpy_words_arm:
    push    {r4-r10, lr}
1:
    cmp     r2, #32
    blo     2f
    ldmia   r1!, {r3-r10}
    stmia   r0!, {r3-r10}
    subs    r2, r2, #32
    bne     1b
2:
    pop     {r4-r10, pc}
.size memcpy_words_arm, .-memcpy_words_arm

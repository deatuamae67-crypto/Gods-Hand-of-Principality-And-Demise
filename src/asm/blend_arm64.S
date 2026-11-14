// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.
//
// The note renderer's inner loop in ARM64 assembly (NEON), for Android phones,
// Apple Silicon Macs and ARM Linux; blend_x64.S is the same for x86-64.
//
//   void gh_blend_row_neon(uint32_t* dst, const uint32_t* src, size_t n, uint32_t alpha256)
//
// Blends n premultiplied RGBA8 pixels of src over dst, src first scaled by
// alpha256 / 256, exactly as blendRowC in src/note_raster.cpp:
//   s = src * alpha256 >> 8
//   t = dst * (255 - s.a) + 128;   dst = s + ((t + (t >> 8)) >> 8)   (saturated)
// Four pixels per step (two registers of eight 16-bit words), then two, then
// the last one. Only v0-v7 and v16-v21 are used: scratch registers in the
// AArch64 calling convention, so nothing has to be saved.

#if defined(__APPLE__)
#define FUNC _gh_blend_row_neon
#else
#define FUNC gh_blend_row_neon
#endif

// Two pixels: source words in \s, destination words in \d; the result, as
// words, in \t (\a is scratch). v16 = alpha256, v17 = 255, v18 = 128 in every
// word; v19 picks each pixel's alpha word for its four words.
.macro BLEND2 s, d, a, t
    mul     \s\().8h, \s\().8h, v16.8h
    ushr    \s\().8h, \s\().8h, #8
    tbl     \a\().16b, {\s\().16b}, v19.16b
    eor     \a\().16b, \a\().16b, v17.16b
    mov     \t\().16b, v18.16b
    mla     \t\().8h, \d\().8h, \a\().8h
    usra    \t\().8h, \t\().8h, #8
    ushr    \t\().8h, \t\().8h, #8
    add     \t\().8h, \t\().8h, \s\().8h
.endm

    .text
    .p2align 4
    .globl FUNC
#if defined(__ELF__)
    .type FUNC, %function
#endif
FUNC:
    dup     v16.8h, w3              // alpha256
    movi    v17.8h, #255            // 255 - a == a ^ 255 for a <= 255
    movi    v18.8h, #128
    adr     x4, 9f
    ldr     q19, [x4]
    cmp     x2, #4
    b.lo    2f
1:  // four pixels
    ldr     q0, [x1], #16
    ldr     q1, [x0]
    uxtl    v2.8h, v0.8b
    uxtl2   v3.8h, v0.16b
    uxtl    v4.8h, v1.8b
    uxtl2   v5.8h, v1.16b
    BLEND2  v2, v4, v6, v20
    BLEND2  v3, v5, v7, v21
    uqxtn   v0.8b, v20.8h           // back to bytes, at most 255
    uqxtn2  v0.16b, v21.8h
    str     q0, [x0], #16
    sub     x2, x2, #4
    cmp     x2, #4
    b.hs    1b
2:  // two pixels
    cmp     x2, #2
    b.lo    3f
    ldr     d0, [x1], #8
    ldr     d1, [x0]
    uxtl    v2.8h, v0.8b
    uxtl    v4.8h, v1.8b
    BLEND2  v2, v4, v6, v20
    uqxtn   v0.8b, v20.8h
    str     d0, [x0], #8
    sub     x2, x2, #2
3:  // the last pixel
    cbz     x2, 4f
    ldr     s0, [x1]
    ldr     s1, [x0]
    uxtl    v2.8h, v0.8b
    uxtl    v4.8h, v1.8b
    BLEND2  v2, v4, v6, v20
    uqxtn   v0.8b, v20.8h
    str     s0, [x0]
4:
    ret
    .p2align 4
9:  // the bytes of word 3 (pixel 0's alpha) and word 7 (pixel 1's), four times each
    .byte 6, 7, 6, 7, 6, 7, 6, 7, 14, 15, 14, 15, 14, 15, 14, 15
#if defined(__ELF__)
    .size FUNC, . - FUNC
    .section .note.GNU-stack, "", %progbits
#endif

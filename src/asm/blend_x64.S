// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.
//
// The note renderer's inner loop in x86-64 assembly (SSE2), for GCC and Clang
// (Linux, Windows with MinGW, Android x86_64); blend_x64.asm is the same for MSVC.
//
//   void gh_blend_row_sse2(uint32_t* dst, const uint32_t* src, size_t n, uint32_t alpha256)
//
// Blends n premultiplied RGBA8 pixels of src over dst, src first scaled by
// alpha256 / 256, exactly as blendRowC in src/note_raster.cpp:
//   s = src * alpha256 >> 8
//   t = dst * (255 - s.a) + 128;   dst = s + ((t + (t >> 8)) >> 8)   (saturated)
// Two pixels per step (eight 16-bit words in an xmm register), then the last
// one when n is odd. Only xmm0-xmm5 and the argument registers are touched:
// they are scratch registers in the System V and the Microsoft x64 calling
// conventions alike, so nothing has to be saved.

#if defined(_WIN32)
#define DST rcx
#define SRC rdx
#define CNT r8
#define ALPHA r9d
#else
#define DST rdi
#define SRC rsi
#define CNT rdx
#define ALPHA ecx
#endif

#if defined(__APPLE__)
#define FUNC _gh_blend_row_sse2
#else
#define FUNC gh_blend_row_sse2
#endif

// One step on the pixels in xmm0 (source) and xmm1 (destination), as words;
// xmm3 = 255, xmm4 = alpha256, xmm5 = 0 in every word. Result in xmm1, as words.
#define BLEND_WORDS            \
    pmullw  xmm0, xmm4;        \
    psrlw   xmm0, 8;           \
    movdqa  xmm2, xmm0;        \
    pshuflw xmm2, xmm2, 0xFF;  \
    pshufhw xmm2, xmm2, 0xFF;  \
    pxor    xmm2, xmm3;        \
    pmullw  xmm1, xmm2;        \
    pcmpeqw xmm2, xmm2;        \
    psllw   xmm2, 15;          \
    psrlw   xmm2, 8;           \
    paddw   xmm1, xmm2;        \
    movdqa  xmm2, xmm1;        \
    psrlw   xmm2, 8;           \
    paddw   xmm1, xmm2;        \
    psrlw   xmm1, 8;           \
    paddw   xmm1, xmm0

    .intel_syntax noprefix
    .text
    .p2align 4
    .globl FUNC
#if defined(__ELF__)
    .type FUNC, @function
#endif
FUNC:
    pxor        xmm5, xmm5          // 0
    movd        xmm4, ALPHA
    pshuflw     xmm4, xmm4, 0       // alpha256 in the four low words...
    punpcklqdq  xmm4, xmm4          // ...and in all eight
    pcmpeqw     xmm3, xmm3
    psrlw       xmm3, 8             // 255 in every word (255 - a == a ^ 255 for a <= 255)
    cmp         CNT, 2
    jb          2f
1:  // two pixels
    movq        xmm0, qword ptr [SRC]
    punpcklbw   xmm0, xmm5
    movq        xmm1, qword ptr [DST]
    punpcklbw   xmm1, xmm5
    BLEND_WORDS
    packuswb    xmm1, xmm1
    movq        qword ptr [DST], xmm1
    add         SRC, 8
    add         DST, 8
    sub         CNT, 2
    cmp         CNT, 2
    jae         1b
2:  // the last pixel when n is odd
    test        CNT, CNT
    jz          3f
    movd        xmm0, dword ptr [SRC]
    punpcklbw   xmm0, xmm5
    movd        xmm1, dword ptr [DST]
    punpcklbw   xmm1, xmm5
    BLEND_WORDS
    packuswb    xmm1, xmm1
    movd        dword ptr [DST], xmm1
3:
    ret
#if defined(__ELF__)
    .size FUNC, . - FUNC
    .section .note.GNU-stack, "", @progbits
#endif

; God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
; All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.
;
; The note renderer's inner loop in x86-64 assembly (SSE2), for MSVC (ml64);
; blend_x64.S is the same for GCC and Clang and explains the arithmetic.
;
;   void gh_blend_row_sse2(uint32_t* dst, const uint32_t* src, size_t n, uint32_t alpha256)
;   Microsoft x64 calling convention: rcx = dst, rdx = src, r8 = n, r9d = alpha256.
;
; A leaf function that touches only rcx, rdx, r8, r9 and xmm0-xmm5 (all scratch
; registers) and never the stack, so it needs no prologue or unwind data.

.code

ALIGN 16
gh_blend_row_sse2 PROC
    pxor        xmm5, xmm5          ; 0
    movd        xmm4, r9d
    pshuflw     xmm4, xmm4, 0       ; alpha256 in every word
    punpcklqdq  xmm4, xmm4
    pcmpeqw     xmm3, xmm3
    psrlw       xmm3, 8             ; 255 in every word
    cmp         r8, 2
    jb          last
pair:                               ; two pixels
    movq        xmm0, QWORD PTR [rdx]
    punpcklbw   xmm0, xmm5
    movq        xmm1, QWORD PTR [rcx]
    punpcklbw   xmm1, xmm5
    pmullw      xmm0, xmm4
    psrlw       xmm0, 8             ; s = src * alpha256 >> 8
    movdqa      xmm2, xmm0
    pshuflw     xmm2, xmm2, 0FFh
    pshufhw     xmm2, xmm2, 0FFh    ; s.a in each pixel's four words
    pxor        xmm2, xmm3          ; 255 - s.a
    pmullw      xmm1, xmm2          ; t = dst * (255 - s.a)
    pcmpeqw     xmm2, xmm2
    psllw       xmm2, 15
    psrlw       xmm2, 8             ; 128
    paddw       xmm1, xmm2
    movdqa      xmm2, xmm1
    psrlw       xmm2, 8
    paddw       xmm1, xmm2
    psrlw       xmm1, 8             ; (t + 128 + ((t + 128) >> 8)) >> 8
    paddw       xmm1, xmm0          ; + s
    packuswb    xmm1, xmm1
    movq        QWORD PTR [rcx], xmm1
    add         rdx, 8
    add         rcx, 8
    sub         r8, 2
    cmp         r8, 2
    jae         pair
last:                               ; the last pixel when n is odd
    test        r8, r8
    jz          done
    movd        xmm0, DWORD PTR [rdx]
    punpcklbw   xmm0, xmm5
    movd        xmm1, DWORD PTR [rcx]
    punpcklbw   xmm1, xmm5
    pmullw      xmm0, xmm4
    psrlw       xmm0, 8
    movdqa      xmm2, xmm0
    pshuflw     xmm2, xmm2, 0FFh
    pshufhw     xmm2, xmm2, 0FFh
    pxor        xmm2, xmm3
    pmullw      xmm1, xmm2
    pcmpeqw     xmm2, xmm2
    psllw       xmm2, 15
    psrlw       xmm2, 8
    paddw       xmm1, xmm2
    movdqa      xmm2, xmm1
    psrlw       xmm2, 8
    paddw       xmm1, xmm2
    psrlw       xmm1, 8
    paddw       xmm1, xmm0
    packuswb    xmm1, xmm1
    movd        DWORD PTR [rcx], xmm1
done:
    ret
gh_blend_row_sse2 ENDP

END

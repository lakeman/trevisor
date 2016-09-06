/***************************************************************************
 *
 * Cold boot resistant AES for 64-bit machines with AES-NI support
 * (currently all Core-i5/7 processors and some Core-i3)
 * 
 * Copyright (C) 2010  Tilo Mueller <tilo.mueller@informatik.uni-erlangen.de>
 * Copyright (C) 2013  Johannes Goetzfried <johannes@jgoetzfried.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 ***************************************************************************/


/* 64-bit debugging registers */
.set   db0,    %db0    /* round key 0a */
.set   db1,    %db1    /* round key 0b */
.set   db2,    %db2    /* round key 1a */
.set   db3,    %db3    /* round key 1b */


/* 128-bit SSE registers */
.set   rstate, %xmm0       /* AES state */
.set   rhelp,  %xmm1       /* helping register */
.set   rk0,    %xmm2       /* round key  0 */
.set   rk1,    %xmm3       /* round key  1 */
.set   rk2,    %xmm4       /* round key  2 */
.set   rk3,    %xmm5       /* round key  3 */
.set   rk4,    %xmm6       /* round key  4 */
.set   rk5,    %xmm7       /* round key  5 */
.set   rk6,    %xmm8       /* round key  6 */
.set   rk7,    %xmm9       /* round key  7 */
.set   rk8,    %xmm10      /* round key  8 */
.set   rk9,    %xmm11      /* round key  9 */
.set   rk10,   %xmm12      /* round key 10 */
.set   rk11,   %xmm13      /* round key 11 */
.set   rk12,   %xmm14      /* round key 12 */
.set   rk13,   %xmm15      /* round key 13 */
.set   rk14,   %xmm2       /* round key 14 */



/***************************************************************************
 *                 MACROs
 ***************************************************************************/


/* function epilogue */
.macro epilog

   /* write output */
   movdqu  rstate,0(%rdi)

   /* reset XMMs */
   pxor    %xmm0,%xmm0
   pxor    %xmm1,%xmm1
   pxor    %xmm2,%xmm2
   pxor    %xmm3,%xmm3
   pxor    %xmm4,%xmm4
   pxor    %xmm5,%xmm5
   pxor    %xmm6,%xmm6
   pxor    %xmm7,%xmm7
   pxor    %xmm8,%xmm8
   pxor    %xmm9,%xmm9
   pxor    %xmm10,%xmm10
   pxor    %xmm11,%xmm11
   pxor    %xmm12,%xmm12
   pxor    %xmm13,%xmm13
   pxor    %xmm14,%xmm14
   pxor    %xmm15,%xmm15

   /* return true */
   xorq    %rax,%rax
   retq
.endm


/* generate next round key (192-bit) */
.macro key_schedule_192 r0 r1 r2 rcon
   movdqu      \r0,\r2
   shufps      $0x4e,\r1,\r2
   movdqu      \r0,rhelp
   shufps      $0x99,\r2,rhelp
   pxor        rhelp,\r2
   movdqu      \r0,rhelp
   pxor        rhelp,\r2
   pslldq      $0x4,rhelp
   pxor        rhelp,\r2
   pslldq      $0x4,rhelp
   pxor        rhelp,\r2
   pslldq      $0x4,rhelp
   pxor        rhelp,\r2
   shufps      $0x44,\r0,\r1
   pxor        rhelp,\r1
   aeskeygenassist $\rcon,\r1,rhelp
   shufps      $0x55,rhelp,rhelp
   pxor        rhelp,\r2
   pslldq      $0x8,rhelp
   pxor        rhelp,\r1
.endm
.macro key_schedule_192_ r0 r1 r2 r3 rcon
   movdqu          \r0,\r2
   shufps          $0x4e,\r1,\r2
   pxor            rhelp,rhelp
   shufps          $0x1f,\r2,rhelp
   pxor            rhelp,\r2
   shufps          $0x8c,\r2,rhelp
   pxor            rhelp,\r2
   pxor            \r3,\r3
   shufps          $0xe0,\r2,\r3
   pxor            \r1,\r3
   movdqu          \r1,rhelp
   pslldq          $0x4,rhelp
   pxor            rhelp,\r3
   aeskeygenassist $\rcon,\r1,rhelp
   shufps          $0xff,rhelp,rhelp
   pxor            rhelp,\r2
   pxor            rhelp,\r3
   shufps          $0xae,\r0,\r3
.endm


/* generate next round key (128- and 256-bit) */
.macro  key_schedule r0 r1 r2 rcon
        pxor            rhelp,rhelp
        movdqu          \r0,\r2
        shufps          $0x1f,\r2,rhelp
        pxor            rhelp,\r2
        shufps          $0x8c,\r2,rhelp
        pxor            rhelp,\r2
        aeskeygenassist $\rcon,\r1,rhelp
        .if (\rcon == 0)
        shufps          $0xaa,rhelp,rhelp
        .else
        shufps          $0xff,rhelp,rhelp
        .endif
        pxor            rhelp,\r2
.endm


/* generate round keys rk1 to rk10 (128-bit) */
.macro generate_rks_10
   key_schedule        rk0  rk0  rk1  0x1
   key_schedule        rk1  rk1  rk2  0x2
   key_schedule        rk2  rk2  rk3  0x4
   key_schedule        rk3  rk3  rk4  0x8
   key_schedule        rk4  rk4  rk5  0x10
   key_schedule        rk5  rk5  rk6  0x20
   key_schedule        rk6  rk6  rk7  0x40
   key_schedule        rk7  rk7  rk8  0x80
   key_schedule        rk8  rk8  rk9  0x1b
   key_schedule        rk9  rk9  rk10 0x36
.endm


/* generate round keys rk1 to rk12 (192-bit) */
.macro generate_rks_12
   key_schedule_192    rk0   rk1   rk2         0x1
   key_schedule_192_   rk1   rk2   rk3   rk4   0x2
   key_schedule_192    rk3   rk4   rk5         0x4
   key_schedule_192_   rk4   rk5   rk6   rk7   0x8
   key_schedule_192    rk6   rk7   rk8         0x10
   key_schedule_192_   rk7   rk8   rk9   rk10  0x20
   key_schedule_192    rk9   rk10  rk11        0x40
   key_schedule_192_   rk10  rk11  rk12  rk13  0x80
.endm


/* generate round keys rk1 to rk14 (256-bit) */
.macro generate_rks_14
   key_schedule        rk0  rk1  rk2  0x1
   key_schedule        rk1  rk2  rk3  0x0
   key_schedule        rk2  rk3  rk4  0x2
   key_schedule        rk3  rk4  rk5  0x0
   key_schedule        rk4  rk5  rk6  0x4
   key_schedule        rk5  rk6  rk7  0x0
   key_schedule        rk6  rk7  rk8  0x8
   key_schedule        rk7  rk8  rk9  0x0
   key_schedule        rk8  rk9  rk10 0x10
   key_schedule        rk9  rk10 rk11 0x0
   key_schedule        rk10 rk11 rk12 0x20
   key_schedule        rk11 rk12 rk13 0x0
   key_schedule        rk12 rk13 rk14 0x40
.endm


/* inversed normal round */
.macro aesdec_ rk rstate
   aesimc      \rk,\rk
   aesdec      \rk,\rstate
.endm


/* copy secret key from dbg regs into xmm regs (lower part of key) */
.macro read_key_0 r0 r1 rounds
   movq    db0,%rax
   movq    %rax,\r0
   movq    db1,%rax
   movq    %rax,rhelp
   shufps  $0x44,rhelp,\r0
   .if (\rounds > 10)
   movq    db2,%rax
   movq    %rax,\r1
   .endif
   .if (\rounds > 12)
   movq    db3,%rax
   movq    %rax,rhelp
   shufps  $0x44,rhelp,\r1
   .endif
.endm

/* copy secret key from dbg regs into xmm regs (upper part of key)
 * only valid for AES-128
 */
.macro read_key_1 r0 r1 rounds
   movq    db2,%rax
   movq    %rax,\r0
   movq    db3,%rax
   movq    %rax,rhelp
   shufps  $0x44,rhelp,\r0
.endm


/* Encrypt */
.macro encrypt_block rounds up
   movdqu  0(%rsi),rstate
   read_key_\up  rk0 rk1 \rounds
   pxor          rk0,rstate
   generate_rks_\rounds
   aesenc        rk1,rstate
   aesenc        rk2,rstate
   aesenc        rk3,rstate
   aesenc        rk4,rstate
   aesenc        rk5,rstate
   aesenc        rk6,rstate
   aesenc        rk7,rstate
   aesenc        rk8,rstate
   aesenc        rk9,rstate
   .if (\rounds > 10)
   aesenc        rk10,rstate
   aesenc        rk11,rstate
   .endif
   .if (\rounds > 12)
   aesenc        rk12,rstate
   aesenc        rk13,rstate
   .endif
   aesenclast    rk\rounds,rstate
   epilog
.endm


/* Decrypt */
.macro decrypt_block rounds up
   movdqu  0(%rsi),rstate
   read_key_\up  rk0 rk1 \rounds
   generate_rks_\rounds
   pxor          rk\rounds,rstate
   .if (\rounds > 12)
   read_key_\up  rk0,rk1,10
   aesdec_       rk13,rstate
   aesdec_       rk12,rstate
   .endif
   .if (\rounds > 10)
   aesdec_       rk11,rstate
   aesdec_       rk10,rstate
   .endif
   aesdec_       rk9,rstate
   aesdec_       rk8,rstate
   aesdec_       rk7,rstate
   aesdec_       rk6,rstate
   aesdec_       rk5,rstate
   aesdec_       rk4,rstate
   aesdec_       rk3,rstate
   aesdec_       rk2,rstate
   aesdec_       rk1,rstate
   aesdeclast    rk0,rstate
   epilog
.endm



/***************************************************************************
 *             CODE SEGMENT
 **************************************************************************/

.text
   .globl  tresor_encblk_128
   .globl  tresor_decblk_128
   .globl  tresor_encblk_192
   .globl  tresor_decblk_192
   .globl  tresor_encblk_256
   .globl  tresor_decblk_256

   .globl  tresor_encblk_128_up
   .globl  tresor_decblk_128_up


/* void tresor_encblk(u8 *out, const u8 *in) */
tresor_encblk_128:
   encrypt_block   10 0
tresor_encblk_192:
   encrypt_block   12 0
tresor_encblk_256:
   encrypt_block   14 0
tresor_encblk_128_up:
   encrypt_block   10 1


/* void tresor_decblk(u8 *out, const u8 *in) */
tresor_decblk_128:
   decrypt_block   10 0
tresor_decblk_192:
   decrypt_block   12 0
tresor_decblk_256:
   decrypt_block   14 0
tresor_decblk_128_up:
   decrypt_block   10 1


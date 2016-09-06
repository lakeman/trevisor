/***************************************************************************
 *
 * Tresor set/get to debug registers
 * 
 * 
 * Copyright (C) 2010  Tilo Mueller <tilo.mueller@informatik.uni-erlangen.de>
 *                      Benjamin Taubmann <taubmann.benjamin@informatik.stud.uni-erlangen.de> 
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


        .text
        .code64
        .globl  tresor_set_key
        .globl  tresor_get_key
        .globl  tresor_capable
        .align  16


/* 64-bit debugging registers */
.set    db0,    %db0    /* round key 0a */
.set    db1,    %db1    /* round key 0b */
.set    db2,    %db2    /* round key 1a */
.set    db3,    %db3    /* round key 1b */


/* void tresor_set_key(const u8 *in_key) */
tresor_set_key:
   movq    0(%rdi),%rax
   movq    %rax,db0
   movq    8(%rdi),%rax
   movq    %rax,db1
   movq    16(%rdi),%rax
   movq    %rax,db2
   movq    24(%rdi),%rax
   movq    %rax,db3
   movl    $0,%eax
   retq

/* void tresor_get_key(const u8 *out_key) */
tresor_get_key:
   movq    db0,%rax
   movq    %rax, 0(%rdi)
   movq    db1,%rax
   movq    %rax, 8(%rdi)
   movq    db2,%rax
   movq    %rax, 16(%rdi)
   movq    db3,%rax
   movq    %rax, 24(%rdi)
   movl    $0,%eax
   retq

/* bool    tresor_capable(void) */
tresor_capable:
   push    %rcx
   mov $0x00000001,%rax
   cpuid 
   and $0x02000000,%rcx
   jz  not_capable
   mov $1,%rax
   pop     %rcx
   retq
   not_capable:
   mov $0,%rax
   pop %rcx
   retq

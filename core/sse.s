/*
 * TRESOR Core
 *
 * Copyright (C) 2012   Benjamin Taubmann <taubmann.benjamin@informatik.stud.uni-erlangen.de>
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
 */


    .text
    .code64
    .globl  enableSSE
    .align  16


enableSSE:
	mov %cr0, %rax
    or  $0x2, %rax
/*      set ts bit to 0 */ 
    and $0xf7, %al
    mov %rax, %cr0
    retq

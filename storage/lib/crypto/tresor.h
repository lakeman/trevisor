/*
 * Cold boot resistant AES for 64-bit machines with AES-NI support
 * (currently all Core-i5/7 processors and some Core-i3)
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

#ifndef _CRYPTO_TRESOR_H
#define _CRYPTO_TRESOR_H
#include "core.h"
#include <core/linkage.h>


/* number of iterations for key derivation */
#define TRESOR_KDF_ITER 2000 
#define AES_KEYSIZE_128 128
#define AES_KEYSIZE_192 192
#define AES_KEYSIZE_256 256
#define AES_BLOCK_SIZE 16

void crypto_tresor_init(void);

/* 
 * Assembly functions implemented in tresor-intel_asm.S
 */
asmlinkage void tresor_encblk_128(u8 *out, const u8 *in);
asmlinkage void tresor_decblk_128(u8 *out, const u8 *in);
asmlinkage void tresor_encblk_192(u8 *out, const u8 *in);
asmlinkage void tresor_decblk_192(u8 *out, const u8 *in);
asmlinkage void tresor_encblk_256(u8 *out, const u8 *in);
asmlinkage void tresor_decblk_256(u8 *out, const u8 *in);

#endif /* _CRYPTO_TRESOR_H */


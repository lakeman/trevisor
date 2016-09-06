/*
 * Cold boot resistant AES for 64-bit machines with AES-NI support
 * (currently all Core-i5/7 processors and some Core-i3)
 * 
 * Copyright (C) 2012  Benjamin Taubmann <taubmann.benjamin@informatik.stud.uni-erlangen.de>
 * Copyright (C) 2013  Johannes Goetzfried <johannes@jgoetzfried.de>
 *
 * based on 'tresor_xts.c' (see below)
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
 *
 * Copyright (c) 2007, 2008 University of Tsukuba
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the University of Tsukuba nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <core.h>
#include "crypto.h"
#include "tresor.h"

#define AES_BLK_BYTES   16

typedef void (*aes_crypt_func_t)(u8 *out, const u8 *in);

static void inline xor128(void *dst, const void *src1, const void *src2)
{
	core_mem_t *s1 = (core_mem_t *)src1, *s2 = (core_mem_t *)src2;
	core_mem_t *d = (core_mem_t *)dst;

	d[0].qword = s1[0].qword ^ s2[0].qword;
	d[1].qword = s1[1].qword ^ s2[1].qword;
}

static void inline movzx128(void *dst, const u64 src)
{
	core_mem_t *d = (core_mem_t *)dst;

	d[0].qword = src;
	d[1].qword = 0;
}

static int shl128(void *dst, const void *src)
{
	int c0, c1;
	core_mem_t *s = (core_mem_t *)src;
	core_mem_t *d = (core_mem_t *)dst;

	c0 = s[0].qword & (1ULL << 63) ? 1 : 0;
	c1 = s[1].qword & (1ULL << 63) ? 1 : 0;
	d[0].qword = s[0].qword << 1;
	d[1].qword = s[1].qword << 1 | c0;
	return c1;
}

static void gf_mul128(u8 *dst, const u8 *src)
{
	int carry;
	static const u8 gf_128_fdbk = 0x87;

	carry = shl128(dst, src);
	if (carry)
		dst[0] ^= gf_128_fdbk;
}

static void tresor_xts_crypt(u8 *dst, u8 *src, aes_crypt_func_t crypt, u64 lba, u32 sector_size)
{
	int i;
	u8 tweak[AES_BLK_BYTES];
	static char fxsave_region[512] __attribute__((aligned(16)));

	ASSERT(sector_size % AES_BLK_BYTES == 0);

	asm volatile("pushf; cli");
	asm volatile("fxsave %0" : "=m"(fxsave_region));

	movzx128(tweak, lba);				// convert sector number to tweak plaintext
	tresor_encblk_128_up(tweak, tweak);		// encrypt the tweak
	for(i = sector_size; i > 0; i -= AES_BLK_BYTES) {
		xor128(dst, src, tweak);		// merge the tweak into the input block
		crypt(dst, dst);			// encrypt one block
		xor128(dst, dst, tweak);
		gf_mul128(tweak, tweak);
		dst += AES_BLK_BYTES;
		src += AES_BLK_BYTES;
	}

	asm volatile("fxrstor %0 ":"=m"(fxsave_region));
	asm volatile("popf");
}

static void tresor_xts_encrypt(void *dst, void *src, void *keyctx, lba_t lba, int sector_size)
{
	tresor_xts_crypt(dst, src, tresor_encblk_128, lba, sector_size);
}

static void tresor_xts_decrypt(void *dst, void *src, void *keyctx, lba_t lba, int sector_size)
{
	tresor_xts_crypt(dst, src, tresor_decblk_128, lba, sector_size);
}

static void *tresor_xts_setkey(const u8 *key, int bits)
{
	ASSERT(bits == 256);

	return NULL;
}

static struct crypto tresor_xts_crypto = {
	.name = 	"tresor",
	.block_size =	AES_BLK_BYTES,
	.keyctx_size =	0,
	.encrypt =	tresor_xts_encrypt,
	.decrypt =	tresor_xts_decrypt,
	.setkey =	tresor_xts_setkey,
};

void
tresor_xts_init (void)
{
	printf("Init Tresor\n");
	crypto_register (&tresor_xts_crypto);
}

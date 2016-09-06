/*
 * Cold boot resistant AES for 64-bit machines with AES-NI support
 * (currently all Core-i5/7 processors and some Core-i3)
 * 
 * Copyright (C) 2012  Benjamin Taubmann <taubmann.benjamin@informatik.stud.uni-erlangen.de>
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

#include <core.h>
#include <core/process.h>
#include "crypto.h"
#include "tresor.h"

static int key_length=AES_KEYSIZE_256;

static void crypto_tresor_encrypt (void *dst, void *src, void *keyctx, lba_t lba,
        int sector_size)
{

    int i;
    u8* ip=src;
    u8* op=dst;
    static char fxsave_region[512] __attribute__((aligned(16)));

    asm volatile("pushf; cli");
    asm volatile("fxsave %0" : "=m"(fxsave_region));


    for(i = 0; i < sector_size; i += AES_BLOCK_SIZE) {
        switch(key_length) {
            case AES_KEYSIZE_128: tresor_encblk_128(op,ip); break;
            case AES_KEYSIZE_192: tresor_encblk_192(op,ip); break;
            case AES_KEYSIZE_256: tresor_encblk_256(op,ip); break;
            default:   
               printf("Wrong keysize\n");
        }
        ip += AES_BLOCK_SIZE;
        op += AES_BLOCK_SIZE;
    }

    asm volatile("fxrstor %0 ":"=m"(fxsave_region));
    asm volatile("popf");
}

static void crypto_tresor_decrypt (void *dst, void *src, void *keyctx, lba_t lba,
        int sector_size)
{
    int i;
    u8* ip=src;
    u8* op=dst;
    static char fxsave_region[512] __attribute__((aligned(16)));
    asm volatile("pushf; cli");
    asm volatile("fxsave %0" : "=m"(fxsave_region));

    for(i = 0; i < sector_size; i += AES_BLOCK_SIZE) {
        switch(key_length) {
            case AES_KEYSIZE_128: tresor_decblk_128(op,ip); break;
            case AES_KEYSIZE_192: tresor_decblk_192(op,ip); break;
            case AES_KEYSIZE_256: tresor_decblk_256(op,ip); break;
            default:   
               printf("Wrong keysize\n");
        }
        ip += AES_BLOCK_SIZE;
        op += AES_BLOCK_SIZE;
    }

    asm volatile("fxrstor %0 ":"=m"(fxsave_region));
    asm volatile("popf");
}


static void *crypto_tresor_setkey (const u8 *key, int bits)
{
    key_length = bits;
    return NULL;
}



static struct crypto crypto_tresor_crypto = {
    .name =    "tresor",
    .block_size =  16,
    .keyctx_size = 0,
    .encrypt = crypto_tresor_encrypt,
    .decrypt = crypto_tresor_decrypt,
    .setkey =  crypto_tresor_setkey,
};

void crypto_tresor_init (void)
{
    printf("Init Tresor\n");
    crypto_register (&crypto_tresor_crypto);
}

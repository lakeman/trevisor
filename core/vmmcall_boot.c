/*
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

#include "asm.h"
#include "assert.h"
#include "ap.h"
#include "config.h"
#include "current.h"
#include "initfunc.h"
#include "main.h"
#include "mm.h"
#include "panic.h"
#include "pcpu.h"
#include "printf.h"
#include "string.h"
#include "tresor.h"
#include "vmmcall.h"
#include "vmmcall_boot.h"
#include "../crypto/decryptcfg.h"

struct loadcfg_data {
	u32 len;
	u32 pass, passlen;
	u32 data, datalen;
};

struct vmmcall_boot_thread_data {
	void (*func) (void *);
	void *arg;
};

static bool enable = false;
static bool volatile continue_flag;

void
vmmcall_boot_continue (void)
{
	ASSERT (enable);
	continue_flag = true;
	while (continue_flag)
		schedule ();
}

static void
wait_for_boot_continue (void)
{
	continue_flag = false;
	while (!continue_flag)
		schedule ();
}

static void
vmmcall_boot_thread (void *arg)
{
	struct vmmcall_boot_thread_data *data;

	data = arg;
	data->func (data->arg);
	continue_flag = true;
	enable = false;
	free (data);
}

static void
boot_guest (void)
{
	struct config_data *d;
	ulong rbx;

	if (!enable)
		return;

	if (currentcpu->cpunum != 0)
		panic ("boot from AP");
	current->vmctl.read_general_reg (GENERAL_REG_RBX, &rbx);
	rbx &= 0xFFFFFFFF;
	if (rbx) {
#ifdef TRESOR
        d = mapmem_hphys (rbx, sizeof *d, 1);
#else  
        d = mapmem_hphys (rbx, sizeof *d, 0);
#endif
		ASSERT (d);
		if (d->len != sizeof *d)
			panic ("config size mismatch: %d, %d\n", d->len,
			       (int)sizeof *d);
		memcpy (&config, d, sizeof *d);
#ifdef TRESOR
       // erase the config in the guest!
       unsigned char *tmp = (unsigned char*) d;
       unsigned int i;
       for (i=0; i<sizeof *d; i++)
           tmp[i] = 0;
#endif
		unmapmem (d, sizeof *d);
	}
#ifdef TRESOR
   tresor_init_bsp();
#endif
	wait_for_boot_continue ();
}

static void
loadcfg (void)
{
	struct loadcfg_data *d;
	u8 *pass, *data;
	ulong rbx;
	struct config_data *tmpbuf;

	if (!enable)
		return;

	current->vmctl.read_general_reg (GENERAL_REG_RBX, &rbx);
	rbx &= 0xFFFFFFFF;
	d = mapmem_hphys (rbx, sizeof *d, 0);
	ASSERT (d);
	if (d->len != sizeof *d)
		panic ("size mismatch: %d, %d\n", d->len,
		       (int)sizeof *d);
	pass = mapmem_hphys (d->pass, d->passlen, 0);
	ASSERT (pass);
	data = mapmem_hphys (d->data, d->datalen, 0);
	ASSERT (data);
	tmpbuf = alloc (d->datalen);
	ASSERT (tmpbuf);
#ifdef CRYPTO
	decryptcfg (pass, d->passlen, data, d->datalen, tmpbuf);
#else
	panic ("cannot decrypt");
#endif
	unmapmem (pass, d->passlen);
	unmapmem (data, d->datalen);
	unmapmem (d, sizeof *d);
	config.len = 0;
	if ((tmpbuf->len + 15) / 16 == d->datalen / 16) {
		if (tmpbuf->len != sizeof config)
			panic ("config size mismatch: %d, %d\n", tmpbuf->len,
			       (int)sizeof config);
		data = mapmem_hphys (d->data, sizeof config, MAPMEM_WRITE);
		ASSERT (data);
		memcpy (data, tmpbuf, sizeof config);
		unmapmem (data, d->datalen);
		current->vmctl.write_general_reg (GENERAL_REG_RAX, 1);
	} else {
		current->vmctl.write_general_reg (GENERAL_REG_RAX, 0);
	}
	free (tmpbuf);
}

void showPCR(void)
{
#ifdef TCG_BIOS
#define TPM_DIGEST_SIZE 20
#define PCR_BLOCK 5*(TPM_DIGEST_SIZE*2+1)
   printf("VMMCALL showPCR\n");
   u8 *d;  
   ulong rbx;
   u8 pcr[TPM_DIGEST_SIZE];
   char tostr[PCR_BLOCK];
   u32 i;


   getPCR(0, pcr);
   for (i=0; i<20; i++)
       snprintf(&tostr[i*2],3, "%02hhx", pcr[i] );
   getPCR(1, pcr);
   for (i=0; i<20; i++)
       snprintf(&tostr[41+i*2],3, "%02hhx", pcr[i]);
   getPCR(2, pcr);
   for (i=0; i<20; i++)
       snprintf(&tostr[82+i*2],3, "%02hhx", pcr[i]);
   getPCR(3, pcr);
   for (i=0; i<20; i++)
       snprintf(&tostr[123+i*2],3, "%02hhx", pcr[i]);
   getPCR(4, pcr);
   for (i=0; i<20; i++)
       snprintf(&tostr[164+i*2],3, "%02hhx", pcr[i]);


   if (currentcpu->cpunum != 0)
       panic ("boot from AP");

   current->vmctl.read_general_reg (GENERAL_REG_RBX, &rbx);
   rbx &= 0xFFFFFFFF;

   if (rbx) {
       d = mapmem_hphys (rbx, PCR_BLOCK, 1);
       memcpy (d, tostr, PCR_BLOCK);
       unmapmem (d, PCR_BLOCK);
   }
#endif
}

void
vmmcall_boot_enable (void (*func) (void *), void *arg)
{
	struct vmmcall_boot_thread_data *data;

	enable = true;
	data = alloc (sizeof *data);
	data->func = func;
	data->arg = arg;
	thread_new (vmmcall_boot_thread, data, VMM_STACKSIZE);
	wait_for_boot_continue ();
}

static void
vmmcall_boot_init (void)
{
	vmmcall_register ("boot", boot_guest);
	vmmcall_register ("loadcfg", loadcfg);
    vmmcall_register ("showPCR", showPCR);
	config.len = 0;
	enable = false;
}

INITFUNC ("vmmcal0", vmmcall_boot_init);

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

#include "ap.h"
#include "assert.h"
#include "callrealmode.h"
#include "config.h"
#include "convert.h"
#include "current.h"
#include "debug.h"
#include "guest_boot.h"
#include "initfunc.h"
#include "linkage.h"
#include "main.h"
#include "mm.h"
#include "multiboot.h"
#include "osloader.h"
#include "panic.h"
#include "pcpu.h"
#include "printf.h"
#include "process.h" 
#include "regs.h"
#include "string.h"
#include "svm.h"
#include "svm_init.h"
#include "types.h"
#include "vcpu.h"
#include "vmmcall.h"
#include "vmmcall_boot.h"
#include "vramwrite.h"
#include "vt.h"
#include "vt_init.h"

static struct multiboot_info mi;
static u32 minios_startaddr;
static void *bios_data_area;
static int shiftkey;

static void
print_boot_msg (void)
{
	printf ("Starting BitVisor...\n");
	printf ("Copyright (c) 2007, 2008 University of Tsukuba\n");
	printf ("All rights reserved.\n");
}

static void
print_startvm_msg (void)
{
	printf ("Starting a virtual machine.\n");
}

static void
load_drivers (void)
{
	printf ("Loading drivers.\n");
	call_initfunc ("driver");
}

static u8
detect_bios_boot_device (struct multiboot_info *mi)
{
	if (mi->flags.boot_device) {
		return mi->boot_device.drive;
	} else {
		printf ("BIOS boot device detection failed. Using 0x80.\n");
		return 0x80;
	}
}

static void
copy_minios (void)
{
	struct multiboot_modules *q;

	minios_startaddr = 0;
	if (mi.flags.mods && mi.mods_count) {
		q = mapmem (MAPMEM_HPHYS, mi.mods_addr, sizeof *q * 2);
		ASSERT (q);
		if (mi.mods_count >= 2)
			minios_startaddr = load_minios (q[0].mod_start,
							q[0].mod_end -
							q[0].mod_start,
							q[1].mod_start,
							q[1].mod_end -
							q[1].mod_start);
		else
			minios_startaddr = load_minios (q[0].mod_start,
							q[0].mod_end -
							q[0].mod_start, 0, 0);
		unmapmem (q, sizeof *q * 2);
	} else {
		printf ("Module not found.\n");
	}
}

/* the head 640KiB area is saved by save_bios_data_area and */
/* restored by reinitialize_vm. */
/* this function clears other RAM space that may contain sensitive data. */
static void
clear_guest_pages (void)
{
	u64 base, len;
	u32 type;
	u32 n, nn;
	static const u32 maxlen = 0x100000;
	void *p;

	n = 0;
	for (nn = 1; nn; n = nn) {
		nn = getfakesysmemmap (n, &base, &len, &type);
		if (type != SYSMEMMAP_TYPE_AVAILABLE)
			continue;
		if (base < 0x100000) /* < 1MiB */
			continue;
		if (base + len <= 0x100000) /* < 1MiB */
			continue;
		while (len >= maxlen) {
			p = mapmem (MAPMEM_HPHYS | MAPMEM_WRITE, base, maxlen);
			ASSERT (p);
			memset (p, 0, maxlen);
			unmapmem (p, maxlen);
			base += maxlen;
			len -= maxlen;
		}
		if (len > 0) {
			p = mapmem (MAPMEM_HPHYS, base, len);
			ASSERT (p);
			memset (p, 0, len);
			unmapmem (p, len);
		}
	}
}

/* make CPU's virtualization extension usable */
static void
virtualization_init_pcpu (void)
{
	currentcpu->fullvirtualize = FULLVIRTUALIZE_NONE;
	if (vt_available ()) {
		vt_init ();
		currentcpu->fullvirtualize = FULLVIRTUALIZE_VT;
	}
	if (svm_available ()) {
		svm_init ();
		currentcpu->fullvirtualize = FULLVIRTUALIZE_SVM;
	}
}

/* set current vcpu for full virtualization */
static void
set_fullvirtualize (void)
{
	switch (currentcpu->fullvirtualize) {
	case FULLVIRTUALIZE_NONE:
		panic ("Fatal error: This processor does not support"
		       " Intel VT or AMD Virtualization");
		break;
	case FULLVIRTUALIZE_VT:
		vmctl_vt_init ();
		break;
	case FULLVIRTUALIZE_SVM:
		vmctl_svm_init ();
		break;
	}
}

static void
initregs (bool bsp, u8 bios_boot_drive)
{
	void *p;

	current->vmctl.reset ();
	current->vmctl.write_control_reg (CONTROL_REG_CR0,
					  CR0_PE_BIT | CR0_ET_BIT);
	current->vmctl.write_control_reg (CONTROL_REG_CR0, CR0_ET_BIT);
	current->vmctl.write_control_reg (CONTROL_REG_CR3, 0);
	current->vmctl.write_control_reg (CONTROL_REG_CR4, 0);
	current->vmctl.write_control_reg (CONTROL_REG_CR4, 0);
	current->vmctl.write_realmode_seg (SREG_ES, 0);
	current->vmctl.write_realmode_seg (SREG_CS, 0);
	current->vmctl.write_realmode_seg (SREG_SS, 0);
	current->vmctl.write_realmode_seg (SREG_DS, 0);
	current->vmctl.write_realmode_seg (SREG_FS, 0);
	current->vmctl.write_realmode_seg (SREG_GS, 0);
	current->vmctl.write_general_reg (GENERAL_REG_RAX, 0);
	current->vmctl.write_general_reg (GENERAL_REG_RCX, 0);
	current->vmctl.write_general_reg (GENERAL_REG_RDX, 0);
	current->vmctl.write_general_reg (GENERAL_REG_RBX, 0);
	current->vmctl.write_general_reg (GENERAL_REG_RSP, 0);
	current->vmctl.write_general_reg (GENERAL_REG_RBP, 0);
	current->vmctl.write_general_reg (GENERAL_REG_RSI, 0);
	current->vmctl.write_general_reg (GENERAL_REG_RDI, 0);
	current->vmctl.write_ip (0);
	current->vmctl.write_flags (RFLAGS_ALWAYS1_BIT);
	current->vmctl.write_idtr (0, 0x3FF);
	if (bsp) {
		p = mapmem_hphys (GUEST_BOOT_OFFSET, GUEST_BOOT_LENGTH,
				  MAPMEM_WRITE);
		ASSERT (p);
		memcpy (p, guest_boot_start, GUEST_BOOT_LENGTH);
		unmapmem (p, GUEST_BOOT_LENGTH);
		current->vmctl.write_general_reg (GENERAL_REG_RCX,
						  bios_boot_drive);
		if (config.vmm.boot_active)
			current->vmctl.write_general_reg (GENERAL_REG_RSI, 1);
		current->vmctl.write_realmode_seg (SREG_CS, 0x0);
		current->vmctl.write_ip (GUEST_BOOT_OFFSET);
	} else {
		current->vmctl.init_signal ();
	}
}

static void
sync_cursor_pos (void)
{
	unsigned int row, col;
	u16 dx;

	vramwrite_get_cursor_pos (&col, &row);
	conv8to16 (col, row, &dx);
	current->vmctl.write_general_reg (GENERAL_REG_RBX, 0);
	current->vmctl.write_general_reg (GENERAL_REG_RDX, dx);
}

static void
save_bios_data_area (void)
{
	void *p;

	bios_data_area = alloc (0xA0000);
	p = mapmem_hphys (0, 0xA0000, 0);
	ASSERT (p);
	memcpy (bios_data_area, p, 0xA0000);
	unmapmem (p, 0xA0000);
}

void
reinitialize_vm (bool bsp, u8 bios_boot_drive)
{
	void *p;

	if (bsp) {
		p = mapmem_hphys (0, 0xA0000, MAPMEM_WRITE);
		ASSERT (p);
		memcpy (p, bios_data_area, 0xA0000);
		unmapmem (p, 0xA0000);
		clear_guest_pages ();
		call_initfunc ("config0");
		load_drivers ();
		call_initfunc ("config1");
		initregs (bsp, bios_boot_drive);
		sync_cursor_pos ();
	} else {
		panic ("reinitialize_vm: !bsp");
	}
}

static void
create_pass_vm (void)
{
	bool bsp = false;
	u8 bios_boot_drive = 0;
	static struct vcpu *vcpu0;

	if (currentcpu->cpunum == 0) {
		bsp = true;
		bios_boot_drive = detect_bios_boot_device (&mi);
	}
	sync_all_processors ();
	if (bsp) {
		load_new_vcpu (NULL);
		vcpu0 = current;
	}
	sync_all_processors ();
	if (!bsp)
		load_new_vcpu (vcpu0);
	set_fullvirtualize ();
	sync_all_processors ();
	current->vmctl.vminit ();
	call_initfunc ("pass");
	if (bsp)
		save_bios_data_area ();
	initregs (bsp, bios_boot_drive);
	current->initialized = true;
	sync_all_processors ();
	if (bsp) {
		if (!minios_startaddr)
			load_drivers ();
		print_startvm_msg ();
		sync_cursor_pos ();
	}
	sync_all_processors ();
#ifdef DEBUG_GDB
	if (!bsp)
		for (;;)
			asm_cli_and_hlt ();
#endif
	if (bsp && minios_startaddr) {
		current->vmctl.write_general_reg (GENERAL_REG_RSI,
						  0x10000);
		current->vmctl.write_general_reg (GENERAL_REG_RDI,
						  minios_startaddr);
		vmmcall_boot_enable (bios_boot_drive);
	}
	current->vmctl.start_vm ();
	panic ("VM stopped.");
}

static void
get_shiftflags (void)
{
	shiftkey = callrealmode_getshiftflags ();
}

static void
debug_on_shift_key (void)
{
	int d;

	d = newprocess ("init");
	debug_msgregister ();
	msgsendint (d,
		    !!((shiftkey & GETSHIFTFLAGS_RSHIFT_BIT) ||
		       (shiftkey & GETSHIFTFLAGS_LSHIFT_BIT)));
	debug_msgunregister ();
	msgclose (d);
}

static void
ap_proc (void)
{
	call_initfunc ("ap");
	call_initfunc ("pcpu");
}

static void
bsp_proc (void)
{
	call_initfunc ("bsp");
	call_initfunc ("pcpu");
}

asmlinkage void
vmm_main (struct multiboot_info *mi_arg)
{
	memcpy (&mi, mi_arg, sizeof (struct multiboot_info));
	initfunc_init ();
	call_initfunc ("global");
	start_all_processors (bsp_proc, ap_proc);
}

INITFUNC ("pcpu1", sync_all_processors);
INITFUNC ("pcpu2", virtualization_init_pcpu);
INITFUNC ("pcpu5", create_pass_vm);
INITFUNC ("bsp0", debug_on_shift_key);
INITFUNC ("global1", print_boot_msg);
INITFUNC ("global3", copy_minios);
INITFUNC ("global3", get_shiftflags);

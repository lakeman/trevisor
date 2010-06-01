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

#ifndef _CORE_VCPU_H
#define _CORE_VCPU_H

#include "cpu_mmu_spt.h"
#include "cpuid.h"
#include "gmm.h"
#include "io_io.h"
#include "mmio.h"
#include "msr.h"
#include "svm.h"
#include "types.h"
#include "vmctl.h"
#include "vt.h"
#include "xsetbv.h"

struct exint_func {
	void (*int_enabled) (void);
	void (*exintfunc_default) (int num);
	void (*hlt) (void);
};

struct nmi_func {
	unsigned int (*get_nmi_count) (void);
};

struct apic_func {
	void (*read_cr8) (u64 *val);
	void (*write_cr8) (u64 val);
};

struct vcpu {
	struct vcpu *next;
	union {
		struct vt vt;
		struct svm svm;
	} u;
	bool halt;
	bool initialized;
	u64 tsc_offset;
	bool updateip;
	struct cpu_mmu_spt_data spt;
	struct cpuid_data cpuid;
	struct exint_func exint;
	struct gmm_func gmm;
	struct io_io_data io;
	struct msr_data msr;
	struct vmctl_func vmctl;
	/* vcpu0: data per VM */
	struct vcpu *vcpu0;
	struct mmio_data mmio;
	struct nmi_func nmi;
	struct apic_func apic;
	struct xsetbv_data xsetbv;
};

void vcpu_list_foreach (bool (*func) (struct vcpu *p, void *q), void *q);
void load_new_vcpu (struct vcpu *vcpu0);

#endif

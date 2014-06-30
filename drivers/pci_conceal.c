/*
 * Copyright (c) 2007, 2008 University of Tsukuba
 * Copyright (c) 2010 Igel Co., Ltd
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
#include <core/mmio.h>
#include <token.h>
#include "pci.h"
#include "pci_conceal.h"

/*
   slot=%02x:%02x.%u           (bus_no, device_no, func_no)
   class=%04x                  (class_code >> 8)
   id=%04x:%04x                (vendor_id, device_id)
   subsystem=%04x:%04x         (sub_vendor_id, sub_device_id)
   revision=%02x               (revision_id)
   rev=%02x                    (revision_id)
   programming_interface=%02x  (programming_interface)
   if=%02x                     (programming_interface)
   class_code=%06x             (class_code)
   header_type=%02x            (header_type)

   example:
   - hide all vendor_id=0x1234 devices, except slot 11:22.3
     vmm.driver.pci_conceal=slot=11:22.3, action=allow, id=1234:*, action=deny
   - hide all network controllers
     vmm.driver.pci_conceal=class=02*, action=deny
   - hide all network controllers except vendor_id=0x1234 devices
     vmm.driver.pci_conceal=class=02*, id=1234:*, action=allow,
     class=02*, action=deny
*/
static bool
get_value (char *buf, int bufsize, struct token *tname, struct pci_device *dev)
{
	if (match_token ("slot", tname)) {
		snprintf (buf, bufsize, "%02x:%02x.%u", dev->address.bus_no,
			  dev->address.device_no, dev->address.func_no);
		return true;
	}
	if (match_token ("class", tname)) {
		snprintf (buf, bufsize, "%04x",
			  dev->config_space.class_code >> 8);
		return true;
	}
	if (match_token ("id", tname)) {
		snprintf (buf, bufsize, "%04x:%04x",
			  dev->config_space.vendor_id,
			  dev->config_space.device_id);
		return true;
	}
	if (match_token ("subsystem", tname)) {
		snprintf (buf, bufsize, "%04x:%04x",
			  dev->config_space.sub_vendor_id,
			  dev->config_space.sub_device_id);
		return true;
	}
	if (match_token ("revision", tname) || match_token ("rev", tname)) {
		snprintf (buf, bufsize, "%02x", dev->config_space.revision_id);
		return true;
	}
	if (match_token ("programming_interface", tname) ||
	    match_token ("if", tname)) {
		snprintf (buf, bufsize, "%02x",
			  dev->config_space.programming_interface);
		return true;
	}
	if (match_token ("class_code", tname)) {
		snprintf (buf, bufsize, "%06x", dev->config_space.class_code);
		return true;
	}
	if (match_token ("header_type", tname)) {
		snprintf (buf, bufsize, "%02x", dev->config_space.header_type);
		return true;
	}
	return false;
}

static bool
pci_conceal (struct pci_device *dev, char *p)
{
	char c;
	struct token tname, tvalue;
	char buf[32];
	bool ac, skip;

	skip = false;
	for (;;) {
		c = get_token (p, &tname);
		if (tname.start == tname.end)
			break;
		if (!tname.start)
			panic ("pci_conceal: syntax error 1 %s", p);
		if (c != '=')
			panic ("pci_conceal: syntax error 2 %s", p);
		c = get_token (tname.next, &tvalue);
		if (!tvalue.start)
			panic ("pci_conceal: syntax error 3 %s", p);
		if (c != ',' && c != '\0')
			panic ("pci_conceal: syntax error 4 %s", p);
		if (match_token ("action", &tname)) {
			if (match_token ("allow", &tvalue))
				ac = false;
			else if (match_token ("deny", &tvalue))
				ac = true;
			else
				panic ("pci_conceal: invalid action %s", p);
			if (skip)
				skip = false;
			else
				return ac;
		} else if (!get_value (buf, sizeof buf, &tname, dev)) {
			panic ("pci_conceal: invalid name %s", p);
		} else if (!skip && !match_token (buf, &tvalue)) {
			skip = true;
		}
		p = tvalue.next;
	}
	return false;
}

static int
iohandler (core_io_t io, union mem *data, void *arg)
{
	if (io.dir == CORE_IO_DIR_IN)
		memset (data, 0xFF, io.size);
	return CORE_IO_RET_DONE;
}

static int
mmhandler (void *data, phys_t gphys, bool wr, void *buf, uint len, u32 flags)
{
	if (!wr)
		memset (buf, 0xFF, len);
	return 1;
}

int
pci_conceal_config_read (struct pci_device *pci_device, u8 iosize, u16 offset,
			 union mem *data)
{
	/* Provide fake values for reading the PCI configration space. */
	memset (data, 0xFF, iosize);
	return CORE_IO_RET_DONE;
}

int
pci_conceal_config_write (struct pci_device *pci_device, u8 iosize, u16 offset,
			  union mem *data)
{
	/* Do nothing, ignore any writing. */
	return CORE_IO_RET_DONE;
}

void
pci_conceal_new (struct pci_device *pci_device)
{
	int i;
	struct pci_bar_info bar;

	printf ("[%02X:%02X.%u/%04X:%04X] concealed\n",
		pci_device->address.bus_no, pci_device->address.device_no,
		pci_device->address.func_no,
		pci_device->config_space.vendor_id,
		pci_device->config_space.device_id);
	for (i = 0; i < PCI_CONFIG_BASE_ADDRESS_NUMS; i++) {
		pci_get_bar_info (pci_device, i, &bar);
		if (bar.type == PCI_BAR_INFO_TYPE_IO) {
			core_io_register_handler
				(bar.base, bar.len, iohandler, NULL,
				 CORE_IO_PRIO_EXCLUSIVE, "pci_conceal");
		} else if (bar.type == PCI_BAR_INFO_TYPE_MEM) {
			mmio_register (bar.base, bar.len, mmhandler, NULL);
		}
	}
}

static struct pci_driver pci_conceal_driver = {
	.name		= "conceal",
	.longname	= "PCI device concealer",
	.device		= "id=:", /* this matches no devices */
	.new		= pci_conceal_new,
	.config_read	= pci_conceal_config_read,
	.config_write	= pci_conceal_config_write,
};

struct pci_driver *
pci_conceal_new_device (struct pci_device *dev)
{
	if (pci_conceal (dev, config.vmm.driver.pci_conceal))
		return &pci_conceal_driver;
	else
		return NULL;
}

static void
pci_conceal_init (void)
{
	pci_register_driver (&pci_conceal_driver);
}

PCI_DRIVER_INIT (pci_conceal_init);

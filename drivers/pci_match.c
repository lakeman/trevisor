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
#include "pci_match.h"

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

bool
pci_match (struct pci_device *device, struct pci_driver *driver)
{
	struct token tname, tvalue;
	char *p = driver->device;
	char buf[32], c;

	for (;;) {
		c = get_token (p, &tname);
		if (tname.start == tname.end)
			break;
		if (!tname.start)
			panic ("pci_match: syntax error 1 %s", p);
		if (c != '=')
			panic ("pci_match: syntax error 2 %s", p);
		c = get_token (tname.next, &tvalue);
		if (!tvalue.start)
			panic ("pci_match: syntax error 3 %s", p);
		if (c != ',' && c != '\0')
			panic ("pci_match: syntax error 4 %s", p);
		if (!get_value (buf, sizeof buf, &tname, device))
			panic ("pci_match: invalid name %s", p);
		else if (!match_token (buf, &tvalue))
			return false;
		p = tvalue.next;
	}
	return true;
}

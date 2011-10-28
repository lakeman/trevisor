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

/**
 * @file	drivers/ehci.c
 * @brief	generic EHCI para pass-through driver
 * @author	K. Matsubara
 */
#include <core.h>
#include <core/mmio.h>
#include <core/timer.h>
#include "pci.h"
#include "usb.h"
#include "usb_device.h"
#include "ehci.h"
#include "ehci_debug.h"

static const char driver_name[] = "ehci_generic_driver";
static const char driver_longname[] = 
	"Generic EHCI para pass-through driver 0.9";
static const char virtual_model[40] = 
	"BitVisor Virtual USB2 Host Controller   ";
static const char virtual_revision[8] = "0.9     "; // 8 chars

DEFINE_ALLOC_FUNC(ehci_host);

static struct usb_operations ehciop = {
	.shadow_buffer = ehci_shadow_buffer,
	.submit_control = ehci_submit_control,
	.submit_bulk = ehci_submit_bulk,
	.submit_interrupt = ehci_submit_interrupt,
	.check_advance = ehci_check_urb_advance,
	.deactivate_urb = ehci_deactivate_urb,
};

static void 
ehci_new(struct pci_device *pci_device)
{
	int i;
	struct ehci_host *host;
#if defined(HANDLE_USBMSC)
	extern void usbmsc_init_handle(struct usb_host *host);
#endif
#if defined(HANDLE_USBHUB)
	extern void usbhub_init_handle(struct usb_host *host);
#endif

	dprintft(2, "A EHCI found.\n");
	host = alloc_ehci_host();
	memset(host, 0, sizeof(*host));
	spinlock_init(&host->lock);
	pci_device->host = host;
	for (i = 0; i < EHCI_URBHASH_SIZE; i++)
		LIST2_HEAD_INIT (host->urbhash[i], urbhash);
	LIST2_HEAD_INIT (host->need_shadow, need_shadow);
	LIST2_HEAD_INIT (host->update, update);
	host->usb_host = 
		usb_register_host((void *)host, &ehciop, USB_HOST_TYPE_EHCI);
	ASSERT(host->usb_host != NULL);
	usb_init_device_monitor(host->usb_host);
#if defined(HANDLE_USBMSC)
	usbmsc_init_handle(host->usb_host);
#endif
#if defined(HANDLE_USBHUB)
	usbhub_init_handle(host->usb_host);
#endif

	return;
}

static int
ehci_register_handler(void *data, phys_t gphys, bool wr, void *buf,
		      uint len, u32 flags)
{
	struct ehci_host *host = (struct ehci_host *)data;
	phys_t offset;
	u32 *reg, val;
	u64 portno = 0;
	int ret = 0;

	if (!host)
		return 0;

	offset = gphys - host->iobase;
	reg = (u32 *)mapmem_gphys(gphys, sizeof(u32), MAPMEM_WRITE|MAPMEM_PCD);

#define REGPRN(_level, _wr, _regname) {			    \
		if (_wr)				    \
			dprintft(_level, "write");	    \
		else					    \
			dprintft(_level, "read");	    \
		dprintf(_level, "(" _regname ", %d)", len); \
	}
#define REGPRNLN(_level, _wr, _regname) {		      \
		if (_wr)				      \
			dprintft(_level, "write");	      \
		else					      \
			dprintft(_level, "read");	      \
		dprintf(_level, "(" _regname ", %d)\n", len); \
	}

	if (offset < 0x20) {
		switch (offset) {
		case 0x00: /* CAPLENGTH */
			if (len == sizeof(u8)) {
				REGPRN(2, wr, "CAPLENGTH");
				if (!wr)
					dprintf(3, " = %02x", *(u8 *)reg);
				dprintf(3, "\n");
				break;
			} else if (len > 2) {
				reg = (void *)reg + 2;
				/* through */
			}
		case 0x02: /* HCIVERSION */
			REGPRN(2, wr, "HCIVERSION");
			dprintf(3, " = %04x\n", *(u16 *)reg);
			break;
		case 0x04: /* HCSPARAMS */
			REGPRN(2, wr, "HCSPARAMS");
			if (!wr)
				dprintf(3, " = %08x[N_PORTS:%d, "
					"N_PCC:%d, N_CC:%d]",
					  *(u32 *)reg,
					  *reg & 0x0000000f,
					  (*reg & 0x00000f00) >> 8,
					  (*reg & 0x0000f000) >> 12);
			dprintf(3, "\n");
			break;
		case 0x08: /* HCCPARAMS */
			REGPRN(2, wr, "HCCPARAMS");
			if (!wr)
				dprintf(3, "= %08x[ADDR:%2d, EECP:%02x]", 
					  *reg, 
					  32 + (*reg & 0x00000001) * 32,
					  (*reg & 0x0000ff00) >> 8);
			dprintf(3, "\n");
			break;
		case 0x0c: /* HCSP-PORTROUTE */
			break;
		default:
			REGPRN(2, wr, "adr");
			dprintft(3, ": %x + %02x\n", 
				 host->iobase, offset);
		}
	} else {
		switch (offset - 0x20) {
		case 0x00: /* USBCMD */
			if (!wr) {
				dprintft(3, "read(USBCMD, %d) = %08x[", 
					len, *reg);
				if (*reg & 0x00000001)
					dprintf(3, "RUN,");
				else
					dprintf(3, "STOP,");
				if (*reg & 0x00000002)
					dprintf(3, "HCRESET,");
				if (*reg & 0x00000010)
					dprintf(3, "PSEN,");
				if (*reg & 0x00000020) 
					dprintf(3, "ASEN,");
				if (*reg & 0x00000040)
					dprintf(3, "DBELL,");
				if (*reg & 0x00000080)
					dprintf(3, "LHCRESET,");
				dprintf(3, "]\n");
			} else {
				u32 cmd = *(u32 *)buf;
				dprintft(3, "write(USBCMD, %08x[", cmd);
				if (cmd & 0x00000001) {
					dprintf(3, "RUN,");
					host->running = 1;
					if (host->intr)
						host->usb_stopped = 0;
				} else {
					dprintf(3, "STOP,");
					host->running = 0;
					if (!host->intr)
						host->usb_stopped = 1;
				}
				if (cmd & 0x00000002) {
					dprintf(3, "HCRESET,");
					host->hcreset = 1;
				}
				if (cmd & 0x00000010)
					dprintf(3, "PSEN,");
				if (cmd & 0x00000020) {
					dprintf(3, "ASEN,");
					if (!host->enable_async) {
#if defined(ENABLE_DELAYED_START)
						*(u32 *)buf = 
							cmd & ~(0x00000020U);
#endif /* defined(ENABLE_DELAYED_START) */
						spinlock_lock(&host->lock);
						host->enable_async = 1;
						spinlock_unlock(&host->lock);
					}
				} else {
					spinlock_lock(&host->lock);
					host->enable_async = 0;
					spinlock_unlock(&host->lock);
				}
				if (cmd & 0x00000040) {
					dprintf(3, "DBELL,");
					spinlock_lock(&host->lock);
					if (host->doorbell)
						host->doorbell = 0;
					spinlock_unlock(&host->lock);
				} else {
					spinlock_lock(&host->lock);
					if (host->doorbell)
						*(u32 *)buf |= 0x00000040U;
					spinlock_unlock(&host->lock);
				}
				if (cmd & 0x00000080)
					dprintf(3, "LHCRESET,");
				dprintf(3, "], %d)\n", len);
			}
			break;
		case 0x04: /* USBSTS */
			if (!wr) {
				spinlock_lock(&host->lock);
				ehci_check_advance(host->usb_host);
				spinlock_unlock(&host->lock);
				dprintft(3, "read(USBSTS, %d) = %08x[", 
					len, *reg);
				if (*reg & 0x00000001)
					dprintf(3, "INT,");
				if (*reg & 0x00000002)
					dprintf(3, "EINT,");
				if (*reg & 0x00000004)
					dprintf(3, "PTCH,");
				if (*reg & 0x00000008)
					dprintf(3, "FLRO,");
				if (*reg & 0x00000010)
					dprintf(3, "SYSE,");
				if (*reg & 0x00000020) {
					dprintf(3, "ASADV,");
					spinlock_lock(&host->lock);
					if (host->doorbell)
					host->doorbell = 0;
					ehci_cleanup_urbs (host);
					spinlock_unlock(&host->lock);
				}
				if (*reg & 0x00001000)
					dprintf(3, "HALT,");
				if (*reg & 0x00002000)
					dprintf(3, "ASEMP,");
				if (*reg & 0x00004000)
					dprintf(3, "PSEN,");
				if (*reg & 0x00008000)
					dprintf(3, "ASEN,");
				dprintf(3, "]\n");
			} else {
				dprintft(3, "write(USBSTS, %08x, %d)\n", 
					*(u32 *)buf, len);
			}
			break;
		case 0x08: /* USBINTR */
			if (!wr) {
				dprintft(3, "read(USBINTR, %d) = %08x\n", 
					len, *reg);
			} else {
				u32 intr = *(u32 *)buf;
				dprintft(3, "write(USBINTR, %08x[", intr);
				if (intr & 0x00000001)
					dprintf(3, "INT,");
				if (intr & 0x00000002)
					dprintf(3, "ERR,");
				if (intr & 0x00000004)
					dprintf(3, "PORTCH,");
				if (intr & 0x00000008)
					dprintf(3, "FLRO,");
				if (intr & 0x00000010)
					dprintf(3, "SYSE,");
				if (intr & 0x00000020)
					dprintf(3, "ASADV,");
				dprintf(3, "], %d)\n", len);
				host->intr = intr & 0x003f;
				if (!host->intr && !host->running)
					host->usb_stopped = 1;
				else if (host->intr && host->running)
					host->usb_stopped = 0;
			}
			break;
		case 0x0c: /* FRINDEX */
			REGPRNLN(3, wr, "FRINDEX");
			break;
		case 0x10: /* CTRLDSSEGMENT */
			REGPRN(2, wr, "CTRLDSSEGMENT");
			val = (wr) ? *(u32 *)buf : *(u32 *)reg;
			dprintf(3, ": %08x\n", val);
			break;
		case 0x14: /* PERIODICLISTBASE */
			REGPRNLN(2, wr, "PERIODICLISTBASE");
			break;
		case 0x18: /* ASYNCLISTADDR */
			REGPRN(2, wr, "ASYNCLISTADDR");
			if (wr) {
				dprintf(3, ": %08x", *(u32 *)buf);
				spinlock_lock(&host->lock);
				if (host->headqh_phys[0] &&
				    (host->headqh_phys[0] != 
				     (*(u32 *)buf & 0xffffffe0U)))
					dprintft(1, "FATAL: "
						 "overwrite address!!\n");
				host->headqh_phys[0] = 
					*(u32 *)buf & 0xffffffe0U;
				host->usb_stopped = 0;
				host->hcreset = 0;
				if (host->headqh_phys[0] && 
				    !host->headqh_phys[1]) {
					host->headqh_phys[1] = 
						ehci_shadow_async_list(host);
				}
#if defined(ENABLE_SHADOW)
				*reg = host->headqh_phys[1] | 0x00000002U;
				spinlock_unlock(&host->lock);
				dprintf(3, " -> %08x\n", *reg);
				ret = 1;
#else
				dprintf(3, "\n");
#endif
			} else {
				spinlock_lock(&host->lock);
				*(u32 *)buf = 
					host->headqh_phys[0] | 0x00000002U;
				spinlock_unlock(&host->lock);
				dprintf(3, ": %08x == %08x\n", 
					*(u32 *)buf, *(u32 *)reg);
				ret = 1;
			}
			break;
		case 0x40: /* CONFIGFLAG */
			REGPRN(2, wr, "CONFIGFLAG");
			if (!wr) {
				if (*reg)
					dprintf(3, ": ROUTE TO EHCI\n");
				else
					dprintf(3, ": ROUTE TO CLASSIC HC\n");
			} else {
				u32 cflag = *(u32 *)buf;
				if (cflag)
					dprintf(3, ": ROUTE TO EHCI");
				else
					dprintf(3, ": ROUTE TO CLASSIC HC");
			}
			dprintf(3, "\n", len);
			break;
		case 0x44: /* PORTSC1 */
		case 0x48: /* PORTSC2 */
		case 0x4c: /* PORTSC3 */
		case 0x50: /* PORTSC4 */
		case 0x54: /* PORTSC5 */
		case 0x58: /* PORTSC6 */
		case 0x5c: /* PORTSC7 */
		case 0x60: /* PORTSC8 */
			portno = (offset - 0x64) >> 2;
			ASSERT(portno < EHCI_MAX_N_PORTS);
			if (!wr) {
				u32 status;
				u32 mask = 0x0000ffff;
				u16 port_sc;
				
				status = *reg; /* atomic */
				if (host->portsc[portno] == status)
					break;

				spinlock_lock(&host->lock);
				host->portsc[portno] = status;
				port_sc = status & mask;
				handle_connect_status(host->usb_host, 
							portno, port_sc);
				spinlock_unlock(&host->lock);

				dprintft(4, "read(PORTSC%d, %d) = %08x[", 
					portno + 1, len, status);
				if (status & 0x00000001)
					dprintf(4, "CON,");
				if (status & 0x00000002)
					dprintf(4, "STSCH,");
				if (status & 0x00000004)
					dprintf(4, "EN,");
				if (status & 0x00000008)
					dprintf(4, "ENCH,");
				if (status & 0x00000010)
					dprintf(4, "OC,");
				if (status & 0x00000020)
					dprintf(4, "OCCH,");
				if (status & 0x00000040)
					dprintf(4, "RES,");
				if (status & 0x00000080)
					dprintf(4, "SUS,");
				if (status & 0x00000100)
					dprintf(4, "RESET,");
				if ((status & 0x00000c00) == 0)
					dprintf(4, "SE0,");
				else if ((status & 0x00000c00) == 0x00000400)
					dprintf(4, "K-state,");
				else if ((status & 0x00000c00) == 0x00000800)
					dprintf(4, "J-state,");
				if (status & 0x00001000)
					dprintf(4, "PP,");
				if (status & 0x00002000)
					dprintf(4, "COMPHC,");
				if (status & 0x0000c000)
					dprintf(4, "LED,");
				if (status & 0x000f0000)
					dprintf(4, "TEST,");
				if (status & 0x00100000)
					dprintf(4, "WOC,");
				if (status & 0x00200000)
					dprintf(4, "WODC,");
				if (status & 0x00400000)
					dprintf(4, "WOOC,");
				dprintf(4, "]\n");
			} else {
				u16 port_sc;
				port_sc = *(u32 *)buf & 0x0000FFFF;

				handle_port_reset(host->usb_host, portno,
							port_sc, 8);
				dprintft(3, "write(PORTSC%d, %08x, %d)\n", 
					 portno + 1, *(u32 *)buf, len);
			}
			break;
		default:
			REGPRN(2, wr, "adr");
			dprintf(3, ":%x + %02x\n", 
				 host->iobase, offset);
		}
	}
	unmapmem(reg, sizeof(u32));

	return ret;
}

static int 
ehci_config_read(struct pci_device *pci_device, 
		 core_io_t io, u8 offset, union mem *data)
{
	switch (offset) {
	case PCI_CONFIG_BASE_ADDRESS0:
		dprintft(2, "reading PCI_CONFIG_BASE_ADDRESS0\n");
		break;
	case 0x61:
		dprintft(2, "reading FLADJ\n");
		break;
	case 0x62:
		dprintft(2, "reading PORTWAKECAP\n");
		break;
	default:
		break;
	}

	return CORE_IO_RET_DEFAULT;
}

static int 
ehci_config_write(struct pci_device *pci_device, 
		  core_io_t io, u8 offset, union mem *data)
{
	struct ehci_host *host = pci_device->host;
	u32 iobase;
	void *handle;

	switch (offset) {
	case PCI_CONFIG_BASE_ADDRESS0:
		iobase = (*data).dword;
		dprintft(2, "updating PCI_CONFIG_BASE_ADDRESS0 "
			 "with %08x\n", iobase);
		if (iobase < 0xffffffdeU) {
			host->iobase = iobase;
			handle = mmio_register(iobase, 0x80, 
					       ehci_register_handler, 
					       (void *)host);
		}
		break;
	case 0x61:
		dprintft(2, "updating FLADJ with %02x\n", (*data).byte);
		       
		break;
	case 0x62:
		dprintft(2, "updating PORTWAKECAP with %04x\n", (*data).word);
		break;
	default:
		break;
	}

	return CORE_IO_RET_DEFAULT;
}

static struct pci_driver ehci_driver = {
	.name		= driver_name,
	.longname	= driver_longname,
	.id		= { PCI_ID_ANY, PCI_ID_ANY_MASK },
	.class		= { 0x0C0320, 0xFFFFFF },
	.new		= ehci_new,	
	.config_read	= ehci_config_read,
	.config_write	= ehci_config_write,
};

void
ehci_conceal_portroute(void *handle, void *data)
{
	u32 *reg = data;

	if (*reg) {
		*reg = 0U;
		dprintft(0, "EHCI: Set PORT ROUTE "
			 "TO COMPANION HC (UHCI)\n");
	}

	timer_set(handle, 1000 * 1000);
}

static void 
ehci_conceal_new(struct pci_device *pci_device)
{
	u32 *reg;
	phys_t iobase;
	void *handle;

	printf("An EHCI host controller found. Disable it.\n");
	iobase = pci_device->config_space.base_address[0];
	if ((iobase == 0) || (iobase >= 0xffffffdeU))
		return;

	/* CONFIGFLAG */
	reg = (u32 *)mapmem_gphys(iobase + 0x20U + 0x40U, sizeof(u32), 0);
	printf("EHCI: CONFIGFLAG: %08x -> ", *reg);
	if (*reg) {
		*reg = 0U;
		printf("%08x\n", *reg);
	}

	handle = timer_new(ehci_conceal_portroute, reg);
	timer_set(handle, 1000 * 1000);

	/* HCSPARAMS */
	reg = (u32 *)mapmem_gphys(iobase + 0x04U, sizeof(u32), 0);
	printf("EHCI: HCSPARAMS: %08x\n", *reg);
	unmapmem(reg, sizeof(u32));

	/* HCSP-PORTROUTE */
	reg = (u32 *)mapmem_gphys(iobase + 0x0cU, sizeof(u32), 0);
	printf("EHCI: HCSP-PORTROUTE: %08x\n", *reg);
	unmapmem(reg, sizeof(u32));

	return;
}

static int 
ehci_conceal_config_read(struct pci_device *pci_device, 
		 core_io_t io, u8 offset, union mem *data)
{
	/* provide fake values 
	   for reading the PCI configration space. */
	data->dword = 0UL;
	return CORE_IO_RET_DONE;
}

static int 
ehci_conceal_config_write(struct pci_device *pci_device, 
		  core_io_t io, u8 offset, union mem *data)
{
	/* do nothing, ignore any writing. */
	return CORE_IO_RET_DONE;
}

static struct pci_driver ehci_conceal_driver = {
	.name		= driver_name,
	.longname	= driver_longname,
	.id		= { PCI_ID_ANY, PCI_ID_ANY_MASK },
	.class		= { 0x0C0320, 0xFFFFFF },
	.new		= ehci_conceal_new,	
	.config_read	= ehci_conceal_config_read,
	.config_write	= ehci_conceal_config_write,
};

/**
 * @brief	driver init function automatically called at boot time
 */
void 
ehci_init(void) __initcode__
{
	if (config.vmm.driver.concealEHCI)
		pci_register_driver(&ehci_conceal_driver);
	else if (config.vmm.driver.usb.ehci)
		pci_register_driver(&ehci_driver);
	return;
}
PCI_DRIVER_INIT(ehci_init);

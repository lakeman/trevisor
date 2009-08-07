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
 * @file	drivers/uhci_shadow.c
 * @brief	script for shadowing the frame
 * @author	K. Matsubara
 */
#include <core.h>
#include <core/thread.h>
#include "pci.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_hook.h"
#include "usb_log.h"
#include "uhci.h"

extern phys32_t uhci_monitor_boost_hc;

/** 
 * @brief skip all the isochronous TDs (not yet supported)
 * @param host structure uhci_host 
 * @param phys phys32_t 
 */
static inline phys32_t
skip_isochronous(struct uhci_host *host, phys32_t phys)
{
	phys32_t p;
	struct uhci_td *td;

	for (p = phys; !is_terminate(p) && !is_qh_link(p); p = td->link)
		td = uhci_gettdbypaddr(host, p, 0);
	
	if (is_terminate(p)) {
		dprintf(3, "(none)\n");
		return UHCI_FRAME_LINK_TE;
	}

	return uhci_link(p);
}

/**
 * @brief returns the urb from the physicl address of QH
 * @param list struct usb_request_block 
 * @param qh_phys phys32_t 
 */
static inline struct usb_request_block *
geturbbyqh_phys(struct usb_request_block *list, phys32_t qh_phys)
{
	struct usb_request_block *urb;

	for (urb = list; urb; urb = urb->next) {
		if ((phys32_t)URB_UHCI(urb)->qh_phys == uhci_link(qh_phys))
			return urb;
	}

	return NULL;
}

/**
* @brief check if the urb is skeleton or not 
* @param urb struct usb_request_block 
*/
static inline int
is_skelton(struct usb_request_block *urb)
{
	return (urb->address == URB_ADDRESS_SKELTON);
}


/**
* @brief check if the QH is shadowd 
* @param host struch uhci_host 
* @param qh_phys phys32_t 
*/ 
static inline int
is_shadowed(struct uhci_host *host, phys32_t qh_phys)
{
	return (geturbbyqh_phys(host->guest_urbs, qh_phys) != NULL);
}

/* release_urb(): cleaning up usb_request_block structure 
   especially for guest-issued urbs(QH and TDs),
   in contrasted with destroy_urb() for host-issued urbs 
* @param host struct uhci_host 
* @param urb usb_request_block 
*/


static inline void
release_urb(struct uhci_host *host, struct usb_request_block *urb)
{
	struct uhci_td_meta *tdm, *nexttdm;
	struct usb_buffer_list *next_buffer;

	dprintft(2, "%04x: %s: urb(%p) released.\n",
		 host->iobase, __FUNCTION__, urb);

	if (is_skelton(urb)) {
		dprintft(2, "%04x: %s: really want to delete "
			 "skelton(%p)??\n", host->iobase, __FUNCTION__, urb);
		return;
	}

	if (URB_UHCI(urb)->qh)
		unmapmem(URB_UHCI(urb)->qh, sizeof(struct uhci_qh));

	tdm = URB_UHCI(urb)->tdm_head;
	while (tdm) {
		if (tdm->td)
			unmapmem(tdm->td, sizeof(struct uhci_td));
		nexttdm = tdm->next;
		free(tdm);
		tdm = nexttdm;
	}

	/* clean up buffers */
	while (urb->buffers) {
		next_buffer = urb->buffers->next;
		free(urb->buffers);
		urb->buffers = next_buffer;
	}

	if (urb->shadow)
		urb->shadow->shadow = NULL;

	free(urb->hcpriv);
	free(urb);

	return;
}

/**
 * @brief checks whether if it is a linked loop
 * @param phys u32
 * @param g_tdm struct uhci_td_meta
 * @param h_tdm struct uhci_td_meta   
 */
static inline u32
is_link_loop(u32 phys, struct uhci_td_meta *g_tdm, 
	     struct uhci_td_meta *h_tdm)
{
	if (!phys || !g_tdm || !h_tdm)
		return 0U;

	while (g_tdm && h_tdm) {
		if (g_tdm->td_phys == phys)
			return h_tdm->td_phys;

		g_tdm = g_tdm->next;
		h_tdm = h_tdm->next;
	}

	/* no loop detected */
	return 0U;
}

DEFINE_ZALLOC_FUNC(usb_buffer_list)

/**
 * @brief figure out continuous buffer blocks, make a list of them
 * @param urb usb request block issued by guest
 */
static int
make_buffer_list(struct usb_request_block *urb)
{
	struct usb_buffer_list *ub, *ub_tail;
	struct uhci_td_meta *tdm;
	phys_t next_addr_phys;
	size_t offset, len;
	int n = 0;

	tdm = URB_UHCI(urb)->tdm_head;
	ub_tail = NULL;
	offset = 0;
	while (tdm && is_active_td(tdm->td) && tdm->td->buffer) {
		/* create a new entry */
		ub = zalloc_usb_buffer_list();
		ASSERT(ub != NULL);
		ub->pid = get_pid_from_td(tdm->td);
		ub->padr = next_addr_phys = (phys_t)tdm->td->buffer;
		ub->offset = offset;

		/* continuous buffers should be treated as one */
		do {
			len = uhci_td_maxlen(tdm->td);
			next_addr_phys += len;
			ub->len += len;
			
			tdm = tdm->next;
		} while (tdm && is_active_td(tdm->td) && 
			 (ub->pid == get_pid_from_td(tdm->td)) &&
			 (tdm->td->buffer == (phys32_t)next_addr_phys));

		/* append the entry at the tail */
		if (ub_tail)
			ub_tail->next = ub;
		else
			urb->buffers = ub;
		ub_tail = ub;
			
		/* update offset for next */
		if (ub->pid != UHCI_TD_TOKEN_PID_SETUP)
			offset += ub->len;
		else
			offset = 0;

		n++;
	}

	if (ub_tail)
		ub_tail->next = NULL;

	return n;
}
	
/**
 * @brief duplicate transfer buffers
 * @param host uhci host controller
 * @param urb guest urb
 */
int
uhci_shadow_buffer(struct usb_host *usbhc,
		   struct usb_request_block *gurb, u32 flag)
{
	struct usb_request_block *hurb = gurb->shadow;
	struct usb_buffer_list *gub, *hub, *ub_tail;
	struct uhci_td_meta *tdm;
	virt_t gvadr;
	phys_t diff;

	ASSERT(gurb->buffers != NULL);
	ASSERT(hurb != NULL);
	ASSERT(hurb->buffers == NULL);

	/* duplicate buffers */
	ub_tail = NULL;
	for (gub = gurb->buffers; gub; gub = gub->next) {
		hub = zalloc_usb_buffer_list();
		ASSERT(hub != NULL);
		hub->pid = gub->pid;
		hub->offset = gub->offset;
		hub->len = gub->len;
		hub->vadr = (virt_t)alloc2_aligned(hub->len, &hub->padr);

		ASSERT(hub->vadr);
		if (flag) {
			gvadr = (virt_t)mapmem_gphys(gub->padr, gub->len, 0);
			ASSERT(!gvadr);
			memcpy((void *)hub->vadr, (void *)gvadr, hub->len);
			unmapmem((void *)gvadr, gub->len);
		}
		if (ub_tail)
			ub_tail->next = hub;
		else
			hurb->buffers = hub;
		ub_tail = hub;
	}

	if (ub_tail)
		ub_tail->next = NULL;

	/* fit buffer pointer in shadow TDs with the duplicated buffers */
	for (tdm = URB_UHCI(hurb)->tdm_head; tdm; tdm = tdm->next) {
		/* identificate which buffer a TD's buffer points to. */
		if (!tdm->td->buffer || !is_active_td(tdm->td))
			continue;

		gub = gurb->buffers;
		hub = hurb->buffers;
		while (gub && hub) {
			ASSERT(hub->padr >= gub->padr);
			diff = hub->padr - gub->padr;
			if ((gub->padr <= (phys_t)tdm->td->buffer) &&
			    ((phys_t)tdm->td->buffer < 
			     (gub->padr + gub->len))) {
				tdm->td->buffer += diff;
				break;
			}
			gub = gub->next;
			hub = hub->next;
		}
		if (!gub || !hub)
			printf("warn: \n");
	}

	return 0;
}

/**
 * @brief copy the qcontext 
 * @param host struct uhci_host 
 * @param g_urb struct usb_request_block 
 */
static inline struct usb_request_block *
duplicate_qcontext(struct uhci_host *host, struct usb_request_block *g_urb)
{
	struct usb_request_block *urb;
	struct uhci_td *td;
	struct uhci_td_meta **h_tdm_p, **g_tdm_p;
	u32 *td_phys_p;
	u32 loop_phys;
	int n_tds;

	URB_UHCI(g_urb)->qh_element_copy = URB_UHCI(g_urb)->qh->element;

	/* create a urb for host(VM)'s (shadow) qcontext. */
	urb = create_urb(host);
	if (!urb)
		return NULL;
	URB_UHCI(urb)->qh = uhci_alloc_qh(host, &URB_UHCI(urb)->qh_phys);
	if (!URB_UHCI(urb)->qh)
		goto fail_duplicate;

	URB_UHCI(urb)->qh->link = UHCI_QH_LINK_TE;
	if (URB_UHCI(g_urb)->qh->element & UHCI_QH_LINK_QH) {
		dprintft(2, "%04x: FIXME: QH(%llx)'s element "
			 "points to another QH(%x)\n", 
			 host->iobase, URB_UHCI(g_urb)->qh_phys, 
			 URB_UHCI(g_urb)->qh->element);
		goto fail_duplicate;
	}
	URB_UHCI(urb)->qh->element = URB_UHCI(g_urb)->qh->element;

	/* duplicate TDs and set qh->element */
	td_phys_p = &URB_UHCI(urb)->qh->element;
	h_tdm_p = &URB_UHCI(urb)->tdm_head;
	g_tdm_p = &URB_UHCI(g_urb)->tdm_head;
	*g_tdm_p = NULL;
	n_tds = 0;
	while (!is_terminate(*td_phys_p)) {
		td = uhci_gettdbypaddr(host, *td_phys_p, 0);
		*g_tdm_p = uhci_new_td_meta(host, td);
		(*g_tdm_p)->td_phys = uhci_link(*td_phys_p);
		*h_tdm_p = uhci_new_td_meta(host, NULL);

		if (td->link & UHCI_TD_LINK_QH) {
			dprintft(1, "%04x: FIXME: TD(%x)'s link "
				 "points to another QH(%x)\n", 
				 host->iobase, *td_phys_p, td->link);
			goto fail_duplicate;
		}

		(*h_tdm_p)->td->link = td->link;
		(*h_tdm_p)->status_copy = 
			(*h_tdm_p)->td->status = td->status;
		(*h_tdm_p)->token_copy = 
			(*h_tdm_p)->td->token = td->token;
		(*h_tdm_p)->td->buffer = td->buffer;

		*td_phys_p = (phys32_t)(*h_tdm_p)->td_phys | 
			(*td_phys_p & UHCI_TD_LINK_VF);
		td_phys_p = &(*h_tdm_p)->td->link;
		
		n_tds++;

		/* loop detection */
		loop_phys = is_link_loop(td->link, 
					 URB_UHCI(g_urb)->tdm_head, 
					 URB_UHCI(urb)->tdm_head);
		if (loop_phys) {
			*td_phys_p = loop_phys;
			break;
		} 

		/* stop if inactive TD */
		if (!is_active_td(td)) {
			*td_phys_p = UHCI_TD_LINK_TE;
			break;
		}

		h_tdm_p = &(*h_tdm_p)->next;
		g_tdm_p = &(*g_tdm_p)->next;
	}

	URB_UHCI(g_urb)->tdm_tail = *g_tdm_p;
	URB_UHCI(urb)->tdm_tail = *h_tdm_p;

	/* link each other */
	urb->shadow = g_urb;
	g_urb->shadow = urb;

	/* fix up the shadow urb */
	if (URB_UHCI(urb)->tdm_head) {
		u8 endpoint, pid;

		urb->address = g_urb->address =
			get_address_from_td(URB_UHCI(urb)->tdm_head->td);
		urb->dev = g_urb->dev =
			get_device_by_address(host->hc, urb->address);
		endpoint = get_endpoint_from_td(URB_UHCI(urb)->tdm_head->td);
		pid = get_pid_from_td(URB_UHCI(urb)->tdm_head->td);
		if (pid == USB_PID_IN)
			endpoint |= 0x80U;
		urb->endpoint = g_urb->endpoint = 
			get_edesc_by_address(urb->dev, endpoint);
	}
	
	dprintft(2, "%04x: %s: %d TDs in a urb(%p:%p)\n",
		 host->iobase, __FUNCTION__, n_tds, urb, g_urb);
	return urb;
fail_duplicate:
	urb->status = URB_STATUS_UNLINKED;
	destroy_urb(host, urb);
	return NULL;
}

/**
 * @brief  callback
 * @param host struct uhci_host 
 * @param urb usb_request_block
 * @param g_urb usb_request_block 
*/
static int
_copyback_qcontext(struct uhci_host *host,	
		   struct usb_request_block *urb, 
		   struct usb_request_block *g_urb)
{
	struct uhci_td_meta *h_tdm, *g_tdm;
	phys32_t qh_element = URB_UHCI(urb)->qh->element;

	/* make sure that the original exists */
	if (!is_linked(host->guest_urbs, g_urb)) {
		dprintft(2, "%04x: %s: the original(%p) has gone.\n",
			 host->iobase, __FUNCTION__, g_urb);
		goto not_copyback;
	}

	/* copyback each TD's status field */
	h_tdm = URB_UHCI(urb)->tdm_head;
	g_tdm = URB_UHCI(g_urb)->tdm_head;
	while (h_tdm) {

		/* copyback each TD's status which 
		   had been active before activated. */
		if ((h_tdm->status_copy & UHCI_TD_STAT_AC) &&
		    (g_tdm->td->status == g_tdm->status_copy))
			URB_UHCI(g_urb)->td_stat = g_tdm->status_copy =
				g_tdm->td->status = h_tdm->td->status;

		/* qh->element still points to a TD 
		   if short packet occured */
		if (qh_element == (phys32_t)h_tdm->td_phys) {
			URB_UHCI(g_urb)->qh_element_copy = 
				URB_UHCI(g_urb)->qh->element = 
				(phys32_t)g_tdm->td_phys;
			break;
		}

		h_tdm = h_tdm->next;
		g_tdm = g_tdm->next;
	}

	/* reset qh->element if terminated */
	if (is_terminate(qh_element)) {
		URB_UHCI(g_urb)->qh_element_copy = 
			URB_UHCI(g_urb)->qh->element = qh_element;
		URB_UHCI(g_urb)->td_stat = 0;	/* copy td status */
	}

	dprintft(2, "%04x: %s: copybacked TDs.\n",
		 host->iobase, __FUNCTION__);
not_copyback:
	return 0;
}

/**
 * @brief copyback qcontext 
 * @param host struct uhci_host 
 * @param urb struct usb_request_block 
 * @param arg void *
 */
static int
uhci_copyback_qcontext(struct usb_host *hc,
		       struct usb_request_block *urb, void *arg)
{
	struct uhci_host *host = (struct uhci_host *)hc->private;
	struct usb_request_block *g_urb = (struct usb_request_block *)arg;
	int r;

	dprintft(2, "%04x: %s(%p, %p) invoked.\n", 
		 host->iobase, __FUNCTION__, urb, g_urb);
	
	spinlock_lock(&host->lock_gfl);

	/* process some interest urbs */
	r = usb_hook_process(host->hc, urb, USB_HOOK_REPLY);
	if (r != USB_HOOK_DISCARD)
		_copyback_qcontext(host, urb, g_urb);

	spinlock_unlock(&host->lock_gfl);

#if 0
       /* unlink the urb from frame list */
       uhci_deactivate_urb(host->hc, urb);
#else
       urb->status = URB_STATUS_FINALIZED;
#endif
	dprintft(2, "%04x: %s exit.\n", host->iobase, __FUNCTION__);

	return 0;
}

/**
 * @brief remove and deactivate urbs 
 * @param host struct uhci_host
 * @param urb usb_request_block
 */
static inline int
remove_and_deactivate_urb(struct uhci_host *host,
			  struct usb_request_block *urb)
{
	/* remove the urb */
	if (urb->shadow) {
		if ((urb->shadow->status != URB_STATUS_FINALIZED) &&
		    (urb->shadow->status != URB_STATUS_NAK)) {
			struct uhci_td_meta *tdm;
			phys32_t qh_element;
			int n, elapse;

			qh_element = URB_UHCI(urb->shadow)->qh_element_copy;
			dprintft(0, "%04x: WARNING: An active(%02x) "
				 "urb removed.\n", host->iobase, 
				 urb->shadow->status);
		
			elapse = (host->frame_number + UHCI_NUM_FRAMES - 
				  URB_UHCI(urb)->frnum_issued) & 
				(UHCI_NUM_FRAMES - 1);

			for (tdm = URB_UHCI(urb->shadow)->tdm_head, n = 1; 
			     tdm; tdm = tdm->next, n++) {
				if (qh_element == (phys32_t)tdm->td_phys)
					dprintft(0, "%04x:       "
						 "%4d(stat=%02x)/",
						 host->iobase, n,
						 UHCI_TD_STAT_STATUS(tdm->td));
			}
			dprintf(0, "%4d TDs completed "
				"for %4d ms.\n", n-1, elapse);
		}

		/* uhci_deactivate_urb() does nothing 
		   if already unlinked */
		uhci_deactivate_urb(host->hc, urb->shadow);
	}

	remove_urb(&host->guest_urbs, urb);
	release_urb(host, urb);

	return 1;
}

/**
 * @brief unmark all urbs
 * @param urblist struct usb_request_block 
 */
static inline int
unmark_all_urbs(struct usb_request_block *urblist)
{
	struct usb_request_block *urb;
	int n_unmarked;

	for (urb = urblist; urb; urb = urb->next) {
		urb->mark = 0U;
		n_unmarked++;
	}

	return n_unmarked;
}

static inline void
shadow_ioc_in_td(struct uhci_td_meta *g_tdm,
		 struct uhci_td_meta *h_tdm)
{
	u32 ioc;

	if (!g_tdm || !h_tdm)
		return;

	ioc = g_tdm->td->status & UHCI_TD_STAT_IC;

	if ((g_tdm->status_copy & UHCI_TD_STAT_IC) != ioc) {
		g_tdm->status_copy ^= UHCI_TD_STAT_IC;
		h_tdm->td->status ^= UHCI_TD_STAT_IC;
	}

	return;
}

static inline int
is_updated_td(struct uhci_td_meta *tdm)
{
	u32 status_copy, status;

	if (!tdm)
		return 0;

	/* exclude the IOC bit */
	status_copy = tdm->status_copy & ~UHCI_TD_STAT_IC;
	status = tdm->td->status & ~UHCI_TD_STAT_IC;

	return ((status_copy != status) ||
		(tdm->token_copy != tdm->td->token));
}

static inline u32
get_toptd_stat(struct uhci_host *host, struct usb_request_block *urb)
{
	struct uhci_td *td;
	u32 tdstat;

	if (!urb || !host)
		return 0;
	if (!URB_UHCI(urb)->qh)
		return 0;

	tdstat = URB_UHCI(urb)->td_stat;

	if (!is_terminate(URB_UHCI(urb)->qh->element)) {
		td = uhci_gettdbypaddr(host, URB_UHCI(urb)->qh->element, 0);
		if (td) {
			tdstat = td->status;
			unmapmem(td, sizeof(struct uhci_td));
		}
	} else {
		tdstat = 0;
	}

	return tdstat;
}

/** 
 * @brief mark the in linked urbs 
 * @param host struct uhci_host 
 * @param urblist array of struct usb_request_block 
 * @param skelurb struct usb_request_block
 */
static inline int
mark_inlinked_urbs(struct uhci_host *host, 
		       struct usb_request_block *urblist,
		       struct usb_request_block *skelurblist[],
		       int interval)
{
	struct usb_request_block *urb, *prevurb, *skelurb;
	phys32_t qh_link_phys, qh_element;
	u32 tdstat;
	int n_marked = 0;

	for (; interval < UHCI_NUM_INTERVALS; interval++) {
		skelurb = prevurb = skelurblist[interval];
		/* skip scanning if already marked */
		if (skelurb->mark & URB_MARK_INLINK)
			continue;
		
		skelurb->mark |= URB_MARK_INLINK;
		qh_link_phys = uhci_link(URB_UHCI(skelurb)->qh->link);
		while (!is_terminate(qh_link_phys)) {
			urb = geturbbyqh_phys(urblist, qh_link_phys);
			if (!urb) {
				dprintf(2, "%04x: %s: a new urb(%x) "
					"that follows skelurb(%p:%llx) "
					"found.\n", host->iobase,
					__FUNCTION__, qh_link_phys,
					skelurb, URB_UHCI(skelurb)->qh_phys);
				urb = create_urb(host);
				if (!urb) {
					dprintft(2, "%04x: %s: "
						 "create_urb failed.\n",
						 host->iobase, __FUNCTION__);
					break;
				}
				URB_UHCI(urb)->qh = 
					uhci_getqhbypaddr(host,
							  qh_link_phys, 0);
				if (!URB_UHCI(urb)->qh) {
					dprintft(2, "%04x: %s: "
						 "getqh(%x) failed.\n",
						 host->iobase, __FUNCTION__,
						 qh_link_phys);
					release_urb(host, urb);
					break;
				}
				URB_UHCI(urb)->qh_element_copy = 
					URB_UHCI(urb)->qh->element;
				URB_UHCI(urb)->qh_phys = qh_link_phys;
				urb->mark = URB_MARK_NEED_SHADOW;
				URB_UHCI(urb)->td_stat = 
					get_toptd_stat(host, urb);

				urb->next = prevurb->next;
				prevurb->next = urb;
			} else {
				/* stop marking if already marked. */
				if (urb->mark & URB_MARK_INLINK)
					break;
			}
			urb->mark |= URB_MARK_INLINK;
			qh_element = URB_UHCI(urb)->qh->element;

			/* a urb must be updated by guest
			   if active status transition detected */
			tdstat = get_toptd_stat(host, urb);
			if (is_active(tdstat) != 
			    is_active(URB_UHCI(urb)->td_stat)) {

				dprintft(2, "%04x: top td's status linked "
					 "to qh was changed. "
					 "(saved td_stat =%p, tdstat=%p)\n",
					 host->iobase, 
					 URB_UHCI(urb)->td_stat, tdstat);

				URB_UHCI(urb)->qh_element_copy = qh_element;
				urb->mark |= URB_MARK_NEED_UPDATE;

			}
			URB_UHCI(urb)->td_stat = tdstat;

			if (urb->shadow)
				shadow_ioc_in_td(URB_UHCI(urb)->tdm_tail, 
						 URB_UHCI(urb->shadow)
						 ->tdm_tail);

			n_marked++;
			qh_link_phys = uhci_link(URB_UHCI(urb)->qh->link);
			prevurb = urb;
		}
	}
		
	return n_marked;
}

/**
 * @brief update the marked urbs
 * @param host struct uhci_host
 * @param urblist struct usb_request_block  
 */
static inline int
update_marked_urbs(struct uhci_host *host, 
		       struct usb_request_block *urblist)
{
	struct usb_request_block *urb;
	struct uhci_td_meta *g_tdm, *h_tdm, *nexttdm;
	int n_updated = 0;

	for (urb = urblist; urb; urb = urb->next) {

		if (!(urb->mark & URB_MARK_NEED_UPDATE))
			continue;

		if (!urb->shadow)
			goto create_new;

		n_updated++;
		urb->mark &= ~URB_MARK_NEED_UPDATE;
		if (is_terminate(URB_UHCI(urb)->qh_element_copy)) {
			URB_UHCI(urb->shadow)->qh->element = 
				URB_UHCI(urb)->qh_element_copy;
			continue;
		}

		g_tdm = URB_UHCI(urb)->tdm_head;
		h_tdm = URB_UHCI(urb->shadow)->tdm_head;
		while (g_tdm) {
			if (URB_UHCI(urb)->qh_element_copy == 
			    (phys32_t)g_tdm->td_phys)
					break;

			/* clean up a completed TDmeta in guest urb */
			nexttdm = g_tdm->next;
			unmapmem(g_tdm->td, sizeof(struct uhci_td));
			free(g_tdm);
			g_tdm = URB_UHCI(urb)->tdm_head = nexttdm;

			h_tdm = h_tdm->next;
		}
		if (g_tdm && !is_updated_td(g_tdm)) {
			struct usb_buffer_list *next_buffer;

			/* update buffer list */
			while (urb->buffers) {
				next_buffer = urb->buffers->next;
				free(urb->buffers);
				urb->buffers = next_buffer;
			}
			make_buffer_list(urb);

			h_tdm->td->status = g_tdm->td->status;
			uhci_reactivate_urb(host, urb->shadow, h_tdm);
			dprintft(2, "%04x: QH's element synchronized"
				 " (%p:%llx <= %p:%llx).\n",
				 host->iobase, urb->shadow,
				 URB_UHCI(urb->shadow)->qh_phys, 
				 urb, URB_UHCI(urb)->qh_phys);
		} else {
			struct usb_request_block *newurb;

		create_new:
			/* must be replaced with new TDs */
			dprintft(2, "%04x: %s: TDs replaced with new one.\n", 
				 host->iobase, __FUNCTION__);
			urb->mark = 0; /* unmark for delete */
			newurb = create_urb(host);
			if (!newurb) {
				dprintft(1, "%04x: %s: create_urb failed.\n",
					 host->iobase, __FUNCTION__);
				goto shadow_fail;
			}
			URB_UHCI(newurb)->qh = uhci_getqhbypaddr(host, 
								URB_UHCI(urb)
								->qh_phys, 0);
			if (!URB_UHCI(newurb)->qh) {
				dprintft(1, "%04x: %s: getqh(%x) failed.\n",
					 host->iobase, __FUNCTION__,
					 URB_UHCI(urb)->qh_phys);
				release_urb(host, newurb);
				break;
			}
			URB_UHCI(newurb)->qh_element_copy = 
				URB_UHCI(urb)->qh_element_copy;
			URB_UHCI(newurb)->td_stat = 
				URB_UHCI(urb)->td_stat;
			URB_UHCI(newurb)->qh_phys = 
				URB_UHCI(urb)->qh_phys;
			newurb->mark = URB_MARK_NEED_SHADOW | URB_MARK_INLINK;

			newurb->next = urb->next;
			urb->next = newurb;
		}
	}

shadow_fail:
	return n_updated;
}

/**
 * @brief shadow the marked urbs 
 * @param host struct uhci_host
 * @param urblist struct usb_request_block  
 */ 
static inline int
shadow_marked_urbs(struct uhci_host *host, 
		       struct usb_request_block *urblist)
{
	struct usb_request_block *urb;
	int r, n_shadowed = 0;

	for (urb = urblist; urb; urb = urb->next) {

		if (!(urb->mark & URB_MARK_NEED_SHADOW))
			continue;

		dprintft(3, "%04x: %s: a urb(%p:%llx) "
			 "that need be shadowed found.\n", 
			host->iobase, __FUNCTION__, urb,
			URB_UHCI(urb)->qh_phys);

		if (!duplicate_qcontext(host, urb))
			continue;

		/* figure out buffer blocks pointed by TDs
		   and make a list of them */
		make_buffer_list(urb);
		
		n_shadowed++;
		urb->mark &= ~URB_MARK_NEED_SHADOW;
		dprintft(2, "%04x: shadowed [%p:%llx] into "
			 "[%p:%llx].\n", host->iobase, 
			 urb, URB_UHCI(urb)->qh_phys, 
			 urb->shadow, URB_UHCI(urb->shadow)->qh_phys);

		{
			const char *typestr[4] = {
				"CONTROL", "ISOCHRONOUS",
				"BULK", "INTERRUPT" 
			};
			u8 type;

			type = (urb->endpoint) ?
				USB_EP_TRANSTYPE(urb->endpoint) :
				USB_ENDPOINT_TYPE_CONTROL;

			dprintft(3, "%04x: %s: "
				 "transfer type of urb(%p) = %s\n",
				 host->iobase, __FUNCTION__, 
				 urb, typestr[type]);
		}

		urb->shadow->callback = uhci_copyback_qcontext;
		urb->shadow->cb_arg = (void *)urb;

		/* process some interest urbs */
		r = usb_hook_process(host->hc, urb->shadow, USB_HOOK_REQUEST);
		if (r == USB_HOOK_DISCARD) {
			/* discarded shadow must be never activated. */
			urb->shadow->status = URB_STATUS_UNLINKED;
			destroy_urb(host, urb->shadow);
			urb->shadow = NULL;
			continue;
		}
		
		/* activate the shadow in host's frame list */
		uhci_activate_urb(host, urb->shadow);
		link_urb(&host->inproc_urbs, urb->shadow);
	}

	return n_shadowed;
}

/**
 * @brief sweep out the unmarked urbs  
 * @param host struct uhci_host
 * @param urblist struct usb_request_block  
 */
static inline int
sweep_unmarked_urbs(struct uhci_host *host, 
			struct usb_request_block *urblist)
{
 	struct usb_request_block *nexturb, *urb = urblist;
	int n_sweeped = 0;

 	while (urb) {
 		nexturb = urb->next;
		if (!is_skelton(urb) && !urb->mark) {
			dprintft(2, "%04x: %s: a urb(%p:%llx) "
				 "that need be removed found.\n", 
				host->iobase, __FUNCTION__, urb,
				URB_UHCI(urb)->qh_phys);
			remove_and_deactivate_urb(host, urb);
			n_sweeped++;
		}
		urb = nexturb;
	}

	return n_sweeped;
}

static inline int
update_iso_urb(struct uhci_host *host,
		   u16 frame_number, phys32_t link_phys)
{
	struct uhci_td_meta *tdm;

	tdm = host->iso_urbs[frame_number];

	/* no update */
	if ((link_phys & UHCI_FRAME_LINK_QH) && !tdm)
		return 0;

	if (!tdm) {
		struct uhci_td *td;

		/* insert */
		dprintft(2, "%04x: an isochronous TD inserted "
			 "in no.%04d frame\n", host->iobase, frame_number);

		td = uhci_gettdbypaddr(host, link_phys, 0);
		tdm = uhci_new_td_meta(host, NULL);
		tdm->td->link = host->hframelist_virt[frame_number];
		tdm->td->status = td->status;
		tdm->td->token = td->token;
		tdm->td->buffer = td->buffer;
		tdm->shadow_td = td;

		host->hframelist_virt[frame_number] = tdm->td_phys;
		host->iso_urbs[frame_number] = tdm;
	} else if (link_phys & UHCI_FRAME_LINK_QH) {
		/* delete */
		dprintft(2, "%04x: the isochronous "
			 "in no.%04d frame TD deleted\n",
			 host->iobase, frame_number);

		host->hframelist_virt[frame_number] = tdm->td->link;

		free(tdm->td);
		unmapmem(tdm->shadow_td, sizeof(struct uhci_td));
		free(tdm);

		host->iso_urbs[frame_number] = NULL;
	} else {
		/* copyback */
		tdm->td->status = tdm->shadow_td->status;
	}

	return 1;
}

static inline int
check_need_for_monitor_boost(struct uhci_host *host, u16 interval)
{
	dprintft(4, "%04x: %s: frame intervals %d msecs.\n", 
		 host->iobase, __FUNCTION__, interval);

	if (interval <= (1 << UHCI_TICK_INTERVAL))
		return 0;

	/* uhci_monitor_boost_hc is protected by 'host->lock_gfl' */
	/* is there a monitor boost already? */
	if (uhci_monitor_boost_hc != 0)
		return 0;

	return 1;
}

static inline int
unregister_monitor_boost(struct uhci_host *host)
{
	/* uhci_monitor_boost_hc is protected by 'host->lock_gfl' */
	/* check which host registered monitor boost */
	if (uhci_monitor_boost_hc != host->iobase)
		return 1;

	uhci_monitor_boost_hc = 0;
	
	/* unset IOC bit */
	URB_UHCI(host->host_skelton[UHCI_TICK_INTERVAL])
		->tdm_head->td->status = 0;

	return 0;
}

static inline int
register_monitor_boost(struct uhci_host *host)
{
	struct usb_request_block *skelurb;

	skelurb = host->host_skelton[UHCI_TICK_INTERVAL];

	/* sometime TD for skelton already might be allocated */
	if (!URB_UHCI(skelurb)->tdm_head) {

		/* create new td meta structure*/
		URB_UHCI(skelurb)->tdm_head = uhci_new_td_meta(host, NULL);
		if (!URB_UHCI(skelurb)->tdm_head)
			return -1;

		/* initialize TD */
		URB_UHCI(skelurb)->tdm_head->td->link = UHCI_TD_LINK_TE;
		URB_UHCI(skelurb)->tdm_head->td->token = 
			UHCI_TD_TOKEN_DEVADDRESS(0x7f)| 
			UHCI_TD_TOKEN_ENDPOINT(0) | 
			UHCI_TD_TOKEN_PID_IN | uhci_td_explen(0);
		URB_UHCI(skelurb)->tdm_head->td->buffer = 0U;
		URB_UHCI(skelurb)->qh->element = (phys32_t)
			URB_UHCI(skelurb)->tdm_head->td_phys;
	}

	/* set IOC bits */
	URB_UHCI(skelurb)->tdm_head->td->status = UHCI_TD_STAT_IC;

	/* uhci_monitor_boost_hc is protected by 'host->lock_gfl' */
	uhci_monitor_boost_hc = host->iobase;

	dprintft(1, "%04x: framelist monitor boost activated"
		 " (threshold: %d ms).\n",
		 host->iobase, 1 << UHCI_TICK_INTERVAL);

	return 0;
}

/**
* @brief frame list monitor
* @param data void*
*/
	
void
uhci_framelist_monitor(void *data)
{
	struct uhci_host *host = data;
	phys32_t link_phys;
	u32 mask;
	int n, i, cur_frnum, intvl;

	for (;;) {

		if (!host->running)
			goto skip_a_turn;

		/* look for any updates in guest's framelist */
		spinlock_lock(&host->lock_gfl);

		unmark_all_urbs(host->guest_urbs);
		
		/* get current frame number */
		cur_frnum = uhci_current_frame_number(host);
		
		/* check need for monitor boost */
		if (check_need_for_monitor_boost(host, 
			(cur_frnum + UHCI_NUM_FRAMES - host->frame_number)
				& (UHCI_NUM_FRAMES - 1)))
			register_monitor_boost(host);

		/* select which skeltons should be scaned 
		   by current frame number progress */
		intvl = UHCI_NUM_INTERVALS - 1;
	        while (host->frame_number != cur_frnum) {
			host->frame_number = 
				(host->frame_number + 1) & 
				(UHCI_NUM_FRAMES - 1);
			for (i = 0, mask = UHCI_NUM_FRAMES - 1; 
			     mask; mask = mask >> 1, i++) {
				if (!(host->frame_number & mask)) {
					intvl = (intvl > i) ? i : intvl;
					break;
				}
			}

			/* check a frame list slot for isochronous TD */
			link_phys = host->
				gframelist_virt[host->frame_number];
			update_iso_urb(host, 
					   host->frame_number, link_phys);
		}

		/* scan skeltons and the following urbs */
		mark_inlinked_urbs(host, host->guest_urbs, 
				       host->guest_skeltons, intvl);
		/* update urb content link (QH element) if needed */
		update_marked_urbs(host, host->guest_skeltons[intvl]);
		/* make copies of urb and activate it if needed */
		shadow_marked_urbs(host, host->guest_skeltons[intvl]);
		/* deactivate and delete pairs of urb if needed */
		sweep_unmarked_urbs(host, host->guest_skeltons[intvl]);

		spinlock_unlock(&host->lock_gfl);

		/* look for any advance in host's framelist */
		if ((n = uhci_check_advance(host->hc)) > 0)
			dprintft(3, "%04x: %s: "
				 "%d urb(s) advanced.\n", 
				 host->iobase, __FUNCTION__, n);
	skip_a_turn:
		schedule();
	}

	return;
}

/** 
 * @brief scan through a single frame
 * @param host struct uhci_host 
 * @param g_frame phys32_t
 * @param skeltons struct usb_request_block 
*/
static inline int 
scan_a_frame(struct uhci_host *host,
	     phys32_t link_phys, struct usb_request_block **skeltons) 
{

	int lv, skel_n = 0;
	struct usb_request_block *g_urb;
	struct uhci_td *td;
	phys32_t g_qh_phys;

	/* skip isochronous TDs */
	g_qh_phys = skip_isochronous(host, link_phys);

	/* asynchronous QHs */
	for (lv = 1; !is_terminate(g_qh_phys) && (g_qh_phys != 0U); lv++) {

		g_urb = geturbbyqh_phys(*skeltons, g_qh_phys);

		if (g_urb) {
			/* a corresponding shadow already exists */
			URB_UHCI(g_urb)->refcount++;
			dprintf(4, "-><%p>", g_urb);
		} else {
			g_urb = create_urb(host);
			if (!g_urb)
				break;
			URB_UHCI(g_urb)->qh_phys = uhci_link(g_qh_phys);
			URB_UHCI(g_urb)->qh = uhci_getqhbypaddr(host, 
							       URB_UHCI(g_urb)
							       ->qh_phys, 0);
			if (!URB_UHCI(g_urb)->qh)
				break;
			URB_UHCI(g_urb)->qh_element_copy = 
				URB_UHCI(g_urb)->qh->element;
			if (!is_terminate(URB_UHCI(g_urb)->qh->element)) {
				td = uhci_gettdbypaddr(host, 
						       URB_UHCI(g_urb)
						       ->qh->element, 0);
				if (!td)
					break;

				URB_UHCI(g_urb)->td_stat = td->status;
				URB_UHCI(g_urb)->tdm_head = 
					uhci_new_td_meta(host, td);
				if (!URB_UHCI(g_urb)->tdm_head)
					break;
			} else {
				URB_UHCI(g_urb)->td_stat = 0;
			}
			g_urb->address = URB_ADDRESS_SKELTON;
			dprintf(4, "->(%p)", g_urb);

			append_urb(skeltons, g_urb);

			skel_n++;
		}

		/* tail loop detection */
		if (g_qh_phys == uhci_link(URB_UHCI(g_urb)->qh->link)) {
			dprintft(4, "%04x: %s: a tail loop detected.\n",
				 host->iobase, __FUNCTION__);
			break;
		}

		/* next */
		g_qh_phys = uhci_link(URB_UHCI(g_urb)->qh->link);
	}

	return skel_n;
}

/**
 * @brief sort skeltons by descending order of refcount
 * @param skeltons structure usb_request_block
 * @param index structure array of usb_request_block
 * @param maxorder int maximum order
 * @return Sorted urb
 */
static struct usb_request_block *
sort_skeltons(struct usb_request_block *skeltons, 
	      struct usb_request_block *index[], int maxorder)
{
	struct usb_request_block *urb, *tailurb, *nexturb, *list;
	u16 upper, lower;
	int order, i;

	list = tailurb = (struct usb_request_block *)NULL;
	for (order = 0; order < maxorder; order++) {
		urb = skeltons;
		while (urb) {
			upper = 1 << order;
			lower = upper >> 1;
			nexturb = urb->next;
			if ((upper >= (URB_UHCI(urb)->refcount + 1)) &&
			    (lower < (URB_UHCI(urb)->refcount + 1))) {
				if (urb->prev)
					urb->prev->next = urb->next;
				if (urb->next)
					urb->next->prev = urb->prev;
				if (skeltons == urb)
					skeltons = urb->next;
				if (!index[order])
					index[order] = urb;
				if (tailurb)
					tailurb->next = urb;
				urb->prev = tailurb;
				tailurb = urb; /* for the next */
			}
			urb = nexturb;
		}

		if (!list && index[order]) {
			list = index[order];
			for (i = order - 1; i >= 0 ; i--)
				index[i] = list;
		}
	}

	return list;
}

/**
 * @brief scan the guest OS frame list
 * @param host corrsponding uhci_host
 * @return skeleton numbers 
 */
int
scan_gframelist(struct uhci_host *host)
{
	struct usb_request_block *skeltons;
	int frid;
	int skel_n = 0;

	skeltons = (struct usb_request_block *)NULL;
	spinlock_lock(&host->lock_gfl);

	host->gframelist_virt = 
		mapmem_gphys(host->gframelist, PAGESIZE, 0);
	for (frid = 0; frid < UHCI_NUM_FRAMES; frid++) {
			dprintft(4, "%04x: %s: FRAME[%04d]:", 
			host->iobase, __FUNCTION__, frid);
		skel_n += scan_a_frame(host, 
				       host->gframelist_virt[frid], 
				       &skeltons);
		dprintf(4, ".\n");
	}

	host->guest_urbs =
		sort_skeltons(skeltons, 
			      host->guest_skeltons, UHCI_NUM_INTERVALS);

	spinlock_unlock(&host->lock_gfl);

	dprintft(2, "%04x: %s: %d skeltons registered.\n",
		 host->iobase, __FUNCTION__, skel_n);

	return skel_n;
}


/*-
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_ether.c	8.1 (Berkeley) 6/10/93
 */

#include <rte_mbuf.h>
#include <rte_byteorder.h>
#include <rte_ether.h>

#include "if_arp.h"
#include "netisr.h"

#define RTE_LOGTYPE_ETHER RTE_LOGTYPE_USER1



/*
 * Common length and type checks are done here,
 * then the protocol-specific routine is called.
 */
static void
arpintr(struct rte_mbuf *m)
{
	struct arphdr *ar;
	struct ifnet *ifp;
	char *layer;
	int hlen;


	ar = rte_pktmbuf_mtod(m, struct arphdr *);

	hlen = 0;
	layer = "";
	switch (rte_be_to_cpu_16(ar->ar_hrd)) {
	case ARPHRD_ETHER:
		hlen = ETHER_ADDR_LEN; /* RFC 826 */
		layer = "ethernet";
		break;
	case ARPHRD_IEEE802:
		hlen = 6; /* RFC 1390, FDDI_ADDR_LEN */
		layer = "fddi";
		break;
	case ARPHRD_ARCNET:
		hlen = 1; /* RFC 1201, ARC_ADDR_LEN */
		layer = "arcnet";
		break;
	case ARPHRD_INFINIBAND:
		hlen = 20;	/* RFC 4391, INFINIBAND_ALEN */ 
		layer = "infiniband";
		break;
	case ARPHRD_IEEE1394:
		hlen = 0; /* SHALL be 16 */ /* RFC 2734 */
		layer = "firewire";

		/*
		 * Restrict too long hardware addresses.
		 * Currently we are capable of handling 20-byte
		 * addresses ( sizeof(lle->ll_addr) )
		 */
		if (ar->ar_hln >= 20)
			hlen = 16;
		break;
	default:
		RTE_LOG(NOTICE, ETHER,
		    "packet with unknown hardware format 0x%02d received on "
		    "%s\n", ntohs(ar->ar_hrd), if_name(ifp));
		m_freem(m);
		return;
	}

	if (hlen != 0 && hlen != ar->ar_hln) {
		RTE_LOG(NOTICE, ETHER,
		    "packet with invalid %s address length %d received on %s\n",
		    layer, ar->ar_hln, if_name(ifp));
		m_freem(m);
		return;
	}

	switch (ntohs(ar->ar_pro)) {
	case ETHER_TYPE_IPv4:
		in_arpinput(m);
		return;
	}
	m_freem(m);
}

static const struct netisr_handler arp_nh = {
	.nh_name = "arp",
	.nh_handler = arpintr,
	.nh_proto = NETISR_ARP,
	.nh_policy = NETISR_POLICY_SOURCE,
};

vnet_arp_init(void)
{

	netisr_register(&arp_nh);
}

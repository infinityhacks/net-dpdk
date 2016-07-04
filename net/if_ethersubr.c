/*-
 * Copyright (c) 1982, 1989, 1993
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
 *	@(#)if_ethersubr.c	8.1 (Berkeley) 6/10/93
 *	@(#)if_ethersubr.c	8.1 (jlijian3@gmail.com) 1/7/16
 * $FreeBSD$
 */

#include <rte_mbuf.h>
#include <rte_ether.h>

#include "if.h"
#include "if_var.h"
#include "netisr.h"

#define RTE_LOGTYPE_NET RTE_LOGTYPE_USER1

static void
ether_input(struct ifnet *ifp, struct rte_mbuf *m)
{

	struct rte_mbuf *mn;

	/*
	 * The drivers are allowed to pass in a chain of packets linked with
	 * next. We split them up into separate packets here and pass
	 * them up. This allows the drivers to amortize the receive lock.
	 */
	while (m) {
		mn = m->next;
		m->next = NULL;

		netisr_dispatch(NETISR_ETHER, m);
		m = mn;
	}
}

/*
 * Upper layer processing for a received Ethernet packet.
 */
void
ether_demux(struct ifnet *ifp, struct rte_mbuf *m)
{
	struct ether_hdr *eh;
	int i, isr;
	u_short ether_type;

	//KASSERT(ifp != NULL, ("%s: NULL interface pointer", __func__));

	eh = rte_pktmbuf_mtod(m, struct ether_hdr *);
	ether_type = rte_be_to_cpu_16(eh->ether_type);

	rte_pktmbuf_adj(m, ETHER_HDR_LEN);

	/*
	 * Dispatch frame to upper layer.
	 */
	switch (ether_type) {
	case ETHER_TYPE_IPv4:
		isr = NETISR_IP;
		break;

	case ETHER_TYPE_ARP:
		isr = NETISR_ARP;
		break;
#ifdef INET6
	case ETHER_TYPE_IPv6:
		isr = NETISR_IPV6;
		break;
#endif
	default:
		goto discard;
	}
	netisr_dispatch(isr, m);
	return;

discard:
	/*
	 * Packet is to be discarded.  If netgraph is present,
	 * hand the packet to it for last chance processing;
	 * otherwise dispose of it.
	 */
	rte_pktmbuf_free(m);
}

/*
 * Process a received Ethernet packet; the packet is in the
 * mbuf chain m with the ethernet header at the front.
 */
static void
ether_input_internal(struct ifnet *ifp, struct rte_mbuf *m)
{
	struct ether_header *eh;
	u_short etype;

	if (m->data_len < ETHER_HDR_LEN) {
		/* XXX maybe should pullup? */
		RTE_LOG(ERR, NET, "discard frame w/o leading ethernet "
				"header (len %u pkt len %u)\n",
				m->data_len, m->l2_len);
		rte_pktmbuf_free(m);
		return;
	}

	ether_demux(ifp, m);
}

static void
ether_nh_input(struct rte_mbuf *m)
{

	M_ASSERTPKTHDR(m);
	ether_input_internal(NULL, m);
}

static struct netisr_handler	ether_nh = {
	.nh_name = "ether",
	.nh_handler = ether_nh_input,
	.nh_proto = NETISR_ETHER,
#ifdef RSS
	.nh_policy = NETISR_POLICY_CPU,
	.nh_dispatch = NETISR_DISPATCH_DIRECT,
	.nh_m2cpuid = rss_m2cpuid,
#else
	.nh_policy = NETISR_POLICY_SOURCE,
	.nh_dispatch = NETISR_DISPATCH_DIRECT,
#endif
};

static void
ether_init(void *arg)
{
	netisr_register(&ether_nh);
}

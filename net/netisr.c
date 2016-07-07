/*-
 * Copyright (c) 2007-2009 Robert N. M. Watson
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract
 * to Juniper Networks, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @{#)netisr.c 	port to dpdk (jlijian3@gmail.com) 4/7/16
 * 
 */

#include <sys/cdefs.h>

/*
 * netisr is a packet dispatch service, allowing synchronous (directly
 * dispatched) and asynchronous (deferred dispatch) processing of packets by
 * registered protocol handlers.  Callers pass a protocol identifier and
 * packet to netisr, along with a direct dispatch hint, and work will either
 * be immediately processed by the registered handler, or passed to a
 * software interrupt (SWI) thread for deferred dispatch.  Callers will
 * generally select one or the other based on:
 *
 * - Whether directly dispatching a netisr handler lead to code reentrance or
 *   lock recursion, such as entering the socket code from the socket code.
 * - Whether directly dispatching a netisr handler lead to recursive
 *   processing, such as when decapsulating several wrapped layers of tunnel
 *   information (IPSEC within IPSEC within ...).
 *
 * Maintaining ordering for protocol streams is a critical design concern.
 * Enforcing ordering limits the opportunity for concurrency, but maintains
 * the strong ordering requirements found in some protocols, such as TCP.  Of
 * related concern is CPU affinity--it is desirable to process all data
 * associated with a particular stream on the same CPU over time in order to
 * avoid acquiring locks associated with the connection on different CPUs,
 * keep connection data in one cache, and to generally encourage associated
 * user threads to live on the same CPU as the stream.  It's also desirable
 * to avoid lock migration and contention where locks are associated with
 * more than one flow.
 *
 * netisr supports several policy variations, represented by the
 * NETISR_POLICY_* constants, allowing protocols to play various roles in
 * identifying flows, assigning work to CPUs, etc.  These are described in
 * netisr.h.
 */

#include <stdint.h>
#include <rte_mbuf.h>

#define	_WANT_NETISR_INTERNAL	/* Enable definitions from netisr_internal.h */
#include "if_var.h"
#include "netisr.h"
#include "netisr_internal.h"

/*
 * The netisr_proto array describes all registered protocols, indexed by
 * protocol number.  See netisr_internal.h for more details.
 */
static struct netisr_proto	netisr_proto[NETISR_MAXPROT];

#define	NETISR_DEFAULT_MAXQLIMIT	10240
static u_int	netisr_maxqlimit = NETISR_DEFAULT_MAXQLIMIT;

#define	NETISR_DEFAULT_DEFAULTQLIMIT	256
static u_int	netisr_defaultqlimit = NETISR_DEFAULT_DEFAULTQLIMIT;

/*
 * Dispatch a packet for netisr processing; direct dispatch is permitted by
 * calling context.
 */
int
netisr_dispatch_src(u_int proto, uintptr_t source, struct rte_mbuf *m)
{
	struct netisr_workstream *nwsp;
	struct netisr_proto *npp;
	struct netisr_work *npwp;
	int dosignal, error;
	u_int cpuid, dispatch_policy;

	KASSERT(proto < NETISR_MAXPROT,
	    ("%s: invalid proto %u", __func__, proto));

	npp = &netisr_proto[proto];
	KASSERT(npp->np_handler != NULL, ("%s: invalid proto %u", __func__,
	    proto));

	npp->np_handler(m);

	return (error);
}

int
netisr_dispatch(u_int proto, struct rte_mbuf *m)
{

	return (netisr_dispatch_src(proto, 0, m));
}

/*
 * Register a new netisr handler, which requires initializing per-protocol
 * fields for each workstream.  All netisr work is briefly suspended while
 * the protocol is installed.
 */
void
netisr_register(const struct netisr_handler *nhp)
{
	const char *name;
	u_int i, proto;

	proto = nhp->nh_proto;
	name = nhp->nh_name;

	/*
	 * Test that the requested registration is valid.
	 */
	KASSERT(nhp->nh_name != NULL,
	    ("%s: nh_name NULL for %u", __func__, proto));
	KASSERT(nhp->nh_handler != NULL,
	    ("%s: nh_handler NULL for %s", __func__, name));
	KASSERT(nhp->nh_policy == NETISR_POLICY_SOURCE ||
	    nhp->nh_policy == NETISR_POLICY_FLOW ||
	    nhp->nh_policy == NETISR_POLICY_CPU,
	    ("%s: unsupported nh_policy %u for %s", __func__,
	    nhp->nh_policy, name));
	KASSERT(nhp->nh_policy == NETISR_POLICY_FLOW ||
	    nhp->nh_m2flow == NULL,
	    ("%s: nh_policy != FLOW but m2flow defined for %s", __func__,
	    name));
	KASSERT(nhp->nh_policy == NETISR_POLICY_CPU || nhp->nh_m2cpuid == NULL,
	    ("%s: nh_policy != CPU but m2cpuid defined for %s", __func__,
	    name));
	KASSERT(nhp->nh_policy != NETISR_POLICY_CPU || nhp->nh_m2cpuid != NULL,
	    ("%s: nh_policy == CPU but m2cpuid not defined for %s", __func__,
	    name));
	KASSERT(nhp->nh_dispatch == NETISR_DISPATCH_DEFAULT ||
	    nhp->nh_dispatch == NETISR_DISPATCH_DEFERRED ||
	    nhp->nh_dispatch == NETISR_DISPATCH_HYBRID ||
	    nhp->nh_dispatch == NETISR_DISPATCH_DIRECT,
	    ("%s: invalid nh_dispatch (%u)", __func__, nhp->nh_dispatch));

	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u, %s): protocol too big", __func__, proto, name));

	/*
	 * Test that no existing registration exists for this protocol.
	 */
	NETISR_WLOCK();
	KASSERT(netisr_proto[proto].np_name == NULL,
	    ("%s(%u, %s): name present", __func__, proto, name));
	KASSERT(netisr_proto[proto].np_handler == NULL,
	    ("%s(%u, %s): handler present", __func__, proto, name));

	netisr_proto[proto].np_name = name;
	netisr_proto[proto].np_handler = nhp->nh_handler;
	netisr_proto[proto].np_m2flow = nhp->nh_m2flow;
	netisr_proto[proto].np_m2cpuid = nhp->nh_m2cpuid;
	netisr_proto[proto].np_drainedcpu = nhp->nh_drainedcpu;

	if (nhp->nh_qlimit == 0)
		netisr_proto[proto].np_qlimit = netisr_defaultqlimit;
	else if (nhp->nh_qlimit > netisr_maxqlimit) {
		printf("%s: %s requested queue limit %u capped to "
		    "net.isr.maxqlimit %u\n", __func__, name, nhp->nh_qlimit,
		    netisr_maxqlimit);
		netisr_proto[proto].np_qlimit = netisr_maxqlimit;
	} else
		netisr_proto[proto].np_qlimit = nhp->nh_qlimit;
	netisr_proto[proto].np_policy = nhp->nh_policy;
	netisr_proto[proto].np_dispatch = nhp->nh_dispatch;
}

/*
 * Remove the registration of a network protocol, which requires clearing
 * per-protocol fields across all workstreams, including freeing all mbufs in
 * the queues at time of unregister.  All work in netisr is briefly suspended
 * while this takes place.
 */
void
netisr_unregister(const struct netisr_handler *nhp)
{
	const char *name;
	u_int i, proto;

	proto = nhp->nh_proto;
	name = nhp->nh_name;

	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u): protocol too big for %s", __func__, proto, name));

	NETISR_WLOCK();
	KASSERT(netisr_proto[proto].np_handler != NULL,
	    ("%s(%u): protocol not registered for %s", __func__, proto,
	    name));


	netisr_proto[proto].np_name = NULL;
	netisr_proto[proto].np_handler = NULL;
	netisr_proto[proto].np_m2flow = NULL;
	netisr_proto[proto].np_m2cpuid = NULL;
	netisr_proto[proto].np_qlimit = 0;
	netisr_proto[proto].np_policy = 0;
}

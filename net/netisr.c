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

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

static void
ether_input(struct ifnet *ifp, struct mbuf *m)
{

	struct mbuf *mn;

	/*
	 * The drivers are allowed to pass in a chain of packets linked with
	 * m_nextpkt. We split them up into separate packets here and pass
	 * them up. This allows the drivers to amortize the receive lock.
	 */
	while (m) {
		mn = m->m_nextpkt;
		m->m_nextpkt = NULL;

		/*
		 * We will rely on rcvif being set properly in the deferred context,
		 * so assert it is correct here.
		 */
		KASSERT(m->m_pkthdr.rcvif == ifp, ("%s: ifnet mismatch m %p "
		    "rcvif %p ifp %p", __func__, m, m->m_pkthdr.rcvif, ifp));
		CURVNET_SET_QUIET(ifp->if_vnet);
		netisr_dispatch(NETISR_ETHER, m);
		CURVNET_RESTORE();
		m = mn;
	}
}

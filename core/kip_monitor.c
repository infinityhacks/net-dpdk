#include <sys/socket.h>

#include "libnetlink.h"
#include "utils.h"

struct rtnl_handle rth = { .fd = -1 };

int kip_monitor_init(void)
{
	int ret;
	unsigned int groups = 0;

	groups |= nl_mgrp(RTNLGRP_LINK);
	groups |= nl_mgrp(RTNLGRP_IPV4_IFADDR);
	groups |= nl_mgrp(RTNLGRP_IPV6_IFADDR);
	groups |= nl_mgrp(RTNLGRP_IPV4_ROUTE);
	groups |= nl_mgrp(RTNLGRP_IPV6_ROUTE);
	//groups |= nl_mgrp(RTNLGRP_MPLS_ROUTE);
	groups |= nl_mgrp(RTNLGRP_IPV4_MROUTE);
	groups |= nl_mgrp(RTNLGRP_IPV6_MROUTE);
	groups |= nl_mgrp(RTNLGRP_IPV6_PREFIX);
	groups |= nl_mgrp(RTNLGRP_NEIGH);
	//groups |= nl_mgrp(RTNLGRP_IPV4_NETCONF);
	//groups |= nl_mgrp(RTNLGRP_IPV6_NETCONF);
	groups |= nl_mgrp(RTNLGRP_IPV4_RULE);
	groups |= nl_mgrp(RTNLGRP_IPV6_RULE);
	//groups |= nl_mgrp(RTNLGRP_NSID);

	if ((ret = rtnl_open(&rth, groups)) < 0) {
		return ret;
	}
}

int kip_monitor_poll(void)
{

}

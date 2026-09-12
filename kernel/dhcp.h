#ifndef AXIOME_DHCP_H
#define AXIOME_DHCP_H

#include <stdint.h>
#include "netdev.h"

/* Start the DHCP client for `dev`, asynchronously in a kernel thread.  On
   success this configures dev->ip / netmask / gateway / dns_server.  Called
   by NIC drivers after the interface comes up (see e1000 module). */
int dhcp_start(struct netdev *dev);

#endif
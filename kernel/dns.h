#ifndef AXIOME_DNS_H
#define AXIOME_DNS_H

#include <stdint.h>
#include "netdev.h"
#include "net_util.h"

/* Resolve `name` to an IPv4 address using `dev`'s configured DNS server.
   Dotted-quad literals are parsed without touching the network.  Returns 0
   and stores the address in `ip_out` (network byte order) on success. */
int dns_resolve(struct netdev *dev, const char *name, ip4_addr_t *ip_out);

#endif
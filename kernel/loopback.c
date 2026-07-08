#include "loopback.h"
#include "netdev.h"
#include "net_buf.h"
#include "string.h"
#include "printk.h"

#define LOOPBACK_MTU 65535

static int loopback_tx(struct netdev *dev, struct mbuf *m)
{
    (void)dev;
    /* Loop back: feed the packet straight into the RX path. */
    netdev_rx_poll(dev, m);
    return 0;
}

void loopback_init(void)
{
    static struct netdev lo_dev;
    memset(&lo_dev, 0, sizeof(lo_dev));

    strcpy(lo_dev.name, "lo");
    lo_dev.mac[0] = 0x00;
    lo_dev.mtu = LOOPBACK_MTU;
    lo_dev.ip = ip4_make(127, 0, 0, 1);
    lo_dev.netmask = ip4_make(255, 0, 0, 0);
    lo_dev.gateway = 0;
    lo_dev.tx = loopback_tx;

    netdev_register(&lo_dev);
    printk("NET: loopback initialised (127.0.0.1)\n");
}

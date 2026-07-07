#ifndef AXIOME_DRIVER_H
#define AXIOME_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include "pci.h"

#define DRV_ANY 0xFFFFu

enum { DEV_CHAR = 0, DEV_BLOCK = 1 };

struct device;
struct driver;
struct pci_device;

/* Device I/O operations (used by the /dev filesystem). */
struct dev_ops {
    long (*read)(struct device *dev, uint64_t off, void *buf, size_t len);
    long (*write)(struct device *dev, uint64_t off, const void *buf, size_t len);
};

struct device {
    char name[32];
    uint32_t major;
    uint32_t minor;
    int type;            /* DEV_CHAR / DEV_BLOCK */
    struct driver *drv;
    void *priv;
    struct dev_ops ops;
    struct device *next;
};

struct driver {
    const char *name;
    uint16_t vendor;     /* DRV_ANY to ignore */
    uint16_t device;     /* DRV_ANY to ignore */
    uint8_t  pci_class;  /* DRV_ANY to ignore */
    uint8_t  pci_subclass;
    int (*probe)(struct pci_device *pdev);
    struct driver *next;
};

/* Driver registry. */
void driver_register(struct driver *d);
int  driver_probe_pci(struct pci_device *pdev);
void driver_probe_all(void);
void driver_rescan(void);          /* 12.9 hotplug re-scan hook */

/* Device registry (the source of /dev nodes). */
void device_register(struct device *d);
struct device *device_find(const char *name);
int  device_enumerate(struct device **out, int max);

/* Built-in drivers + framework bootstrap. */
void driver_init(void);

#endif

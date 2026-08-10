#ifndef AXIOME_XHCI_H
#define AXIOME_XHCI_H

struct pci_device;

int xhci_probe(struct pci_device *pdev);
void xhci_poll(void);

#endif

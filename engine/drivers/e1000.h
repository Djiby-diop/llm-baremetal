#ifndef OO_DRIVERS_E1000_H
#define OO_DRIVERS_E1000_H

#include "pci.h"

void oo_e1000_init(OoPciDevice *dev);
int oo_e1000_status(void);

#endif

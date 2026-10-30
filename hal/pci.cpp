// PCI: how the kernel finds devices on the motherboard. Each device has a small
// block of settings, which we read through two I/O ports.

#include "hal.h"

#define PCI_ADDRESS_PORT 0xCF8
#define PCI_DATA_PORT 0xCFC
#define PCI_ENABLE 0x80000000u

#define PCI_BUSES 256
#define PCI_DEVICES 32
#define PCI_FUNCTIONS 8

#define PCI_CLASS_REGISTER 0x08
#define NO_DEVICE 0xFFFFFFFFu

static uint32_t make_address(const PciLocation *where, uint8_t offset)
{
    return PCI_ENABLE | ((uint32_t)where->bus << 16) | ((uint32_t)where->device << 11) |
           ((uint32_t)where->function << 8) | (offset & 0xFC);
}

uint32_t pci_read(const PciLocation *where, uint8_t offset)
{
    outl(PCI_ADDRESS_PORT, make_address(where, offset));
    return inl(PCI_DATA_PORT);
}

void pci_write(const PciLocation *where, uint8_t offset, uint32_t value)
{
    outl(PCI_ADDRESS_PORT, make_address(where, offset));
    outl(PCI_DATA_PORT, value);
}

// Looks at every possible device until one has this class and subclass.
bool pci_find_device(uint8_t class_code, uint8_t subclass, PciLocation *result)
{
    for (uint32_t bus = 0; bus < PCI_BUSES; bus++) {
        for (uint32_t device = 0; device < PCI_DEVICES; device++) {
            for (uint32_t function = 0; function < PCI_FUNCTIONS; function++) {
                PciLocation where = { (uint8_t)bus, (uint8_t)device, (uint8_t)function };
                uint32_t value = pci_read(&where, PCI_CLASS_REGISTER);

                if (value == NO_DEVICE)
                    continue;

                if ((value >> 24) == class_code && ((value >> 16) & 0xFF) == subclass) {
                    *result = where;
                    return true;
                }
            }
        }
    }
    return false;
}

#ifndef SATZ_SDK_H
#define SATZ_SDK_H
#include "satz.h"
#endif
unsigned long satz_bus_address(const volatile void* pointer);
unsigned int satz_bus_read(unsigned long address, int size);
#define SATZ_PORT_READ32(pointer) (satz_bus_read(satz_bus_address(pointer), 4))
#define SATZ_PORT_WRITE32(pointer, value)                                         \
    do {                                                                          \
        unsigned int satz_value_ = (value);                                       \
        satz_bus_store((unsigned int)satz_bus_address(pointer), &satz_value_, 4); \
    } while (0)

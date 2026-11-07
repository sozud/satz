#include "sega_xpt.h"

void sprMemCpyL(void* dst, void* src, int count)
{
    if (count > 0)
        satz_bus_write16s(satz_bus_address(dst), src, (unsigned long)count * 2);
}

void sprMemCpyW(void* dst, void* src, int count)
{
    if (count > 0)
        satz_bus_write16s(satz_bus_address(dst), src, (unsigned long)count);
}

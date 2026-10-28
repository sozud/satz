#include "sega_xpt.h"

void sprMemCpyL(void* dst, void* src, int count)
{
    if (count > 0)
        satz_bus_store((unsigned int)satz_bus_address(dst), src, (unsigned long)count * 4);
}

void sprMemCpyW(void* dst, void* src, int count)
{
    if (count > 0)
        satz_bus_store((unsigned int)satz_bus_address(dst), src, (unsigned long)count * 2);
}

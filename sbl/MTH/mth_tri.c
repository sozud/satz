#include "sega_mth.h"
#include "mth_tri_table.h"

Fixed32 fsin(Fixed32 a)
{
    Uint32 degree = ((Uint32)a >> 16) & 0xFFFF;
    Uint32 grad = mth_sin_table[degree][0], value = mth_sin_table[degree][1];
    return (Fixed32)(value + (((grad * ((Uint32)a & 0xFFFF)) >> 16) & 0xFFFF));
}

Fixed32 fcos(Fixed32 a) { return fsin(0x005A0000 - a); }

Fixed32 MTH_Sin(Fixed32 degree)
{
    int negative = degree < 0;
    if (negative)
        degree = -degree;
    if (degree >= 0x00B40000)
        degree = 0;
    else if (degree > 0x005A0000)
        degree = 0x00B40000 - degree;
    Fixed32 r = fsin(degree);
    return negative ? -r : r;
}

Fixed32 MTH_Cos(Fixed32 degree)
{
    int negative = 0;
    if (degree < 0)
        degree = -degree;
    if (degree >= 0x00B40000) {
        negative = 1;
        degree = 0;
    } else if (degree > 0x005A0000) {
        negative = 1;
        degree = 0x00B40000 - degree;
    }
    Fixed32 r = fcos(degree);
    return negative ? -r : r;
}

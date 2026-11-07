#include "sega_mth.h"

static Sint32 divu_s64(long long dividend, Sint32 divisor)
{
    int neg = ((Sint32)(dividend >> 32) ^ divisor) < 0;
    if (!divisor)
        return neg ? (Sint32)0x80000000 : 0x7FFFFFFF;
    long long q = dividend / divisor;
    if (q < -0x7FFFFFFFLL || q > 0x7FFFFFFFLL)
        return neg ? (Sint32)0x80000000 : 0x7FFFFFFF;
    return (Sint32)q;
}

static long long dmuls(Fixed32 a, Fixed32 b) { return (long long)a * b; }

void MTH_Pers2D(MthXyz* p3d, MthXy* unitPixel, XyInt* p2d)
{
    Fixed32 hz = divu_s64(1LL << 32, p3d->z);
    Fixed32 wx = (Fixed32)(Uint32)((unsigned long long)dmuls(p3d->x, unitPixel->x) >> 16);
    Fixed32 wy = (Fixed32)(Uint32)((unsigned long long)dmuls(p3d->y, unitPixel->y) >> 16);
    p2d->x = (Sint16)(Uint16)((unsigned long long)dmuls(hz, wx) >> 32);
    p2d->y = (Sint16)(Uint16)((unsigned long long)dmuls(hz, wy) >> 32);
}

Sint32 MTH_ComputeBright(MthXyz* lightVector, MthXyz* normalVector)
{
    unsigned long long acc = (unsigned long long)dmuls(lightVector->x, normalVector->x)
        + (unsigned long long)dmuls(lightVector->y, normalVector->y)
        + (unsigned long long)dmuls(lightVector->z, normalVector->z);
    Uint32 product = (Uint32)((acc + (1ULL << 32)) >> 16);
    return (Sint32)(((product * 16u) >> 16) & 0x1F);
}

#include "sega_mth.h"

Fixed32 MTH_Mul(Fixed32 a, Fixed32 b) { return (Fixed32)(Uint32)((unsigned long long)((long long)a * b) >> 16); }

Fixed32 MTH_Div(Fixed32 a, Fixed32 b) { return b ? (Fixed32)(((long long)a << 16) / b) : 0; }

Fixed32 MTH_IntToFixed(Sint32 a) { return (Fixed32)((Uint32)a << 16); }

Sint32 MTH_FixedToInt(Fixed32 a) { return (Sint16)((Uint32)a >> 16); }

Fixed32 MTH_Product(Fixed32* a, Fixed32* b)
{
    long long sum = 0;
    for (int i = 0; i < 3; ++i)
        sum += (long long)a[i] * b[i];
    return (Fixed32)(Uint32)((unsigned long long)sum >> 16);
}

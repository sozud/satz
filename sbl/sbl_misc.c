#include "sega_xpt.h"

void CSH_Init(Uint16 sw) { (void)sw; }
void CSH_AllClr(void) { }
void CSH_Purge(void* adrs, Uint32 size) { (void)adrs; (void)size; }

void nop(void) { }
void set_cr(unsigned int sr) { satz_set_sr(sr); }
unsigned int get_cr(void) { return satz_get_sr(); }
void set_sr(unsigned int sr) { satz_set_sr(sr); }
unsigned int get_sr(void) { return satz_get_sr(); }
unsigned int get_imask(void) { return (satz_get_sr() & 0xF0u) >> 4; }
void set_imask(unsigned int imask) { satz_set_sr((satz_get_sr() & ~0xF0u) | (imask << 4)); }
unsigned int _builtin_get_imask(void) { return get_imask(); }
void _builtin_set_imask(unsigned int imask) { set_imask(imask); }

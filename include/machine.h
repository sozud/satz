#ifndef __MACHINE_H
#define __MACHINE_H
#include "satz.h"

void nop(void);
void set_cr(unsigned int sr);
unsigned int get_cr(void);
void set_sr(unsigned int sr);
unsigned int get_sr(void);
unsigned int get_imask(void);
void set_imask(unsigned int imask);
#endif

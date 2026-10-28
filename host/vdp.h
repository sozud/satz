#ifndef SATZ_VDP_H
#define SATZ_VDP_H
#include <stdint.h>

uint32_t vdp1_read_reg(uint32_t offset);
void vdp1_write_reg(uint32_t offset, uint32_t value, int size);
uint32_t vdp2_read_reg(uint32_t offset);

#endif

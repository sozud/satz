#include "satz_host.h"
#include "vdp.h"
#include "satz_words.h"
#include "../video/satz_video.h"

static uint16_t vdp1_regs[0x10];

void vdp1_write_reg(uint32_t offset, uint32_t value, int size)
{
    if (size == 4) {
        vdp1_write_reg(offset, value >> 16, 2);
        vdp1_write_reg(offset + 2, value & 0xFFFF, 2);
        return;
    }
    satz_words_write(vdp1_regs, offset, value, size);
    offset &= ~1u;
    satz_video_vdp1_write(offset, vdp1_regs[offset >> 1]);
}

uint32_t vdp1_read_reg(uint32_t offset)
{
    return satz_video_vdp1_read(offset & ~1u);
}

uint32_t vdp2_read_reg(uint32_t offset)
{
    uint32_t line = satz_line();
    switch (offset) {
    case 0x04:
        return line >= 224 ? 8 : 0;
    case 0x08:
        return 0;
    case 0x0A:
        return line;
    }
    return satz_r16(SATZ_VDP2_REGS + offset);
}

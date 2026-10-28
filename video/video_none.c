#include "satz_video.h"
#include "../host/satz_host.h"
#include "../host/satz_words.h"

static uint16_t edsr, copr;
static uint16_t vdp1_vram[0x40000], vdp1_fb[0x20000], vdp1_regs[0x10];
static uint16_t vdp2_vram[0x40000], vdp2_cram[0x800], vdp2_regs[0x100];

static uint16_t* memory(uint32_t phys, uint32_t* offset)
{
    if (phys - 0x05C00000u < 0x80000u) {
        *offset = phys & 0x7FFFFu;
        return vdp1_vram;
    }
    if (phys - 0x05C80000u < 0x80000u) {
        *offset = phys & 0x3FFFFu;
        return vdp1_fb;
    }
    if (phys - 0x05E00000u < 0x100000u) {
        *offset = phys & 0x7FFFFu;
        return vdp2_vram;
    }
    if (phys - 0x05F00000u < 0x80000u) {
        *offset = phys & 0xFFFu;
        return vdp2_cram;
    }
    *offset = phys & 0x1FFu;
    return vdp2_regs;
}

uint32_t satz_video_read(uint32_t phys, int size)
{
    uint32_t offset;
    uint16_t* words = memory(phys, &offset);
    return satz_words_read(words, offset, size);
}

void satz_video_write(uint32_t phys, uint32_t value, int size)
{
    uint32_t offset;
    uint16_t* words = memory(phys, &offset);
    satz_words_write(words, offset, value, size);
}
static uint64_t end_at;

void satz_video_init(void) { }

void satz_video_advance(uint64_t cycles)
{
    if (end_at && cycles >= end_at) {
        end_at = 0;
        edsr |= 2;
        satz_raise_scu(0x4D);
    }
}

uint64_t satz_video_next_event(void) { return end_at; }

uint16_t satz_video_vdp1_read(uint32_t offset)
{
    if (offset == 0x10)
        return edsr;
    if (offset == 0x14)
        return copr;
    if (offset == 0x16)
        return 0x1000;
    return offset == 0x12 ? 0 : vdp1_regs[(offset >> 1) & 0xF];
}

extern uint64_t satz_now(void);

void satz_video_vdp1_write(uint32_t offset, uint16_t value)
{
    vdp1_regs[(offset >> 1) & 0xF] = value;
    if (offset != 0x04 || !(value & 3))
        return;
    edsr = (uint16_t)((edsr & 2) ? 1 : 0);
    uint32_t command = 0, cycles = 200;
    for (int i = 0; i < 0x4000; ++i) {
        uint16_t control = vdp1_vram[command * 16];
        if (control & 0x8000)
            break;
        cycles += 40;
        uint32_t jump = (control >> 12) & 7;
        uint32_t link = vdp1_vram[command * 16 + 1];
        command = (jump == 1 || jump == 2) ? link >> 2 : command + 1;
        command &= 0x3FFF;
    }
    copr = (uint16_t)(command * 4);
    end_at = satz_now() + cycles;
}

void satz_video_vblank(uint64_t cycles, int in_vblank)
{
    (void)cycles;
    if (in_vblank && (vdp1_regs[1] & 3) == 3)
        edsr = (uint16_t)((edsr & 2) ? 1 : 0);
}

int satz_video_render(uint32_t* pixels, int* width, int* height)
{
    (void)pixels;
    *width = *height = 0;
    return 0;
}

void satz_video_hblank(uint64_t cycles) { (void)cycles; }

#include "satz_sound.h"
#include "../host/satz_host.h"
#include "../host/satz_words.h"

static int active;
static uint16_t ram[0x40000];

uint32_t satz_sound_ram_read(uint32_t offset, int size) { return satz_words_read(ram, offset, size); }
void satz_sound_ram_write(uint32_t offset, uint32_t value, int size) { satz_words_write(ram, offset, value, size); }

void satz_sound_init(void) { active = 0; }
void satz_sound_set_68k(int on) { active = on; }

void satz_sound_advance(uint64_t sh2_cycles)
{
    (void)sh2_cycles;
    if (!active)
        return;
    uint32_t area = satz_r16(SATZ_SOUND_RAM + 0x412) << 16 | satz_r16(SATZ_SOUND_RAM + 0x414);
    if (area && area < 0x80000) {
        uint32_t pending = SATZ_SOUND_RAM + area + 0x60;
        satz_w8(pending, satz_r8(pending) & 0x7F);
        satz_w8(SATZ_SOUND_RAM + area + 0x61, satz_r8(SATZ_SOUND_RAM + area + 0x61) & 0x7F);
    }
}

uint32_t satz_sound_reg_read(uint32_t offset, int size)
{
    (void)offset;
    (void)size;
    return 0;
}

void satz_sound_reg_write(uint32_t offset, uint32_t value, int size)
{
    (void)offset;
    (void)value;
    (void)size;
}

int satz_sound_take_samples(int16_t* stereo, int max_frames)
{
    (void)stereo;
    (void)max_frames;
    return 0;
}

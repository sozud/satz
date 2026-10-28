#ifndef SATZ_SOUND_H
#define SATZ_SOUND_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void satz_sound_init(void);
void satz_sound_set_68k(int active);
void satz_sound_advance(uint64_t sh2_cycles);
uint32_t satz_sound_reg_read(uint32_t offset, int size);
void satz_sound_reg_write(uint32_t offset, uint32_t value, int size);
uint32_t satz_sound_ram_read(uint32_t offset, int size);
void satz_sound_ram_write(uint32_t offset, uint32_t value, int size);
int satz_sound_take_samples(int16_t* stereo, int max_frames);

#ifdef __cplusplus
}
#endif
#endif

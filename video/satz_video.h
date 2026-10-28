#ifndef SATZ_VIDEO_H
#define SATZ_VIDEO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void satz_video_init(void);
void satz_video_advance(uint64_t cycles);
uint64_t satz_video_next_event(void);
uint16_t satz_video_vdp1_read(uint32_t offset);
void satz_video_vdp1_write(uint32_t offset, uint16_t value);
uint32_t satz_video_read(uint32_t phys, int size);
void satz_video_write(uint32_t phys, uint32_t value, int size);
void satz_video_vblank(uint64_t cycles, int in_vblank);
void satz_video_hblank(uint64_t cycles);
int satz_video_render(uint32_t* pixels, int* width, int* height);

#ifdef __cplusplus
}
#endif
#endif

#include <mednafen/mednafen.h>
#include <mednafen/resampler/resampler.h>
#include "satz_sound.h"

using namespace Mednafen;

#define MDFN_SSFPLAY_COMPILE
#define SS_DBG(a, ...) ((void)0)
#define CDB_GetCDDA(n) ((void)0)
#define SS_SetPhysMemMap(...) ((void)0)
typedef int32 sscpu_timestamp_t;
#include "sound.cpp"

using namespace MDFN_IEN_SSFPLAY;

static uint64_t base_cycles;
static int16 out_buffer[65536 * 2];

extern "C" void satz_sound_init(void)
{
 SOUND_Init(false);
 SOUND_Reset(true);
 SOUND_SetClockRatio((uint32)(((uint64)1 << 32) * 11289600ULL / (1820ULL * 263ULL * 60ULL)));
 SOUND_StartFrame(44100, 3);
 SOUND_Set68KActive(false);
 base_cycles = 0;
}

extern "C" void satz_sound_set_68k(int active)
{
 SOUND_Set68KActive(active != 0);
}

extern "C" void satz_sound_advance(uint64_t sh2_cycles)
{
 if(sh2_cycles - base_cycles >= 0x40000000ULL)
 {
  int32 delta = (int32)(sh2_cycles - base_cycles - 0x100000);
  SOUND_Update(delta);
  SOUND_AdjustTS(-delta);
  base_cycles += (uint64)delta;
 }
 SOUND_Update((sscpu_timestamp_t)(sh2_cycles - base_cycles));
}

extern "C" uint32_t satz_sound_reg_read(uint32_t offset, int size)
{
 uint32 a = 0x100000 | (offset & 0xFFFFF);
 if(size == 4)
  return ((uint32)SOUND_Read16(a) << 16) | SOUND_Read16(a + 2);
 uint16 v = SOUND_Read16(a & ~1u);
 return size == 1 ? ((a & 1) ? (v & 0xFF) : (v >> 8)) : v;
}

extern "C" void satz_sound_reg_write(uint32_t offset, uint32_t value, int size)
{
 uint32 a = 0x100000 | (offset & 0xFFFFF);
 if(size == 4)
 {
  SOUND_Write16(a, value >> 16);
  SOUND_Write16(a + 2, value);
 }
 else if(size == 2)
  SOUND_Write16(a, value);
 else
  SOUND_Write8(a, value);
}

extern "C" uint32_t satz_sound_ram_read(uint32_t offset, int size)
{
 if(size == 4)
  return ((uint32)SOUND_Read16(offset) << 16) | SOUND_Read16(offset + 2);
 uint16 v = SOUND_Read16(offset & ~1u);
 return size == 1 ? ((offset & 1) ? (v & 0xFF) : (v >> 8)) : v;
}

extern "C" void satz_sound_ram_write(uint32_t offset, uint32_t value, int size)
{
 if(size == 4)
 {
  SOUND_Write16(offset, value >> 16);
  SOUND_Write16(offset + 2, value);
 }
 else if(size == 2)
  SOUND_Write16(offset, value);
 else
  SOUND_Write8(offset, value);
}

extern "C" int satz_sound_take_samples(int16_t* stereo, int max_frames)
{
 int32 frames = SOUND_FlushOutput(out_buffer, 65536, false);
 if(frames > max_frames)
  frames = max_frames;
 if(stereo)
  memcpy(stereo, out_buffer, (size_t)frames * 2 * sizeof(int16));
 SOUND_StartFrame(44100, 3);
 return frames;
}


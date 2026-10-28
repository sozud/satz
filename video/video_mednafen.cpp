#include <mednafen/mednafen.h>
#include "ss.h"
#include "scu.h"
#include "vdp1.h"
#include "vdp2_render.h"
#include "satz_video.h"
#include "../host/satz_words.h"

extern "C" void satz_raise_scu(int vector);
extern "C" uint64_t satz_now(void);

using namespace Mednafen;

namespace MDFN_IEN_SS
{
int32 SH7095_mem_timestamp;
event_list_entry events[SS_EVENT__COUNT];
uint32 ss_horrible_hacks = 0;

void SS_SetEventNT(event_list_entry* e, const sscpu_timestamp_t next_timestamp)
{
 e->event_time = next_timestamp;
}

void SS_SetPhysMemMap(uint32, uint32, uint16*, uint32, bool) { }

void SCU_SetInt(unsigned which, bool active)
{
 if(which == SCU_INT_VDP1 && active)
  satz_raise_scu(0x4D);
}

bool SCU_CheckVDP1HaltKludge(void) { return false; }

namespace VDP2 { sscpu_timestamp_t Update(sscpu_timestamp_t) { return SS_EVENT_DISABLED_TS; } }
}

MDFN_Surface::MDFN_Surface(void* const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix,
 const MDFN_PixelFormat& nf, const bool)
{
 pixels8 = nullptr;
 pixels16 = nullptr;
 pixels = (uint32*)p_pixels;
 palette = nullptr;
 pixels_is_external = true;
 w = p_width;
 h = p_height;
 pitchinpix = p_pitchinpix;
 format = nf;
}

MDFN_Surface::~MDFN_Surface() { }

namespace MDFN_IEN_SS { void VDP2REND_SatzSync(const uint16* regs, const uint16* cram, const uint16* vram); }
using namespace MDFN_IEN_SS;


static uint64_t base;
static bool vblank;
static uint16 vram2[0x40000], cram2[0x800], regs2[0x100];
static int32 lines[512];
static uint16 vdp1_lines[256][352];
static bool vdp1_hires8[256];


static uint16* vdp2_memory(uint32 phys, uint32* offset, bool write)
{
 if(phys - 0x05E00000u < 0x100000u)
 {
  *offset = phys & 0x7FFFFu;
  return vram2;
 }
 if(phys - 0x05F00000u < 0x80000u)
 {
  *offset = phys & 0xFFFu;
  return cram2;
 }
 *offset = phys & 0x1FFu;
 return (write && *offset >= 0x120u) ? nullptr : regs2;
}

extern "C" uint32_t satz_video_read(uint32_t phys, int size)
{
 if(phys - 0x05C00000u < 0x100000u)
 {
  const uint32 a = phys & 0xFFFFFu;
  if(size == 4)
   return ((uint32)VDP1::Read16_DB(a) << 16) | VDP1::Read16_DB(a + 2);
  const uint16 v = VDP1::Read16_DB(a & ~1u);
  return size == 1 ? ((a & 1) ? (v & 0xFFu) : (uint32)(v >> 8)) : v;
 }
 uint32 offset;
 const uint16* words = vdp2_memory(phys, &offset, false);
 return satz_words_read(words, offset, size);
}

extern "C" void satz_video_write(uint32_t phys, uint32_t value, int size)
{
 if(phys - 0x05C00000u < 0x100000u)
 {
  const uint32 a = phys & 0xFFFFFu;
  if(size == 4)
  {
   VDP1::Write16_DB(a, value >> 16);
   VDP1::Write16_DB(a + 2, value);
  }
  else if(size == 2)
   VDP1::Write16_DB(a, value);
  else
   VDP1::Write8_DB(a, (uint16)((value & 0xFFu) * 0x101u));
  return;
 }
 uint32 offset;
 if(uint16* words = vdp2_memory(phys, &offset, true))
  satz_words_write(words, offset, value, size);
}

static void read_vdp1_line(uint32 line)
{
 const uint16 tvmd = regs2[0];
 const unsigned vres = (tvmd >> 4) & 3;
 const uint32 active = vres == 0 ? 224 : (vres == 1 ? 240 : 256);
 if(line < active)
  vdp1_hires8[line] = VDP1::GetLine(line, vdp1_lines[line], (tvmd & 1) ? 352 : 320, 0, 0, 0, 0);
}

static int32 stamp(uint64_t cycles)
{
 if(cycles - base > 0x20000000ULL)
 {
  int32 delta = (int32)(cycles - base - 0x1000000);
  VDP1::AdjustTS(-delta);
  if(events[SS_EVENT_VDP1].event_time != SS_EVENT_DISABLED_TS)
   events[SS_EVENT_VDP1].event_time -= delta;
  base += (uint64_t)delta;
 }
 SH7095_mem_timestamp = (int32)(cycles - base);
 return SH7095_mem_timestamp;
}

extern "C" void satz_video_init(void)
{
 for(auto& e : events)
  e.event_time = SS_EVENT_DISABLED_TS;
 VDP1::Init();
 VDP1::Reset(true);
 VDP2REND_Init(false, 0);
 alignas(MDFNGI) static uint8 gi_storage[sizeof(MDFNGI)];
 VDP2REND_SetGetVideoParams((MDFNGI*)gi_storage, true, 0, 239, true, false);
 VDP2REND_Reset(true);
 base = 0;
}

extern "C" void satz_video_advance(uint64_t cycles)
{
 int32 ts = stamp(cycles);
 while(events[SS_EVENT_VDP1].event_time != SS_EVENT_DISABLED_TS && ts >= events[SS_EVENT_VDP1].event_time)
  events[SS_EVENT_VDP1].event_time = VDP1::Update(events[SS_EVENT_VDP1].event_time);
}

extern "C" uint64_t satz_video_next_event(void)
{
 if(events[SS_EVENT_VDP1].event_time == SS_EVENT_DISABLED_TS)
  return 0;
 return base + (uint64_t)(int64)events[SS_EVENT_VDP1].event_time;
}

extern "C" uint16_t satz_video_vdp1_read(uint32_t offset)
{
 satz_video_advance(satz_now());
 return VDP1::Read16_DB(0x100000 | (offset & 0x1E));
}

extern "C" void satz_video_vdp1_write(uint32_t offset, uint16_t value)
{
 satz_video_advance(satz_now());
 VDP1::Write16_DB(0x100000 | (offset & 0x1E), value);
}

extern "C" void satz_video_vblank(uint64_t cycles, int in_vblank)
{
 int32 ts = stamp(cycles);
 satz_video_advance(cycles);
 if(in_vblank && !vblank)
  for(uint32 line = 0; line < 256; line++)
   read_vdp1_line(line);
 vblank = in_vblank != 0;
 VDP1::SetHBVB(ts, false, vblank);
}

extern "C" void satz_video_hblank(uint64_t cycles)
{
 int32 ts = stamp(cycles);
 satz_video_advance(cycles);
 VDP1::SetHBVB(ts, true, vblank);
 VDP1::SetHBVB(ts, false, vblank);
}

extern "C" int satz_video_render(uint32_t* pixels, int* width, int* height)
{
 VDP2REND_SatzSync(regs2, cram2, vram2);

 const uint16 tvmd = regs2[0];
 const unsigned hres = tvmd & 7;
 const unsigned vres = (tvmd >> 4) & 3;
 const unsigned active = vres == 0 ? 224 : (vres == 1 ? 240 : 256);
 const bool clock28m = (hres & 1) != 0;
 const unsigned top = (240 - (active > 240 ? 240 : active)) / 2;

 MDFN_PixelFormat format(MDFN_PixelFormat::ABGR32_8888);
 MDFN_Surface surface(pixels, 704, 512, 704, format);
 EmulateSpecStruct espec;
 espec.surface = &surface;
 espec.LineWidths = lines;
 VDP2REND_StartFrame(&espec, clock28m, -1);
 for(unsigned crt = 0; crt < 240; crt++)
 {
  if(crt < top || crt >= top + active)
  {
   VDP2REND_DrawLine(-1, crt, false);
   continue;
  }
  const unsigned line = crt - top;
  VDP2Rend_LIB* lib = VDP2REND_GetLIB(line);
  for(unsigned w = 0; w < 2; w++)
  {
   const uint16 sy = regs2[(0xC2 >> 1) + w * 4] & 0x1FF, ey = regs2[(0xC6 >> 1) + w * 4] & 0x1FF;
   lib->win_ymet[w] = line >= sy && line <= ey;
  }
  memset(lib->rv, 0, sizeof(lib->rv));
  memcpy(lib->vdp1_line, vdp1_lines[line], sizeof(lib->vdp1_line));
  lib->vdp1_hires8 = vdp1_hires8[line];
  VDP2REND_DrawLine(line, crt, false);
 }
 VDP2REND_EndFrame();
 *width = espec.DisplayRect.w;
 for(int y = 0; !espec.DisplayRect.w && y < espec.DisplayRect.h; y++)
  *width = std::max<int>(*width, lines[espec.DisplayRect.y + y]);
 *height = espec.DisplayRect.h;
 if(espec.DisplayRect.x || espec.DisplayRect.y)
 {
  for(int y = 0; y < *height; y++)
   memmove(pixels + y * 704, pixels + (y + espec.DisplayRect.y) * 704 + espec.DisplayRect.x, (size_t)*width * 4);
 }
 return 1;
}

#include "sega_xpt.h"

typedef struct {
    Uint8 id;
    Uint8 con;
} PerMulInfo;

static Uint8* per_work;
static Uint8* per_get;
static Uint8* per_set;
static Uint16 per_num;
static Uint8 per_size;
static Uint8 per_vblank;
static Uint8 per_vblank_count;
static Uint8 per_kind;
static PerMulInfo per_mul[2][2];
static int per_mul_get;

static void sample(Uint8* out, PerMulInfo* mul)
{
    Uint32 b = ~satz_pad_buttons() & ~0x3000u & 0xFFFFu;
    out[0] = 0x00;
    out[1] = 0x02;
    out[2] = (Uint8)((((b >> 4) & 0xF) << 4) | ((b >> 8) & 0xF));
    out[3] = (Uint8)(((b & 0xF) << 4) | ((b >> 12) & 0xF) | 0x7);
    mul[0].id = 0xF0;
    mul[0].con = 1;
    mul[1].id = 0xF0;
    mul[1].con = 0;
}

Uint8 per_get_sys_data[12];
volatile Uint8 per_set_sys_flg;
#define PER_SYS_DATA per_get_sys_data
#define PER_SYS_FLAG per_set_sys_flg

Uint32 PER_LInit(Uint8 kind, Uint16 num, Uint8 size, Uint8* work, Uint8 v_blank)
{
    if (kind == 0) {
        PER_SYS_DATA[0] = 0x00;
        PER_SYS_DATA[1] = 0x04;
        *(Uint16*)(PER_SYS_DATA + 2) = 0x3F42;
        *(Uint32*)(PER_SYS_DATA + 4) = 0;
        PER_SYS_DATA[8] = 0x80;
        PER_SYS_FLAG = 1;
        return 0;
    }
    per_kind = kind;
    per_num = num;
    per_size = size;
    per_work = work;
    per_vblank = v_blank;
    per_vblank_count = 0;
    per_get = work;
    per_set = work + num * (size + 2);
    for (int i = 0; i < 2; ++i) {
        per_mul[i][0].id = per_mul[i][1].id = 0xF0;
        per_mul[i][0].con = per_mul[i][1].con = 0;
    }
    sample(per_set, per_mul[per_mul_get ^ 1]);
    return 0;
}

Uint32 PER_LGetPer(void** output_dt, PerMulInfo** mul_info)
{
    if (!per_work) {
        *output_dt = 0;
        return 1;
    }
    if (per_vblank_count < per_vblank) {
        per_vblank_count++;
        return 0;
    }
    per_vblank_count = 0;
    Uint8* swap = per_set;
    per_set = per_get;
    per_get = swap;
    per_mul_get ^= 1;
    *output_dt = per_get;
    *mul_info = per_mul[per_mul_get];
    sample(per_set, per_mul[per_mul_get ^ 1]);
    return 0;
}

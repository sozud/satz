#include <machine.h>
#include "sega_xpt.h"
#include "sega_def.h"
#include "sega_mth.h"
#define _SPR2_
#include "sega_spr.h"
#include "sega_scl.h"
#include "sega_int.h"

#define COMMAND_MAX 64
#define GOUR_TBL_MAX 8
#define LOOKUP_TBL_MAX 8
#define CHAR_MAX 4
#define DRAW_PRTY_MAX 256

SPR_2DefineWork(work2d, COMMAND_MAX, GOUR_TBL_MAX, LOOKUP_TBL_MAX, CHAR_MAX, DRAW_PRTY_MAX)

#define RGB(r, g, b) (0x8000 | ((b) << 10) | ((g) << 5) | (r))
#define TEXTURE_SIZE 64
#define CUBE_CHAR 0
#define FAR_CHARS SCL_VDP2_VRAM_A0
#define FAR_MAP SCL_VDP2_VRAM_A1
#define NEAR_CHARS SCL_VDP2_VRAM_B0
#define NEAR_MAP SCL_VDP2_VRAM_B1

static Uint8 texture[TEXTURE_SIZE * TEXTURE_SIZE * 2];

static Uint16 cycle_table[] = {
    0x15FF, 0xFFFF,
    0xFFFF, 0xFFFF,
    0x04FF, 0xFFFF,
    0xFFFF, 0xFFFF,
};

static Uint16 far_palette[16] = {
    0x0000, RGB(2, 2, 8), RGB(4, 4, 14), RGB(31, 31, 31), RGB(20, 20, 31), RGB(10, 10, 24),
};
static Uint16 near_palette[16] = {
    0x0000, RGB(4, 14, 4), RGB(8, 20, 6), RGB(12, 26, 8), RGB(18, 12, 6), RGB(24, 18, 10),
};

static const Sint16 cube_vertices[8][3] = {
    { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
    { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 },
};
static const Uint8 cube_faces[6][4] = {
    { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 0, 3, 7 }, { 1, 5, 6, 2 }, { 4, 5, 1, 0 }, { 3, 2, 6, 7 },
};

static void vblank_in(void) { SCL_VblankStart(); }
static void vblank_out(void) { SCL_VblankEnd(); }

static void put_pixel4(Uint32 cell, int x, int y, int color)
{
    Uint32 p = cell + y * 4 + x / 2;
    Uint8 v = (Uint8)satz_bus_read(p, 1);
    satz_bus_write(p, x & 1 ? (v & 0xF0) | color : (v & 0x0F) | (color << 4), 1);
}

static void make_texture(void)
{
    for (int y = 0; y < TEXTURE_SIZE; ++y)
        for (int x = 0; x < TEXTURE_SIZE; ++x) {
            int checker = ((x >> 3) ^ (y >> 3)) & 1;
            int edge = x < 2 || y < 2 || x >= TEXTURE_SIZE - 2 || y >= TEXTURE_SIZE - 2;
            Uint16 c = edge ? RGB(31, 31, 31) : checker ? RGB(31, x / 3, 4) : RGB(4, y / 3, 31 - x / 4);
            texture[(y * TEXTURE_SIZE + x) * 2] = c >> 8;
            texture[(y * TEXTURE_SIZE + x) * 2 + 1] = c & 0xFF;
        }
}

static void make_cells(void)
{
    Uint32 far = FAR_CHARS;
    Uint32 near = NEAR_CHARS;
    for (int i = 0; i < 32 * 4; i += 4) {
        satz_bus_write(far + i, 0, 4);
        satz_bus_write(near + i, 0, 4);
    }
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            put_pixel4(far + 32, x, y, (x + y) & 4 ? 1 : 2);
            put_pixel4(far + 64, x, y, x == 3 && y == 3 ? 3 : (x + y) & 4 ? 1 : 2);
            put_pixel4(far + 96, x, y, (x == 4 && y > 2 && y < 6) || (y == 4 && x > 2 && x < 6) ? 4 : 1);
            put_pixel4(near + 32, x, y, y >= 6 - (x & 3) ? 1 : 0);
            put_pixel4(near + 64, x, y, (x ^ y) & 2 ? 2 : 3);
            put_pixel4(near + 96, x, y, y < 2 ? 4 : 5);
        }
}

static void put_name(Uint32 map, int x, int y, Uint16 name)
{
    satz_bus_write(map + (y * 64 + x) * 2, name, 2);
}

static void make_maps(void)
{
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            unsigned hash = (x * 73 + y * 151) * 2654435761u >> 24;
            int far_cell = hash < 12 ? 2 : hash < 16 ? 3 : 1;
            int ground = 22 + ((x * 5) & 7) - ((x * 3) & 3);
            int near_cell = y < ground - 1 ? 0 : y == ground - 1 ? 1 : y == ground ? 3 : 2;
            put_name(FAR_MAP, x, y, far_cell);
            put_name(NEAR_MAP, x, y, near_cell);
        }
}

static void configure_plane(Uint32 plane, Uint32 map, Uint16 supplement)
{
    SclConfig config;
    SCL_InitConfigTb(&config);
    config.dispenbl = ON;
    config.charsize = SCL_CHAR_SIZE_1X1;
    config.pnamesize = SCL_PN1WORD;
    config.flip = SCL_PN_12BIT;
    config.platesize = SCL_PL_SIZE_1X1;
    config.coltype = SCL_COL_TYPE_16;
    config.datatype = SCL_CELL;
    config.patnamecontrl = supplement;
    for (int i = 0; i < 4; ++i)
        config.plate_addr[i] = map;
    SCL_SetConfig(plane, &config);
}

static void setup_vdp2(void)
{
    Uint16 back = RGB(0, 0, 4);
    make_cells();
    make_maps();
    SCL_SetBack(SCL_VDP2_VRAM + 0x80000 - 2, 1, &back);
    SCL_AllocColRam(SCL_NBG0, 16, OFF);
    SCL_SetColRam(SCL_NBG0, 0, 16, near_palette);
    SCL_AllocColRam(SCL_NBG1, 16, OFF);
    SCL_SetColRam(SCL_NBG1, 0, 16, far_palette);
    configure_plane(SCL_NBG0, NEAR_MAP, 0x0008);
    configure_plane(SCL_NBG1, FAR_MAP, 0x0000);
    SCL_SetCycleTable(cycle_table);
    SCL_SetPriority(SCL_NBG0, 5);
    SCL_SetPriority(SCL_NBG1, 3);
}

static Fixed32 wrap_degrees(Fixed32 a)
{
    while (a > MTH_FIXED(180))
        a -= MTH_FIXED(360);
    while (a < MTH_FIXED(-180))
        a += MTH_FIXED(360);
    return a;
}

static void draw_cube(Fixed32 yaw, Fixed32 pitch)
{
    Fixed32 sy = MTH_Sin(yaw), cy = MTH_Cos(yaw), sp = MTH_Sin(pitch), cp = MTH_Cos(pitch);
    XyInt screen[8];
    Fixed32 depth[8];
    for (int i = 0; i < 8; ++i) {
        Fixed32 x = MTH_IntToFixed(cube_vertices[i][0] * 48);
        Fixed32 y = MTH_IntToFixed(cube_vertices[i][1] * 48);
        Fixed32 z = MTH_IntToFixed(cube_vertices[i][2] * 48);
        Fixed32 x1 = MTH_Mul(x, cy) + MTH_Mul(z, sy);
        Fixed32 z1 = MTH_Mul(z, cy) - MTH_Mul(x, sy);
        Fixed32 y2 = MTH_Mul(y, cp) - MTH_Mul(z1, sp);
        Fixed32 z2 = MTH_Mul(y, sp) + MTH_Mul(z1, cp) + MTH_IntToFixed(240);
        screen[i].x = (Sint16)MTH_FixedToInt(MTH_Div(MTH_Mul(x1, MTH_IntToFixed(200)), z2));
        screen[i].y = (Sint16)MTH_FixedToInt(MTH_Div(MTH_Mul(y2, MTH_IntToFixed(200)), z2));
        depth[i] = z2;
    }
    for (int f = 0; f < 6; ++f) {
        const Uint8* v = cube_faces[f];
        Sint32 ax = screen[v[1]].x - screen[v[0]].x, ay = screen[v[1]].y - screen[v[0]].y;
        Sint32 bx = screen[v[2]].x - screen[v[0]].x, by = screen[v[2]].y - screen[v[0]].y;
        if (ax * by - ay * bx <= 0)
            continue;
        XyInt quad[4];
        for (int i = 0; i < 4; ++i)
            quad[i] = screen[v[i]];
        Fixed32 z = depth[v[0]] + depth[v[1]] + depth[v[2]] + depth[v[3]];
        SPR_2DistSpr(DRAW_PRTY_MAX - 1 - (MTH_FixedToInt(z) >> 2), 0, ECDSPD_DISABLE | COMPO_REP, 0, CUBE_CHAR, quad, NO_GOUR);
    }
}

void main(void)
{
    XyInt clip = { 320 - 1, 224 - 1 }, origin = { 160, 112 };
    Fixed32 yaw = 0, pitch = MTH_FIXED(20);
    Fixed32 far_x = 0, near_x = 0, near_y = 0;
    int frame = 0;

    set_imask(0);
    SCL_Vdp2Init();
    SCL_SetPriority(SCL_SP0 | SCL_SP1 | SCL_SP2 | SCL_SP3 | SCL_SP4 | SCL_SP5 | SCL_SP6 | SCL_SP7, 7);
    SCL_SetSpriteMode(SCL_TYPE1, SCL_MIX, SCL_SP_WINDOW);
    SCL_SetColRamMode(SCL_CRM15_2048);
    INT_ChgMsk(INT_MSK_NULL, INT_MSK_VBLK_IN | INT_MSK_VBLK_OUT);
    INT_SetScuFunc(INT_SCU_VBLK_IN, vblank_in);
    INT_SetScuFunc(INT_SCU_VBLK_OUT, vblank_out);
    INT_ChgMsk(INT_MSK_VBLK_IN | INT_MSK_VBLK_OUT, INT_MSK_NULL);
    SPR_2Initial(&work2d);
    SCL_SetFrameInterval(0xffff);
    SCL_DisplayFrame();
    SCL_DisplayFrame();
    SCL_SetDisplayMode(SCL_NON_INTER, SCL_224LINE, SCL_NORMAL_A);
    SPR_2SetTvMode(SPR_TV_NORMAL, SPR_TV_320X224, OFF);
    SPR_2FrameEraseData(0x0000);
    SPR_2FrameChgIntr(1);
    make_texture();
    SPR_2SetChar(CUBE_CHAR, COLOR_5, 0, TEXTURE_SIZE, TEXTURE_SIZE, texture);
    setup_vdp2();
    for (;;) {
        SPR_2OpenCommand(SPR_2DRAW_PRTY_ON);
        SPR_2SysClip(SPR_2MOST_FAR, &clip);
        SPR_2LocalCoord(SPR_2MOST_FAR, &origin);
        draw_cube(yaw, pitch);
        SPR_2CloseCommand();
        SCL_Open(SCL_NBG1);
        SCL_MoveTo(far_x, 0, 0);
        SCL_Close();
        SCL_Open(SCL_NBG0);
        SCL_MoveTo(near_x, near_y, 0);
        SCL_Close();
        SCL_DisplayFrame();
        yaw = wrap_degrees(yaw + MTH_FIXED(2));
        pitch = wrap_degrees(pitch + MTH_FIXED(1) / 2);
        far_x += MTH_FIXED(0.5);
        near_x += MTH_FIXED(2);
        near_y = MTH_Mul(MTH_Sin(wrap_degrees(MTH_IntToFixed(frame * 3))), MTH_FIXED(8)) + MTH_FIXED(8);
        ++frame;
    }
}

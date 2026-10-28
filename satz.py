#!/usr/bin/env python3
#
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

SATZ = Path(__file__).resolve().parent
MEDNAFEN = Path(os.environ.get('SATZ_MEDNAFEN', SATZ / 'third_party/mednafen'))
CC = os.environ.get('SATZ_CC', 'clang-18')
CXX = os.environ.get('SATZ_CXX', 'clang++-18')
MEDNAFEN_CXX = os.environ.get('SATZ_MEDNAFEN_CXX', 'g++')
MEDNAFEN_CC = os.environ.get('SATZ_MEDNAFEN_CC', 'gcc')
M68K_CXX = os.environ.get('SATZ_M68K_CXX', MEDNAFEN_CXX)
COMMON = ['-O2', '-gline-tables-only', '-fno-strict-aliasing', '-fwrapv', '-fno-common', '-funwind-tables',
          '-ffunction-sections', '-fdata-sections', '-Wno-everything', '-Werror=pointer-to-int-cast',
          '-Werror=int-to-pointer-cast', '-Werror=int-conversion',
          '-Werror=implicit-function-declaration', '-Werror=implicit-int']
SATURN = ['-ffreestanding', '-fno-builtin', '-std=gnu99', '-DSATZ=1', '-I' + str(SATZ / 'include'),
          '-include', str(SATZ / 'include/satz.h')]
GCC_COMMON = ['-O2', '-g1', '-fno-strict-aliasing', '-fwrapv', '-fno-common', '-funwind-tables', '-ffunction-sections',
              '-fdata-sections', '-w', '-Werror=implicit-function-declaration', '-Werror=implicit-int',
              '-Werror=int-conversion', '-Werror=pointer-to-int-cast', '-Werror=int-to-pointer-cast']
if sys.platform == 'darwin':
    GCC_COMMON = [f for f in GCC_COMMON if f != '-Werror=implicit-function-declaration'] + ['-fpermissive']
GCC = os.environ.get('SATZ_GCC', 'gcc')
SANITIZE = ['-fsanitize=address,undefined', '-fno-sanitize=alignment,function', '-fno-omit-frame-pointer']
GCC_SANITIZE = ['-fsanitize=address,undefined', '-fno-sanitize=alignment', '-fno-omit-frame-pointer']
HOST = ['-std=gnu11', '-I' + str(SATZ / 'include'), '-I' + str(SATZ / 'host')]
if sys.platform == 'darwin':
    HOST += ['-D_XOPEN_SOURCE=700', '-D_DARWIN_C_SOURCE']
    SATURN += ['-D_FORTIFY_SOURCE=0']
DEFAULT_RAM_AREAS = ((0x00200000, 0x100000), (0x06000000, 0x100000))
STDLIB = '#ifndef NULL\n#define NULL ((void *)0)\n#endif\n#ifndef _SIZE_T\n#define _SIZE_T\ntypedef unsigned long size_t;\n#endif\n'
STRING = STDLIB + ''.join(f'{p};\n' for p in (
    'void *memcpy(void *, const void *, size_t)', 'void *memset(void *, int, size_t)',
    'int memcmp(const void *, const void *, size_t)', 'int strncmp(const char *, const char *, size_t)',
    'int strcmp(const char *, const char *)', 'char *strncpy(char *, const char *, size_t)',
    'char *strcpy(char *, const char *)', 'size_t strlen(const char *)'))

SDK_PORTS = {
    'sega_xpt.h': [
        ('typedef unsigned    long    Uint32;', 'typedef unsigned    int     Uint32;'),
        ('typedef signed      long    Sint32;', 'typedef signed      int     Sint32;'),
    ],
    'gfs_dir': [
        ('@function', ('GFS_LOCAL Uint16 gfdr_readWordNB(GfdrFile *fp, Sint32 *ec)',
                       '    Uint16 value, other;\n    *ec = gfdr_read(fp, &value, sizeof(value));\n    if (*ec != GFS_ERR_OK)\n        return 0;\n'
                       '    *ec = gfdr_read(fp, &other, sizeof(other));\n    return *ec == GFS_ERR_OK ? value : 0;\n')),
        ('@function', ('GFS_LOCAL Uint32 gfdr_readLongNB(GfdrFile *fp, Sint32 *ec)',
                       '    Uint32 value, other;\n    *ec = gfdr_read(fp, &value, sizeof(value));\n    if (*ec != GFS_ERR_OK)\n        return 0;\n'
                       '    *ec = gfdr_read(fp, &other, sizeof(other));\n    return *ec == GFS_ERR_OK ? value : 0;\n')),
    ],
    'gfs': [
        ('#define DIRREC_EXT(rec)         ((rec) + 6)', '#define DIRREC_EXT(rec)         ((rec) + 2)'),
        ('#define DIRREC_LEN(rec)         ((rec) + 14)', '#define DIRREC_LEN(rec)         ((rec) + 10)'),
        ('    if (((Uint32)work) & 3) {', '    if (((unsigned long)work) & 3) {'),
        ('    INT_ChgMsk(INT_MSK_DMA0, INT_MSK_NULL);', '    satz_bios_change_scu_mask(~(INT_MSK_DMA0), INT_MSK_NULL);'),
    ],
    'sega_gfs.h': [
        ('#define GFS_DDS_ID()    (*(Uint8 *)(GFS_DDS_ADDR+0))', '#define GFS_DDS_ID()    ((Uint8)satz_bus_read(GFS_DDS_ADDR, 1))'),
    ],
    'gfs_cdc': [
        ('#define GFCD_SES_NSES(ses)      ((ses)->nses[0])', '#define GFCD_SES_NSES(ses)      ((Uint8)((ses)->xfad >> 24))'),
    ],
    'gfs_trn': [
        ('#define TO_CADR(adr)    (((Uint32)adr) & 0x0fffffff)', '#define TO_CADR(adr)    (satz_bus_address(adr) & 0x0fffffff)'),
        ('    prm.dxr = (Uint32)src;\n    prm.dxw = (Uint32)dst;', '    prm.dxr = satz_bus_address(src);\n    prm.dxw = satz_bus_address(dst);'),
        ('        prm.sar = (Uint32)src;\n        prm.dar = (Uint32)dst;', '        prm.sar = satz_bus_address(src);\n        prm.dar = satz_bus_address(dst);'),
        ('    Uint32      *sp = src, *dp = dst;', '    volatile Uint32 *sp = src;\n    Uint32 *dp = dst;'),
        ('        *dp = *sp;', '        SATZ_PORT_WRITE32(dp, SATZ_PORT_READ32(sp));'),
        ('        frac_buf = *sp;', '        frac_buf = SATZ_PORT_READ32(sp);'),
        ('            *frac++ = ((Uint8 *)&frac_buf)[i];', '            satz_bus_store((unsigned int)satz_bus_address(frac++), (Uint8 *)&frac_buf + i, 1);'),
        ('GFS_LOCAL void gftr_copyFrac(Sint32 n, Uint32 *dp, Uint32 *sp);', 'GFS_LOCAL void gftr_copyFrac(Sint32 n, Uint32 *dp, volatile Uint32 *sp);'),
        ('GFS_LOCAL void gftr_copyFrac(Sint32 n, Uint32 *dp, Uint32 *sp)\n', 'GFS_LOCAL void gftr_copyFrac(Sint32 n, Uint32 *dp, volatile Uint32 *sp)\n'),
    ],
}
BIG_ENDIAN = '\n#pragma scalar_storage_order big-endian\n'
NATIVE_ORDER = '\n#pragma scalar_storage_order default\n'
SBL_SYS_CALLS = [(f'\\(\\*\\*\\([^\\n]*?\\)0x{address:X}\\)', function) for address, function in (
    (0x6000300, 'satz_scu_vector_set'), (0x6000304, 'satz_scu_vector_get'), (0x6000310, 'satz_scu_vector_set'),
    (0x6000314, 'satz_scu_vector_get'), (0x6000320, 'satz_bios_change_clock'), (0x6000330, 'satz_bios_test_and_set'),
    (0x6000334, 'satz_bios_clear_semaphore'), (0x6000340, 'satz_set_scu_mask'), (0x6000344, 'satz_bios_change_scu_mask'))]
SBL_PORTS = {
    'sega_sys.h': [('@re', p) for p in SBL_SYS_CALLS] + [
        ('@re', (r'\(\*\(volatile Uint32\s*\*\)0x6000348\)', '(satz_scu_mask())')),
        ('@re', (r'\(\*\(volatile Uint32\s*\*\)0x6000324\)', '(0)')),
        ('@re', (r'\(\(\*\*\([^\n]*?\)0x6000280\)\(_IprTab_\)\)', '((void)(_IprTab_))')),
    ],
    'sega_def.h': [
        ('@before', ('typedef struct  XyInt {', BIG_ENDIAN.lstrip('\n'))),
        ('@after', ('} XyInt;', NATIVE_ORDER.rstrip('\n'))),
    ],
    'sega_spr.h': [
        ('#define  USE_SLAVE', ''),
        ('@before', ('struct SprSpCmd {', BIG_ENDIAN.lstrip('\n'))),
        ('@before', ('typedef struct SprSpCmd     SprSpCmd;', NATIVE_ORDER.lstrip('\n'))),
        ('#define SPR_WRITE_REG(reg, val) \\\n      *(volatile Uint16*)(VDP1_REG_BASE+(reg)) = (val)',
         '#define SPR_WRITE_REG(reg, val) satz_bus_write(VDP1_REG_BASE+(reg), (val), 2)'),
        ('#define SPR_READ_REG(reg) \\\n      (*(volatile Uint16*)(VDP1_REG_BASE+(reg)))',
         '#define SPR_READ_REG(reg) ((Uint16)satz_bus_read(VDP1_REG_BASE+(reg), 2))'),
        ('#define SPR_SCLWRITE_REG(reg, val) \\\n      *(volatile Uint16*)(VDP2_REG_BASE+(reg)) = (val)',
         '#define SPR_SCLWRITE_REG(reg, val) satz_bus_write(VDP2_REG_BASE+(reg), (val), 2)'),
        ('#define SPR_SCLREAD_REG(reg) \\\n      (*(volatile Uint16*)(VDP2_REG_BASE+(reg)))',
         '#define SPR_SCLREAD_REG(reg) ((Uint16)satz_bus_read(VDP2_REG_BASE+(reg), 2))'),
    ],
    'sega_scl.h': [
        ('@after', ('#include <sega_mth.h> ', BIG_ENDIAN.rstrip('\n'))),
        ('@before', ('#ifndef __PRI_GLVAR', BIG_ENDIAN.lstrip('\n'))),
        ('@end', NATIVE_ORDER.lstrip('\n')),
    ],
    'spr_1c': [
        ('    *VRAM     = (Uint8*)VRAM_ADDR;\n\n    *(*VRAM)     = 0x80;                 /* set END command to VRAM top area */\n    *((*VRAM)+1) = 0x00;',
         '    *VRAM     = (Uint8*)satz_bus_pointer(VRAM_ADDR);\n\n    satz_bus_write(VRAM_ADDR, 0x80, 1);\n    satz_bus_write(VRAM_ADDR + 1, 0x00, 1);'),
    ],
    'spr_2c': [
        ('@all:&spCmd->ax, xy,', '(Uint8*)spCmd + 12, xy,'),
        ('    struct NullBlock *next;                 /* Next Null Block Pointer       */', '    Uint32            next;'),
        ('@all:(NullBlock*)((Uint32)', '(NullBlock*)((unsigned long)'),
        ('@re', (r'\b(nullBlock|minNullBlock|befMinNullBlock|memblk|prev|second_best|nullBlockTop|blockTop|pt1|pt2)->next\s*=(?!=)\s*([^;]+);', r'SATZ_FIELD_SET(\1, NullBlock, next, satz_bus_address(\2));')),
        ('@re', (r'\b(nullBlock|minNullBlock|befMinNullBlock|memblk|prev|second_best|nullBlockTop|blockTop|pt1|pt2)->next\b', r'((NullBlock*)satz_bus_pointer(SATZ_FIELD_GET(\1, NullBlock, next)))')),
        ('@re', (r'\b(spCmd|topSpCmd|chainLastCmd|CMD|wSpCmd|s_chainLastCmd)->(\w+)\s*\|=\s*([^;]+);', r'SATZ_FIELD_SET(\1, SprSpCmd, \2, SATZ_FIELD_GET(\1, SprSpCmd, \2) | (\3));')),
        ('@re', (r'\b(spCmd|topSpCmd|chainLastCmd|CMD|wSpCmd|s_chainLastCmd)->(\w+)\s*=(?!=)\s*([^;]+);', r'SATZ_FIELD_SET(\1, SprSpCmd, \2, \3);')),
        ('@re', (r'\b(spCmd|topSpCmd|chainLastCmd|CMD|wSpCmd|s_chainLastCmd)->(\w+)', r'SATZ_FIELD_GET(\1, SprSpCmd, \2)')),
        ('@re', (r'\b(nullBlock|minNullBlock|befMinNullBlock|memblk|prev|second_best|nullBlockTop|blockTop|pt1|pt2)->(\w+)\s*\+=\s*([^;]+);', r'SATZ_FIELD_SET(\1, NullBlock, \2, SATZ_FIELD_GET(\1, NullBlock, \2) + (\3));')),
        ('@re', (r'\b(nullBlock|minNullBlock|befMinNullBlock|memblk|prev|second_best|nullBlockTop|blockTop|pt1|pt2)->(\w+)\s*\-=\s*([^;]+);', r'SATZ_FIELD_SET(\1, NullBlock, \2, SATZ_FIELD_GET(\1, NullBlock, \2) - (\3));')),
        ('@re', (r'\b(nullBlock|minNullBlock|befMinNullBlock|memblk|prev|second_best|nullBlockTop|blockTop|pt1|pt2)->(\w+)\s*=(?!=)\s*([^;]+);', r'SATZ_FIELD_SET(\1, NullBlock, \2, \3);')),
        ('@re', (r'\b(nullBlock|minNullBlock|befMinNullBlock|memblk|prev|second_best|nullBlockTop|blockTop|pt1|pt2)->(\w+)', r'SATZ_FIELD_GET(\1, NullBlock, \2)')),
        ('#if 0\n\n/*****************************************************************************\n *\n * NAME:  flushDrawPrtyBlock()',
         '#if 1\n\n/*****************************************************************************\n *\n * NAME:  flushDrawPrtyBlock()'),
    ],
    'scl_func': [
        ('@re', (r'^\tregaddr\[(\d)\] = (Scl_s_reg\.\w+);', r'\tsatz_bus_write((unsigned long)&regaddr[\1], \2, 2);')),
        ('\t\tfor (tcr = 0;tcr < tsize;tcr++){\n\t\t\t*((Uint16 *)dest) = *((Uint16 *)src);\n\t\t\tdest = (Uint8 *)dest + 2;\n'
         '\t\t\tsrc = (Uint8 *)src + 2;\n\t\t}',
         '\t\tsatz_bus_store((unsigned long)dest, src, tsize * 2);'),
        ('@all:&Scl_s_reg.ramcontrl ,', '(Uint8 *)&Scl_s_reg + __builtin_offsetof(SclSysreg, ramcontrl) ,'),
    ],
    'scl_fu03': [
        ('@re', (r'^(\t)Uint16(\t\*(?:mapoffset|map|map2)\b)', r'\1SatzBe16\2')),
        ('@re', (r'\b(mapoffset|map2?)(\s*)= &Scl_d_reg\.(\w+(?:\[\d+\])?);', r'\1\2= SATZ_BE16_FIELD(Scl_d_reg, \3);')),
        ('@re', (r'\*mapoffset\b(?! = 0;)', 'mapoffset->v')),
        ('@re', (r'\b(map2?)\[i\]', r'\1[i].v')),
    ],
    'scl_fu05': [
        ('\tUint32\t*addr, *addr1;', '\tSatzBe32\t*addr, *addr1;'),
        ('@re', (r'\b(addr1?) = &Scl_n_reg\.(\w+(?:\[\d+\])?);', r'\1 = SATZ_BE32_FIELD(Scl_n_reg, \2);')),
        ('@re', (r'^\t\*(addr1?) = ', r'\t\1->v = ')),
    ],
    'scl_win1': [
        ('@re', (r'^(    )Uint16(\t\*sxy,\*exy;)', r'\1SatzBe16\2')),
        ('@re', (r'\b([se]xy) = &Scl_w_reg\.(\w+)\[0\];', r'\1 = SATZ_BE16_FIELD(Scl_w_reg, \2[0]);')),
        ('@re', (r'\b([se]xy)\[([01])\] = ', r'\1[\2].v = ')),
        ('@all:(Uint8 *)&Scl_w_reg.wincontrl[0]', '((Uint8 *)&Scl_w_reg + __builtin_offsetof(SclWinscl, wincontrl))'),
    ],
    'scl_win2': [
        ('@re', (r'^(    )Uint16(\t\*sxy,\*exy;)', r'\1SatzBe16\2')),
        ('@re', (r'\b([se]xy) = &Scl_w_reg\.(\w+)\[0\];', r'\1 = SATZ_BE16_FIELD(Scl_w_reg, \2[0]);')),
        ('@re', (r'\b([se]xy)\[([01])\] = ', r'\1[\2].v = ')),
        ('@all:(Uint8 *)&Scl_w_reg.wincontrl[0]', '((Uint8 *)&Scl_w_reg + __builtin_offsetof(SclWinscl, wincontrl))'),
        ('@re', (r'\*addr16 = ([^;]+);', r'satz_bus_write((unsigned long)addr16, \1, 2);')),
    ],
    'scl_win3': [
        ('@all:(Uint8 *)&Scl_w_reg.wincontrl[0]', '((Uint8 *)&Scl_w_reg + __builtin_offsetof(SclWinscl, wincontrl))'),
    ],
    'scl_vblv': [
        ('@all:while(ReqDisplayFlag);', 'while(ReqDisplayFlag) satz_idle();'),
        ('@all:while(ReqDisplayFlag2);', 'while(ReqDisplayFlag2) satz_idle();'),
    ],
    'scl_back': [
        ('\tSCL_Memcpyw((void *)(Scl_n_reg.backcolmode|0x25e00000),dataTb,dataSize*2);',
         '\tsatz_bus_write16s(Scl_n_reg.backcolmode|0x25e00000,dataTb,dataSize);'),
    ],
    'scl_pr02': [
        ('\t    ram32[0] = color32[0];', '\t    satz_bus_write((unsigned long)ram32, color32[0], 4);'),
        ('\t    SCL_Memcpyw(ram32,color32,num*4);', '\t    satz_bus_write32s((unsigned long)ram32,color32,num);'),
        ('\t    ram16[0] = color16[0];', '\t    satz_bus_write((unsigned long)ram16, color16[0], 2);'),
        ('\t    SCL_Memcpyw(ram16,color16,num*2);', '\t    satz_bus_write16s((unsigned long)ram16,color16,num);'),
    ],
    'scl_pr04': [
        ('@re', (r'\bLWork\[0\]', 'satz_bus_read((unsigned long)LWork, 4)')),
        ('@re', (r'(?<!L)\bWork\[0\]', 'satz_bus_read((unsigned long)Work, 2)')),
    ],
    'scl_lncl': [
        ('\tfor (i=0;i<tbnum;i++)\tvram16[i] = palNumTb[i]+Offset;',
         '\tfor (i=0;i<tbnum;i++)\tsatz_bus_write((unsigned long)&vram16[i], palNumTb[i]+Offset, 2);'),
    ],
}
SDK_REPLACED = {'int', 'int_trmp', 'csh_main'}
MEDNAFEN_PORTS = {
    'scsp.h': [],
    'scsp.inc': [],
    'sound.cpp': [],
}
MEDNAFEN_VIDEO_PORTS = {
    'vdp1.cpp': [('@blank', 'void MakeDump(const std::string& path)')],
    'vdp1_line.cpp': [], 'vdp1_poly.cpp': [], 'vdp1_sprite.cpp': [],
    'vdp2_render.cpp': [('@after', ('static MThreading::Sem* WakeupSem;\n', 'static MThreading::Sem* DoneSem;\n')),
                        ('@after', (' WakeupSem = MThreading::Sem_Create();\n', ' DoneSem = MThreading::Sem_Create();\n')),
                        ('@all:\tDrawCounter.fetch_sub(1, std::memory_order_release);',
                         '\tif(DrawCounter.fetch_sub(1, std::memory_order_acq_rel) == 1)\n\t MThreading::Sem_Post(DoneSem);'),
                        ('@re', (r'^ while\(MDFN_UNLIKELY\(DrawCounter\.load\(std::memory_order_acquire\) != 0\)\)\n \{\n'
                                 r'  //fprintf\(stderr, "SLEEEEP\\n"\);\n  //Time::SleepMS\(1\);\n \}\n',
                                 ' while(MDFN_UNLIKELY(DrawCounter.load(std::memory_order_acquire) != 0))\n'
                                 '  MThreading::Sem_TimedWait(DoneSem, 1);\n')),
                        ('@append', '''
void VDP2REND_SatzSync(const uint16* regs, const uint16* cram, const uint16* vram)
{
 while(WQ_InCount.load(std::memory_order_acquire) != 0 || DrawCounter.load(std::memory_order_acquire) != 0)
  Time::SleepMS(0);
 for(unsigned i = 0; i < 0x100; i++)
  RegsWrite(i << 1, regs[i]);
 memcpy(VRAM, vram, sizeof(VRAM));
 memcpy(CRAM, cram, sizeof(CRAM));
 RecalcColorCache();
}
}
''')],
}
PORTS = {**SDK_PORTS, **SBL_PORTS, **MEDNAFEN_PORTS, **MEDNAFEN_VIDEO_PORTS}


def write_if_changed(path, data):
    path = Path(path)
    if isinstance(data, str):
        data = data.encode()
    if not path.exists() or path.read_bytes() != data:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def port_text(text, ports, source):
    for old, new in ports:
        if old == '@re':
            pattern, replacement = new
            text, count = re.subn(pattern, replacement, text, flags=re.M)
            if not count:
                raise SystemExit(f'{source}: port pattern not found: {pattern!r}')
            continue
        if old in ('@after', '@before'):
            anchor, insert = new
            if text.count(anchor) != 1:
                raise SystemExit(f'{source}: port anchor not found exactly once: {anchor!r}')
            text = text.replace(anchor, anchor + insert if old == '@after' else insert + anchor)
            continue
        if old == '@end':
            text = text.rstrip('\n') + '\n' + new
            continue
        if old.startswith('@all:'):
            if old[5:] not in text:
                raise SystemExit(f'{source}: port pattern not found: {old[5:]!r}')
            text = text.replace(old[5:], new)
            continue
        if old == '@function':
            signature, body = new
            start = text.index(signature + '\n{\n')
            end = text.index('\n}\n', start)
            text = text[:start] + signature + '\n{\n' + body + text[end + 1:]
            continue
        if old == '@blank':
            start = text.index(new + '\n{\n')
            end = text.index('\n}\n', start)
            text = text[:start] + new + '\n{\n' + text[end + 1:]
            continue
        if old == '@append':
            close = text.rstrip().rindex('}')
            text = text[:close] + new.lstrip('\n')
            continue
        if text.count(old) != 1:
            raise SystemExit(f'{source}: port pattern not found exactly once: {old!r}')
        text = text.replace(old, new)
    return text


def step_port(source, output, unit):
    text = Path(source).read_bytes().decode('latin-1').replace('\r\n', '\n')
    write_if_changed(output, port_text(text, PORTS.get(unit, []), source).encode('latin-1'))


def q(flags):
    return ' '.join(map(shlex.quote, map(str, flags)))


class Writer:
    def __init__(self, stream):
        self.stream = stream

    def rule(self, name, command, depfile=None, deps=None, description=None, restat=False):
        self.stream.write(f'rule {name}\n  command = {command}\n')
        for key, value in (('depfile', depfile), ('deps', deps), ('description', description)):
            if value:
                self.stream.write(f'  {key} = {value}\n')
        if restat:
            self.stream.write('  restat = 1\n')

    def build(self, outputs, rule, inputs=(), implicit=(), order_only=(), variables=None):
        esc = lambda p: str(p).replace('$', '$$').replace(' ', '$ ').replace(':', '$:')
        outputs = [outputs] if isinstance(outputs, (str, Path)) else outputs
        inputs = [inputs] if isinstance(inputs, (str, Path)) else inputs
        line = f'build {" ".join(map(esc, outputs))}: {rule} {" ".join(map(esc, inputs))}'
        if implicit:
            line += ' | ' + ' '.join(map(esc, implicit))
        if order_only:
            line += ' || ' + ' '.join(map(esc, order_only))
        self.stream.write(line + '\n')
        for key, value in (variables or {}).items():
            self.stream.write(f'  {key} = {value}\n')

    def default(self, paths):
        self.stream.write('default ' + ' '.join(map(str, paths)) + '\n')



def ram_pieces(areas, objects):
    """Splits the work-RAM areas at the named objects, sorted by address: (start, size, name or None)."""
    pieces, objects = [], sorted(objects)
    for start, size in sorted(areas):
        cursor, end = start, start + size
        for o_start, o_size, name in objects:
            if not start <= o_start < end:
                continue
            if o_start < cursor or o_start + o_size > end:
                raise SystemExit(f'{name} at {o_start:08X}+0x{o_size:X} overlaps another object or leaves its area')
            if o_start > cursor:
                pieces.append((cursor, o_start - cursor, None))
            pieces.append((o_start, o_size, name))
            cursor = o_start + o_size
        if cursor < end:
            pieces.append((cursor, end - cursor, None))
    missing = {name for _, _, name in objects} - {name for _, _, name in pieces}
    if missing:
        raise SystemExit(f'outside the work-RAM areas: {", ".join(sorted(missing))}')
    return pieces

class Build:

    def __init__(self, stream, output, video='mednafen', sound='mednafen', sanitize=False, gcc=False):
        self.n = Writer(stream)
        self.output = Path(output)
        self.video, self.sound, self.sanitize = video, sound, sanitize
        self.objects = []
        self.libs = []
        self.common = COMMON + (SANITIZE if sanitize else [])
        self.saturn_common = (GCC_COMMON + (GCC_SANITIZE if sanitize else [])) if gcc else self.common
        self.saturn_rule = 'gcc' if gcc else 'cc'
        n = self.n
        n.rule('cc', f'{CC} $flags -c $in -o $out -MMD -MF $out.d', depfile='$out.d', deps='gcc', description='cc $in')
        n.rule('gcc', f'{GCC} $flags -c $in -o $out -MMD -MF $out.d', depfile='$out.d', deps='gcc', description='gcc $in')
        n.rule('cxx', f'{CXX} $flags -c $in -o $out -MMD -MF $out.d', depfile='$out.d', deps='gcc', description='c++ $in')
        n.rule('mcxx', f'{MEDNAFEN_CXX} $flags -c $in -o $out -MMD -MF $out.d', depfile='$out.d', deps='gcc', description='c++ $in')
        n.rule('m68kcxx', f'{M68K_CXX} $flags -c $in -o $out -MMD -MF $out.d', depfile='$out.d', deps='gcc', description='c++ $in')
        n.rule('mcc', f'{MEDNAFEN_CC} $flags -c $in -o $out -MMD -MF $out.d', depfile='$out.d', deps='gcc', description='cc $in')
        n.rule('copy', 'cp $in $out', description='copy $out')
        n.rule('port', f'{shlex.quote(sys.executable)} {shlex.quote(str(Path(__file__).resolve()))} port $in $out $unit',
               description='port $in')
        sanitize = ' '.join(SANITIZE) + ' ' if sanitize else ''
        if sys.platform == 'darwin':
            n.rule('link', f'{CC} {sanitize}-Wl,-dead_strip $in -o $out -Wl,-map,$out.map $libs -lm', description='link $out')
        else:
            n.rule('link', f'{CC} {sanitize}-fuse-ld=bfd -no-pie -Wl,--gc-sections $in -o $out -Wl,-Map,$out.map $libs -lm',
                   description='link $out')

    def object_path(self, source):
        return self.output / 'obj' / (str(Path(source).resolve()).lstrip('/').replace('/', '_') + '.o')

    def cc(self, source, flags, implicit=(), order_only=(), saturn=False):
        target = self.object_path(source)
        rule, common = (self.saturn_rule, self.saturn_common) if saturn else ('cc', self.common)
        self.n.build(target, rule, source, implicit=implicit, order_only=order_only, variables={'flags': q(common + flags)})
        self.objects.append(target)
        return target

    def saturn_cc(self, source, flags=(), implicit=(), order_only=()):
        return self.cc(source, SATURN + list(flags), implicit, order_only, saturn=True)

    def host_cc(self, source, flags=()):
        return self.cc(source, HOST + list(flags))

    def port(self, source, target, unit):
        self.n.build(target, 'port', source, implicit=[Path(__file__).resolve()], variables={'unit': unit})
        return target

    def sdk_prepare(self, segalib, libraries, name):
        segalib = Path(segalib)
        work = self.output / 'sdk' / name
        files = {}
        for library in [*libraries, 'INCLUDE']:
            directory = segalib / library
            if not directory.is_dir():
                continue
            for source in sorted(directory.iterdir()):
                if source.suffix.upper() in ('.C', '.H'):
                    files[work / source.name.lower()] = source
        for target, source in files.items():
            write_if_changed(target, source.read_bytes().replace(b'\x1a', b''))
        write_if_changed(work / 'stdlib.h', STDLIB)
        write_if_changed(work / 'string.h', STRING)
        port_dir = self.output / 'sdk_port' / name
        headers = [self.port(target, port_dir / target.name, target.name) for target in files if target.suffix == '.h']
        return work, port_dir, headers

    def sdk_units(self, segalib, library_units, name='sbl'):
        segalib = Path(segalib)
        work, port_dir, headers = self.sdk_prepare(segalib, list(library_units), name)
        flags = ['-ffreestanding', '-fno-builtin', '-DSATZ=1', '-include', SATZ / 'include/satz_sdk.h',
                 '-I' + str(port_dir), '-I' + str(work)]
        if 'MTH' in library_units and (segalib / 'MTH/MTH_TRI.SRC').exists():
            write_if_changed(work / 'mth_tri_table.h', sine_table(segalib / 'MTH/MTH_TRI.SRC'))
        for library, units in library_units.items():
            for unit in units:
                if unit in SDK_REPLACED:
                    continue
                replacement = SATZ / 'sbl' / library / f'{unit}.c'
                if replacement.exists():
                    source = replacement
                else:
                    source = self.port(work / f'{unit}.c', port_dir / f'{unit}.c', unit)
                target = self.output / 'obj' / f'sdk_{unit}.o'
                self.n.build(target, self.saturn_rule, source, implicit=headers,
                             variables={'flags': q(self.saturn_common + ['-I' + str(SATZ / 'include')] + flags)})
                self.objects.append(target)
        return ['-I' + str(port_dir), '-I' + str(work)], headers

    def core(self, sdk_includes, sdk_headers, ram_areas=DEFAULT_RAM_AREAS, ram_objects=()):
        """ram_objects are (start, size, name) for objects the program defines inside ram_areas; the bus maps their
        Saturn addresses to them, and the rest of each area becomes an anonymous array."""
        for source in sorted((SATZ / 'host').glob('*.c')):
            self.host_cc(source)
        for source in sorted((SATZ / 'sbl').glob('*.c')):
            self.saturn_cc(source, sdk_includes, implicit=sdk_headers)
        work_ram = self.output / 'work_ram.c'
        pieces = ram_pieces(ram_areas, ram_objects)
        lines = ['#include "satz_host.h"', '']
        for start, size, name in pieces:
            if name:
                lines.append(f'extern uint8_t {name}[];')
            else:
                lines.append(f'uint8_t satz_ram_{start:08X}[0x{size:X}] __attribute__((aligned(16)));')
        lines += ['', 'const SatzRamArea satz_ram_areas[] = {']
        lines += [f'    {{ 0x{start:08X}u, 0x{size:X}u, {name or f"satz_ram_{start:08X}"} }},' for start, size, name in pieces]
        lines += ['};', f'const unsigned satz_ram_area_count = {len(pieces)};', '']
        write_if_changed(work_ram, '\n'.join(lines))
        self.host_cc(work_ram)
        self.backends()

    def backends(self):
        n, output = self.n, self.output
        config_dir = output / 'mednafen'
        config = config_dir / 'config.h'
        uses_mednafen = 'mednafen' in (self.sound, self.video)
        if uses_mednafen:
            n.build(config, 'copy', SATZ / 'sound/mednafen_config.h')
            target = output / 'obj' / 'mednafen_common.o'
            n.build(target, 'mcxx', SATZ / 'mednafen/mednafen_common.cpp', implicit=[config],
                    variables={'flags': q(['-O2', '-DHAVE_CONFIG_H', '-I' + str(config_dir), '-I' + str(MEDNAFEN / 'include'),
                                           '-w', '-std=gnu++17'])})
            self.objects.append(target)
            self.libs += (['-lc++'] if sys.platform == 'darwin' else ['-lstdc++', '-lpthread'])
        if self.sound == 'mednafen':
            ported = [self.port(MEDNAFEN / 'src/ss' / name, config_dir / name, name) for name in MEDNAFEN_PORTS]
            flags = ['-O2', '-g1', '-DHAVE_CONFIG_H', '-I' + str(config_dir), '-I' + str(MEDNAFEN / 'include'), '-w']
            for source, kind in [(SATZ / 'sound/sound_mednafen.cpp', 'mcxx'), (MEDNAFEN / 'src/hw_cpu/m68k/m68k.cpp', 'm68kcxx'),
                                 (MEDNAFEN / 'src/resampler/resample.c', 'mcc')]:
                target = output / 'obj' / ('mednafen_' + source.name + '.o')
                extra = ['-std=gnu++17'] if kind != 'mcc' else ['-DOUTSIDE_SPEEX', '-DRANDOM_PREFIX=mdfn', '-DFLOATING_POINT']
                n.build(target, kind, source, implicit=[config] + ported, variables={'flags': q(flags + extra)})
                self.objects.append(target)
        else:
            self.host_cc(SATZ / 'sound/sound_hle.c')
        if self.video == 'mednafen':
            ported = [self.port(MEDNAFEN / 'src/ss' / name, config_dir / name, name) for name in MEDNAFEN_VIDEO_PORTS]
            trio = [config_dir / 'trio' / h for h in ('trio.h', 'triodef.h', 'triop.h')]
            for header in trio:
                n.build(header, 'copy', MEDNAFEN / 'src/trio' / header.name)
            flags = ['-O2', '-g1', '-DHAVE_CONFIG_H', '-I' + str(config_dir), '-I' + str(MEDNAFEN / 'include'),
                     '-I' + str(MEDNAFEN / 'src/ss'), '-I' + str(SATZ / 'video'), '-w', '-std=gnu++17']
            for source in [p for p in ported if p.suffix == '.cpp'] + [SATZ / 'video/video_mednafen.cpp']:
                target = output / 'obj' / ('mednafen_' + source.name + '.o')
                n.build(target, 'mcxx', source, implicit=[config] + trio + ported, variables={'flags': q(flags)})
                self.objects.append(target)
        else:
            self.host_cc(SATZ / 'video/video_none.c')

    def link(self, binary, extra=(), window=False):
        if window:
            sdl = lambda option: shlex.split(subprocess.run(['sdl2-config', option], check=True, capture_output=True,
                                                            text=True).stdout)
            self.host_cc(SATZ / 'window/window_sdl.c', sdl('--cflags'))
            self.libs += sdl('--libs')
        else:
            self.host_cc(SATZ / 'window/window_none.c')
        self.n.build(binary, 'link', self.objects + list(extra), variables={'libs': ' '.join(dict.fromkeys(self.libs))})
        return [binary]


def sine_table(source):
    text = Path(source).read_bytes().decode('latin-1')
    rows = []
    for line in text[text.index('SINTBL:'):].splitlines()[1:]:
        match = re.match(r"\s*\.DATA\.W\s+H'([0-9A-Fa-f]+),\s*H'([0-9A-Fa-f]+)", line)
        if match:
            rows.append(f'    {{ 0x{match.group(1)}, 0x{match.group(2)} }},')
        elif rows and line.strip() and not line.strip().startswith(';'):
            break
    if len(rows) != 91:
        raise SystemExit(f'{source}: expected 91 SINTBL rows, found {len(rows)}')
    return 'static const unsigned short mth_sin_table[91][2] = {\n' + '\n'.join(rows) + '\n};\n'


def ninja(output, jobs=os.cpu_count(), keep_going=1):
    result = subprocess.run(['ninja', '-f', str(Path(output) / 'build.ninja'), '-j', str(jobs), '-k', str(keep_going)])
    if result.returncode:
        raise SystemExit(result.returncode)


def build_samples(args):
    output = args.output.resolve()
    (output / 'obj').mkdir(parents=True, exist_ok=True)
    samples = sorted(p for p in (SATZ / 'samples').iterdir() if (p / 'sample.txt').exists())
    with (output / 'build.ninja').open('w') as stream:
        defaults = []
        for sample in samples:
            sub = output / sample.name
            (sub / 'obj').mkdir(parents=True, exist_ok=True)
            with (sub / 'build.ninja').open('w') as substream:
                build = Build(substream, sub, video=args.video, sound=args.sound, sanitize=args.sanitize, gcc=True)
                libraries = {}
                for line in (sample / 'sample.txt').read_text().splitlines():
                    if line.strip() and not line.startswith('#'):
                        library, *units = line.split()
                        libraries[library] = units
                includes, headers = build.sdk_units(args.sbl, libraries)
                build.core(includes, headers)
                for source in sorted(sample.glob('*.c')):
                    build.saturn_cc(source, includes + ['-Dmain=satz_saturn_main'], implicit=headers)
                defaults += build.link(sub / sample.name, window=args.window)
            stream.write(f'subninja {sub / "build.ninja"}\n')
        Writer(stream).default(defaults)
    ninja(output, args.jobs)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'port':
        step_port(Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4])
        return
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    samples = sub.add_parser('samples', help='build samples/*')
    samples.add_argument('--sbl', type=Path, default=SATZ / 'third_party/saturn-sdk/dts-1996-11/fs/LIBRARY/SBL6/SEGALIB',
                         help='SBL SEGALIB directory with library sources (SCL/, SPR/, ...)')
    samples.add_argument('--output', type=Path, default=Path('build/satz-samples'))
    samples.add_argument('--video', choices=['mednafen', 'none'], default='mednafen')
    samples.add_argument('--sound', choices=['mednafen', 'hle'], default='hle')
    samples.add_argument('--sanitize', action='store_true')
    samples.add_argument('--window', action='store_true')
    samples.add_argument('--jobs', type=int, default=os.cpu_count())
    args = parser.parse_args()
    build_samples(args)


if __name__ == '__main__':
    main()

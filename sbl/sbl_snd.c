#include "sega_xpt.h"
#include "sega_per.h"
#include "sega_snd.h"


#define SOUND_RAM 0x25A00000ul
#define SCSP_REG 0x25B00400ul
#define SYS_TBL (SOUND_RAM + 0x400)
#define SOUND_RAM_CLEAR 0xB000

#define SYS_INFO 0x00
#define HOST_INT 0x04
#define AREA_CURRENT 0x08
#define SYS_INT_WORK 0x12
#define AREA_ADDRESS 0x08
#define SONG_STAT 0x80
#define PCM_STAT 0xA0

#define COMMAND_BLOCK 0x10
#define COMMAND_BLOCKS 8

#define COM_START_SEQ 0x01
#define COM_STOP_SEQ 0x02
#define COM_PAUSE_SEQ 0x03
#define COM_CONT_SEQ 0x04
#define COM_SET_SEQ_VL 0x05
#define COM_CHG_MAP 0x08
#define COM_STOP_DSP 0x0C
#define COM_OFF_ALL_SOUND 0x0D
#define COM_SET_SEQ_PAN 0x0E
#define COM_SET_TL_VL 0x82
#define COM_START_PCM 0x85
#define COM_STOP_PCM 0x86
#define COM_CHG_MIX_PRM 0x88
#define COM_CHG_PCM_PRM 0x8A

typedef struct {
    SndPcmChgPrm prm;
    SndLev t_lev;
} SndPcmChgPrmTL;

Uint8* snd_adr_sys_int_work;
Uint32 snd_msk_work_work;

static unsigned long sys_info;
static unsigned long host_int_work;
static unsigned long area_current;
static unsigned long command_block;
static int busy;

static unsigned int peek(unsigned long address, int size) { return satz_bus_read(address, size); }
static void poke(unsigned long address, unsigned int value, int size) { satz_bus_write(address, value, size); }

static Uint8 pan_bits(SndPan pan) { return (Uint8)(pan < 0 ? ~pan + 0x10 + 1 : pan); }

static int next_command_block(void)
{
    unsigned long end = host_int_work + COMMAND_BLOCK * COMMAND_BLOCKS - COMMAND_BLOCK;
    if (peek(command_block, 1)) {
        if (command_block >= end)
            return 0;
        command_block += COMMAND_BLOCK;
    } else {
        command_block = host_int_work;
    }
    for (; command_block < end; command_block += COMMAND_BLOCK)
        if (!peek(command_block, 1))
            return 1;
    return 0;
}

static int begin(void)
{
    if (busy)
        return 0;
    busy = 1;
    if (next_command_block())
        return 1;
    busy = 0;
    return 0;
}

static void param(int index, unsigned int value) { poke(command_block + 2 + index, (Uint8)value, 1); }

static SndRet send(Uint8 command)
{
    poke(command_block, (Uint16)(command << 8), 2);
    busy = 0;
    return SND_RET_SET;
}

static void copy_words(unsigned long address, const Uint16* words, Uint32 bytes)
{
    for (Uint32 i = 0; i < bytes / 2; ++i)
        poke(address + i * 2, words[i], 2);
}

void SND_Init(SndIniDt* ini)
{
    static const Uint8 zero[0x400];
    PER_SMPC_SND_OFF();
    poke(SCSP_REG, 0x0200, 2);
    for (Uint32 done = 0; done < SOUND_RAM_CLEAR; done += sizeof(zero))
        satz_bus_store(SOUND_RAM + done, zero, sizeof(zero));
    copy_words(SOUND_RAM, SND_INI_PRG_ADR(*ini), SND_INI_PRG_SZ(*ini));
    sys_info = SOUND_RAM + peek(SYS_TBL + SYS_INFO, 4);
    host_int_work = SOUND_RAM + peek(SYS_TBL + HOST_INT, 4);
    snd_adr_sys_int_work =
        (Uint8*)(SOUND_RAM + (peek(SYS_TBL + SYS_INT_WORK, 2) << 16 | peek(SYS_TBL + SYS_INT_WORK + 2, 2)));
    command_block = host_int_work;
    area_current = SOUND_RAM + peek(SYS_TBL + AREA_CURRENT, 4);
    copy_words(SOUND_RAM + peek(sys_info + AREA_ADDRESS, 4), SND_INI_ARA_ADR(*ini),
               SND_INI_ARA_SZ(*ini) * 2 + (4 - SND_INI_ARA_SZ(*ini) * 2 % 4));
    busy = 0;
    PER_SMPC_SND_ON();
}

SndRet SND_ChgMap(SndAreaMap area)
{
    if (!begin())
        return SND_RET_NSET;
    for (int i = 0; i < 2; ++i) {
        if (i && !next_command_block()) {
            busy = 0;
            return SND_RET_NSET;
        }
        param(0, area);
        poke(command_block, COM_CHG_MAP << 8, 2);
        while (peek(command_block, 2))
            ;
    }
    busy = 0;
    return SND_RET_SET;
}

SndRet SND_SetTlVl(SndTlVl volume)
{
    if (!begin())
        return SND_RET_NSET;
    param(0, volume);
    return send(COM_SET_TL_VL);
}

SndRet SND_ChgMixPrm(SndEfctOut out, SndLev level, SndPan pan)
{
    if (!begin())
        return SND_RET_NSET;
    param(0, out);
    param(1, pan_bits(pan) | level << 5);
    return send(COM_CHG_MIX_PRM);
}

SndRet SND_StopDsp(void)
{
    if (!begin())
        return SND_RET_NSET;
    return send(COM_STOP_DSP);
}

SndRet SND_OffAllSound(void)
{
    if (!begin())
        return SND_RET_NSET;
    return send(COM_OFF_ALL_SOUND);
}

SndRet SND_StartSeq(SndSeqNum seq, SndSeqBnkNum bank, SndSeqSongNum song, SndSeqPri priority)
{
    if (!begin())
        return SND_RET_NSET;
    param(0, seq);
    param(1, bank);
    param(2, song);
    param(3, priority);
    return send(COM_START_SEQ);
}

static SndRet seq_command(Uint8 command, SndSeqNum seq)
{
    if (!begin())
        return SND_RET_NSET;
    param(0, seq);
    return send(command);
}

SndRet SND_StopSeq(SndSeqNum seq) { return seq_command(COM_STOP_SEQ, seq); }
SndRet SND_PauseSeq(SndSeqNum seq) { return seq_command(COM_PAUSE_SEQ, seq); }
SndRet SND_ContSeq(SndSeqNum seq) { return seq_command(COM_CONT_SEQ, seq); }

SndRet SND_SetSeqVl(SndSeqNum seq, SndSeqVl volume, SndFade fade)
{
    if (!begin())
        return SND_RET_NSET;
    param(0, seq);
    param(1, volume);
    param(2, fade);
    return send(COM_SET_SEQ_VL);
}

SndRet SND_SetSeqPan(SndSeqNum seq, Uint8 ctrl_sw, Uint8 md_pan)
{
    if (!begin())
        return SND_RET_NSET;
    param(0, seq);
    param(1, ctrl_sw | md_pan);
    return send(COM_SET_SEQ_PAN);
}

static void pcm_params(int index, const SndPcmChgPrm* prm)
{
    param(index, SND_PRM_PICH(*prm) >> 8);
    param(index + 1, SND_PRM_PICH(*prm));
    param(index + 2, SND_R_EFCT_IN(*prm) << 3 | SND_R_EFCT_LEV(*prm));
    param(index + 3, SND_L_EFCT_IN(*prm) << 3 | SND_L_EFCT_LEV(*prm));
}

SndRet SND_StartPcmTL(SndPcmStartPrm* start, SndPcmChgPrmTL* tl)
{
    SndPcmChgPrm* prm = &tl->prm;
    if (!begin())
        return SND_RET_NSET;
    param(0, SND_PRM_MODE(*start) | SND_PRM_NUM(*prm));
    param(1, SND_PRM_LEV(*prm) << 5 | pan_bits(SND_PRM_PAN(*prm)));
    param(2, SND_PRM_SADR(*start) >> 8);
    param(3, SND_PRM_SADR(*start));
    param(4, SND_PRM_SIZE(*start) >> 8);
    param(5, SND_PRM_SIZE(*start));
    pcm_params(6, prm);
    param(10, tl->t_lev);
    param(11, 0);
    return send(COM_START_PCM);
}

SndRet SND_StopPcm(SndPcmNum pcm)
{
    if (!begin())
        return SND_RET_NSET;
    param(0, pcm);
    return send(COM_STOP_PCM);
}

SndRet SND_ChgPcmTL(SndPcmChgPrmTL* tl)
{
    SndPcmChgPrm* prm = &tl->prm;
    if (!begin())
        return SND_RET_NSET;
    param(0, SND_PRM_NUM(*prm));
    param(1, SND_PRM_LEV(*prm) << 5 | pan_bits(SND_PRM_PAN(*prm)));
    pcm_params(2, prm);
    param(6, tl->t_lev);
    return send(COM_CHG_PCM_PRM);
}

void SND_GetSeqStat(SndSeqStat* status, SndSeqNum seq)
{
    unsigned long stat = host_int_work + SONG_STAT + seq * 2;
    SND_SEQ_STAT_MODE(*status) = (Uint16)peek(stat, 2) & 0xFF00;
    SND_SEQ_STAT_STAT(*status) = (Uint8)peek(stat, 2);
}

void SND_GetPcmPlayAdr(SndPcmPlayAdr* address, SndPcmNum pcm)
{
    unsigned long play = host_int_work + PCM_STAT + pcm * 2;
    SND_PCM_RADR(*address) = (Uint8)(peek(play, 2) >> 8);
    SND_PCM_LADR(*address) = (Uint8)peek(play, 2);
}

#define SND_UNPORTED(name) satz_die("%s: not ported to satz", name)

SndRet SND_ChgEfct(SndEfctBnkNum bank) { SND_UNPORTED("SND_ChgEfct"); }
SndRet SND_ChgMix(SndToneBnkNum tone, SndMixBnkNum mix) { SND_UNPORTED("SND_ChgMix"); }
SndRet SND_StartPcm(SndPcmStartPrm* start, SndPcmChgPrm* prm) { SND_UNPORTED("SND_StartPcm"); }
SndRet SND_ChgPcm(SndPcmChgPrm* prm) { SND_UNPORTED("SND_ChgPcm"); }
void SND_MoveData(Uint16* data, Uint32 size, Uint16 kind, Uint16 number) { SND_UNPORTED("SND_MoveData"); }
SndRet SND_ChkHard(SndHardStat* status, SndHardPrm prm) { SND_UNPORTED("SND_ChkHard"); }
SndRet SND_ChgTempo(SndSeqNum seq, SndTempo tempo) { SND_UNPORTED("SND_ChgTempo"); }
SndRet SND_CtrlDirMidi(SndSeqNum seq, SndSeqPri pri, Uint8 a, Uint8 b, Uint8 c, Uint8 d) { SND_UNPORTED("SND_CtrlDirMidi"); }
SndRet SND_StartVlAnl(void) { SND_UNPORTED("SND_StartVlAnl"); }
SndRet SND_StopVlAnl(void) { SND_UNPORTED("SND_StopVlAnl"); }
SndRet SND_SetCdDaLev(SndLev left, SndLev right) { SND_UNPORTED("SND_SetCdDaLev"); }
SndRet SND_SetCdDaPan(SndPan left, SndPan right) { SND_UNPORTED("SND_SetCdDaPan"); }
void SND_GetSeqPlayPos(SndSeqPlayPos* position, SndSeqNum seq) { SND_UNPORTED("SND_GetSeqPlayPos"); }
void SND_GetAnlTlVl(SndCdVlAnl* left, SndCdVlAnl* right) { SND_UNPORTED("SND_GetAnlTlVl"); }
void SND_GetAnlHzVl(SndCdHzSrVl* volume) { SND_UNPORTED("SND_GetAnlHzVl"); }

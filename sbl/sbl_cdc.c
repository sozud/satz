#include "sega_xpt.h"
#include "sega_cdc.h"
#include "../host/cdblock.h"

void* satz_bus_pointer(unsigned long address);

static Sint32 status(Sint32 command, CdcStat* stat)
{
    Sint32 ret = cdb_cmd(command, 0, 0, 0, 0);
    stat->status = (Uint8)cdb_result(0);
    stat->report.flgrep = (Uint8)cdb_result(1);
    stat->report.ctladr = (Uint8)cdb_result(2);
    stat->report.tno = (Uint8)cdb_result(3);
    stat->report.idx = (Uint8)cdb_result(4);
    stat->report.fad = cdb_result(5);
    satz_charge(64);
    return ret;
}

Sint32 CDC_GetCurStat(CdcStat* stat) { return status(CDB_GETCURSTAT, stat); }
Sint32 CDC_GetLastStat(CdcStat* stat) { return status(CDB_GETLASTSTAT, stat); }
Sint32 CDC_GetPeriStat(CdcStat* stat) { return status(CDB_GETPERISTAT, stat); }

Sint32 CDC_GetHwInfo(CdcHw* hw)
{
    Sint32 ret = cdb_cmd(CDB_GETHWINFO, 0, 0, 0, 0);
    hw->hflag = (Uint8)cdb_result(0);
    hw->ver = (Uint8)cdb_result(1);
    hw->mpver = (Uint8)cdb_result(2);
    hw->drv = (Uint8)cdb_result(3);
    hw->rev = (Uint8)cdb_result(4);
    return ret;
}

Sint32 CDC_TgetToc(Uint32* toc)
{
    for (int i = 0; i < 102; ++i)
        toc[i] = cdb_toc(i);
    return CDC_ERR_OK;
}

Sint32 CDC_GetSes(Sint32 sesno, Uint32* ses)
{
    Sint32 ret = cdb_cmd(CDB_GETSES, sesno, 0, 0, 0);
    *ses = (Uint32)cdb_result(0);
    return ret;
}

Sint32 CDC_CdInit(Sint32 iflag, Sint32 stnby, Sint32 ecc, Sint32 retry) { return cdb_cmd(CDB_CDINIT, iflag, stnby, ecc, retry); }
Sint32 CDC_CdOpen(void) { return cdb_cmd(CDB_CDOPEN, 0, 0, 0, 0); }
Sint32 CDC_DataReady(Sint32 dtype) { return cdb_cmd(CDB_DATAREADY, dtype, 0, 0, 0); }

Sint32 CDC_DataEnd(Sint32* cdwnum)
{
    Sint32 ret = cdb_cmd(CDB_DATAEND, 0, 0, 0, 0);
    *cdwnum = cdb_result(0);
    return ret;
}

static Sint32 position(CdcPos* pos)
{
    if (pos->ptype == CDC_PTYPE_TNO)
        return (pos->pbody.trkidx.tno << 8) | pos->pbody.trkidx.idx;
    return pos->pbody.fad;
}

Sint32 CDC_CdPlay(CdcPly* ply)
{
    Sint32 a = (ply->start.ptype & 0xFF) | ((ply->end.ptype & 0xFF) << 8) | ((ply->pmode & 0xFF) << 16);
    return cdb_cmd(CDB_CDPLAY, a, position(&ply->start), position(&ply->end), 0);
}

Sint32 CDC_CdSeek(CdcPos* pos) { return cdb_cmd(CDB_CDSEEK, pos->ptype, position(pos), 0, 0); }
Sint32 CDC_CdScan(Sint32 scandir) { (void)scandir; return CDC_ERR_OK; }
Sint32 CDC_CdSetCon(Sint32 filtno) { return cdb_cmd(CDB_CDSETCON, filtno, 0, 0, 0); }

Sint32 CDC_CdGetCon(Sint32* filtno)
{
    Sint32 ret = cdb_cmd(CDB_CDGETCON, 0, 0, 0, 0);
    *filtno = cdb_result(0);
    return ret;
}

Sint32 CDC_CdGetLastBuf(Sint32* bufno)
{
    Sint32 ret = cdb_cmd(CDB_CDGETLASTBUF, 0, 0, 0, 0);
    *bufno = cdb_result(0);
    return ret;
}

Sint32 CDC_SetFiltRange(Sint32 filtno, Sint32 fad, Sint32 fasnum) { return cdb_cmd(CDB_SETFILTRANGE, filtno, fad, fasnum, 0); }

Sint32 CDC_GetFiltRange(Sint32 filtno, Sint32* fad, Sint32* fasnum)
{
    Sint32 ret = cdb_cmd(CDB_GETFILTRANGE, filtno, 0, 0, 0);
    *fad = cdb_result(0);
    *fasnum = cdb_result(1);
    return ret;
}

Sint32 CDC_SetFiltSubh(Sint32 filtno, CdcSubh* subh)
{
    return cdb_cmd(CDB_SETFILTSUBH, filtno, (subh->fn << 8) | subh->cn, (subh->smmsk << 8) | subh->smval,
        (subh->cimsk << 8) | subh->cival);
}

Sint32 CDC_GetFiltSubh(Sint32 filtno, CdcSubh* subh)
{
    Sint32 ret = cdb_cmd(CDB_GETFILTSUBH, filtno, 0, 0, 0);
    subh->fn = (Uint8)cdb_result(0);
    subh->cn = (Uint8)cdb_result(1);
    subh->smmsk = (Uint8)cdb_result(2);
    subh->smval = (Uint8)cdb_result(3);
    subh->cimsk = (Uint8)cdb_result(4);
    subh->cival = (Uint8)cdb_result(5);
    return ret;
}

Sint32 CDC_SetFiltMode(Sint32 filtno, Sint32 fmode) { return cdb_cmd(CDB_SETFILTMODE, filtno, fmode, 0, 0); }

Sint32 CDC_GetFiltMode(Sint32 filtno, Sint32* fmode)
{
    Sint32 ret = cdb_cmd(CDB_GETFILTMODE, filtno, 0, 0, 0);
    *fmode = cdb_result(0);
    return ret;
}

Sint32 CDC_SetFiltCon(Sint32 filtno, Sint32 cflag, Sint32 bufno, Sint32 flnout) { return cdb_cmd(CDB_SETFILTCON, filtno, cflag, bufno, flnout); }

Sint32 CDC_GetFiltCon(Sint32 filtno, Sint32* bufno, Sint32* flnout)
{
    Sint32 ret = cdb_cmd(CDB_GETFILTCON, filtno, 0, 0, 0);
    *bufno = cdb_result(0);
    *flnout = cdb_result(1);
    return ret;
}

Sint32 CDC_ResetSelector(Sint32 rflag, Sint32 bufno) { return cdb_cmd(CDB_RESETSEL, rflag, bufno, 0, 0); }

Sint32 CDC_GetBufSiz(Sint32* totalsiz, Sint32* bufnum, Sint32* freesiz)
{
    Sint32 ret = cdb_cmd(CDB_GETBUFSIZ, 0, 0, 0, 0);
    *totalsiz = cdb_result(0);
    *bufnum = cdb_result(1);
    *freesiz = cdb_result(2);
    return ret;
}

Sint32 CDC_GetSctNum(Sint32 bufno, Sint32* snum)
{
    Sint32 ret = cdb_cmd(CDB_GETSCTNUM, bufno, 0, 0, 0);
    *snum = cdb_result(0);
    satz_charge(64);
    return ret;
}

Sint32 CDC_CalActSiz(Sint32 bufno, Sint32 spos, Sint32 snum) { return cdb_cmd(CDB_CALACTSIZ, bufno, spos, snum, 0); }

Sint32 CDC_GetActSiz(Sint32* actwnum)
{
    Sint32 ret = cdb_cmd(CDB_GETACTSIZ, 0, 0, 0, 0);
    *actwnum = cdb_result(0);
    return ret;
}

Sint32 CDC_GetSctInfo(Sint32 bufno, Sint32 spos, CdcSct* sct)
{
    Sint32 ret = cdb_cmd(CDB_GETSCTINFO, bufno, spos, 0, 0);
    sct->fad = cdb_result(0);
    sct->fn = (Uint8)cdb_result(1);
    sct->cn = (Uint8)cdb_result(2);
    sct->sm = (Uint8)cdb_result(3);
    sct->ci = (Uint8)cdb_result(4);
    return ret;
}

Sint32 CDC_SetSctLen(Sint32 getslen, Sint32 putslen) { return cdb_cmd(CDB_SETSCTLEN, getslen, putslen, 0, 0); }
Sint32 CDC_GetSctData(Sint32 bufno, Sint32 spos, Sint32 snum) { return cdb_cmd(CDB_GETSCTDATA, bufno, spos, snum, 0); }
Sint32 CDC_DelSctData(Sint32 bufno, Sint32 spos, Sint32 snum) { return cdb_cmd(CDB_DELSCTDATA, bufno, spos, snum, 0); }
Sint32 CDC_GetdelSctData(Sint32 bufno, Sint32 spos, Sint32 snum) { return cdb_cmd(CDB_GETDELSCTDATA, bufno, spos, snum, 0); }
Sint32 CDC_PutSctData(Sint32 filtno, Sint32 snum) { return cdb_cmd(CDB_PUTSCTDATA, filtno, snum, 0, 0); }
Sint32 CDC_CopySctData(Sint32 srcbn, Sint32 spos, Sint32 snum, Sint32 dstfln) { return cdb_cmd(CDB_COPYSCTDATA, srcbn, spos, snum, dstfln); }
Sint32 CDC_MoveSctData(Sint32 srcbn, Sint32 spos, Sint32 snum, Sint32 dstfln) { return cdb_cmd(CDB_MOVESCTDATA, srcbn, spos, snum, dstfln); }

Sint32 CDC_GetCopyErr(Sint32* cpyerr)
{
    Sint32 ret = cdb_cmd(CDB_GETCOPYERR, 0, 0, 0, 0);
    *cpyerr = cdb_result(0);
    return ret;
}

Sint32 CDC_ChgDir(Sint32 filtno, Sint32 fid) { return cdb_cmd(CDB_CHGDIR, filtno, fid, 0, 0); }
Sint32 CDC_ReadDir(Sint32 filtno, Sint32 fid) { return cdb_cmd(CDB_READDIR, filtno, fid, 0, 0); }

Sint32 CDC_GetFileScope(Sint32* fid, Sint32* infnum, Bool* drend)
{
    Sint32 ret = cdb_cmd(CDB_GETFILESCOPE, 0, 0, 0, 0);
    *fid = cdb_result(0);
    *infnum = cdb_result(1);
    *drend = cdb_result(2);
    return ret;
}

Sint32 CDC_TgetFileInfo(Sint32 fid, CdcFile* file)
{
    Sint32 ret = cdb_cmd(CDB_TGETFILEINFO, fid, 0, 0, 0);
    file->fad = cdb_result(0);
    file->size = cdb_result(1);
    file->unit = (Uint8)cdb_result(2);
    file->gap = (Uint8)cdb_result(3);
    file->fn = (Uint8)cdb_result(4);
    file->atr = (Uint8)cdb_result(5);
    return ret;
}

Sint32 CDC_ReadFile(Sint32 filtno, Sint32 fid, Sint32 offset) { return cdb_cmd(CDB_READFILE, filtno, fid, offset, 0); }
Sint32 CDC_AbortFile(void) { return cdb_cmd(CDB_ABORTFILE, 0, 0, 0, 0); }
Uint32* CDC_GetDataPtr(void) { return (Uint32*)satz_bus_pointer(0x25818000); }

Sint32 CDC_GetHirqReq(void)
{
    satz_charge(64);
    return (Sint32)cdb_hirq();
}

void CDC_ClrHirqReq(Sint32 bitpat) { cdb_hirq_clear((Uint32)bitpat); }
Sint32 CDC_GetHirqMsk(void) { return 0; }
void CDC_SetHirqMsk(Sint32 bitpat) { (void)bitpat; }
Uint32* CDC_GetMpegPtr(void) { return (Uint32*)satz_bus_pointer(0x25818000); }

#ifndef SATZ_CDBLOCK_H
#define SATZ_CDBLOCK_H
#include <stdint.h>

enum {
    CDB_GETCURSTAT, CDB_GETLASTSTAT, CDB_GETPERISTAT, CDB_GETSES, CDB_CDINIT, CDB_DATAREADY, CDB_DATAEND,
    CDB_CDPLAY, CDB_CDSEEK, CDB_CDSETCON, CDB_CDGETCON, CDB_SETFILTRANGE, CDB_GETFILTRANGE, CDB_SETFILTSUBH,
    CDB_GETFILTSUBH, CDB_SETFILTMODE, CDB_GETFILTMODE, CDB_SETFILTCON, CDB_GETFILTCON, CDB_RESETSEL,
    CDB_GETBUFSIZ, CDB_GETSCTNUM, CDB_CALACTSIZ, CDB_GETACTSIZ, CDB_GETSCTINFO, CDB_SETSCTLEN, CDB_GETSCTDATA,
    CDB_DELSCTDATA, CDB_GETDELSCTDATA, CDB_MOVESCTDATA, CDB_COPYSCTDATA, CDB_CHGDIR, CDB_READDIR,
    CDB_GETFILESCOPE, CDB_TGETFILEINFO, CDB_READFILE, CDB_ABORTFILE, CDB_CDOPEN, CDB_PUTSCTDATA,
    CDB_GETCOPYERR, CDB_CDGETLASTBUF, CDB_GETHWINFO
};

int cdb_open(const char* cue);
int32_t cdb_cmd(int command, int32_t a, int32_t b, int32_t c, int32_t d);
int32_t cdb_result(int index);
uint32_t cdb_toc(int index);
uint32_t cdb_hirq(void);
void cdb_hirq_clear(uint32_t keep);
uint32_t cdb_data_read(int bytes);
int cdb_data_pending(void);
void cdb_advance(uint64_t from, uint64_t to);
uint64_t cdb_next_event(uint64_t now);
int cdb_read_sector(int32_t fad, uint8_t* out2048);
int cdb_stream_phase_hint(void);

#endif

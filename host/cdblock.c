#include "satz_host.h"
#include "cdblock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CYCLES_PER_SECOND (1820ull * 263ull * 60ull)
#define DATA_SECTOR_CYCLES (CYCLES_PER_SECOND / 150)
#define AUDIO_SECTOR_CYCLES (CYCLES_PER_SECOND / 75)
#define DRIVE_HZ 11289600ull
#define DRIVE_SECTOR (DRIVE_HZ / 150)

static uint64_t seek_cycles(int32_t from, int32_t to)
{
    int64_t delta = (int64_t)to - from;
    uint64_t ticks = 500 + 12 * DRIVE_SECTOR + (uint64_t)(delta < 0 ? -delta : delta) * (delta < 0 ? 28 : 26);
    if (delta < 0 || delta >= 150)
        ticks += DRIVE_SECTOR;
    return ticks * CYCLES_PER_SECOND / DRIVE_HZ;
}
#define POOL 200
#define SELECTORS 24
#define NONE 0xFF

enum { ST_BUSY = 0, ST_PAUSE = 1, ST_STANDBY = 2, ST_PLAY = 3, ST_SEEK = 4 };
enum { HIRQ_CMOK = 1, HIRQ_DRDY = 2, HIRQ_CSCT = 4, HIRQ_BFUL = 8, HIRQ_PEND = 0x10, HIRQ_DCHG = 0x20,
    HIRQ_ESEL = 0x40, HIRQ_EHST = 0x80, HIRQ_ECPY = 0x100, HIRQ_EFLS = 0x200, HIRQ_SCDQ = 0x400 };

typedef struct {
    FILE* file;
    int32_t start_fad;
    int32_t sectors;
    int32_t index1_offset;
    int audio;
} Track;

typedef struct {
    int32_t fad;
    uint8_t fn, cn, sm, ci;
    uint8_t raw[2352];
} Sector;

typedef struct {
    int32_t fad, range;
    uint8_t mode, fn, cn, smmsk, smval, cimsk, cival;
    uint8_t true_conn, false_conn;
} Filter;

typedef struct {
    int count;
    int items[POOL];
} Partition;

typedef struct {
    int32_t fad;
    int32_t size;
    uint8_t unit, gap, fn, atr;
    int32_t parent_fad, parent_size;
} FileEntry;

static Track tracks[99];
static int track_count;
static int32_t leadout_fad;
static Sector pool[POOL];
static uint8_t pool_used[POOL];
static Filter filters[SELECTORS];
static Partition parts[SELECTORS];
static int cd_filter = NONE;
static uint32_t hirq = HIRQ_CMOK | HIRQ_ESEL | HIRQ_EHST | HIRQ_ECPY | HIRQ_EFLS;
static int32_t results[16];
static int status = ST_PAUSE;
static int32_t cur_fad = 150;
static int32_t play_start, play_end;
static int play_repeat, play_repeat_max;
static uint64_t next_sector_at, next_scdq_at;
static int get_sector_length = 2048, put_sector_length = 2048;
static int32_t act_size;
static uint8_t* fifo;
static size_t fifo_len, fifo_pos;
static int transfer_part = -1, transfer_spos, transfer_snum, transfer_delete;
static int transfer_active;
static FileEntry dir_entries[1024];
static int dir_count;
static FileEntry dir_self, dir_parent;
static int32_t root_fad, root_size;
static int32_t cur_dir_fad, cur_dir_size;
static uint64_t now_cycles;

static Track* track_for(int32_t fad)
{
    for (int i = 0; i < track_count; ++i)
        if (fad >= tracks[i].start_fad && fad < tracks[i].start_fad + tracks[i].sectors)
            return &tracks[i];
    return NULL;
}

static int read_raw(int32_t fad, uint8_t* out)
{
    Track* t = track_for(fad);
    if (!t) {
        memset(out, 0, 2352);
        return 0;
    }
    if (fseek(t->file, (long)(fad - t->start_fad) * 2352L, SEEK_SET) || fread(out, 1, 2352, t->file) != 2352) {
        memset(out, 0, 2352);
        return 0;
    }
    return 1;
}

int cdb_read_sector(int32_t fad, uint8_t* out2048)
{
    uint8_t raw[2352];
    int ok = read_raw(fad, raw);
    memcpy(out2048, raw + 16, 2048);
    return ok;
}

static int32_t msf_to_frames(const char* text)
{
    int m, s, f;
    if (sscanf(text, "%d:%d:%d", &m, &s, &f) != 3)
        return 0;
    return (m * 60 + s) * 75 + f;
}

static void load_directory(int32_t fad, int32_t size, FileEntry* self, FileEntry* parent, FileEntry* out, int* count)
{
    *count = 0;
    uint8_t sector[2048];
    int32_t sectors = (size + 2047) / 2048;
    for (int32_t s = 0; s < sectors; ++s) {
        cdb_read_sector(fad + s, sector);
        int off = 0;
        while (off < 2048) {
            int len = sector[off];
            if (!len)
                break;
            const uint8_t* r = sector + off;
            FileEntry e;
            e.fad = (int32_t)(((uint32_t)r[6] << 24) | ((uint32_t)r[7] << 16) | ((uint32_t)r[8] << 8) | r[9]) + 150;
            e.size = (int32_t)(((uint32_t)r[14] << 24) | ((uint32_t)r[15] << 16) | ((uint32_t)r[16] << 8) | r[17]);
            e.unit = r[26];
            e.gap = r[27];
            e.fn = 0;
            e.atr = (r[25] & 2) ? 0x02 : 0x00;
            int name_len = r[32];
            if (name_len == 1 && r[33] == 0)
                *self = e;
            else if (name_len == 1 && r[33] == 1)
                *parent = e;
            else if (*count < 1024)
                out[(*count)++] = e;
            off += len;
        }
    }
}

int cdb_open(const char* cue)
{
    FILE* f = fopen(cue, "r");
    if (!f)
        return -1;
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", cue);
    char* slash = strrchr(dir, '/');
    if (slash)
        slash[1] = 0;
    else
        dir[0] = 0;
    char line[1024];
    FILE* current = NULL;
    int32_t file_base = 150, file_sectors = 0;
    Track* t = NULL;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!strncmp(p, "FILE", 4)) {
            char* a = strchr(p, '"');
            char* b = a ? strchr(a + 1, '"') : NULL;
            if (!a || !b)
                return -2;
            *b = 0;
            char path[2048];
            snprintf(path, sizeof(path), "%s%s", a[1] == '/' ? "" : dir, a + 1);
            if (current)
                file_base += file_sectors;
            current = fopen(path, "rb");
            if (!current)
                return -3;
            fseek(current, 0, SEEK_END);
            file_sectors = (int32_t)(ftell(current) / 2352);
        } else if (!strncmp(p, "TRACK", 5)) {
            t = &tracks[track_count++];
            t->file = current;
            t->start_fad = file_base;
            t->sectors = file_sectors;
            t->index1_offset = 0;
            t->audio = strstr(p, "AUDIO") != NULL;
        } else if (!strncmp(p, "INDEX 01", 8) && t) {
            t->index1_offset = msf_to_frames(p + 9);
        }
    }
    fclose(f);
    leadout_fad = file_base + file_sectors;
    uint8_t pvd[2048];
    cdb_read_sector(166, pvd);
    if (memcmp(pvd + 1, "CD001", 5))
        return -4;
    const uint8_t* root = pvd + 156;
    root_fad = (int32_t)(((uint32_t)root[6] << 24) | ((uint32_t)root[7] << 16) | ((uint32_t)root[8] << 8) | root[9]) + 150;
    root_size = (int32_t)(((uint32_t)root[14] << 24) | ((uint32_t)root[15] << 16) | ((uint32_t)root[16] << 8) | root[17]);
    fifo = malloc(POOL * 2352);
    for (int i = 0; i < SELECTORS; ++i) {
        filters[i].true_conn = (uint8_t)i;
        filters[i].false_conn = NONE;
    }
    return 0;
}

static int free_sectors(void)
{
    int n = 0;
    for (int i = 0; i < POOL; ++i)
        n += !pool_used[i];
    return n;
}

static int alloc_sector(void)
{
    for (int i = 0; i < POOL; ++i)
        if (!pool_used[i]) {
            pool_used[i] = 1;
            return i;
        }
    return -1;
}

static void part_clear(int p)
{
    for (int i = 0; i < parts[p].count; ++i)
        pool_used[parts[p].items[i]] = 0;
    parts[p].count = 0;
}

static int filter_pass(const Filter* f, const Sector* s)
{
    if ((f->mode & 0x40) && (s->fad < f->fad || s->fad >= f->fad + f->range))
        return 0;
    int sub = 1;
    if ((f->mode & 0x01) && s->fn != f->fn)
        sub = 0;
    if ((f->mode & 0x02) && s->cn != f->cn)
        sub = 0;
    if ((f->mode & 0x04) && (s->sm & f->smmsk) != f->smval)
        sub = 0;
    if ((f->mode & 0x08) && (s->ci & f->cimsk) != f->cival)
        sub = 0;
    if (f->mode & 0x10)
        sub = !sub;
    return sub;
}

static void route_sector(int index)
{
    int filter = cd_filter;
    for (int depth = 0; depth < SELECTORS && filter != NONE; ++depth) {
        Filter* f = &filters[filter];
        if (filter_pass(f, &pool[index])) {
            if (f->true_conn == NONE)
                break;
            Partition* p = &parts[f->true_conn];
            p->items[p->count++] = index;
            hirq |= HIRQ_CSCT;
            return;
        }
        filter = f->false_conn;
    }
    pool_used[index] = 0;
}

static void play_sector(void)
{
    Track* t = track_for(cur_fad);
    if (t && !t->audio) {
        if (free_sectors() == 0) {
            hirq |= HIRQ_BFUL;
            return;
        }
        int index = alloc_sector();
        Sector* s = &pool[index];
        read_raw(cur_fad, s->raw);
        s->fad = cur_fad;
        if (s->raw[15] == 2) {
            s->fn = s->raw[16];
            s->cn = s->raw[17];
            s->sm = s->raw[18];
            s->ci = s->raw[19];
        } else
            s->fn = s->cn = s->sm = s->ci = 0;
        route_sector(index);
        if (free_sectors() == 0)
            hirq |= HIRQ_BFUL;
    }
    cur_fad++;
    if (cur_fad >= play_end) {
        if (play_repeat < play_repeat_max || play_repeat_max == 0xF) {
            play_repeat++;
            cur_fad = play_start;
        } else {
            status = ST_PAUSE;
            hirq |= HIRQ_PEND;
        }
    }
}

static uint64_t sector_cycles(void)
{
    Track* t = track_for(cur_fad);
    return t && t->audio ? AUDIO_SECTOR_CYCLES : DATA_SECTOR_CYCLES;
}

void cdb_advance(uint64_t from, uint64_t to)
{
    (void)from;
    now_cycles = to;
    while (next_scdq_at <= to) {
        hirq |= HIRQ_SCDQ;
        next_scdq_at += AUDIO_SECTOR_CYCLES;
    }
    while ((status == ST_SEEK || status == ST_PLAY) && next_sector_at <= to) {
        if (status == ST_SEEK) {
            status = ST_PLAY;
            next_sector_at += sector_cycles();
            continue;
        }
        play_sector();
        next_sector_at += sector_cycles();
    }
}

uint64_t cdb_next_event(uint64_t now)
{
    uint64_t next = next_scdq_at;
    if ((status == ST_SEEK || status == ST_PLAY) && next_sector_at < next)
        next = next_sector_at;
    return next > now ? next : 0;
}

uint32_t cdb_hirq(void) { return hirq; }
void cdb_hirq_clear(uint32_t keep) { hirq &= keep | HIRQ_CMOK; }
int32_t cdb_result(int index) { return results[index & 15]; }

uint32_t cdb_toc(int index)
{
    if (index < 99) {
        if (index >= track_count)
            return 0xFFFFFFFFu;
        Track* t = &tracks[index];
        return ((t->audio ? 0x01u : 0x41u) << 24) | (uint32_t)(t->start_fad + t->index1_offset);
    }
    if (index == 99)
        return ((tracks[0].audio ? 0x01u : 0x41u) << 24) | (1u << 16);
    if (index == 100)
        return ((tracks[track_count - 1].audio ? 0x01u : 0x41u) << 24) | ((uint32_t)track_count << 16);
    return (0x01u << 24) | (uint32_t)leadout_fad;
}

static int resolve_position(int ptype, int32_t value, int end)
{
    if (ptype == 1)
        return value;
    if (ptype == 2) {
        int tno = (value >> 8) & 0xFF;
        if (tno < 1)
            tno = 1;
        if (tno > track_count)
            tno = track_count;
        Track* t = &tracks[tno - 1];
        return end ? t->start_fad + t->sectors : t->start_fad + t->index1_offset;
    }
    return -1;
}

static void fill_status(void)
{
    Track* t = track_for(cur_fad);
    int tno = t ? (int)(t - tracks) + 1 : 0xFF;
    results[0] = status | (transfer_active ? 0x40 : 0);
    results[1] = (t && !t->audio ? 0x80 : 0) | (play_repeat & 0xF);
    results[2] = t ? (t->audio ? 0x01 : 0x41) : 0xFF;
    results[3] = tno;
    results[4] = 1;
    results[5] = cur_fad;
}

static int sector_offset(void)
{
    switch (get_sector_length) {
    case 2336:
        return 16;
    case 2340:
        return 12;
    case 2352:
        return 0;
    }
    return 16;
}

static void delete_range(int p, int spos, int snum)
{
    Partition* part = &parts[p];
    if (spos == 0xFFFF || spos < 0)
        spos = part->count - 1;
    if (snum == 0xFFFF || snum < 0)
        snum = part->count - spos;
    if (spos < 0 || spos >= part->count)
        return;
    if (spos + snum > part->count)
        snum = part->count - spos;
    for (int i = 0; i < snum; ++i)
        pool_used[part->items[spos + i]] = 0;
    memmove(&part->items[spos], &part->items[spos + snum], (size_t)(part->count - spos - snum) * sizeof(int));
    part->count -= snum;
}

uint32_t cdb_data_read(int bytes)
{
    uint32_t value = 0;
    for (int i = 0; i < bytes; ++i)
        value = (value << 8) | (fifo_pos < fifo_len ? fifo[fifo_pos++] : 0);
    return value;
}

int cdb_data_pending(void) { return transfer_active; }

static void start_transfer(int p, int spos, int snum, int del)
{
    Partition* part = &parts[p];
    if (spos == 0xFFFF || spos < 0)
        spos = part->count - 1;
    if (snum == 0xFFFF || snum < 0)
        snum = part->count - spos;
    if (spos + snum > part->count)
        snum = part->count - spos;
    if (snum < 0)
        snum = 0;
    fifo_len = 0;
    fifo_pos = 0;
    int off = sector_offset();
    for (int i = 0; i < snum; ++i) {
        memcpy(fifo + fifo_len, pool[part->items[spos + i]].raw + off, (size_t)get_sector_length);
        fifo_len += (size_t)get_sector_length;
    }
    transfer_part = p;
    transfer_spos = spos;
    transfer_snum = snum;
    transfer_delete = del;
    transfer_active = 1;
    hirq |= HIRQ_DRDY;
}

static int sector_length_code(int32_t code, int current)
{
    switch (code & 0xFF) {
    case 0:
        return 2048;
    case 1:
        return 2336;
    case 2:
        return 2340;
    case 3:
        return 2352;
    }
    return current;
}

static int32_t cdb_cmd_inner(int command, int32_t a, int32_t b, int32_t c, int32_t d);

int32_t cdb_cmd(int command, int32_t a, int32_t b, int32_t c, int32_t d)
{
    static int trace = -1;
    if (trace < 0)
        trace = getenv("SATZ_TRACE_CD") != NULL;
    int32_t ret = cdb_cmd_inner(command, a, b, c, d);
    if (trace && command > CDB_GETPERISTAT && command != CDB_GETSCTNUM)
        satz_log("cdb %llu cmd %d (%d %d %d %d) -> %d [%d %d %d] hirq %04x st %d fad %d\n",
            (unsigned long long)satz_frame(), command, a, b, c, d, ret, results[0], results[1], results[2], hirq, status, cur_fad);
    return ret;
}

static int32_t cdb_cmd_inner(int command, int32_t a, int32_t b, int32_t c, int32_t d)
{
    memset(results, 0, sizeof(results));
    switch (command) {
    case CDB_GETCURSTAT:
    case CDB_GETLASTSTAT:
    case CDB_GETPERISTAT:
        fill_status();
        return 0;
    case CDB_GETHWINFO:
        results[0] = 0;
        results[1] = 1;
        results[2] = 0;
        results[3] = 0;
        results[4] = 1;
        return 0;
    case CDB_GETSES:
        if (a == 0)
            results[0] = (int32_t)((1u << 24) | (uint32_t)leadout_fad);
        else
            results[0] = (int32_t)(1u << 24);
        return 0;
    case CDB_CDINIT:
        if (a & 1) {
            for (int i = 0; i < SELECTORS; ++i)
                part_clear(i);
            cd_filter = NONE;
        }
        status = ST_PAUSE;
        cur_fad = 150;
        hirq |= HIRQ_CMOK | HIRQ_ESEL;
        return 0;
    case CDB_CDOPEN:
        return 0;
    case CDB_DATAREADY:
        return transfer_active ? 0 : -3;
    case CDB_DATAEND:
        results[0] = transfer_active ? (int32_t)(fifo_pos / 2) : 0;
        if (transfer_active && transfer_delete)
            delete_range(transfer_part, transfer_spos, transfer_snum);
        transfer_active = 0;
        fifo_len = fifo_pos = 0;
        hirq &= ~HIRQ_DRDY;
        hirq |= HIRQ_EHST;
        return 0;
    case CDB_CDPLAY: {
        int stype = a & 0xFF, etype = (a >> 8) & 0xFF, pmode = (a >> 16) & 0xFF;
        int32_t seek_from = cur_fad;
        if (stype != 3 && stype != 0) {
            play_start = resolve_position(stype, b, 0);
            if (!(pmode & 0x80) || cur_fad < play_start)
                cur_fad = play_start;
        }
        if (etype == 1)
            play_end = play_start + c;
        else if (etype == 2)
            play_end = resolve_position(2, c, 1);
        else if (etype == 0)
            play_end = leadout_fad;
        if ((pmode & 0x7F) != 0x7F)
            play_repeat_max = pmode & 0xF;
        play_repeat = 0;
        status = ST_SEEK;
        next_sector_at = now_cycles + seek_cycles(seek_from, cur_fad);
        hirq &= ~HIRQ_PEND;
        hirq |= HIRQ_CMOK;
        return 0;
    }
    case CDB_CDSEEK:
        if (a == 0)
            status = ST_STANDBY;
        else {
            int32_t fad = resolve_position(a, b, 0);
            if (fad >= 0)
                cur_fad = fad;
            status = ST_PAUSE;
        }
        hirq |= HIRQ_CMOK;
        return 0;
    case CDB_CDSETCON:
        cd_filter = a & 0xFF;
        hirq |= HIRQ_ESEL;
        return 0;
    case CDB_CDGETCON:
        results[0] = cd_filter;
        return 0;
    case CDB_SETFILTRANGE:
        filters[a].fad = b;
        filters[a].range = c;
        hirq |= HIRQ_ESEL;
        return 0;
    case CDB_GETFILTRANGE:
        results[0] = filters[a].fad;
        results[1] = filters[a].range;
        return 0;
    case CDB_SETFILTSUBH:
        filters[a].fn = (uint8_t)(b >> 8);
        filters[a].cn = (uint8_t)b;
        filters[a].smmsk = (uint8_t)(c >> 8);
        filters[a].smval = (uint8_t)c;
        filters[a].cimsk = (uint8_t)(d >> 8);
        filters[a].cival = (uint8_t)d;
        hirq |= HIRQ_ESEL;
        return 0;
    case CDB_GETFILTSUBH:
        results[0] = filters[a].fn;
        results[1] = filters[a].cn;
        results[2] = filters[a].smmsk;
        results[3] = filters[a].smval;
        results[4] = filters[a].cimsk;
        results[5] = filters[a].cival;
        return 0;
    case CDB_SETFILTMODE:
        if (b & 0x80) {
            filters[a].mode = 0;
            filters[a].fad = filters[a].range = 0;
            filters[a].fn = filters[a].cn = filters[a].smmsk = filters[a].smval = filters[a].cimsk = filters[a].cival = 0;
        } else
            filters[a].mode = (uint8_t)b;
        hirq |= HIRQ_ESEL;
        return 0;
    case CDB_GETFILTMODE:
        results[0] = filters[a].mode;
        return 0;
    case CDB_SETFILTCON:
        if (b & 1)
            filters[a].true_conn = (uint8_t)c;
        if (b & 2)
            filters[a].false_conn = (uint8_t)d;
        hirq |= HIRQ_ESEL;
        return 0;
    case CDB_GETFILTCON:
        results[0] = filters[a].true_conn;
        results[1] = filters[a].false_conn;
        return 0;
    case CDB_RESETSEL:
        if (a == 0) {
            if (b >= 0 && b < SELECTORS)
                part_clear(b);
        } else {
            for (int i = 0; i < SELECTORS; ++i) {
                if (a & 0x04)
                    part_clear(i);
                if (a & 0x10) {
                    filters[i].mode = 0;
                    filters[i].fad = filters[i].range = 0;
                    filters[i].fn = filters[i].cn = filters[i].smmsk = filters[i].smval = 0;
                    filters[i].cimsk = filters[i].cival = 0;
                }
                if (a & 0x40)
                    filters[i].true_conn = (uint8_t)i;
                if (a & 0x80)
                    filters[i].false_conn = NONE;
            }
            if (a & 0x20)
                cd_filter = NONE;
        }
        hirq |= HIRQ_ESEL;
        return 0;
    case CDB_GETBUFSIZ:
        results[0] = POOL;
        results[1] = SELECTORS;
        results[2] = free_sectors();
        return 0;
    case CDB_GETSCTNUM:
        results[0] = (a >= 0 && a < SELECTORS) ? parts[a].count : 0;
        return 0;
    case CDB_CALACTSIZ: {
        Partition* p = &parts[a];
        int spos = (b == 0xFFFF || b < 0) ? p->count - 1 : b;
        int snum = (c == 0xFFFF || c < 0) ? p->count - spos : c;
        if (spos + snum > p->count)
            snum = p->count - spos;
        act_size = snum > 0 ? snum * get_sector_length / 2 : 0;
        hirq |= HIRQ_ESEL;
        return 0;
    }
    case CDB_GETACTSIZ:
        results[0] = act_size;
        return 0;
    case CDB_GETSCTINFO: {
        Partition* p = &parts[a];
        int spos = (b == 0xFFFF || b < 0) ? p->count - 1 : b;
        if (spos < 0 || spos >= p->count)
            return -5;
        Sector* s = &pool[p->items[spos]];
        results[0] = s->fad;
        results[1] = s->fn;
        results[2] = s->cn;
        results[3] = s->sm;
        results[4] = s->ci;
        return 0;
    }
    case CDB_SETSCTLEN:
        get_sector_length = sector_length_code(a, get_sector_length);
        put_sector_length = sector_length_code(b, put_sector_length);
        hirq |= HIRQ_ESEL;
        return 0;
    case CDB_GETSCTDATA:
    case CDB_GETDELSCTDATA:
        if (transfer_active)
            return -1;
        start_transfer(a, b, c, command == CDB_GETDELSCTDATA);
        return 0;
    case CDB_DELSCTDATA:
        delete_range(a, b, c);
        hirq |= HIRQ_EHST;
        return 0;
    case CDB_MOVESCTDATA:
    case CDB_COPYSCTDATA: {
        Partition* p = &parts[a];
        int spos = (b == 0xFFFF || b < 0) ? p->count - 1 : b;
        int snum = (c == 0xFFFF || c < 0) ? p->count - spos : c;
        if (spos + snum > p->count)
            snum = p->count - spos;
        int dst = filters[d].true_conn;
        for (int i = 0; i < snum && dst != NONE; ++i) {
            int index = p->items[spos + i];
            if (command == CDB_COPYSCTDATA) {
                int copy = alloc_sector();
                if (copy < 0)
                    break;
                pool[copy] = pool[index];
                index = copy;
            }
            parts[dst].items[parts[dst].count++] = index;
        }
        if (command == CDB_MOVESCTDATA && dst != NONE) {
            memmove(&p->items[spos], &p->items[spos + snum], (size_t)(p->count - spos - snum) * sizeof(int));
            p->count -= snum;
        }
        hirq |= HIRQ_ECPY;
        return 0;
    }
    case CDB_GETCOPYERR:
        results[0] = 0;
        return 0;
    case CDB_CHGDIR:
    case CDB_READDIR: {
        int32_t fid = b & 0xFFFFFF;
        if (fid == 0xFFFFFF || (cur_dir_fad == 0)) {
            cur_dir_fad = root_fad;
            cur_dir_size = root_size;
        }
        if (fid != 0xFFFFFF && fid >= 2 && fid - 2 < dir_count) {
            cur_dir_fad = dir_entries[fid - 2].fad;
            cur_dir_size = dir_entries[fid - 2].size;
        } else if (fid == 1) {
            cur_dir_fad = dir_parent.fad;
            cur_dir_size = dir_parent.size;
        }
        load_directory(cur_dir_fad, cur_dir_size, &dir_self, &dir_parent, dir_entries, &dir_count);
        hirq |= HIRQ_EFLS;
        return 0;
    }
    case CDB_GETFILESCOPE:
        if (!cur_dir_fad)
            return -5;
        results[0] = 2;
        results[1] = dir_count > 254 ? 254 : dir_count;
        results[2] = dir_count <= 254;
        return 0;
    case CDB_TGETFILEINFO: {
        FileEntry* e = NULL;
        if (a == 0)
            e = &dir_self;
        else if (a == 1)
            e = &dir_parent;
        else if (a >= 2 && a - 2 < dir_count)
            e = &dir_entries[a - 2];
        if (!e)
            return -5;
        results[0] = e->fad;
        results[1] = e->size;
        results[2] = e->unit;
        results[3] = e->gap;
        results[4] = e->fn;
        results[5] = e->atr;
        return 0;
    }
    case CDB_READFILE:
    case CDB_ABORTFILE:
        hirq |= HIRQ_EFLS;
        return 0;
    case CDB_PUTSCTDATA:
    case CDB_CDGETLASTBUF:
        return 0;
    }
    satz_die("unhandled CD block command %d", command);
}

int cdb_stream_phase_hint(void) { return status; }

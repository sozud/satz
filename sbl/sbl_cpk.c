#include <stdint.h>
#include <string.h>

void* calloc(unsigned long count, unsigned long size);
void free(void* pointer);

void* satz_bus_pointer(unsigned long address);
unsigned long satz_bus_address(const volatile void* pointer);
void satz_bus_store(unsigned int address, const void* bytes, unsigned long count);
unsigned int satz_bus_read(unsigned long address, int size);
void satz_bus_write(unsigned long address, unsigned int value, int size);

__attribute__((weak)) int GFS_Fread(void* gfs, int nsct, void* buf, int bsize);
__attribute__((weak)) int GFS_NwFread(void* gfs, int nsct, void* buf, int bsize);
__attribute__((weak)) int GFS_NwExecOne(void* gfs);
__attribute__((weak)) void GFS_NwGetStat(void* gfs, int* amode, int* ndata);
__attribute__((weak)) int GFS_NwCdRead(void* gfs, int nsct);
__attribute__((weak)) void GFS_GetFileSize(void* gfs, int* sctsz, int* nsct, int* lstsz);

enum { STAT_STOP = 0, STAT_START = 2, STAT_HEADER = 3, STAT_TIME = 4, STAT_END = 5 };
enum { SECTOR = 2048, CHUNK_SECTORS = 32, MAX_WIDTH = 352, MAX_HEIGHT = 256 };

typedef struct {
    uint32_t* work_addr;
    uint32_t work_size;
    uint32_t* buf_addr;
    uint32_t buf_size;
    uint16_t* pcm_addr;
    uint32_t pcm_size;
} CpkCreatePara;

typedef struct {
    uint32_t offset;
    int32_t size;
    int32_t time;
    int32_t duration;
} CpkSample;

typedef struct {
    uint32_t film, size_header, version, reserved;
    uint32_t fdsc, size_fdsc, c_type, height, width;
    uint8_t color, sound_channel, sound_sampling_bit, sound_compress;
    uint32_t sound_sampling_rate, ckey;
    uint32_t stab, size_stab, time_scale, sample_total;
    CpkSample sample[];
} CpkHeader;

typedef struct {
    uint8_t v1[256][12];
    uint8_t v4[256][12];
} Strip;

typedef struct {
    int used, status, reading, color;
    void* gfs;
    uint8_t* file;
    uint32_t file_size, loaded, request, progress, start_trigger;
    int load_sectors;
    CpkHeader* header;
    uint32_t decode_addr, decode_pitch;
    uint32_t vbl_start, next_sample, frames;
    int standby;
    int32_t standby_time, standby_duration, time_frame;
    uint32_t pcm_addr, pcm_size, pcm_written, pcm_blocks;
    int pcm_level, pcm_started, pcm_last;
    Strip strips[32];
    uint8_t rgb[MAX_HEIGHT][MAX_WIDTH][3];
} Movie;

static Movie movies[2];
static uint8_t chunk[CHUNK_SECTORS * SECTOR] __attribute__((aligned(16)));
static uint32_t vbl_count;

static void pcm_stop(Movie* m);

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t be24(const uint8_t* p) { return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]; }
static uint32_t be16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint8_t clip(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

int CPK_Init(void) { return 1; }
void CPK_Finish(void) { }
void CPK_VblIn(void) { vbl_count++; }
void CPK_SetErrFunc(void* func, void* obj) { (void)func; (void)obj; }
void CPK_SetCpu(int cpu) { (void)cpu; }
void CPK_SetTrModeCd(Movie* m, int mode) { (void)m; (void)mode; }
void CPK_SetTrModePcm(Movie* m, int mode) { (void)m; (void)mode; }
void CPK_SetVolume(Movie* m, int volume) { m->pcm_level = volume & 7; }
void CPK_SetSilenceRate(Movie* m, int rate) { (void)m; (void)rate; }
void CPK_SetColor(Movie* m, int color) { m->color = color; }
void CPK_SetLoadNum(Movie* m, int sectors) { m->load_sectors = sectors < 1 ? 1 : sectors > CHUNK_SECTORS ? CHUNK_SECTORS : sectors; }
void CPK_SetStartTrgSize(Movie* m, int size) { m->start_trigger = (uint32_t)size; }
int CPK_GetPlayStatus(Movie* m) { return m->status; }
CpkHeader* CPK_GetHeader(Movie* m) { return m->header; }

void CPK_SetDecodeAddr(Movie* m, void* address, int pitch)
{
    m->decode_addr = (uint32_t)satz_bus_address(address);
    m->decode_pitch = (uint32_t)pitch;
}

Movie* CPK_CreateGfsMovie(CpkCreatePara* para, void* gfs)
{
    for (unsigned i = 0; i < sizeof(movies) / sizeof(movies[0]); ++i)
        if (!movies[i].used) {
            Movie* m = &movies[i];
            memset(m, 0, sizeof(*m));
            m->used = 1;
            m->gfs = gfs;
            m->color = 24;
            m->load_sectors = 20;
            m->pcm_addr = (uint32_t)satz_bus_address(para->pcm_addr);
            m->pcm_size = para->pcm_size;
            m->pcm_level = 7;
            int sctsz, nsct, lstsz;
            GFS_GetFileSize(gfs, &sctsz, &nsct, &lstsz);
            m->file_size = nsct > 0 ? (uint32_t)((nsct - 1) * sctsz + lstsz) : 0;
            m->file = calloc(1, (m->file_size + SECTOR - 1) / SECTOR * SECTOR + SECTOR);
            return m;
        }
    return NULL;
}

void CPK_DestroyGfsMovie(Movie* m)
{
    if (!m)
        return;
    pcm_stop(m);
    free(m->file);
    free(m->header);
    m->used = 0;
}

static uint32_t sound_host(void) { return 0x25A00000u + satz_bus_read(0x25A00404u, 4); }

static int sound_command(uint8_t command, const uint8_t* parameters, int count)
{
    uint32_t host = sound_host();
    for (int block = 0; block < 8; ++block) {
        uint32_t address = host + 0x10u * (uint32_t)block;
        if (satz_bus_read(address, 2))
            continue;
        for (int i = 0; i < count; ++i)
            satz_bus_write(address + 2u + (uint32_t)i, parameters[i], 1);
        satz_bus_write(address, (unsigned)command << 8, 2);
        return 1;
    }
    return 0;
}

static uint16_t pitch_word(uint32_t rate)
{
    int oct = 0;
    uint32_t base = 44100;
    while (rate < base && oct > -8) {
        base >>= 1;
        oct--;
    }
    while (rate >= base * 2 && oct < 7) {
        base <<= 1;
        oct++;
    }
    uint32_t fns = (uint32_t)(((uint64_t)(rate - base) << 10) / base);
    return (uint16_t)(((unsigned)oct & 0xFu) << 11 | (fns & 0x3FFu));
}

static int pcm_start(Movie* m)
{
    CpkHeader* h = m->header;
    uint16_t address = (uint16_t)(m->pcm_addr >> 4), size = (uint16_t)m->pcm_size;
    uint16_t pitch = pitch_word(h->sound_sampling_rate >> 16);
    uint8_t parameters[12] = { (uint8_t)((h->sound_channel == 2 ? 0x80 : 0) | (h->sound_sampling_bit == 8 ? 0x10 : 0)),
        (uint8_t)(m->pcm_level << 5), (uint8_t)(address >> 8), (uint8_t)address, (uint8_t)(size >> 8), (uint8_t)size,
        (uint8_t)(pitch >> 8), (uint8_t)pitch, 0, 0, 0, 0 };
    if (!sound_command(0x85, parameters, 12))
        return 0;
    m->pcm_last = (int)satz_bus_read(sound_host() + 0xA0u, 1);
    return 1;
}

static void pcm_stop(Movie* m)
{
    static const uint8_t stream[1] = { 0 };
    if (m->pcm_started)
        sound_command(0x86, stream, 1);
    m->pcm_started = 0;
}

static uint32_t pcm_played(Movie* m)
{
    if (!m->pcm_started)
        return 0;
    uint32_t blocks = (m->pcm_size ? m->pcm_size : 0x10000) / 4096u;
    int now = (int)satz_bus_read(sound_host() + 0xA0u, 1);
    m->pcm_blocks += (uint32_t)(now - m->pcm_last + 256) % blocks;
    m->pcm_last = now;
    return m->pcm_blocks * 4096u;
}

static int pcm_write(Movie* m, const uint8_t* data, uint32_t size)
{
    CpkHeader* h = m->header;
    uint32_t channels = h->sound_channel == 2 ? 2 : 1, width = h->sound_sampling_bit == 16 ? 2 : 1;
    uint32_t samples = size / channels / width, ring = m->pcm_size ? m->pcm_size : 0x10000;
    if (m->pcm_written + samples > pcm_played(m) + ring)
        return 0;
    for (uint32_t c = 0; c < channels; ++c) {
        const uint8_t* source = data + c * samples * width;
        uint32_t base = m->pcm_addr + c * ring * width;
        for (uint32_t done = 0; done < samples;) {
            uint32_t at = (m->pcm_written + done) % ring, n = samples - done;
            if (n > ring - at)
                n = ring - at;
            satz_bus_store(base + at * width, source + done * width, n * width);
            done += n;
        }
    }
    m->pcm_written += samples;
    return 1;
}

static void read_blocking(Movie* m, uint32_t until)
{
    while (m->loaded < until && m->loaded < m->file_size) {
        int nsct = (int)((m->file_size - m->loaded + SECTOR - 1) / SECTOR);
        if (nsct > CHUNK_SECTORS)
            nsct = CHUNK_SECTORS;
        GFS_Fread(m->gfs, nsct, chunk, nsct * SECTOR);
        memcpy(m->file + m->loaded, chunk, (size_t)nsct * SECTOR);
        m->loaded += (uint32_t)nsct * SECTOR;
    }
    if (m->loaded > m->file_size)
        m->loaded = m->file_size;
}

void CPK_PreloadHeader(Movie* m)
{
    read_blocking(m, SECTOR);
    uint32_t size_header = be32(m->file + 4);
    read_blocking(m, size_header);
    const uint8_t* f = m->file;
    uint32_t stab = 16 + be32(f + 20);
    uint32_t total = be32(f + stab + 12);
    CpkHeader* h = calloc(1, sizeof(CpkHeader) + total * sizeof(CpkSample));
    h->film = be32(f);
    h->size_header = size_header;
    h->version = be32(f + 8);
    h->fdsc = be32(f + 16);
    h->size_fdsc = be32(f + 20);
    h->c_type = be32(f + 24);
    h->height = be32(f + 28);
    h->width = be32(f + 32);
    h->color = f[36];
    h->sound_channel = f[37];
    h->sound_sampling_bit = f[38];
    h->sound_compress = f[39];
    h->sound_sampling_rate = be32(f + 40);
    h->ckey = be32(f + 44);
    h->stab = be32(f + stab);
    h->size_stab = be32(f + stab + 4);
    h->time_scale = be32(f + stab + 8);
    h->sample_total = total;
    for (uint32_t i = 0; i < total; ++i) {
        const uint8_t* s = f + stab + 16 + i * 16;
        h->sample[i].offset = be32(s);
        h->sample[i].size = (int32_t)be32(s + 4);
        h->sample[i].time = (int32_t)be32(s + 8);
        h->sample[i].duration = (int32_t)be32(s + 12);
    }
    m->header = h;
}

void CPK_Start(Movie* m)
{
    if (!m->header)
        CPK_PreloadHeader(m);
    if (m->loaded < m->file_size)
        GFS_NwCdRead(m->gfs, (int)((m->file_size - m->loaded + SECTOR - 1) / SECTOR));
    m->status = STAT_START;
}

static void load(Movie* m)
{
    for (int step = 0; step < 64; ++step) {
        if (!m->reading) {
            if (m->loaded >= m->file_size)
                return;
            int nsct = (int)((m->file_size - m->loaded + SECTOR - 1) / SECTOR);
            if (nsct > m->load_sectors)
                nsct = m->load_sectors;
            m->request = (uint32_t)nsct * SECTOR;
            m->progress = 0;
            GFS_NwFread(m->gfs, nsct, chunk, (int)m->request);
            m->reading = 1;
        }
        int amode, ndata;
        GFS_NwExecOne(m->gfs);
        GFS_NwGetStat(m->gfs, &amode, &ndata);
        if (amode == 1 && (uint32_t)ndata < m->request) {
            if ((uint32_t)ndata == m->progress)
                return;
            m->progress = (uint32_t)ndata;
            continue;
        }
        memcpy(m->file + m->loaded, chunk, (size_t)ndata);
        m->loaded += (uint32_t)ndata;
        if (m->loaded > m->file_size)
            m->loaded = m->file_size;
        m->reading = 0;
    }
}

static void codebook(uint8_t book[256][12], int id, const uint8_t* data, const uint8_t* end)
{
    int n = (id & 0x04) ? 4 : 6;
    uint32_t flag = 0, mask = 0;
    for (int i = 0; i < 256; ++i) {
        if ((id & 0x01) && !(mask >>= 1)) {
            if (data + 4 > end)
                return;
            flag = be32(data);
            data += 4;
            mask = 0x80000000u;
        }
        if ((id & 0x01) && !(flag & mask))
            continue;
        if (data + n > end)
            return;
        uint8_t* p = book[i];
        for (int k = 0; k < 4; ++k)
            p[k * 3] = p[k * 3 + 1] = p[k * 3 + 2] = data[k];
        if (n == 6) {
            int u = (int8_t)data[4], v = (int8_t)data[5];
            for (int k = 0; k < 4; ++k) {
                int y = data[k];
                p[k * 3] = clip(y + v * 2);
                p[k * 3 + 1] = clip(y - (u >> 1) - v);
                p[k * 3 + 2] = clip(y + u * 2);
            }
        }
        data += n;
    }
}

static void put(Movie* m, int x, int y, const uint8_t* rgb)
{
    if (x < MAX_WIDTH && y < MAX_HEIGHT)
        memcpy(m->rgb[y][x], rgb, 3);
}

static void vectors(Movie* m, Strip* s, int id, int x1, int y1, int x2, int y2, const uint8_t* data, const uint8_t* end)
{
    uint32_t flag = 0, mask = 0;
    for (int y = y1; y < y2; y += 4)
        for (int x = x1; x < x2; x += 4) {
            if ((id & 0x01) && !(mask >>= 1)) {
                if (data + 4 > end)
                    return;
                flag = be32(data);
                data += 4;
                mask = 0x80000000u;
            }
            if ((id & 0x01) && !(flag & mask))
                continue;
            if (!(id & 0x02) && !(mask >>= 1)) {
                if (data + 4 > end)
                    return;
                flag = be32(data);
                data += 4;
                mask = 0x80000000u;
            }
            if ((id & 0x02) || (~flag & mask)) {
                if (data >= end)
                    return;
                const uint8_t* p = s->v1[*data++];
                for (int dy = 0; dy < 4; ++dy)
                    for (int dx = 0; dx < 4; ++dx)
                        put(m, x + dx, y + dy, p + ((dy >> 1) * 2 + (dx >> 1)) * 3);
            } else {
                if (data + 4 > end)
                    return;
                const uint8_t* cb[4] = { s->v4[data[0]], s->v4[data[1]], s->v4[data[2]], s->v4[data[3]] };
                data += 4;
                for (int dy = 0; dy < 4; ++dy)
                    for (int dx = 0; dx < 4; ++dx)
                        put(m, x + dx, y + dy, cb[(dy >> 1) * 2 + (dx >> 1)] + ((dy & 1) * 2 + (dx & 1)) * 3);
            }
        }
}

static void decode(Movie* m, const uint8_t* data, uint32_t size)
{
    const uint8_t* end = data + size;
    if (size < 10)
        return;
    int flags = data[0];
    int strips = (int)be16(data + 8);
    uint32_t extra = 0;
    if (be24(data + 1) != size && size % be24(data + 1) != 0)
        extra = size >= 16 && data[10] == 0xFE && data[11] == 0 && data[12] == 0 && data[13] == 6 && data[14] == 0 && data[15] == 0 ? 6 : 2;
    data += 10 + extra;
    int y0 = 0;
    for (int i = 0; i < strips && i < 32 && data + 12 <= end; ++i) {
        Strip* s = &m->strips[i];
        if (i > 0 && !(flags & 0x01))
            memcpy(s, &m->strips[i - 1], sizeof(*s));
        int y1 = (int)be16(data + 4), x1 = (int)be16(data + 6);
        int y2 = (int)be16(data + 8), x2 = (int)be16(data + 10);
        if (!y1) {
            y1 = y0;
            y2 += y0;
        }
        const uint8_t* strip_end = data + be24(data + 1);
        if (strip_end > end)
            strip_end = end;
        data += 12;
        while (data + 4 <= strip_end) {
            int id = data[0];
            const uint8_t* chunk_end = data + be24(data + 1);
            if (chunk_end > strip_end)
                chunk_end = strip_end;
            data += 4;
            if (id >= 0x20 && id <= 0x27)
                codebook((id & 0x02) ? s->v1 : s->v4, id, data, chunk_end);
            else if (id >= 0x30 && id <= 0x32) {
                vectors(m, s, id, x1, y1, x2, y2, data, chunk_end);
                break;
            }
            data = chunk_end;
        }
        data = strip_end;
        y0 = y2;
    }
}

static void output(Movie* m)
{
    uint32_t width = m->header->width < MAX_WIDTH ? m->header->width : MAX_WIDTH;
    uint32_t height = m->header->height < MAX_HEIGHT ? m->header->height : MAX_HEIGHT;
    static uint8_t line[MAX_WIDTH * 4];
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t bytes;
        if (m->color == 15) {
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t* p = m->rgb[y][x];
                uint32_t c = 0x8000u | (uint32_t)(p[2] >> 3) << 10 | (uint32_t)(p[1] >> 3) << 5 | (uint32_t)(p[0] >> 3);
                line[x * 2] = (uint8_t)(c >> 8);
                line[x * 2 + 1] = (uint8_t)c;
            }
            bytes = width * 2;
        } else {
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t* p = m->rgb[y][x];
                line[x * 4] = 0x80;
                line[x * 4 + 1] = p[2];
                line[x * 4 + 2] = p[1];
                line[x * 4 + 3] = p[0];
            }
            bytes = width * 4;
        }
        satz_bus_store(m->decode_addr + y * m->decode_pitch, line, bytes);
    }
}

static int32_t film_time(Movie* m)
{
    return (int32_t)((uint64_t)(vbl_count - m->vbl_start) * m->header->time_scale / 60);
}

void CPK_Task(Movie* m)
{
    if (!m || m->status == STAT_STOP || m->status == STAT_END)
        return;
    load(m);
    CpkHeader* h = m->header;
    if (m->status == STAT_START)
        m->status = STAT_HEADER;
    if (m->status == STAT_HEADER) {
        uint32_t trigger = h->size_header + m->start_trigger;
        if (m->loaded < m->file_size && m->loaded < trigger)
            return;
        m->status = STAT_TIME;
        m->vbl_start = vbl_count;
    }
    if (h->sound_channel && !m->pcm_started && m->pcm_written)
        m->pcm_started = pcm_start(m);
    while (!m->standby && m->next_sample < h->sample_total) {
        CpkSample* s = &h->sample[m->next_sample];
        uint32_t start = h->size_header + s->offset;
        if (start + (uint32_t)s->size > m->loaded)
            return;
        if (s->time == -1) {
            if (h->sound_channel && !pcm_write(m, m->file + start, (uint32_t)s->size))
                return;
            m->next_sample++;
            continue;
        }
        m->next_sample++;
        decode(m, m->file + start, (uint32_t)s->size);
        output(m);
        m->standby = 1;
        m->standby_time = s->time & 0x7FFFFFFF;
        m->standby_duration = s->duration;
    }
    if (!m->standby && m->next_sample >= h->sample_total && film_time(m) >= m->time_frame) {
        pcm_stop(m);
        m->status = STAT_END;
    }
}

int CPK_IsDispTime(Movie* m)
{
    return m->status == STAT_TIME && m->standby && film_time(m) >= m->standby_time;
}

void CPK_CompleteDisp(Movie* m)
{
    m->standby = 0;
    m->time_frame = m->standby_time + m->standby_duration;
    m->frames++;
}

int satz_cpk_decode_buffer(uint32_t* address, uint32_t* pitch, uint32_t* width, uint32_t* height)
{
    for (unsigned i = 0; i < sizeof(movies) / sizeof(movies[0]); ++i)
        if (movies[i].used && movies[i].header && movies[i].decode_addr && (movies[i].standby || movies[i].frames)) {
            *address = movies[i].decode_addr;
            *pitch = movies[i].decode_pitch;
            *width = movies[i].header->width;
            *height = movies[i].header->height;
            return 1;
        }
    return 0;
}

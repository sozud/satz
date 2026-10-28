#include "satz_host.h"
#include "cdblock.h"
#include "../sound/satz_sound.h"
#include <string.h>

extern void (*satz_scu_vectors[0x80])(void);
static void (*slave_entry)(void);
static uint8_t semaphores[0x20];

void satz_scu_vector_set(uint32_t vector, void (*handler)(void))
{
    if (vector < 0x80)
        satz_scu_vectors[vector] = handler;
}

void (*satz_scu_vector_get(uint32_t vector))(void) { return vector < 0x80 ? satz_scu_vectors[vector] : NULL; }

void satz_bios_change_scu_mask(uint32_t and_mask, uint32_t or_mask) { satz_set_scu_mask((satz_scu_mask() & and_mask) | or_mask); }
void satz_bios_change_clock(int32_t mode) { (void)mode; }
void satz_bios_cd_error(void) { satz_die("BIOS CD error handler called"); }

void satz_bios_set_slave_vector(int32_t vector, void (*entry)(void))
{
    if (vector == 0x94)
        slave_entry = entry;
}

uint32_t satz_bios_test_and_set(uint32_t number)
{
    uint32_t taken = semaphores[number & 0x1F];
    semaphores[number & 0x1F] = 1;
    return taken ? 0 : 1;
}

void satz_bios_clear_semaphore(uint32_t number) { semaphores[number & 0x1F] = 0; }

typedef struct {
    uint32_t total_size, total_blocks, block_size, free_size, free_blocks, data_blocks;
} BupStat;

typedef struct {
    char file_name[12];
    char comment[11];
    uint8_t language;
    uint32_t date;
    uint32_t data_size;
    uint16_t block_size;
    uint16_t padding;
} BupDir;

typedef struct {
    uint8_t year, month, day, hour, minute, week;
} BupDate;

#define BUP_FILES 16

typedef struct {
    BupDir dir;
    uint8_t data[0x2000];
    int used;
} BupFile;

static BupFile bup_files[BUP_FILES];

static BupFile* bup_find(const char* name)
{
    for (int i = 0; i < BUP_FILES; ++i)
        if (bup_files[i].used && !strncmp(bup_files[i].dir.file_name, name, 11))
            return &bup_files[i];
    return NULL;
}

static int32_t bup_selpart(uint32_t device, uint16_t number) { (void)number; return device == 0 ? 0 : 1; }
static int32_t bup_format(uint32_t device) { return device == 0 ? 0 : 1; }
static uint32_t blocks_for(uint32_t bytes) { return (bytes + 30 + 59) / 60 + 1; }

static int32_t bup_stat(uint32_t device, uint32_t size, BupStat* stat)
{
    if (device != 0)
        return 1;
    uint32_t used = 0;
    for (int i = 0; i < BUP_FILES; ++i)
        if (bup_files[i].used)
            used += blocks_for(bup_files[i].dir.data_size);
    stat->total_size = 0x8000;
    stat->total_blocks = 0x200;
    stat->block_size = 0x40;
    stat->free_blocks = 0x200 - 2 - used;
    stat->free_size = stat->free_blocks * 0x40;
    stat->data_blocks = blocks_for(size);
    return 0;
}

static int32_t bup_write(uint32_t device, const BupDir* dir, const uint8_t* data, uint8_t overwrite)
{
    if (device != 0)
        return 1;
    BupFile* f = bup_find(dir->file_name);
    if (f && !overwrite)
        return 6;
    for (int i = 0; i < BUP_FILES && !f; ++i)
        if (!bup_files[i].used)
            f = &bup_files[i];
    if (!f)
        return 4;
    f->used = 1;
    f->dir = *dir;
    if (f->dir.data_size > sizeof(f->data))
        f->dir.data_size = sizeof(f->data);
    memcpy(f->data, data, f->dir.data_size);
    return 0;
}

static int32_t bup_read(uint32_t device, const char* name, uint8_t* data)
{
    BupFile* f = device == 0 ? bup_find(name) : NULL;
    if (!f)
        return device == 0 ? 5 : 1;
    memcpy(data, f->data, f->dir.data_size);
    return 0;
}

static int32_t bup_delete(uint32_t device, const char* name)
{
    BupFile* f = device == 0 ? bup_find(name) : NULL;
    if (!f)
        return device == 0 ? 5 : 1;
    f->used = 0;
    return 0;
}

static int32_t bup_dir(uint32_t device, const char* name, uint16_t count, BupDir* dir)
{
    if (device != 0)
        return 0;
    size_t len = strnlen(name, 11);
    int32_t found = 0;
    for (int i = 0; i < BUP_FILES; ++i)
        if (bup_files[i].used && !strncmp(bup_files[i].dir.file_name, name, len)) {
            if (found < count)
                dir[found] = bup_files[i].dir;
            found++;
        }
    return found > count ? -found : found;
}

static int32_t bup_verify(uint32_t device, const char* name, const uint8_t* data)
{
    BupFile* f = device == 0 ? bup_find(name) : NULL;
    if (!f)
        return device == 0 ? 5 : 1;
    return memcmp(f->data, data, f->dir.data_size) ? 7 : 0;
}

static void bup_getdate(uint32_t packed, BupDate* date)
{
    uint32_t days = packed / 1440;
    date->minute = (uint8_t)(packed % 60);
    date->hour = (uint8_t)(packed / 60 % 24);
    date->week = (uint8_t)((days + 2) % 7);
    uint32_t year = 0;
    for (;;) {
        uint32_t length = (year % 4) ? 365 : 366;
        if (days < length)
            break;
        days -= length;
        year++;
    }
    static const uint8_t month_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t month = 0;
    while (month < 11 && days >= (uint32_t)(month_days[month] + (month == 1 && year % 4 == 0))) {
        days -= month_days[month] + (month == 1 && year % 4 == 0);
        month++;
    }
    date->year = (uint8_t)year;
    date->month = (uint8_t)(month + 1);
    date->day = (uint8_t)(days + 1);
}

static uint32_t bup_setdate(const BupDate* date)
{
    static const uint16_t before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    uint32_t days = date->year * 365u + (date->year + 3u) / 4 + before[(date->month - 1) % 12] + date->day - 1;
    if (date->month > 2 && date->year % 4 == 0)
        days++;
    return (days * 24 + date->hour) * 60 + date->minute;
}

static void* const bup_table[11] = { NULL, (void*)bup_selpart, (void*)bup_format, (void*)bup_stat, (void*)bup_write,
    (void*)bup_read, (void*)bup_delete, (void*)bup_dir, (void*)bup_verify, (void*)bup_getdate, (void*)bup_setdate };

void* satz_bios_bup_vector;

void satz_bios_bup_init(void* library, void* work, uint16_t* config)
{
    (void)library;
    (void)work;
    satz_bios_bup_vector = (void*)bup_table;
    config[0] = 1;
    config[1] = 1;
    for (int i = 2; i < 6; ++i)
        config[i] = 0;
}

void satz_smpc_command(uint8_t command)
{
    switch (command) {
    case 0x02:
        satz_slave_start(slave_entry);
        break;
    case 0x03: {
        extern void satz_slave_stop(void);
        satz_slave_stop();
        break;
    }
    case 0x06:
        satz_sound_set_68k(1);
        break;
    case 0x07:
        satz_sound_set_68k(0);
        break;
    default:
        break;
    }
}

void satz_dma_copy(unsigned long dst, unsigned long src, unsigned long units, int unit_bytes, int src_step, int dst_step)
{
    uint32_t phys = satz_phys((uint32_t)src);
    int from_cd = phys - 0x05818000u < 0x8000u;
    uint8_t bytes[4];
    for (unsigned long i = 0; i < units; ++i) {
        if (from_cd) {
            uint32_t v = cdb_data_read(unit_bytes);
            for (int b = unit_bytes - 1; b >= 0; --b, v >>= 8)
                bytes[b] = (uint8_t)v;
            satz_bus_store((uint32_t)dst, bytes, (size_t)unit_bytes);
        } else if (src_step == unit_bytes && dst_step == unit_bytes) {
            uint8_t block[4096];
            size_t total = (size_t)(units - i) * (size_t)unit_bytes;
            size_t limit = sizeof(block);
            if (dst > src && dst - src < total && dst - src < limit)
                limit = dst - src < (unsigned long)unit_bytes ? (size_t)unit_bytes : (size_t)(dst - src);
            for (size_t done = 0; done < total;) {
                size_t n = total - done < limit ? total - done : limit;
                satz_bus_load((uint32_t)(src + done), block, n);
                satz_bus_store((uint32_t)(dst + done), block, n);
                done += n;
            }
            return;
        } else {
            satz_bus_load((uint32_t)src, bytes, (size_t)unit_bytes);
            satz_bus_store((uint32_t)dst, bytes, (size_t)unit_bytes);
        }
        src += (unsigned long)(long)src_step;
        dst += (unsigned long)(long)dst_step;
    }
}

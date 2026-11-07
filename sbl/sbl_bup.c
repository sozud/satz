#include "sega_xpt.h"


typedef struct {
    Uint16 unit_id;
    Uint16 partition;
} BupConfig;

typedef struct BupStat BupStat;
typedef struct BupDir BupDir;

typedef struct {
    Uint8 year, month, day, time, min, week;
} BupDate;

#define BUP_NON 1

extern void* satz_bios_bup_vector;
void satz_bios_bup_init(void* library, void* work, Uint16* config);

#define BUP_CALL(type, index) ((type)((void**)satz_bios_bup_vector)[index])

static Uint32 mask_interrupts(void)
{
    Uint32 sr = satz_get_sr();
    satz_set_sr(sr | 0xF0);
    return sr;
}

static void restore_interrupts(Uint32 sr) { satz_set_sr(sr); }

Sint32 BUP_Stat(Uint32 device, Uint32 size, BupStat* stat)
{
    Uint32 sr = mask_interrupts();
    Sint32 result = BUP_CALL(Sint32 (*)(Uint32, Uint32, BupStat*), 3)(device, size, stat);
    restore_interrupts(sr);
    return result;
}

void BUP_Init(Uint32* library, Uint32* work, BupConfig config[3])
{
    struct {
        Uint32 words[6];
    } stat;
    Uint32 sr = mask_interrupts();
    satz_bios_bup_init(library, work, (Uint16*)config);
    restore_interrupts(sr);
    if (BUP_Stat(2, 0, (BupStat*)&stat) != BUP_NON)
        config[2].unit_id = 3;
}

Sint32 BUP_SelPart(Uint32 device, Uint16 number)
{
    Uint32 sr = mask_interrupts();
    Sint32 result = BUP_CALL(Sint32 (*)(Uint32, Uint16), 1)(device, number);
    restore_interrupts(sr);
    return result;
}

Sint32 BUP_Format(Uint32 device)
{
    Uint32 sr = mask_interrupts();
    Sint32 result = BUP_CALL(Sint32 (*)(Uint32), 2)(device);
    restore_interrupts(sr);
    return result;
}

Sint32 BUP_Write(Uint32 device, BupDir* dir, Uint8* data, Uint8 overwrite)
{
    Uint32 sr = mask_interrupts();
    Sint32 result = BUP_CALL(Sint32 (*)(Uint32, BupDir*, Uint8*, Uint8), 4)(device, dir, data, overwrite);
    restore_interrupts(sr);
    return result;
}

Sint32 BUP_Read(Uint32 device, Uint8* name, Uint8* data)
{
    Uint32 sr = mask_interrupts();
    Sint32 result = BUP_CALL(Sint32 (*)(Uint32, Uint8*, Uint8*), 5)(device, name, data);
    restore_interrupts(sr);
    return result;
}

Sint32 BUP_Delete(Uint32 device, Uint8* name)
{
    Uint32 sr = mask_interrupts();
    Sint32 result = BUP_CALL(Sint32 (*)(Uint32, Uint8*), 6)(device, name);
    restore_interrupts(sr);
    return result;
}

Sint32 BUP_Dir(Uint32 device, Uint8* name, Uint16 size, BupDir* dir)
{
    Uint32 sr = mask_interrupts();
    Sint32 result = BUP_CALL(Sint32 (*)(Uint32, Uint8*, Uint16, BupDir*), 7)(device, name, size, dir);
    restore_interrupts(sr);
    return result;
}

Sint32 BUP_Verify(Uint32 device, Uint8* name, Uint8* data)
{
    Uint32 sr = mask_interrupts();
    Sint32 result = BUP_CALL(Sint32 (*)(Uint32, Uint8*, Uint8*), 8)(device, name, data);
    restore_interrupts(sr);
    return result;
}

void BUP_GetDate(Uint32 packed, BupDate* date)
{
    BUP_CALL(void (*)(Uint32, BupDate*), 9)(packed, date);
    if (date->month == 12 && date->day == 32) {
        date->year++;
        date->month = 1;
        date->day = 1;
    }
}

Uint32 BUP_SetDate(BupDate* date) { return BUP_CALL(Uint32 (*)(BupDate*), 10)(date); }

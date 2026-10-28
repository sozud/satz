#define _GNU_SOURCE
#include "satz_host.h"
#include "../video/satz_video.h"
#include "../sound/satz_sound.h"
#include <stdio.h>
#include <string.h>

#define BBUS_BASE 0x05800000u
#define BBUS_SIZE 0x00800000u
#define BIOS_SIZE 0x00080000u

static uint8_t bios[BIOS_SIZE];
#define SMPC_BASE 0x00100000u
#define SMPC_SIZE 0x80u
#define ONCHIP_BASE 0xFFFFFE00u
#define ONCHIP_SIZE 0x200u
static uint8_t smpc_handles[SMPC_SIZE];
static uint8_t onchip_handles[ONCHIP_SIZE];

#ifdef __APPLE__
extern char __executable_start[] __asm("__mh_execute_header");
extern char end[] __asm("segment$end$__DATA");
#else
extern char __executable_start[];
extern char end[];
#endif

const void* satz_low_memory(void) { return bios; }

static const SatzRamArea* ram_area(uint32_t phys)
{
    unsigned low = 0, high = satz_ram_area_count;
    while (low < high) {
        unsigned mid = (low + high) / 2;
        if (phys < satz_ram_areas[mid].start)
            high = mid;
        else if (phys >= satz_ram_areas[mid].start + satz_ram_areas[mid].size)
            low = mid + 1;
        else
            return &satz_ram_areas[mid];
    }
    return NULL;
}

int satz_bus_region(uint32_t address, uint8_t** base, uint32_t* offset)
{
    if (address >= ONCHIP_BASE) {
        *base = onchip_handles;
        *offset = address - ONCHIP_BASE;
        return SATZ_REGION_REGISTER;
    }
    uint32_t phys = address & 0x07FFFFFFu;
    if (phys - SATZ_HOST_WINDOW < SATZ_HOST_WINDOW_SIZE) {
        *base = (uint8_t*)__executable_start;
        *offset = phys - SATZ_HOST_WINDOW;
        return SATZ_REGION_HOST;
    }
    if (phys - SMPC_BASE < SMPC_SIZE) {
        *base = smpc_handles;
        *offset = phys - SMPC_BASE;
        return SATZ_REGION_REGISTER;
    }
    if (phys - 0x00200000u < 0x00100000u || (phys & 0x07F00000u) == 0x06000000u) {
        const SatzRamArea* area = ram_area(phys);
        if (!area)
            satz_die("bus address %08X is outside the fixed work-RAM areas", address);
        *base = area->data;
        *offset = phys - area->start;
        return SATZ_REGION_RAM;
    }
    if (phys - BBUS_BASE < BBUS_SIZE) {
        *base = NULL;
        *offset = phys;
        return SATZ_REGION_DEVICE;
    }
    if (phys < BIOS_SIZE) {
        *base = bios;
        *offset = phys;
        return SATZ_REGION_BIG;
    }
    return SATZ_REGION_NONE;
}

void* satz_bus_pointer(unsigned long address)
{
    uint8_t* base;
    uint32_t offset;
    if (!address)
        return NULL;
    int region = satz_bus_region((uint32_t)address, &base, &offset);
    if (region == SATZ_REGION_NONE)
        satz_die("no host memory behind bus address %08lX", address);
    if (region == SATZ_REGION_DEVICE)
        return (void*)(SATZ_BUS_HANDLE | (uint32_t)address);
    return base + offset;
}

unsigned long satz_bus_address(const volatile void* pointer)
{
    const uint8_t* p = (const uint8_t*)pointer;
    if (((uintptr_t)p & ~0xFFFFFFFFul) == SATZ_BUS_HANDLE)
        return (unsigned long)((uintptr_t)p & 0xFFFFFFFFu);
    if ((uintptr_t)p < BIOS_SIZE)
        return (unsigned long)(uintptr_t)p;
    for (unsigned i = 0; i < satz_ram_area_count; ++i)
        if (p >= satz_ram_areas[i].data && p < satz_ram_areas[i].data + satz_ram_areas[i].size)
            return satz_ram_areas[i].start + (unsigned long)(p - satz_ram_areas[i].data);
    if (p >= smpc_handles && p < smpc_handles + SMPC_SIZE)
        return 0x20000000u + SMPC_BASE + (unsigned long)(p - smpc_handles);
    if (p >= onchip_handles && p < onchip_handles + ONCHIP_SIZE)
        return ONCHIP_BASE + (unsigned long)(p - onchip_handles);
    if (p >= bios && p < bios + BIOS_SIZE)
        return (unsigned long)(p - bios);
    if (p >= (const uint8_t*)__executable_start && p < (const uint8_t*)end &&
        (unsigned long)(p - (const uint8_t*)__executable_start) < SATZ_HOST_WINDOW_SIZE)
        return SATZ_HOST_WINDOW + (unsigned long)(p - (const uint8_t*)__executable_start);
    satz_die("host object %p has no bus address (stack or heap memory passed as a Saturn address)", pointer);
}

static int device(uint32_t phys, uint32_t base, uint32_t size) { return phys - base < size; }

static uint32_t device_read(uint32_t address, uint32_t phys, int size)
{
    if (device(phys, 0x05A00000u, 0x100000u))
        return satz_sound_ram_read(phys & 0x7FFFFu, size);
    if (device(phys, 0x05C00000u, 0x100000u) || device(phys, 0x05E00000u, 0x1C0000u))
        return satz_video_read(phys, size);
    satz_die("read of unmapped B-bus address %08X", address);
}

static void device_write(uint32_t address, uint32_t phys, uint32_t value, int size)
{
    if (device(phys, 0x05A00000u, 0x100000u))
        satz_sound_ram_write(phys & 0x7FFFFu, value, size);
    else if (device(phys, 0x05C00000u, 0x100000u) || device(phys, 0x05E00000u, 0x1C0000u))
        satz_video_write(phys, value, size);
    else
        satz_die("write of unmapped B-bus address %08X", address);
}

uint32_t satz_mem_read(uint32_t address, int size)
{
    uint8_t* base;
    uint32_t offset;
    int region = satz_bus_region(address, &base, &offset);
    if (region == SATZ_REGION_NONE)
        satz_die("read of unmapped bus address %08X", address);
    if (region == SATZ_REGION_DEVICE)
        return device_read(address, offset, size);
    const uint8_t* p = base + offset;
    if (region == SATZ_REGION_BIG) {
        uint32_t v = 0;
        for (int i = 0; i < size; ++i)
            v = (v << 8) | p[i];
        return v;
    }
    if (size == 1)
        return *p;
    if (size == 2) {
        uint16_t v;
        memcpy(&v, p, 2);
        return v;
    }
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

void satz_mem_write(uint32_t address, uint32_t value, int size)
{
    uint8_t* base;
    uint32_t offset;
    int region = satz_bus_region(address, &base, &offset);
    if (region == SATZ_REGION_NONE)
        satz_die("write of unmapped bus address %08X", address);
    if (region == SATZ_REGION_DEVICE) {
        device_write(address, offset, value, size);
        return;
    }
    uint8_t* p = base + offset;
    if (base == bios)
        return;
    if (region == SATZ_REGION_BIG) {
        for (int i = size - 1; i >= 0; --i, value >>= 8)
            p[i] = (uint8_t)value;
        return;
    }
    if (size == 1)
        *p = (uint8_t)value;
    else if (size == 2) {
        uint16_t v = (uint16_t)value;
        memcpy(p, &v, 2);
    } else
        memcpy(p, &value, 4);
}


void satz_map_bios(const char* path)
{
    if ((unsigned long)((const char*)end - __executable_start) > SATZ_HOST_WINDOW_SIZE)
        satz_die("program image is %lu bytes; the bus window holds %u", (unsigned long)((const char*)end - __executable_start),
            SATZ_HOST_WINDOW_SIZE);
    if (!path)
        return;
    FILE* f = fopen(path, "rb");
    if (!f || fread(bios, 1, BIOS_SIZE, f) != BIOS_SIZE)
        satz_die("cannot read 512 KiB BIOS %s", path);
    fclose(f);
}

static size_t contiguous(uint32_t address, size_t count)
{
    uint32_t phys = address & 0x07FFFFFFu;
    if (!(phys - 0x00200000u < 0x00100000u || (phys & 0x07F00000u) == 0x06000000u))
        return count;
    const SatzRamArea* area = ram_area(phys);
    if (area && phys - area->start + count > area->size)
        return area->size - (phys - area->start);
    return count;
}

static void load_part(uint32_t address, void* bytes, size_t count)
{
    uint8_t* base;
    uint32_t offset;
    uint8_t* out = bytes;
    if (satz_bus_region(address, &base, &offset) != SATZ_REGION_DEVICE) {
        memcpy(out, satz_bus_pointer(address), count);
        return;
    }
    size_t i = 0;
    if (address & 1)
        out[i++] = (uint8_t)satz_mem_read(address, 1);
    for (; i + 2 <= count; i += 2) {
        uint32_t v = satz_mem_read(address + (uint32_t)i, 2);
        out[i] = (uint8_t)(v >> 8);
        out[i + 1] = (uint8_t)v;
    }
    if (i < count)
        out[i] = (uint8_t)satz_mem_read(address + (uint32_t)i, 1);
}

static void store_part(uint32_t address, const void* bytes, size_t count)
{
    uint8_t* base;
    uint32_t offset;
    const uint8_t* in = bytes;
    if (satz_bus_region(address, &base, &offset) != SATZ_REGION_DEVICE) {
        if (base != bios)
            memmove(satz_bus_pointer(address), in, count);
        return;
    }
    size_t i = 0;
    if (address & 1) {
        satz_mem_write(address, in[0], 1);
        i = 1;
    }
    for (; i + 2 <= count; i += 2)
        satz_mem_write(address + (uint32_t)i, (uint32_t)(in[i] << 8 | in[i + 1]), 2);
    if (i < count)
        satz_mem_write(address + (uint32_t)i, in[i], 1);
}

void satz_bus_load(uint32_t address, void* bytes, size_t count)
{
    for (size_t done = 0, n; done < count; done += n) {
        n = contiguous(address + (uint32_t)done, count - done);
        load_part(address + (uint32_t)done, (uint8_t*)bytes + done, n);
    }
}

void satz_bus_store(uint32_t address, const void* bytes, size_t count)
{
    for (size_t done = 0, n; done < count; done += n) {
        n = contiguous(address + (uint32_t)done, count - done);
        store_part(address + (uint32_t)done, (const uint8_t*)bytes + done, n);
    }
}

void satz_bus_copy(unsigned long destination, unsigned long source, unsigned long bytes, int unit)
{
    for (unsigned long i = 0; i + (unsigned long)unit <= bytes; i += (unsigned long)unit)
        satz_mem_write((uint32_t)(destination + i), satz_mem_read((uint32_t)(source + i), unit), unit);
}

#include "satz_host.h"
#include <string.h>
#include "cdblock.h"
#include "vdp.h"
#include "../sound/satz_sound.h"

extern uint8_t satz_frt_ftcsr[2];
extern uint8_t satz_frt_tier[2];
static uint8_t onchip[2][0x200];
static uint8_t smpc_status;

static int in_range(uint32_t phys, uint32_t base, uint32_t size) { return phys - base < size; }

static uint32_t scu_regs[0x40];

uint32_t satz_scu_reg(uint32_t offset) { return scu_regs[(offset >> 2) & 0x3F]; }

static int bbus(uint32_t a) { return in_range(satz_phys(a), 0x05A00000u, 0x00600000u); }

static void scu_dma_direct(uint32_t read, uint32_t write, uint32_t count, uint32_t add)
{
    static const uint32_t steps[8] = { 0, 2, 4, 8, 16, 32, 64, 128 };
    uint32_t read_step = (add & 0x100) ? 4 : 0;
    uint32_t write_step = steps[add & 7];
    if (!count)
        count = 0x100000;
    if (bbus(write)) {
        const int from_cd = satz_phys(read) - 0x05818000u < 0x8000u;
        for (uint32_t done = 0; done < count; done += 4, read += read_step) {
            uint8_t bytes[4];
            if (from_cd) {
                uint32_t value = satz_r32(read);
                bytes[0] = (uint8_t)(value >> 24);
                bytes[1] = (uint8_t)(value >> 16);
                bytes[2] = (uint8_t)(value >> 8);
                bytes[3] = (uint8_t)value;
            } else
                satz_bus_load(read, bytes, 4);
            satz_bus_store(write, bytes, 2);
            write += write_step;
            if (done + 2 < count) {
                satz_bus_store(write, bytes + 2, 2);
                write += write_step;
            }
        }
        return;
    }
    if (read_step == 4 && write_step == 4) {
        satz_dma_copy(write, read, count, 1, 1, 1);
        return;
    }
    for (uint32_t done = 0; done < count; done += 4, read += read_step, write += write_step)
        satz_w32(write, satz_r32(read));
}

static void scu_dma_start(int level)
{
    uint32_t* r = &scu_regs[level * 8];
    uint32_t read = r[0], write = r[1], count = r[2], add = r[3], mode = r[5];
    if (mode & 0x01000000) {
        uint32_t table = write;
        for (int guard = 0; guard < 4096; ++guard) {
            uint32_t c = satz_r32(table), w = satz_r32(table + 4), s = satz_r32(table + 8);
            scu_dma_direct(s & 0x07FFFFFF, w, c, add);
            table += 12;
            if (s & 0x80000000u)
                break;
        }
    } else
        scu_dma_direct(read, write, count, add);
    static const int vectors[3] = { 0x4B, 0x4A, 0x49 };
    satz_raise_scu(vectors[level]);
}

static uint32_t onchip_read(uint32_t address, int size)
{
    uint32_t offset = address - 0xFFFFFE00u;
    if (offset == 0x11)
        return satz_frt_ftcsr[satz_cpu];
    if (offset == 0x10)
        return satz_frt_tier[satz_cpu];
    uint32_t v = 0;
    for (int i = 0; i < size; ++i)
        v = (v << 8) | onchip[satz_cpu][(offset + i) & 0x1FF];
    return v;
}

static void onchip_write(uint32_t address, uint32_t value, int size)
{
    uint32_t offset = address - 0xFFFFFE00u;
    if (offset == 0x11) {
        satz_frt_ftcsr[satz_cpu] &= (uint8_t)value;
        return;
    }
    if (offset == 0x10) {
        satz_frt_tier[satz_cpu] = (uint8_t)value;
        return;
    }
    for (int i = size - 1; i >= 0; --i, value >>= 8)
        onchip[satz_cpu][(offset + i) & 0x1FF] = (uint8_t)value;
    if (offset == 0x104 && size == 4) {
        int32_t divisor = (int32_t)(((uint32_t)onchip[satz_cpu][0x100] << 24) | ((uint32_t)onchip[satz_cpu][0x101] << 16) |
            ((uint32_t)onchip[satz_cpu][0x102] << 8) | onchip[satz_cpu][0x103]);
        int32_t dividend = (int32_t)(((uint32_t)onchip[satz_cpu][0x104] << 24) | ((uint32_t)onchip[satz_cpu][0x105] << 16) |
            ((uint32_t)onchip[satz_cpu][0x106] << 8) | onchip[satz_cpu][0x107]);
        int32_t q = divisor ? dividend / divisor : 0, r = divisor ? dividend % divisor : 0;
        uint32_t uq = (uint32_t)q, ur = (uint32_t)r;
        for (int i = 0; i < 4; ++i) {
            onchip[satz_cpu][0x104 + i] = (uint8_t)(uq >> (24 - 8 * i));
            onchip[satz_cpu][0x114 + i] = (uint8_t)(uq >> (24 - 8 * i));
            onchip[satz_cpu][0x110 + i] = (uint8_t)(ur >> (24 - 8 * i));
        }
    }
}

uint32_t satz_mmio_read(uint32_t address, int size, int* handled)
{
    *handled = 1;
    if (address >= 0xFFFFFE00u)
        return onchip_read(address, size);
    uint32_t phys = satz_phys(address);
    if (in_range(phys, 0x05818000u, 0x8000u)) {
        uint32_t stream = cdb_data_read(size), packed = 0;
        uint8_t bytes[4];
        for (int i = size - 1; i >= 0; --i, stream >>= 8)
            bytes[i] = (uint8_t)stream;
        memcpy(&packed, bytes, (size_t)size);
        return packed;
    }
    if (in_range(phys, 0x05890000u, 0x10000u)) {
        uint32_t reg = phys & 0x3F;
        if (reg == 0x08)
            return cdb_hirq();
        return 0;
    }
    if (in_range(phys, 0x00100000u, 0x80u)) {
        uint32_t reg = phys & 0x7F;
        if (reg == 0x63)
            return smpc_status;
        if (reg == 0x75 || reg == 0x77)
            return 0xFF;
        return 0;
    }
    if (in_range(phys, 0x05B00000u, 0x100000u))
        return satz_sound_reg_read(phys & 0xFFFFF, size);
    if (in_range(phys, 0x05D00000u, 0x20u))
        return vdp1_read_reg(phys & 0x1F);
    if (in_range(phys, 0x05F80000u, 0x10u) && size == 2)
        return vdp2_read_reg(phys & 0xF);
    if (in_range(phys, 0x05FE0000u, 0x100u)) {
        uint32_t reg = phys & 0xFF;
        if (reg == 0xA0)
            return satz_scu_mask();
        if (reg == 0x7C)
            return 0;
        return scu_regs[(reg >> 2) & 0x3F];
    }
    *handled = 0;
    return 0;
}

int satz_mmio_write(uint32_t address, uint32_t value, int size)
{
    if (address >= 0xFFFFFE00u) {
        onchip_write(address, value, size);
        return 1;
    }
    uint32_t phys = satz_phys(address);
    if (in_range(phys, 0x01000000u, 0x800000u)) {
        satz_slave_trigger();
        return 1;
    }
    if (in_range(phys, 0x01800000u, 0x800000u))
        return 1;
    if (in_range(phys, 0x05890000u, 0x10000u)) {
        uint32_t reg = phys & 0x3F;
        if (reg == 0x08)
            cdb_hirq_clear(value);
        return 1;
    }
    if (in_range(phys, 0x00100000u, 0x80u)) {
        uint32_t reg = phys & 0x7F;
        if (reg == 0x63)
            smpc_status = (uint8_t)value;
        else if (reg == 0x1F) {
            satz_smpc_command((uint8_t)value);
            smpc_status = 0;
        }
        return 1;
    }
    if (in_range(phys, 0x05B00000u, 0x100000u)) {
        satz_sound_reg_write(phys & 0xFFFFF, value, size);
        return 1;
    }
    if (in_range(phys, 0x05D00000u, 0x20u)) {
        vdp1_write_reg(phys & 0x1F, value, size);
        return 1;
    }
    if (in_range(phys, 0x05FE0000u, 0x100u)) {
        uint32_t reg = phys & 0xFF;
        scu_regs[(reg >> 2) & 0x3F] = value;
        if (reg == 0xA0 && size == 4)
            satz_set_scu_mask(value);
        else if (reg < 0x60 && (reg & 0x1F) == 0x10 && (value & 0x100) && (scu_regs[(reg >> 2) + 1] & 7) == 7)
            scu_dma_start((int)(reg >> 5));
        return 1;
    }
    return 0;
}

unsigned int satz_bus_read(unsigned long address, int size)
{
    int handled;
    uint32_t a = (uint32_t)address;
    uint32_t v = satz_mmio_read(a, size, &handled);
    if (!handled)
        v = satz_mem_read(a, size);
    if (!(handled && satz_phys(a) - 0x05818000u < 0x8000u))
        satz_poll(a, v);
    return v;
}

void satz_bus_write(unsigned long address, unsigned int value, int size)
{
    extern void satz_poll_reset(void);
    uint32_t a = (uint32_t)address;
    satz_poll_reset();
    if (!satz_mmio_write(a, value, size))
        satz_mem_write(a, value, size);
    satz_charge(2);
}

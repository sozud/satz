#ifndef SATZ_H
#define SATZ_H

#include <stdlib.h>
#include <string.h>

unsigned int satz_get_sr(void);
void satz_set_sr(unsigned int sr);
unsigned int satz_get_vbr(void);
void satz_die(const char* format, ...) __attribute__((noreturn));
void satz_log(const char* format, ...);

void satz_thread_prepare(unsigned int slot, void (*entry)(void));
void satz_thread_run(unsigned int slot);
void satz_thread_yield(void);

void satz_dma_copy(unsigned long dst, unsigned long src, unsigned long units, int unit_bytes, int src_step, int dst_step);
void satz_scu_vector_set(unsigned int vector, void (*handler)(void));
void (*satz_scu_vector_get(unsigned int vector))(void);
unsigned int satz_pad_buttons(void);

int cdb_cmd(int command, int a, int b, int c, int d);
int cdb_result(int index);
unsigned int cdb_toc(int index);
unsigned int cdb_hirq(void);
void cdb_hirq_clear(unsigned int keep);
void satz_charge(unsigned int ticks);

#define _builtin_get_cr() satz_get_sr()
#define _builtin_set_cr(sr) satz_set_sr(sr)


void satz_bios_change_scu_mask(unsigned int and_mask, unsigned int or_mask);
void satz_set_scu_mask(unsigned int mask);
unsigned int satz_scu_mask(void);
unsigned int satz_bios_test_and_set(unsigned int number);
void satz_bios_clear_semaphore(unsigned int number);
void satz_bios_change_clock(int mode);
unsigned int satz_bus_read(unsigned long address, int size);
void satz_bus_write(unsigned long address, unsigned int value, int size);
void satz_bus_store(unsigned int address, const void* bytes, unsigned long count);
void* satz_bus_pointer(unsigned long address);
unsigned long satz_bus_address(const volatile void* pointer);
void satz_idle(void);

#if defined(__GNUC__) && !defined(__clang__)
typedef struct __attribute__((scalar_storage_order("big-endian"))) {
    unsigned short v;
} SatzBe16;
typedef struct __attribute__((scalar_storage_order("big-endian"))) {
    unsigned int v;
} SatzBe32;
#define SATZ_BE16_FIELD(object, field) ((SatzBe16*)((unsigned char*)&(object) + __builtin_offsetof(__typeof__(object), field)))
#define SATZ_BE32_FIELD(object, field) ((SatzBe32*)((unsigned char*)&(object) + __builtin_offsetof(__typeof__(object), field)))
#endif

#define SATZ_BUS_HANDLE 0x4000000000000000ul

static inline unsigned int satz_field_get(const volatile void* p, unsigned long offset, int size)
{
    if (((unsigned long)p & ~0xFFFFFFFFul) == SATZ_BUS_HANDLE)
        return satz_bus_read(satz_bus_address(p) + offset, size);
    const volatile unsigned char* b = (const volatile unsigned char*)p + offset;
    unsigned int v = 0;
    for (int i = 0; i < size; ++i)
        v = v << 8 | b[i];
    return v;
}

static inline void satz_field_set(volatile void* p, unsigned long offset, int size, unsigned int value)
{
    if (((unsigned long)p & ~0xFFFFFFFFul) == SATZ_BUS_HANDLE) {
        satz_bus_write(satz_bus_address(p) + offset, value, size);
        return;
    }
    volatile unsigned char* b = (volatile unsigned char*)p + offset;
    for (int i = size - 1; i >= 0; --i, value >>= 8)
        b[i] = (unsigned char)value;
}

#define SATZ_FIELD_GET(p, type, field) \
    ((__typeof__(((type*)0)->field))satz_field_get((p), __builtin_offsetof(type, field), sizeof(((type*)0)->field)))
#define SATZ_FIELD_SET(p, type, field, value) \
    satz_field_set((p), __builtin_offsetof(type, field), sizeof(((type*)0)->field), (unsigned int)(value))

static inline void satz_bus_write16s(unsigned long address, const void* values, unsigned long count)
{
    for (unsigned long i = 0; i < count; ++i)
        satz_bus_write(address + i * 2, ((const unsigned short*)values)[i], 2);
}

static inline void satz_bus_write32s(unsigned long address, const void* values, unsigned long count)
{
    for (unsigned long i = 0; i < count; ++i)
        satz_bus_write(address + i * 4, ((const unsigned int*)values)[i], 4);
}
#endif

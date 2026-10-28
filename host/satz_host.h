#ifndef SATZ_HOST_H
#define SATZ_HOST_H

#include <stdint.h>
#include <stddef.h>
#include <ucontext.h>

#define SATZ_LWRAM 0x00200000u
#define SATZ_HWRAM 0x06000000u
#define SATZ_CACHE_THROUGH 0x20000000u
#define SATZ_BBUS 0x25800000u
#define SATZ_BBUS_SIZE 0x00800000u
#define SATZ_VDP1_VRAM 0x25C00000u
#define SATZ_VDP1_FB 0x25C80000u
#define SATZ_VDP1_REGS 0x25D00000u
#define SATZ_VDP2_VRAM 0x25E00000u
#define SATZ_VDP2_CRAM 0x25F00000u
#define SATZ_VDP2_REGS 0x25F80000u
#define SATZ_SCU_REGS 0x25FE0000u
#define SATZ_SOUND_RAM 0x25A00000u

#define SATZ_BUS_HANDLE 0x4000000000000000ul
#define SATZ_HOST_WINDOW 0x02000000u
#define SATZ_HOST_WINDOW_SIZE 0x03800000u
enum { SATZ_REGION_NONE, SATZ_REGION_RAM, SATZ_REGION_BIG, SATZ_REGION_HOST, SATZ_REGION_REGISTER, SATZ_REGION_DEVICE };

static inline uint32_t satz_phys(uint32_t a) { return a & 0x1FFFFFFFu; }

int satz_bus_region(uint32_t address, uint8_t** base, uint32_t* offset);
uint32_t satz_mem_read(uint32_t address, int size);
void satz_dma_copy(unsigned long dst, unsigned long src, unsigned long units, int unit_bytes, int src_step, int dst_step);
void satz_mem_write(uint32_t address, uint32_t value, int size);
void* satz_bus_pointer(unsigned long address);
unsigned long satz_bus_address(const volatile void* pointer);
void satz_bus_load(uint32_t address, void* bytes, size_t count);
void satz_bus_store(uint32_t address, const void* bytes, size_t count);
typedef struct {
    uint32_t start, size;
    uint8_t* data;
} SatzRamArea;
extern const SatzRamArea satz_ram_areas[];
extern const unsigned satz_ram_area_count;

static inline uint8_t satz_r8(uint32_t a) { return (uint8_t)satz_mem_read(a, 1); }
static inline uint16_t satz_r16(uint32_t a) { return (uint16_t)satz_mem_read(a, 2); }
static inline uint32_t satz_r32(uint32_t a) { return satz_mem_read(a, 4); }
static inline void satz_w8(uint32_t a, uint8_t v) { satz_mem_write(a, v, 1); }
static inline void satz_w16(uint32_t a, uint16_t v) { satz_mem_write(a, v, 2); }
static inline void satz_w32(uint32_t a, uint32_t v) { satz_mem_write(a, v, 4); }

void satz_map_bios(const char* path);

typedef struct SatzCoro {
    ucontext_t context;
    void* stack;
    size_t stack_size;
    const void* asan_stack;
    size_t asan_size;
} SatzCoro;
void satz_coro_init(SatzCoro* coro, size_t stack_size, void (*entry)(uintptr_t), uintptr_t argument);
void satz_coro_switch(SatzCoro* from, SatzCoro* to);
void satz_coro_free(SatzCoro* coro);

enum { SATZ_MASTER = 0, SATZ_SLAVE = 1 };
extern int satz_cpu;

void satz_sched_init(void);
void satz_poll(uint32_t address, uint32_t value);
void satz_charge(uint32_t ticks);
void satz_raise_scu(int vector);
void satz_check_interrupts(void);
uint32_t satz_line(void);
uint64_t satz_frame(void);
uint32_t satz_scu_mask(void);
void satz_set_scu_mask(uint32_t mask);
void satz_slave_start(void (*entry)(void));
void satz_idle(void);
void satz_slave_trigger(void);

uint32_t satz_mmio_read(uint32_t address, int size, int* handled);
uint32_t satz_scu_reg(uint32_t offset);
int satz_mmio_write(uint32_t address, uint32_t value, int size);

void satz_bios_init(void);
void satz_smpc_command(uint8_t command);

void satz_frame_begin(uint64_t frame);
void satz_frame_rendered(uint64_t frame);
void satz_window_open(const char* title);
int satz_window_present(const uint32_t* pixels, int pitch, int width, int height, const int16_t* samples, int frames,
    uint16_t* pad_bits);
void satz_window_close(void);
int satz_window_fast_key(void);
uint64_t satz_now(void);
uint16_t satz_pad_buttons(void);

void satz_die(const char* format, ...) __attribute__((noreturn, format(printf, 1, 2)));
void satz_log(const char* format, ...) __attribute__((format(printf, 1, 2)));

extern const char* satz_output_dir;

#endif

#include "satz_host.h"
#include "cdblock.h"
#include "vdp.h"
#include "../sound/satz_sound.h"
#include "../video/satz_video.h"
#include <stdlib.h>
#include <string.h>

#define CYCLES_PER_LINE 1820u
#define LINES_PER_FRAME 263u
#define VBLANK_LINE 224u
#define SPIN_LIMIT 32

int satz_cpu = SATZ_MASTER;

static uint64_t now;
static uint64_t frame_count;
static uint32_t line;
static uint64_t next_line_at = CYCLES_PER_LINE;
static uint32_t sr[2] = { 0xF0, 0xF0 };
static uint32_t scu_ims = 0xBFFF;
static uint32_t scu_ist;
static int in_interrupt[2];
static int spins;

static const uint8_t levels[16] = { 0xF, 0xE, 0xD, 0xC, 0xB, 0xA, 0x9, 0x8, 0x8, 0x6, 0x6, 0x5, 0x3, 0x2, 0, 0 };

void (*satz_scu_vectors[0x80])(void);

uint32_t satz_line(void) { return line; }
uint64_t satz_frame(void) { return frame_count; }
uint32_t satz_scu_mask(void) { return scu_ims; }

void satz_set_scu_mask(uint32_t mask)
{
    scu_ims = mask;
    satz_check_interrupts();
}

unsigned int satz_get_sr(void) { return sr[satz_cpu]; }

void satz_set_sr(unsigned int value)
{
    sr[satz_cpu] = value;
    satz_check_interrupts();
}

unsigned int satz_get_vbr(void) { return satz_cpu == SATZ_MASTER ? 0x06000000u : 0x06000400u; }

void satz_raise_scu(int vector)
{
    scu_ist |= 1u << (vector - 0x40);
    satz_check_interrupts();
}

void satz_check_interrupts(void)
{
    if (satz_cpu != SATZ_MASTER)
        return;
    for (;;) {
        uint32_t pending = scu_ist & ~scu_ims;
        int best = -1;
        for (int bit = 0; bit < 16; ++bit)
            if ((pending >> bit) & 1 && (best < 0 || levels[bit] > levels[best]))
                best = bit;
        if (best < 0 || levels[best] <= ((sr[SATZ_MASTER] >> 4) & 0xF))
            return;
        scu_ist &= ~(1u << best);
        void (*handler)(void) = satz_scu_vectors[0x40 + best];
        if (!handler)
            continue;
        uint32_t saved = sr[SATZ_MASTER];
        sr[SATZ_MASTER] = (saved & ~0xF0u) | ((uint32_t)levels[best] << 4);
        in_interrupt[SATZ_MASTER]++;
        handler();
        in_interrupt[SATZ_MASTER]--;
        sr[SATZ_MASTER] = saved;
    }
}

static void timer0(uint32_t counter)
{
    if ((satz_scu_reg(0x98) & 1) && counter == (satz_scu_reg(0x90) & 0x3FF))
        satz_raise_scu(0x43);
}

static void end_of_line(void)
{
    satz_video_hblank(now);
    timer0(line + 1);
    if (line < VBLANK_LINE)
        satz_raise_scu(0x42);
    line++;
    if (line == VBLANK_LINE) {
        satz_video_vblank(now, 1);
        satz_frame_rendered(frame_count);
        satz_raise_scu(0x40);
    }
    if (line == LINES_PER_FRAME) {
        line = 0;
        timer0(0);
        frame_count++;
        satz_frame_begin(frame_count);
        satz_video_vblank(now, 0);
        satz_raise_scu(0x41);
    }
}

static void advance_to(uint64_t target)
{
    while (now < target) {
        uint64_t step = target;
        if (next_line_at < step)
            step = next_line_at;
        uint64_t vdp1 = satz_video_next_event();
        if (vdp1 > now && vdp1 < step)
            step = vdp1;
        cdb_advance(now, step);
        now = step;
        satz_sound_advance(now);
        satz_video_advance(now);
        if (now >= next_line_at) {
            next_line_at += CYCLES_PER_LINE;
            end_of_line();
        }
    }
}

void satz_charge(uint32_t ticks)
{
    if (satz_cpu != SATZ_MASTER)
        return;
    advance_to(now + ticks);
}

uint64_t satz_now(void) { return now; }

extern void satz_slave_yield(void);

static void skip_to_next_event(void)
{
    uint64_t target = next_line_at;
    uint64_t vdp1 = satz_video_next_event();
    if (vdp1 > now && vdp1 < target)
        target = vdp1;
    uint64_t cd = cdb_next_event(now);
    if (cd && cd < target)
        target = cd;
    advance_to(target);
}

extern void satz_slave_yield(void);

void satz_idle(void)
{
    if (satz_cpu == SATZ_SLAVE) {
        satz_slave_yield();
        return;
    }
    skip_to_next_event();
}

void satz_poll_reset(void) { spins = 0; }

void satz_poll(uint32_t address, uint32_t value)
{
    (void)address;
    (void)value;
    if (++spins >= SPIN_LIMIT) {
        spins = 0;
        satz_idle();
        return;
    }
    satz_charge(4);
}

void satz_sched_init(void)
{
    memset(satz_scu_vectors, 0, sizeof(satz_scu_vectors));
}

static SatzCoro slave_coro, master_context;
static int slave_running;

static void slave_main(uintptr_t entry)
{
    ((void (*)(void))entry)();
    satz_die("slave entry returned");
}

void satz_slave_yield(void)
{
    satz_cpu = SATZ_MASTER;
    satz_coro_switch(&slave_coro, &master_context);
}

static void run_slave(void)
{
    if (!slave_running || satz_cpu == SATZ_SLAVE)
        return;
    satz_cpu = SATZ_SLAVE;
    satz_coro_switch(&master_context, &slave_coro);
}

void satz_slave_start(void (*entry)(void))
{
    if (!entry)
        return;
    satz_coro_init(&slave_coro, 1 << 20, slave_main, (uintptr_t)entry);
    slave_running = 1;
    run_slave();
}

void satz_slave_stop(void) { slave_running = 0; }

uint8_t satz_frt_ftcsr[2];
uint8_t satz_frt_tier[2];

void satz_slave_trigger(void)
{
    satz_frt_ftcsr[SATZ_SLAVE] |= 0x80;
    run_slave();
}


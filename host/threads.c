#include "satz_host.h"
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

#define THREADS 6

static SatzCoro threads[THREADS];
static SatzCoro scheduler;
static int current = -1;

static void thread_main(uintptr_t entry)
{
    ((void (*)(void))entry)();
    satz_die("game thread %d returned", current);
}

void satz_thread_prepare(uint32_t slot, void (*entry)(void))
{
    if (slot >= THREADS)
        satz_die("thread slot %u out of range", slot);
    satz_coro_init(&threads[slot], 1 << 20, thread_main, (uintptr_t)entry);
}

void satz_thread_run(uint32_t slot)
{
    if (slot >= THREADS || !threads[slot].stack)
        satz_die("resume of unprepared thread %u", slot);
    if (current >= 0)
        satz_die("thread %d resumed thread %u", current, slot);
    current = (int)slot;
    satz_coro_switch(&scheduler, &threads[slot]);
    current = -1;
}

static void trace_yield(void)
{
    static int parsed;
    static unsigned long long from, to;
    if (!parsed) {
        const char* range = getenv("SATZ_TRACE_YIELD");
        parsed = 1;
        if (!range || sscanf(range, "%llu:%llu", &from, &to) != 2)
            from = 1, to = 0;
    }
    unsigned long long frame = satz_frame();
    if (frame < from || frame > to)
        return;
    void* stack[8];
    int n = backtrace(stack, 8);
    fprintf(stderr, "yield %llu thread %d:", frame, current);
    for (int i = 1; i < n; ++i)
        fprintf(stderr, " %p", stack[i]);
    fputc('\n', stderr);
}

void satz_thread_yield(void)
{
    if (current < 0)
        satz_die("thread yield outside a thread");
    trace_yield();
    satz_coro_switch(&threads[current], &scheduler);
}

int satz_thread_current(void) { return current; }

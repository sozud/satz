#define _GNU_SOURCE
#include "satz_host.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <pthread.h>
#include <sanitizer/common_interface_defs.h>
#define SATZ_ASAN 1

static const void* running_stack;
static size_t running_size;

static void running_on_main_stack(void)
{
    pthread_attr_t attr;
    void* bottom;
    size_t size;
    if (running_stack || pthread_getattr_np(pthread_self(), &attr))
        return;
    pthread_attr_getstack(&attr, &bottom, &size);
    pthread_attr_destroy(&attr);
    running_stack = bottom;
    running_size = size;
}
#endif
#endif

static void trampoline(unsigned int entry_high, unsigned int entry_low, unsigned int argument_high, unsigned int argument_low)
{
    void (*entry)(uintptr_t) = (void (*)(uintptr_t))(((uint64_t)entry_high << 32) | entry_low);
#ifdef SATZ_ASAN
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#endif
    entry((uintptr_t)(((uint64_t)argument_high << 32) | argument_low));
    satz_die("coroutine entry returned");
}

#define STACKS 12
#define STACK_SIZE (1u << 20)

static uint8_t stacks[STACKS][STACK_SIZE] __attribute__((aligned(16)));
static uint8_t stack_used[STACKS];

void satz_coro_init(SatzCoro* coro, size_t stack_size, void (*entry)(uintptr_t), uintptr_t argument)
{
    int i;
    satz_coro_free(coro);
    if (stack_size > STACK_SIZE)
        satz_die("coroutine stack of %zu bytes exceeds the pool size", stack_size);
    for (i = 0; i < STACKS && stack_used[i]; ++i) {
    }
    if (i == STACKS)
        satz_die("coroutine stack pool exhausted");
    stack_used[i] = 1;
    coro->stack = stacks[i];
    coro->stack_size = STACK_SIZE;
    getcontext(&coro->context);
    coro->context.uc_stack.ss_sp = coro->stack;
    coro->context.uc_stack.ss_size = stack_size;
    coro->context.uc_link = NULL;
    uint64_t e = (uint64_t)(uintptr_t)entry, a = (uint64_t)argument;
    makecontext(&coro->context, (void (*)(void))trampoline, 4, (unsigned)(e >> 32), (unsigned)e, (unsigned)(a >> 32), (unsigned)a);
}

void satz_coro_switch(SatzCoro* from, SatzCoro* to)
{
#ifdef SATZ_ASAN
    void* fake_stack = NULL;
    running_on_main_stack();
    from->asan_stack = running_stack;
    from->asan_size = running_size;
    running_stack = to->stack ? to->stack : to->asan_stack;
    running_size = to->stack ? to->stack_size : to->asan_size;
    __sanitizer_start_switch_fiber(&fake_stack, running_stack, running_size);
    swapcontext(&from->context, &to->context);
    __sanitizer_finish_switch_fiber(fake_stack, NULL, NULL);
#else
    swapcontext(&from->context, &to->context);
#endif
}

void satz_coro_free(SatzCoro* coro)
{
    if (coro->stack)
        stack_used[((uint8_t(*)[STACK_SIZE])coro->stack) - stacks] = 0;
    coro->stack = NULL;
}

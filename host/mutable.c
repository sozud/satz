#include "satz_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif

#define CHUNK 16

static const uint8_t* segment;
static unsigned long segment_size;
static uint8_t* initial;
static uint8_t* dirty;
static const char* report_path;

static void check(void)
{
    for (unsigned long chunk = 0; chunk < segment_size / CHUNK; ++chunk)
        if (!dirty[chunk] && memcmp(segment + chunk * CHUNK, initial + chunk * CHUNK, CHUNK))
            dirty[chunk] = 1;
}

static void report(void)
{
    if (!initial)
        return;
    check();
    FILE* f = fopen(report_path, "w");
    if (!f)
        return;
    fprintf(f, "# __DATA %p %lx %d\n", (const void*)segment, segment_size, CHUNK);
    for (unsigned long chunk = 0; chunk < segment_size / CHUNK;) {
        if (!dirty[chunk]) {
            ++chunk;
            continue;
        }
        unsigned long end = chunk;
        while (end < segment_size / CHUNK && dirty[end])
            ++end;
        fprintf(f, "%lx %lx\n", chunk * CHUNK, (end - chunk) * CHUNK);
        chunk = end;
    }
    fclose(f);
}

__attribute__((constructor)) static void mutable_init(void)
{
    report_path = getenv("SATZ_MUTABLE_REPORT");
    if (!report_path)
        return;
#ifdef __APPLE__
    segment = getsegmentdata(&_mh_execute_header, "__DATA", &segment_size);
#else
    extern char __data_start[], _end[];
    segment = (const uint8_t*)__data_start;
    segment_size = (unsigned long)(_end - __data_start);
#endif
    initial = malloc(segment_size);
    dirty = calloc(segment_size / CHUNK + 1, 1);
    if (!initial || !dirty)
        return;
    memcpy(initial, segment, segment_size);
    atexit(report);
}

void satz_mutable_tick(uint64_t frame)
{
    if (initial && frame % 60 == 0)
        check();
}

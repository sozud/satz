#include "satz_host.h"
#include "satz_app.h"
#include "cdblock.h"
#include "../sound/satz_sound.h"
#include "../video/satz_video.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char* satz_output_dir = ".";
static uint64_t frame_limit;
static FILE* frames_report;
static uint16_t active_buttons;
static unsigned schedule_frames[4096], schedule_buttons[4096], schedule_count;
static unsigned held_buttons;

void satz_die(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "satz: fatal at frame %llu line %u: ", (unsigned long long)satz_frame(), satz_line());
    vfprintf(stderr, format, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(NULL);
    exit(2);
}

void satz_log(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

extern int satz_display_active(void);
extern uint16_t satz_display_buttons(void);
extern void satz_display_open_window(const char* title);
extern void satz_display_configure(unsigned every);
extern void satz_display_audio(const char* path);

__attribute__((weak)) int satz_app_option(int argc, char** argv, int i)
{
    (void)argc;
    (void)argv;
    (void)i;
    return 0;
}
extern void satz_saturn_main(void) __attribute__((weak));
__attribute__((weak)) void satz_app_entry(void)
{
    if (!satz_saturn_main)
        satz_die("no program entry");
    satz_saturn_main();
}
__attribute__((weak)) void satz_app_start(void) { }
__attribute__((weak)) void satz_app_frame(uint64_t frame) { (void)frame; }
__attribute__((weak)) const char* satz_app_title(void) { return "satz"; }
__attribute__((weak)) const char* satz_app_usage(void) { return ""; }

uint16_t satz_pad_buttons(void) { return active_buttons; }
void satz_pad_set(uint16_t buttons) { active_buttons = buttons; }
int satz_window_active(void) { return satz_display_active(); }

FILE* satz_open_report(const char* name)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", satz_output_dir, name);
    FILE* f = fopen(path, "w");
    if (!f)
        satz_die("cannot write %s", path);
    setvbuf(f, NULL, _IOLBF, 0);
    return f;
}

void satz_frame_begin(uint64_t frame)
{
    if (frames_report)
        fprintf(frames_report, "%llu\t%u\n", (unsigned long long)frame, active_buttons);
    if (frame_limit && frame >= frame_limit) {
        fflush(NULL);
        exit(0);
    }
    for (unsigned i = 0; i < schedule_count; ++i)
        if (schedule_frames[i] == frame)
            held_buttons = schedule_buttons[i];
    active_buttons = satz_display_active() ? satz_display_buttons() : (uint16_t)held_buttons;
    satz_app_frame(frame);
}

static void load_inputs(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f)
        satz_die("cannot read input schedule %s", path);
    unsigned frame, buttons;
    while (fscanf(f, "%u %u", &frame, &buttons) == 2 && schedule_count < 4096) {
        schedule_frames[schedule_count] = frame;
        schedule_buttons[schedule_count++] = buttons;
    }
    fclose(f);
}

static void usage(const char* program)
{
    fprintf(stderr, "usage: %s --output DIR [--cue DISC.cue] [--bios BIOS.bin] [--frames N] [--inputs SCHEDULE] [--snap-every N] "
                    "[--window] [--audio FILE] [--frames-log]%s\n",
        program, satz_app_usage());
    exit(1);
}

static void master_main(uintptr_t argument)
{
    (void)argument;
    satz_app_entry();
    satz_die("program entry returned");
}

int main(int argc, char** argv)
{
    const char* cue = NULL;
    const char* inputs = NULL;
    const char* bios = NULL;
    int window = 0;
    const char* audio = NULL;
    unsigned snap_every = 0;
    int frames_log = 0;
    for (int i = 1; i < argc; ++i) {
        int used;
        if (!strcmp(argv[i], "--cue") && i + 1 < argc)
            cue = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc)
            satz_output_dir = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frame_limit = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--inputs") && i + 1 < argc)
            inputs = argv[++i];
        else if (!strcmp(argv[i], "--bios") && i + 1 < argc)
            bios = argv[++i];
        else if (!strcmp(argv[i], "--window"))
            window = 1;
        else if (!strcmp(argv[i], "--snap-every") && i + 1 < argc)
            snap_every = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--audio") && i + 1 < argc)
            audio = argv[++i];
        else if (!strcmp(argv[i], "--frames-log"))
            frames_log = 1;
        else if ((used = satz_app_option(argc, argv, i)) > 0)
            i += used - 1;
        else
            usage(argv[0]);
    }
    mkdir(satz_output_dir, 0777);
    satz_map_bios(bios);
    if (cue) {
        int rc = cdb_open(cue);
        if (rc)
            satz_die("cannot open disc %s (%d)", cue, rc);
    }
    if (inputs)
        load_inputs(inputs);
    if (frames_log) {
        frames_report = satz_open_report("frames.tsv");
        fprintf(frames_report, "frame\tbuttons\n");
    }
    satz_sched_init();
    satz_sound_init();
    satz_video_init();
    satz_display_configure(snap_every);
    if (audio)
        satz_display_audio(audio);
    if (window)
        satz_display_open_window(satz_app_title());
    satz_app_start();
    satz_frame_begin(0);
    static SatzCoro host, master;
    satz_coro_init(&master, 1 << 20, master_main, 0);
    satz_coro_switch(&host, &master);
    satz_die("program entry returned");
}

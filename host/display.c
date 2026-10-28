#include "satz_host.h"
#include "satz_app.h"
#include "../sound/satz_sound.h"
#include "../video/satz_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t pixels[704 * 512];
static int window_open;
static unsigned snap_every;
static int last_width, last_height, have_picture;
static uint16_t window_buttons;
static int16_t audio[8192 * 2];
static FILE* audio_out;

void satz_display_audio(const char* path)
{
    audio_out = fopen(path, "wb");
    if (!audio_out)
        satz_die("cannot write %s", path);
}

void satz_display_open_window(const char* title)
{
    satz_window_open(title);
    window_open = 1;
}

void satz_display_configure(unsigned every) { snap_every = every; }

int satz_display_active(void) { return window_open; }
uint16_t satz_display_buttons(void) { return window_buttons; }

static void render(void)
{
    have_picture = satz_video_render(pixels, &last_width, &last_height);
}

void satz_display_write_ppm(const char* name)
{
    if (!have_picture || last_width <= 0 || last_height <= 0)
        return;
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", satz_output_dir, name);
    FILE* f = fopen(path, "wb");
    if (!f)
        satz_die("cannot write %s", path);
    fprintf(f, "P6\n%d %d\n255\n", last_width, last_height);
    uint8_t* row = malloc((size_t)last_width * 3);
    for (int y = 0; y < last_height; ++y) {
        for (int x = 0; x < last_width; ++x) {
            uint32_t c = pixels[y * 704 + x];
            row[x * 3] = (uint8_t)c;
            row[x * 3 + 1] = (uint8_t)(c >> 8);
            row[x * 3 + 2] = (uint8_t)(c >> 16);
        }
        fwrite(row, 1, (size_t)last_width * 3, f);
    }
    free(row);
    fclose(f);
}

__attribute__((weak)) int satz_app_snap_name(uint64_t frame, char* name, size_t size)
{
    (void)frame;
    (void)name;
    (void)size;
    return 0;
}

__attribute__((weak)) int satz_app_fast_forward(void) { return 0; }

#define FAST_FORWARD_SHOWN 8

void satz_frame_rendered(uint64_t frame)
{
    static unsigned fast_skipped;
    char app_name[256];
    int app_due = satz_app_snap_name(frame, app_name, sizeof(app_name));
    int snap_due = snap_every && frame % snap_every == 0;
    if (audio_out && !window_open) {
        int frames = satz_sound_take_samples(audio, 8192);
        fwrite(audio, 4, (size_t)frames, audio_out);
    }
    int fast = window_open && (satz_app_fast_forward() || satz_window_fast_key());
    if (fast && !app_due && !snap_due && ++fast_skipped < FAST_FORWARD_SHOWN) {
        satz_sound_take_samples(NULL, 8192);
        return;
    }
    fast_skipped = 0;
    if (!window_open && !app_due && !snap_due)
        return;
    render();
    if (snap_due) {
        char name[64];
        snprintf(name, sizeof(name), "video_%06llu.ppm", (unsigned long long)frame);
        satz_display_write_ppm(name);
    }
    if (app_due)
        satz_display_write_ppm(app_name);
    if (window_open) {
        int frames = satz_sound_take_samples(audio, 8192);
        if (satz_window_present(pixels, 704, have_picture ? last_width : 0, have_picture ? last_height : 0, audio,
                fast ? 0 : frames, &window_buttons)) {
            satz_window_close();
            fflush(NULL);
            exit(0);
        }
    }
}

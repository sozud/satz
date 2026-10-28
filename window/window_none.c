#include "../host/satz_host.h"

void satz_window_open(const char* title)
{
    (void)title;
    satz_die("built without window support (build with --window)");
}

int satz_window_present(const uint32_t* pixels, int pitch, int width, int height, const int16_t* samples, int frames,
    uint16_t* pad_bits)
{
    (void)pixels;
    (void)pitch;
    (void)width;
    (void)height;
    (void)samples;
    (void)frames;
    (void)pad_bits;
    return 1;
}

void satz_window_close(void) { }
int satz_window_fast_key(void) { return 0; }

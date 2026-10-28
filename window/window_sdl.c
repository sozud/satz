#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "../host/satz_host.h"

enum { PAD_Z = 1 << 0, PAD_Y = 1 << 1, PAD_X = 1 << 2, PAD_R = 1 << 3, PAD_UP = 1 << 4, PAD_DOWN = 1 << 5,
    PAD_LEFT = 1 << 6, PAD_RIGHT = 1 << 7, PAD_B = 1 << 8, PAD_C = 1 << 9, PAD_A = 1 << 10, PAD_START = 1 << 11,
    PAD_L = 1 << 15 };

static const struct {
    SDL_Scancode key;
    uint16_t bit;
} keys[] = {
    { SDL_SCANCODE_UP, PAD_UP }, { SDL_SCANCODE_DOWN, PAD_DOWN }, { SDL_SCANCODE_LEFT, PAD_LEFT },
    { SDL_SCANCODE_RIGHT, PAD_RIGHT }, { SDL_SCANCODE_Z, PAD_A }, { SDL_SCANCODE_X, PAD_B }, { SDL_SCANCODE_C, PAD_C },
    { SDL_SCANCODE_A, PAD_X }, { SDL_SCANCODE_S, PAD_Y }, { SDL_SCANCODE_D, PAD_Z }, { SDL_SCANCODE_Q, PAD_L },
    { SDL_SCANCODE_W, PAD_R }, { SDL_SCANCODE_RETURN, PAD_START },
};

static const struct {
    SDL_GameControllerButton button;
    uint16_t bit;
} buttons[] = {
    { SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT },
    { SDL_CONTROLLER_BUTTON_A, PAD_A }, { SDL_CONTROLLER_BUTTON_B, PAD_B }, { SDL_CONTROLLER_BUTTON_X, PAD_X },
    { SDL_CONTROLLER_BUTTON_Y, PAD_Y }, { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_L },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_R }, { SDL_CONTROLLER_BUTTON_START, PAD_START },
    { SDL_CONTROLLER_BUTTON_BACK, PAD_C },
};

static SDL_Renderer* renderer;
static SDL_Texture* texture;
static int texture_w, texture_h;
static SDL_AudioDeviceID audio;
static SDL_GameController* pad;
static uint64_t next, period;

void satz_window_open(const char* title)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER))
        satz_die("window: %s", SDL_GetError());
    SDL_Window* window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 320 * 3, 224 * 3,
        SDL_WINDOW_RESIZABLE);
    if (!window)
        satz_die("window: %s", SDL_GetError());
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
        satz_die("window: %s", SDL_GetError());
    SDL_RenderSetLogicalSize(renderer, 320, 224);
    SDL_AudioSpec want = { 0 }, have;
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio)
        SDL_PauseAudioDevice(audio, 0);
    next = SDL_GetPerformanceCounter();
    period = SDL_GetPerformanceFrequency() / 60;
}

int satz_window_present(const uint32_t* pixels, int pitch, int width, int height, const int16_t* samples, int frames,
    uint16_t* pad_bits)
{
    if (audio && frames && SDL_GetQueuedAudioSize(audio) < 44100 * 4 / 4)
        SDL_QueueAudio(audio, samples, (Uint32)frames * 4);
    if (width && height) {
        if (!texture || texture_w != width || texture_h != height) {
            if (texture)
                SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, width, height);
            texture_w = width;
            texture_h = height;
            SDL_RenderSetLogicalSize(renderer, texture_w > 400 ? texture_w / 2 : texture_w, texture_h > 256 ? texture_h / 2 : texture_h);
        }
        SDL_UpdateTexture(texture, NULL, pixels, pitch * 4);
    }
    int quit = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE))
            quit = 1;
        if (event.type == SDL_CONTROLLERDEVICEADDED && !pad)
            pad = SDL_GameControllerOpen(event.cdevice.which);
    }
    const Uint8* state = SDL_GetKeyboardState(NULL);
    uint16_t bits = 0;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (state[keys[i].key])
            bits |= keys[i].bit;
    if (pad)
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
            if (SDL_GameControllerGetButton(pad, buttons[i].button))
                bits |= buttons[i].bit;
    SDL_RenderClear(renderer);
    if (texture)
        SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    next += period;
    uint64_t now = SDL_GetPerformanceCounter();
    if (next > now)
        SDL_Delay((Uint32)((next - now) * 1000 / SDL_GetPerformanceFrequency()));
    else
        next = now;
    *pad_bits = bits;
    return quit;
}

void satz_window_close(void) { SDL_Quit(); }

int satz_window_fast_key(void) { return SDL_GetKeyboardState(NULL)[SDL_SCANCODE_TAB]; }

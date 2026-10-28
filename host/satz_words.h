#ifndef SATZ_WORDS_H
#define SATZ_WORDS_H
#include <stdint.h>

static inline uint32_t satz_words_read(const uint16_t* words, uint32_t offset, int size)
{
    if (size == 4)
        return ((uint32_t)words[offset >> 1] << 16) | words[(offset >> 1) + 1];
    uint16_t v = words[offset >> 1];
    if (size == 1)
        return (offset & 1) ? (v & 0xFFu) : (uint32_t)(v >> 8);
    return v;
}

static inline void satz_words_write(uint16_t* words, uint32_t offset, uint32_t value, int size)
{
    if (size == 4) {
        words[offset >> 1] = (uint16_t)(value >> 16);
        words[(offset >> 1) + 1] = (uint16_t)value;
    } else if (size == 2)
        words[offset >> 1] = (uint16_t)value;
    else {
        uint16_t* w = &words[offset >> 1];
        *w = (offset & 1) ? (uint16_t)((*w & 0xFF00u) | (value & 0xFFu)) : (uint16_t)((*w & 0x00FFu) | ((value & 0xFFu) << 8));
    }
}

#endif

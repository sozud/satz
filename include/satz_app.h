#ifndef SATZ_APP_H
#define SATZ_APP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void satz_app_entry(void);
int satz_app_option(int argc, char** argv, int i);
void satz_app_start(void);
void satz_app_frame(uint64_t frame);
int satz_app_snap_name(uint64_t frame, char* name, size_t size);
int satz_app_fast_forward(void);
const char* satz_app_title(void);
const char* satz_app_usage(void);

void satz_pad_set(uint16_t buttons);
uint16_t satz_pad_buttons(void);
int satz_window_active(void);
FILE* satz_open_report(const char* name);
void* satz_bus_pointer(unsigned long address);
void satz_bus_load(uint32_t address, void* bytes, size_t count);
extern const char* satz_output_dir;

#endif

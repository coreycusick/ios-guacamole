#pragma once
#include <stdint.h>

#define SLIDER_UNKNOWN	(-1)
#define SLIDER_TOP	0
#define SLIDER_MIDDLE	1
#define SLIDER_BOTTOM	2

/* Alert Slider position; raw = original cfg bytes << 8 | low-pin mask (bit 0 top, 1 middle, 2 bottom). */
int bootsel_slider(uint32_t *raw);
void bootsel_show(const char *label, int pos);
void bootsel_report(uintptr_t dtb, int pos, uint32_t raw);

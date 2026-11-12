/*
 * Boot selector, step 1 (diagnostics only): read the Alert Slider and report
 * what the Android chain-loader will need. Every position still boots iOS.
 *
 * Alert Slider (OnePlus tri-state key, same pins as xnu 0264 / userland/jb/slider.c):
 * TLMM gpio125 (top), gpio134 (middle), gpio27 (bottom), active low, one pin low per
 * position. The bootloader leaves them pulled down (all read 0), so GPIO_CFG is set to
 * 0 (GPIO function, input, no pull: Android's tri_state_key_active) and read after 2 ms.
 * One 0x1000 page per pin: GPIO_CFG +0, GPIO_IN_OUT +4 (bit 0 = level).
 */
#include <stdint.h>
#include "bootsel.h"
#include "console.h"
#include "fdt.h"

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static void delay_us(unsigned us)
{
	__asm__ volatile("isb" ::: "memory");
	uint64_t end = rsr("cntpct_el0") + rsr("cntfrq_el0") / 1000000 * us + 1;
	for (;;) {
		__asm__ volatile("isb" ::: "memory");
		if (rsr("cntpct_el0") >= end)
			break;
	}
}

static uintptr_t slider_pin(int i)
{
	switch (i) {
	case 0: return 0x03100000UL + 125UL * 0x1000;	/* top: WEST tile */
	case 1: return 0x03d00000UL + 134UL * 0x1000;	/* middle: SOUTH tile */
	default: return 0x03500000UL + 27UL * 0x1000;	/* bottom: EAST tile */
	}
}

int bootsel_slider(uint32_t *raw)
{
	uint32_t low = 0, cfg = 0;
	for (int i = 0; i < 3; i++) {
		cfg |= (rd32(slider_pin(i)) & 0xff) << (8 * i);
		wr32(slider_pin(i), 0);
	}
	__asm__ volatile("dsb sy" ::: "memory");
	delay_us(2000);
	for (int i = 0; i < 3; i++)
		if (!(rd32(slider_pin(i) + 4) & 1))
			low |= 1U << i;
	if (raw)
		*raw = cfg << 8 | low;
	switch (low) {
	case 1: return SLIDER_TOP;
	case 2: return SLIDER_MIDDLE;
	case 4: return SLIDER_BOTTOM;
	default: return SLIDER_UNKNOWN;
	}
}

/*
 * Position name on the console. One con_str per branch, never a table of string
 * pointers: the image runs unrelocated, so data may hold no absolute addresses
 * (build.sh rejects ABS64 relocations).
 */
void bootsel_show(const char *label, int pos)
{
	if (pos == SLIDER_TOP)
		con_str(label, "top");
	else if (pos == SLIDER_MIDDLE)
		con_str(label, "middle");
	else if (pos == SLIDER_BOTTOM)
		con_str(label, "bottom");
	else
		con_str(label, "unknown (no single pin low)");
}

/* /chosen/bootargs in lines of 48 characters. */
static void show_bootargs(uintptr_t dtb)
{
	uintptr_t s;
	uint32_t len;
	if (!fdt_chosen_prop(dtb, "bootargs", &s, &len)) {
		con_str("BOOTARGS", "(none)");
		return;
	}
	char line[49];
	uint32_t i = 0;
	int first = 1;
	while (i < len && *(volatile char *)(s + i)) {
		int n = 0;
		while (n < 48 && i < len && *(volatile char *)(s + i))
			line[n++] = *(volatile char *)(s + i++);
		line[n] = 0;
		con_str(first ? "BOOTARGS" : "", line);
		first = 0;
	}
}

extern char _head[], _end[];

void bootsel_report(uintptr_t dtb, int pos, uint32_t raw)
{
	uint64_t a = 0, b = 0;
	bootsel_show("SLIDER", pos);
	con_hex2("SLIDER", "cfg=", raw >> 8, "low=", raw & 0xff);
	con_hex("SHIM at", (uintptr_t)_head);
	con_hex("SHIM end", (uintptr_t)_end);
	con_hex2("DTB", "at=", dtb, "size=", fdt_totalsize(dtb));
	con_hex2("DTB", "struct=", fdt_size_struct(dtb), "strings=", fdt_size_strings(dtb));
	if (fdt_chosen_u64(dtb, "linux,initrd-start", &a) && fdt_chosen_u64(dtb, "linux,initrd-end", &b))
		con_hex2("INITRD", "lo=", a, "hi=", b);
	show_bootargs(dtb);
	con_skip();
}

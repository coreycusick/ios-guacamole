/*
 * Just enough flattened-device-tree parsing to find what ABL put in
 * /chosen (initrd location). Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	/* byte loads are fine on Device memory as long as they are aligned to 1 */
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

/* Find a property of /chosen: its value address and length. Returns 0 if absent. */
int fdt_chosen_prop(uintptr_t fdt, const char *prop, uintptr_t *val_out, uint32_t *len_out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				*val_out = val;
				*len_out = len;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	uintptr_t val;
	uint32_t len;
	if (!fdt_chosen_prop(fdt, prop, &val, &len))
		return 0;
	if (len == 4)
		*out = be32(val);
	else if (len == 8)
		*out = (uint64_t)be32(val) << 32 | be32(val + 4);
	else
		return 0;
	return 1;
}

uint32_t fdt_totalsize(uintptr_t fdt)
{
	return fdt && be32(fdt) == FDT_MAGIC ? be32(fdt + 4) : 0;
}

uint32_t fdt_size_struct(uintptr_t fdt)
{
	return fdt && be32(fdt) == FDT_MAGIC ? be32(fdt + 36) : 0;
}

uint32_t fdt_size_strings(uintptr_t fdt)
{
	return fdt && be32(fdt) == FDT_MAGIC ? be32(fdt + 32) : 0;
}

/* ---- boot selector: a copy of the tree with new /chosen bootargs and initrd range ---- */

static void put8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }
static uint8_t get8(uintptr_t a) { return *(volatile uint8_t *)a; }

static void put32(uintptr_t a, uint32_t v)
{
	put8(a, v >> 24); put8(a + 1, v >> 16); put8(a + 2, v >> 8); put8(a + 3, v);
}

static void copy8(uintptr_t dst, uintptr_t src, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		put8(dst + i, get8(src + i));
}

/* Write one property (value from src bytes, len) at q; returns the next token address. */
static uintptr_t emit_prop(uintptr_t q, uint32_t nameoff, const void *val, uint32_t len)
{
	put32(q, FDT_PROP);
	put32(q + 4, len);
	put32(q + 8, nameoff);
	q += 12;
	copy8(q, (uintptr_t)val, len);
	q += len;
	while (q & 3)
		put8(q++, 0);
	return q;
}

uint32_t fdt_rebuild_chosen(uintptr_t src, uintptr_t dst, uint32_t dst_max,
    const char *bootargs, uint64_t initrd_lo, uint64_t initrd_hi)
{
	if (!src || be32(src) != FDT_MAGIC)
		return 0;
	uint32_t off_struct = be32(src + 8), off_strings = be32(src + 12);
	uint32_t size_strings = be32(src + 32), size_struct = be32(src + 36);
	if (off_strings < off_struct || off_struct < 40)
		return 0;
	uint32_t ba_len = 0;
	while (bootargs[ba_len])
		ba_len++;
	ba_len++;	/* with the NUL */
	if ((uint64_t)off_struct + size_struct + ba_len + 64 + size_strings > dst_max)
		return 0;

	copy8(dst, src, off_struct);	/* header + reserve map */
	uintptr_t p = src + off_struct, end = p + size_struct, q = dst + off_struct;
	uintptr_t strings = src + off_strings;
	int depth = 0, in_chosen = 0, done = 0;
	uint8_t lo[8], hi[8];
	for (int i = 0; i < 8; i++) {
		lo[i] = initrd_lo >> (56 - 8 * i);
		hi[i] = initrd_hi >> (56 - 8 * i);
	}

	while (p < end) {
		uint32_t tok = be32(p);
		if (tok == FDT_BEGIN_NODE) {
			uintptr_t name = p + 4, next = align4(skip_str(name));
			depth++;
			in_chosen = depth == 2 && streq((const char *)name, "chosen");
			copy8(q, p, next - p);
			q += next - p;
			p = next;
		} else if (tok == FDT_PROP) {
			uint32_t len = be32(p + 4), nameoff = be32(p + 8);
			uintptr_t val = p + 12, next = align4(val + len);
			const char *pname = (const char *)(strings + nameoff);
			if (in_chosen && streq(pname, "bootargs")) {
				q = emit_prop(q, nameoff, bootargs, ba_len);
				done |= 1;
			} else if (in_chosen && streq(pname, "linux,initrd-start")) {
				q = emit_prop(q, nameoff, lo, 8);
				done |= 2;
			} else if (in_chosen && streq(pname, "linux,initrd-end")) {
				q = emit_prop(q, nameoff, hi, 8);
				done |= 4;
			} else {
				copy8(q, p, next - p);
				q += next - p;
			}
			p = next;
		} else if (tok == FDT_END_NODE) {
			in_chosen = 0;
			depth--;
			put32(q, tok);
			q += 4;
			p += 4;
		} else if (tok == FDT_NOP) {
			p += 4;
		} else {	/* FDT_END */
			put32(q, tok);
			q += 4;
			break;
		}
	}
	if (done != 7)
		return 0;	/* /chosen lacked one of the three: leave the tree to ABL's */
	uint32_t new_struct = (uint32_t)(q - (dst + off_struct));
	uint32_t new_strings_off = off_struct + new_struct;
	copy8(dst + new_strings_off, strings, size_strings);
	uint32_t total = new_strings_off + size_strings;
	put32(dst + 4, total);
	put32(dst + 12, new_strings_off);
	put32(dst + 32, size_strings);
	put32(dst + 36, new_struct);
	return total;
}

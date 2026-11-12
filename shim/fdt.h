#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);
/* Value address and length of a /chosen property. Returns 0 if absent. */
int fdt_chosen_prop(uintptr_t fdt, const char *prop, uintptr_t *val, uint32_t *len);
/* Header fields (0 for a bad blob): totalsize, size_dt_struct, size_dt_strings. */
uint32_t fdt_totalsize(uintptr_t fdt);
uint32_t fdt_size_struct(uintptr_t fdt);
uint32_t fdt_size_strings(uintptr_t fdt);
/*
 * Copy the tree at src to dst (at most dst_max bytes) with /chosen bootargs replaced and
 * linux,initrd-start/end set (64-bit). Returns the new totalsize, 0 on failure.
 */
uint32_t fdt_rebuild_chosen(uintptr_t src, uintptr_t dst, uint32_t dst_max,
    const char *bootargs, uint64_t initrd_lo, uint64_t initrd_hi);

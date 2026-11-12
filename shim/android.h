#pragma once
#include <stdint.h>

/* Boot Android from boot_a on the UFS (see android.c). Returns only on error. */
const char *android_boot(uintptr_t dtb);

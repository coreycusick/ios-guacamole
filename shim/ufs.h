#pragma once
#include <stdint.h>
void test_ufs(void);
/* Boot selector, read-only: 0 = success. Blocks are 4 KiB. */
int ufs_open(void);
int ufs_find(const char *name, uint8_t *lun, uint32_t *first, uint32_t *last);
uint32_t ufs_read(uint8_t lun, uint32_t lba, uint32_t count, void *buf);
/* Same, plus the unique partition GUID (16 bytes as stored in the GPT entry). */
int ufs_find_guid(const char *name, uint8_t *lun, uint32_t *first, uint32_t *last, uint8_t *guid);

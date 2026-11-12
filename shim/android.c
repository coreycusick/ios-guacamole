/*
 * Boot selector step 2: boot Android from boot_a on the UFS.
 *
 * ABL started this shim from the active slot's boot image (slot b = iOS), with
 * its device tree (the same DTB section as boot_a's) and its command line:
 *   <DT /chosen prefix> <our header cmdline "console=null"> <ABL's additions>
 * For Android the header part becomes boot_a's header cmdline and
 * androidboot.slot_suffix becomes _a, so Android mounts its own slot.
 *
 * Memory (SM8150 fixed reservations all lie in 0x85700000-0xa8800000):
 *   boot_a is read whole to STAGE (0x100000000, RAM the iOS kernel also uses),
 *   the ramdisk goes where ABL put ours (above the DTB, below hyp at 0x85700000),
 *   the new DTB right after the ramdisk, the trampoline behind it, and the
 *   kernel Image to 0x80000000 + text_offset, where ABL puts Android's: over
 *   this shim, hence the trampoline.
 */
#include <stdint.h>
#include "android.h"
#include "console.h"
#include "fdt.h"
#include "ufs.h"

#define STAGE		0x100000000UL
#define STAGE_MAX	(128u << 20)
#define LOW_LIMIT	0x85700000UL	/* reserved-memory hyp_region: nothing of ours may reach it */
#define KERNEL_BASE	0x80000000UL
#define BLK		4096u

extern char linux_tramp[], linux_tramp_end[];

static uint32_t le32(uintptr_t a)
{
	volatile const uint8_t *p = (volatile const uint8_t *)a;
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64p(uintptr_t a)
{
	return le32(a) | (uint64_t)le32(a + 4) << 32;
}

static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static int match(const char *s, const char *w)
{
	while (*w)
		if (*s++ != *w++)
			return 0;
	return 1;
}

/* GPT GUID (on-disk order) as Linux prints a PARTUUID: 36 lower-case characters. */
static void guid_str(char *o, const uint8_t *g)
{
	static const uint8_t order[16] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
	int n = 0;
	for (int i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			o[n++] = '-';
		uint8_t b = g[order[i]];
		o[n++] = (b >> 4) < 10 ? '0' + (b >> 4) : 'a' + (b >> 4) - 10;
		o[n++] = (b & 15) < 10 ? '0' + (b & 15) : 'a' + (b & 15) - 10;
	}
	o[n] = 0;
}

/*
 * New bootargs: the old ones with old_hdr (our header cmdline) replaced by new_hdr, slot _b -> _a,
 * and root=PARTUUID=<36 chars> pointed at root_uuid (system_a; ABL names the active slot's).
 */
static int make_bootargs(char *out, uint32_t max, uintptr_t old, uint32_t old_len,
    const char *old_hdr, uintptr_t new_hdr, uint32_t new_hdr_max, const char *root_uuid)
{
	uint32_t o = 0, i = 0, replaced = 0;
	uint32_t ohl = 0;
	while (old_hdr[ohl])
		ohl++;
	while (i < old_len) {
		char c = *(volatile const char *)(old + i);
		if (!c)
			break;
		if (!replaced && (i == 0 || *(volatile const char *)(old + i - 1) == ' ') &&
		    match((const char *)(old + i), old_hdr)) {
			for (uint32_t k = 0; k < new_hdr_max; k++) {
				char h = *(volatile const char *)(new_hdr + k);
				if (!h)
					break;
				if (o + 1 >= max)
					return 1;
				out[o++] = h;
			}
			i += ohl;
			replaced = 1;
			continue;
		}
		if (root_uuid && match((const char *)(old + i), "root=PARTUUID=") && i + 14 + 36 <= old_len) {
			if (o + 14 + 36 >= max)
				return 1;
			for (int k = 0; k < 14; k++)
				out[o++] = *(volatile const char *)(old + i + k);
			for (int k = 0; k < 36; k++)
				out[o++] = root_uuid[k];
			i += 14 + 36;
			continue;
		}
		if (match((const char *)(old + i), "androidboot.slot_suffix=_b")) {
			if (o + 26 >= max)
				return 1;
			const char *a = "androidboot.slot_suffix=_a";
			for (int k = 0; k < 26; k++)
				out[o++] = a[k];
			i += 26;
			continue;
		}
		if (o + 1 >= max)
			return 1;
		out[o++] = c;
		i++;
	}
	out[o] = 0;
	return replaced ? 0 : 2;
}

static char bootargs[4096];

const char *android_boot(uintptr_t dtb)
{
	uint64_t rd_lo = 0, rd_hi = 0;
	uintptr_t ba;
	uint32_t ba_len;
	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_lo) || !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_hi) ||
	    !fdt_chosen_prop(dtb, "bootargs", &ba, &ba_len))
		return "ABL tree lacks /chosen initrd or bootargs";

	if (ufs_open())
		return "UFS host not ready";
	uint8_t lun;
	uint32_t first, last;
	if (ufs_find("boot_a", &lun, &first, &last))
		return "no boot_a partition";
	con_hex2("BOOT_A", "lun=", lun, "lba=", first);

	/* Header (boot image v2, 4 KiB pages) */
	if (ufs_read(lun, first, 1, (void *)STAGE))
		return "boot_a: header read failed";
	/* Magics compared as numbers: a string compared at a constant address becomes an absolute relocation. */
	if (le32(STAGE) != 0x52444e41 || le32(STAGE + 4) != 0x2144494f)	/* "ANDROID!" */
		return "boot_a: not an Android boot image";
	uint32_t ksz = le32(STAGE + 8), rsz = le32(STAGE + 16), ssz = le32(STAGE + 24);
	uint32_t page = le32(STAGE + 36), hver = le32(STAGE + 40);
	if (page != BLK || hver > 2)
		return "boot_a: unsupported header (want v0-v2, 4K pages)";
	uint32_t kp = (ksz + BLK - 1) / BLK, rp = (rsz + BLK - 1) / BLK, sp = (ssz + BLK - 1) / BLK;
	uint32_t blocks = 1 + kp + rp + sp;
	if ((uint64_t)blocks * BLK > STAGE_MAX || first + blocks - 1 > last)
		return "boot_a: too large";
	con_hex2("BOOT_A", "kernel=", ksz, "ramdisk=", rsz);
	if (ufs_read(lun, first, blocks, (void *)STAGE))
		return "boot_a: read failed";

	/* Kernel: Qualcomm "UNCOMPRESSED_IMG" + u32 size + Image, or a plain Image. */
	uintptr_t img = STAGE + BLK;
	uint64_t img_len = ksz;
	if (le32(img) == 0x4f434e55 && le32(img + 4) == 0x4552504d && le32(img + 8) == 0x44455353 &&
	    le32(img + 12) == 0x474d495f) {	/* "UNCOMPRESSED_IMG" */
		img_len = le32(img + 16);
		img += 20;
		if (img_len + 20 > ksz)
			return "boot_a: bad UNCOMPRESSED_IMG size";
	}
	/*
	 * The MMU is off, so all memory is Device memory, where an 8-byte load must be 8-byte aligned:
	 * the Image behind the 20-byte UNCOMPRESSED_IMG header is only 4-byte aligned (first phone run:
	 * alignment fault, ESR 0x96000021, in the trampoline's ldr). Slide it down to the page start
	 * in 4-byte words first (forward copy, destination below source).
	 */
	if (img & 7) {
		volatile uint32_t *d32 = (volatile uint32_t *)(STAGE + BLK);
		volatile const uint32_t *s32 = (volatile const uint32_t *)img;
		for (uint64_t w = 0; w < (img_len + 3) / 4; w++)
			d32[w] = s32[w];
		img = STAGE + BLK;
	}
	if (le32(img + 56) != 0x644d5241)	/* "ARM\x64" */
		return "boot_a: kernel is not an arm64 Image (compressed?)";
	uint64_t text_offset = le64p(img + 8), image_size = le64p(img + 16);
	if (!image_size || image_size < img_len)
		image_size = img_len;
	uintptr_t kdst = KERNEL_BASE + text_offset;
	con_hex2("IMAGE", "dst=", kdst, "size=", (uint32_t)image_size);

	/* Ramdisk where ABL put ours; DTB and trampoline behind it, all below LOW_LIMIT. */
	uintptr_t rd = (uintptr_t)rd_lo, rd_end = rd + rsz;
	uintptr_t ndtb = (rd_end + BLK - 1) & ~(uintptr_t)(BLK - 1);
	uint32_t dtb_room = fdt_totalsize(dtb) + 0x2000;
	uintptr_t tramp = (ndtb + dtb_room + BLK - 1) & ~(uintptr_t)(BLK - 1);
	uintptr_t tramp_len = (uintptr_t)(linux_tramp_end - linux_tramp);
	if (kdst + image_size > (uintptr_t)dtb || tramp + BLK > LOW_LIMIT || rd < (uintptr_t)dtb + fdt_totalsize(dtb))
		return "memory layout does not fit";

	uint8_t sys_guid[16];
	uint8_t slun;
	uint32_t sfirst, slast;
	char root_uuid[37];
	const char *root = 0;
	if (!ufs_find_guid("system_a", &slun, &sfirst, &slast, sys_guid)) {
		guid_str(root_uuid, sys_guid);
		root = root_uuid;
	}
	if (make_bootargs(bootargs, sizeof(bootargs), ba, ba_len, "console=null", STAGE + 64, 512, root))
		return "bootargs: our header cmdline not found";
	copy64(rd, STAGE + (uintptr_t)(1 + kp) * BLK, rsz);
	uint32_t nsize = fdt_rebuild_chosen(dtb, ndtb, dtb_room, bootargs, rd, rd_end);
	if (!nsize)
		return "device tree rebuild failed";
	con_hex2("RAMDISK", "at=", rd, "end=", rd_end);
	con_hex2("DTB", "at=", ndtb, "size=", nsize);
	copy64(tramp, (uintptr_t)linux_tramp, tramp_len);
	con_hex("TRAMP", tramp);
	con_str("ANDROID", "jumping to Linux");

	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((void (*)(uint64_t, uint64_t, uint64_t, uint64_t))tramp)(kdst, img, (img_len + 7) & ~7UL, ndtb);
	return "trampoline returned";
}

/*
 * rebootbl: reboot the phone into the bootloader (fastboot), from iOS. 2026-11-17.
 *   rebootbl        set the PMIC's restart reason to "bootloader", then reboot
 *   rebootbl -n     only set it and read it back (no reboot; the next reboot of any kind then lands in fastboot)
 *   rebootbl -c     clear it again (normal boot), read it back
 *
 * How (the shim's verified method, shim/spmi.c, guac-probe v5): this ABL ignores IMEM restart_reason; it stays in
 * fastboot when the PM8150's PON_SOFT_RB_SPARE (SID0 0x088f) bits 7:1 hold 2 (Linux qpnp_pon_set_restart_reason
 * PON_RESTART_REASON_BOOTLOADER), and PS_HOLD is left in warm-reset mode by ABL, so an ordinary reset keeps it.
 * The register is reached over the SPMI PMIC arbiter v5 as execution environment 0, through /dev/pmem (boot-arg
 * sweetpmem=1, root): the PON peripheral's APID from the core's PPID table (owned by EE0), a read on its observer
 * channel (obsrvr + 0x80 * apid) and the write on its own channel (chnls + 0x10000 * apid; 0x80 * apid there was an
 * XPU violation -> CrashDump). Bit 0 of the register is kept. Then /var/jb/usr/sbin/reboot.
 * MIT, as userland/.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SPMI_CORE               0x0c440000ull
#define SPMI_CHNLS              0x0c600000ull
#define SPMI_OBSRVR             0x0e600000ull
#define SPMI_CNFG               0x0c40a000ull
#define PON_SID                 0
#define PON_PERIPH              0x08
#define PON_SOFT_RB_SPARE       0x8f
#define PON_REASON_BOOTLOADER   0x02    /* stored << 1 */

static int fd = -1;

static uint32_t rd(uint64_t pa)
{
	uint32_t v = 0xdeadbeef;
	if (pread(fd, &v, 4, (off_t)pa) != 4)
		return 0xdeadbeef;
	return v;
}

static int wr(uint64_t pa, uint32_t v)
{
	if (pwrite(fd, &v, 4, (off_t)pa) != 4) {
		fprintf(stderr, "rebootbl: write %#llx: %s\n", (unsigned long long)pa, strerror(errno));
		return -1;
	}
	return 0;
}

static int wait_done(uint64_t ch)
{
	for (int i = 0; i < 2000; i++) {
		uint32_t st = rd(ch + 0x08);
		if (st != 0xdeadbeef && (st & 1))
			return (st & 0x0e) ? -1 : 0;
		usleep(5);
	}
	return -2;
}

static int find_apid(void)
{
	uint32_t ver = rd(SPMI_CORE + 0x0), n = rd(SPMI_CORE + 0x4) & 0x7ff;
	int found = -1;
	if (ver < 0x50000000u || ver >= 0x70000000u || n > 512) {
		fprintf(stderr, "rebootbl: SPMI arbiter version %08x, %u channels: not the v5 arbiter expected\n", ver, n);
		return -1;
	}
	for (uint32_t i = 0; i < n; i++) {
		uint32_t map = rd(SPMI_CORE + 0x900 + 4 * i);
		if (!map || ((map >> 8) & 0xfff) != ((PON_SID << 8) | PON_PERIPH))
			continue;
		if ((rd(SPMI_CNFG + 0x700 + 4 * i) & 7) == 0)
			found = (int)i;
	}
	if (found < 0)
		fprintf(stderr, "rebootbl: no PON channel owned by EE0\n");
	return found;
}

static int read_spare(int apid, uint8_t *out)
{
	uint64_t obs = SPMI_OBSRVR + 0x80ull * (unsigned)apid;
	for (int tries = 0; tries < 5; tries++) {      /* the kernel's key reader uses the arbiter too: retry */
		if (wr(obs + 0x00, (1u << 27) | (PON_SOFT_RB_SPARE << 4)))     /* EXT_READL, 1 byte */
			return -1;
		if (wait_done(obs) == 0) {
			*out = (uint8_t)rd(obs + 0x18);
			return 0;
		}
		usleep(1000);
	}
	return -1;
}

static int write_spare(int apid, uint8_t v)
{
	uint64_t chn = SPMI_CHNLS + 0x10000ull * (unsigned)apid;
	if (wr(chn + 0x10, v) || wr(chn + 0x00, (0u << 27) | (PON_SOFT_RB_SPARE << 4)))  /* EXT_WRITEL, 1 byte */
		return -1;
	return wait_done(chn);
}

int main(int argc, char **argv)
{
	int only_set = argc > 1 && !strcmp(argv[1], "-n"), clear = argc > 1 && !strcmp(argv[1], "-c");
	fd = open("/dev/pmem", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "rebootbl: /dev/pmem: %s (boot-arg sweetpmem=1, run as root)\n", strerror(errno));
		return 1;
	}
	int apid = find_apid();
	if (apid < 0)
		return 1;
	uint8_t before = 0, after = 0;
	if (read_spare(apid, &before)) {
		fprintf(stderr, "rebootbl: could not read PON_SOFT_RB_SPARE\n");
		return 1;
	}
	uint8_t want = (uint8_t)((before & 0x01) | (clear ? 0 : PON_REASON_BOOTLOADER << 1));
	if (write_spare(apid, want) || read_spare(apid, &after) || after != want) {
		fprintf(stderr, "rebootbl: PON_SOFT_RB_SPARE write failed (was %02x, wanted %02x, reads %02x)\n", before, want, after);
		return 1;
	}
	printf("rebootbl: PON channel %d, PON_SOFT_RB_SPARE %02x -> %02x (%s)\n", apid, before, after,
	    clear ? "normal boot" : "fastboot at the next reset");
	if (only_set || clear)
		return 0;
	fflush(stdout);
	sync();
	/* iOS 27 root: reboot is /var/jb/usr/sbin/reboot (/var/jb/sbin/reboot was the iOS 16 path) */
	execl("/var/jb/usr/sbin/reboot", "reboot", (char *)NULL);
	execl("/var/jb/sbin/reboot", "reboot", (char *)NULL);
	fprintf(stderr, "rebootbl: exec /var/jb/usr/sbin/reboot: %s (the reason stays set: reboot by hand)\n", strerror(errno));
	return 1;
}

/*
 * sweet-cc: launchd job wrapper for CommCenter. launchd does not pass DYLD_*
 * variables from a job to a daemon, so this adds /usr/lib/libsweet-ccvirt.dylib (userland/jb/ccvirt.c: no
 * modem, for CommCenter alone) to DYLD_INSERT_LIBRARIES and execs the real CommCenter: same pid,
 * same job.
 * MIT, as userland/.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define REAL "/System/Library/Frameworks/CoreTelephony.framework/Support/CommCenter"
#define LIB  "/usr/lib/libsweet-ccvirt.dylib"

int
main(int argc, char **argv)
{
	(void)argc;
	if (access(LIB, R_OK) == 0) {
		setenv("DYLD_INSERT_LIBRARIES", LIB, 1);
	}
	argv[0] = (char *)REAL;
	execv(REAL, argv);
	perror("sweet-cc: exec " REAL);
	return 1;
}

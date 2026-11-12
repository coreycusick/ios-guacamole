/*
 * sweet-locd: launchd job wrapper for locationd. launchd does not pass DYLD_*
 * variables from a job to a daemon, so this adds /usr/lib/libsweet-atcsfix.dylib (userland/jb/atcsfix.c: keeps
 * locationd alive without a modem) to DYLD_INSERT_LIBRARIES and execs the real /usr/libexec/locationd: same pid,
 * same job.
 * MIT, as userland/.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define REAL "/usr/libexec/locationd"
#define LIB  "/usr/lib/libsweet-atcsfix.dylib"

int
main(int argc, char **argv)
{
	(void)argc;
	if (access(LIB, R_OK) == 0) {
		setenv("DYLD_INSERT_LIBRARIES", LIB, 1);
	}
	argv[0] = (char *)REAL;
	execv(REAL, argv);
	perror("sweet-locd: exec " REAL);
	return 1;
}

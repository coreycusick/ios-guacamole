/*
 * runas USER PROGRAM [ARGS...]: run a program as another user (as launchd would, minus its sandbox/job), with
 * stdout/stderr on the terminal. Used to see why CommCenter (_wireless) dies under launchd but not as root:
 * a libc++ "terminating due to uncaught exception" message goes to stderr. MIT.
 */
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: runas USER PROGRAM [ARGS...]\n");
		return 2;
	}
	struct passwd *pw = getpwnam(argv[1]);
	if (pw == NULL) {
		fprintf(stderr, "runas: no user %s\n", argv[1]);
		return 1;
	}
	if (initgroups(pw->pw_name, (int)pw->pw_gid) != 0 || setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
		perror("runas");
		return 1;
	}
	setenv("HOME", pw->pw_dir, 1);
	setenv("USER", pw->pw_name, 1);
	setenv("LOGNAME", pw->pw_name, 1);
	execv(argv[2], &argv[2]);
	perror(argv[2]);
	return 127;
}

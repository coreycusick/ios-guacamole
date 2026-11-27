/*
 * perfstate: reads or sets powerlogd's "perfManagementState" (com.apple.powerlogd.plist in its system group
 * container), which Settings' Battery Health page reads before it shows anything (BatteryUsageUI
 * getManagementStateWithSync: notify com.apple.thermalmonitor.ageAwareMitigationState, else this preference;
 * 1..3 valid, anything else "Unknown current state" -> battery data "unavailable" -> endless spinner).
 * PerfPowerServices, which keeps it on an iPhone, is not part of this root. 1 = normal peak performance: this port
 * never throttles for battery age. 2026-11-27. MIT, as userland/.
 *   perfstate            print the current value in the file
 *   perfstate N          set it (1 normal; 2, 3: Apple's mitigation states), then restart cfprefsd yourself
 *                        (killall cfprefsd) so readers see it; exit 3 = already N, nothing written
 * Edits the plist file directly (cfprefsd does not accept this container's writes from a tool); keeps the owner
 * (mobile) and writes through a .new file + rename.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	glob_t g;
	if (glob("/private/var/containers/Shared/SystemGroup/*/Library/Preferences/com.apple.powerlogd.plist", 0, NULL, &g) ||
	    g.gl_pathc == 0) {
		fprintf(stderr, "perfstate: no com.apple.powerlogd.plist in a system group container\n");
		return 1;
	}
	const char *path = g.gl_pathv[0];
	if (g.gl_pathc > 1)
		fprintf(stderr, "perfstate: %zu files, using %s\n", g.gl_pathc, path);
	struct stat st;
	if (stat(path, &st)) {
		perror(path);
		return 1;
	}
	FILE *f = fopen(path, "rb");
	if (!f) {
		perror(path);
		return 1;
	}
	char *buf = malloc((size_t)st.st_size + 1);
	size_t n = fread(buf, 1, (size_t)st.st_size, f);
	fclose(f);
	CFDataRef data = CFDataCreate(NULL, (const UInt8 *)buf, (CFIndex)n);
	CFPropertyListRef pl = CFPropertyListCreateWithData(NULL, data, kCFPropertyListMutableContainers, NULL, NULL);
	if (!pl || CFGetTypeID(pl) != CFDictionaryGetTypeID()) {
		fprintf(stderr, "perfstate: %s is not a dictionary plist\n", path);
		return 1;
	}
	CFMutableDictionaryRef d = (CFMutableDictionaryRef)pl;
	CFStringRef key = CFSTR("perfManagementState");
	int v = -1;
	CFTypeRef cur = CFDictionaryGetValue(d, key);
	if (cur && CFGetTypeID(cur) == CFNumberGetTypeID())
		CFNumberGetValue(cur, kCFNumberIntType, &v);
	printf("perfManagementState = %d in %s\n", v, path);
	if (argc < 2)
		return 0;
	int nv = atoi(argv[1]);
	if (nv == v) {
		printf("unchanged\n");
		return 3;                       /* nothing written: no cfprefsd restart needed */
	}
	CFNumberRef num = CFNumberCreate(NULL, kCFNumberIntType, &nv);
	CFDictionarySetValue(d, key, num);
	CFDataRef out = CFPropertyListCreateData(NULL, d, kCFPropertyListBinaryFormat_v1_0, 0, NULL);
	char tmp[1100];
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	f = fopen(tmp, "wb");
	if (!out || !f || fwrite(CFDataGetBytePtr(out), 1, (size_t)CFDataGetLength(out), f) != (size_t)CFDataGetLength(out)) {
		fprintf(stderr, "perfstate: write failed\n");
		return 1;
	}
	fclose(f);
	if (chown(tmp, st.st_uid, st.st_gid) || chmod(tmp, st.st_mode & 07777) || rename(tmp, path)) {
		perror("perfstate: install");
		return 1;
	}
	printf("perfManagementState = %d written (restart cfprefsd: killall cfprefsd)\n", nv);
	return 0;
}

/*
 * nosim: shows "No SIM" in the status bar through UIKit's status bar override (the mechanism Xcode's
 * `simctl status_bar override` uses), on a phone whose CommCenter runs without a modem (no cellular item at all).
 *   nosim [TEXT] [bars=N] [ITEM...]   post: service string TEXT (default "No SIM"), N signal bars (default 0,
 *                            0..4), and enable status bar items ITEM (default 3: the signal-strength/carrier item
 *                            of the legacy item table)
 *   nosim off                remove every override
 *   nosim show               print what is overridden now (this process's copy)
 *   nosim dump               SpringBoard's own status bar data and override data (read from its memory)
 * The structs are the iOS 16 (20H392) ones, from UIStatusBarServer's type encodings (sbprobe):
 * StatusBarOverrideData is 3944 bytes (postStatusBarOverrideData: copies 0xf68).
 * SpringBoard registers the status bar server (com.apple.UIKit.statusbarserver) in the user session, which a root
 * shell's bootstrap does not see (and launchctl bsexec/asuser are not supported here), so this first takes
 * SpringBoard's bootstrap port (task_for_pid, as tsample) and makes it its own. MIT, as userland/.
 */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

typedef struct {
	bool itemIsEnabled[46];
	char timeString[64];
	char shortTimeString[64];
	char dateString[256];
	int gsmSignalStrengthRaw;
	int secondaryGsmSignalStrengthRaw;
	int gsmSignalStrengthBars;
	int secondaryGsmSignalStrengthBars;
	char serviceString[100];
	char secondaryServiceString[100];
	char serviceCrossfadeString[100];
	char secondaryServiceCrossfadeString[100];
	char serviceImages[2][100];
	char operatorDirectory[1024];
	unsigned int serviceContentType;
	unsigned int secondaryServiceContentType;
	unsigned int b0 : 1, b1 : 1;
	int i0, i1;
	unsigned int b2 : 1;
	unsigned int u0, u1;
	int i2;
	unsigned int u2;
	char s150[150];
	int i3, i4;
	unsigned int b3 : 1, b4 : 1, b5 : 1;
	char s256a[256];
	unsigned int b6 : 1, b7 : 1, b8 : 1, b9 : 2, b10 : 2, b11 : 1;
	unsigned int u3;
	unsigned int b12 : 1, b13 : 1, b14 : 1;
	char s256b[256];
	char s256c[256];
	char s100a[100];
	unsigned int b15 : 1, b16 : 1, b17 : 1, b18 : 1, b19 : 1;
	double d0;
	unsigned int b20 : 1, b21 : 1, b22 : 1;
	char s100b[100];
	char s100c[100];
	char s256d[256];
	char s256e[256];
} RawData;

typedef struct {
	bool overrideItemIsEnabled[46];
	unsigned int overrideTimeString : 1;
	unsigned int overrideDateString : 1;
	unsigned int overrideGsmSignalStrengthRaw : 1;
	unsigned int overrideSecondaryGsmSignalStrengthRaw : 1;
	unsigned int overrideGsmSignalStrengthBars : 1;
	unsigned int overrideSecondaryGsmSignalStrengthBars : 1;
	unsigned int overrideServiceString : 1;
	unsigned int overrideSecondaryServiceString : 1;
	unsigned int overrideServiceImages : 2;
	unsigned int o10 : 1, o11 : 1, o12 : 1, o13 : 1, o14 : 1, o15 : 1, o16 : 1, o17 : 1, o18 : 1, o19 : 1, o20 : 1,
	    o21 : 1, o22 : 1, o23 : 1, o24 : 1, o25 : 1, o26 : 1;
	unsigned int ou;
	unsigned int p0 : 1, p1 : 1, p2 : 1, p3 : 1, p4 : 1, p5 : 1, p6 : 1, p7 : 1;
	RawData values;
} OverrideData;

_Static_assert(sizeof(OverrideData) == 3944, "StatusBarOverrideData is 3944 bytes on 20H392");

/* <servers/bootstrap.h> is not in the iOS SDK */
extern mach_port_t bootstrap_port;
extern kern_return_t bootstrap_look_up(mach_port_t bp, const char *name, mach_port_t *sp);

static mach_port_t sb_task = MACH_PORT_NULL;

/* use SpringBoard's bootstrap namespace, where its status bar server is registered */
static void
join_springboard(void)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	size_t len = 0;
	if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0)
		return;
	struct kinfo_proc *procs = malloc(len);
	if (procs == NULL || sysctl(mib, 3, procs, &len, NULL, 0) != 0) {
		free(procs);
		return;
	}
	pid_t sb = 0;
	for (size_t i = 0; i < len / sizeof(*procs); i++)
		if (strcmp(procs[i].kp_proc.p_comm, "SpringBoard") == 0)
			sb = procs[i].kp_proc.p_pid;
	free(procs);
	mach_port_t task = MACH_PORT_NULL, bp = MACH_PORT_NULL;
	if (sb == 0 || task_for_pid(mach_task_self(), sb, &task) != KERN_SUCCESS ||
	    task_get_special_port(task, TASK_BOOTSTRAP_PORT, &bp) != KERN_SUCCESS || bp == MACH_PORT_NULL) {
		fprintf(stderr, "nosim: cannot take SpringBoard's (pid %d) bootstrap port; staying in our own\n", sb);
		return;
	}
	sb_task = task;
	task_set_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, bp);
	bootstrap_port = bp;
	mach_port_t probe = MACH_PORT_NULL;
	kern_return_t kr = bootstrap_look_up(bp, "com.apple.UIKit.statusbarserver", &probe);
	fprintf(stderr, "nosim: SpringBoard pid %d; status bar server %s\n", sb, kr == KERN_SUCCESS ? "found" : "NOT found");
}

int
main(int argc, char **argv)
{
	join_springboard();
	@autoreleasepool {
		dlopen("/System/Library/PrivateFrameworks/UIKitCore.framework/UIKitCore", RTLD_NOW);
		Class c = objc_getClass("UIStatusBarServer");
		if (c == Nil) {
			fprintf(stderr, "nosim: no UIStatusBarServer\n");
			return 1;
		}
		OverrideData o;
		memset(&o, 0, sizeof(o));
		OverrideData *cur = ((OverrideData * (*)(id, SEL)) objc_msgSend)(c, sel_registerName("getStatusBarOverrideData"));
		if (argc > 1 && strcmp(argv[1], "dump") == 0) {
			/*
			 * SpringBoard's own copies: UIKitCore's static buffers sit at the same address in every process
			 * (one shared cache slide), so read them from SpringBoard's task at the addresses we see here.
			 */
			RawData *raw_here = ((RawData * (*)(id, SEL)) objc_msgSend)(c, sel_registerName("getStatusBarData"));
			RawData raw;
			OverrideData ov;
			vm_size_t got = 0;
			if (sb_task == MACH_PORT_NULL ||
			    vm_read_overwrite(sb_task, (vm_address_t)raw_here, sizeof(raw), (vm_address_t)&raw, &got) != KERN_SUCCESS ||
			    vm_read_overwrite(sb_task, (vm_address_t)cur, sizeof(ov), (vm_address_t)&ov, &got) != KERN_SUCCESS) {
				printf("cannot read SpringBoard\n");
				return 1;
			}
			printf("SpringBoard data: time \"%s\" service \"%s\" crossfade \"%s\" bars %d raw %d contentType %u items on:",
			    raw.timeString, raw.serviceString, raw.serviceCrossfadeString, raw.gsmSignalStrengthBars,
			    raw.gsmSignalStrengthRaw, raw.serviceContentType);
			for (int i = 0; i < 46; i++)
				if (raw.itemIsEnabled[i])
					printf(" %d", i);
			printf("\nSpringBoard override: service %d \"%s\" bars %d/%d contentType %u items:", ov.overrideServiceString,
			    ov.values.serviceString, ov.overrideGsmSignalStrengthBars, ov.values.gsmSignalStrengthBars,
			    ov.values.serviceContentType);
			for (int i = 0; i < 46; i++)
				if (ov.overrideItemIsEnabled[i])
					printf(" %d=%d", i, ov.values.itemIsEnabled[i]);
			printf("\n");
			return 0;
		}
		if (argc > 1 && strcmp(argv[1], "show") == 0) {
			if (cur == NULL) {
				printf("no override data\n");
				return 0;
			}
			printf("service %d \"%s\" bars %d/%d items:", cur->overrideServiceString, cur->values.serviceString,
			    cur->overrideGsmSignalStrengthBars, cur->values.gsmSignalStrengthBars);
			for (int i = 0; i < 46; i++)
				if (cur->overrideItemIsEnabled[i])
					printf(" %d=%d", i, cur->values.itemIsEnabled[i]);
			printf("\n");
			return 0;
		}
		if (!(argc > 1 && strcmp(argv[1], "off") == 0)) {
			const char *text = argc > 1 ? argv[1] : "No SIM";
			strlcpy(o.values.serviceString, text, sizeof(o.values.serviceString));
			o.overrideServiceString = 1;
			int bars = 0, items = 0;
			for (int i = 2; i < argc; i++)
				if (strncmp(argv[i], "bars=", 5) == 0)
					bars = atoi(argv[i] + 5);
			bars = bars < 0 ? 0 : bars > 4 ? 4 : bars;
			o.overrideGsmSignalStrengthBars = 1;
			o.values.gsmSignalStrengthBars = bars;
			o.overrideGsmSignalStrengthRaw = 1;
			o.values.gsmSignalStrengthRaw = bars == 0 ? 0 : -120 + bars * 15;   /* dBm-ish, as the bars */
			for (int i = 2; i < argc; i++)
				items += strncmp(argv[i], "bars=", 5) != 0;
			if (items > 0) {
				for (int i = 2; i < argc; i++) {
					if (strncmp(argv[i], "bars=", 5) == 0)
						continue;
					int item = atoi(argv[i]);
					if (item >= 0 && item < 46) {
						o.overrideItemIsEnabled[item] = 1;
						o.values.itemIsEnabled[item] = 1;
					}
				}
			} else {
				o.overrideItemIsEnabled[3] = 1;
				o.values.itemIsEnabled[3] = 1;
			}
		}
		/*
		 * iOS 16's SpringBoard only shows UIKit's override data while "status bar overrides for recording" are on:
		 * the SpringBoard default SBShowStatusBarOverridesForRecording (SpringBoardFoundation). Set for user mobile
		 * (SpringBoard's), through cfprefsd; SpringBoard may need a respring to read it.
		 */
		CFPreferencesSetValue(CFSTR("SBShowStatusBarOverridesForRecording"), o.overrideServiceString ? kCFBooleanTrue : NULL,
		    CFSTR("com.apple.springboard"), CFSTR("mobile"), kCFPreferencesAnyHost);
		Boolean synced = CFPreferencesSynchronize(CFSTR("com.apple.springboard"), CFSTR("mobile"), kCFPreferencesAnyHost);
		fprintf(stderr, "nosim: SBShowStatusBarOverridesForRecording = %d (%s)\n", o.overrideServiceString ? 1 : 0,
		    synced ? "saved" : "NOT saved");
		((void (*)(id, SEL, OverrideData *))objc_msgSend)(c, sel_registerName("postStatusBarOverrideData:"), &o);
		((void (*)(id, SEL))objc_msgSend)(c, sel_registerName("permanentizeStatusBarOverrideData"));
		printf("nosim: posted (%s)\n", o.overrideServiceString ? o.values.serviceString : "off");
	}
	return 0;
}

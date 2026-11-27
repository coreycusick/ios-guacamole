/*
 * tprobe: does time pass the same way for everyone on the phone? 2026-11-17, for the audio cut-outs: audiomxd's HAL
 * reports a steady 11 % drift between when its I/O thread wakes and where the device clock says it should be
 * (HALS_OverloadMessage SafetyViolationOccurred every 65536 frames, TimeStampOutOfLine), while the DSP and adspd agree
 * on 48 kHz. Prints:
 *   1. mach_timebase_info and the sysctls user space reads the host clock from (hw.tbfrequency, kern.clockrate ...);
 *   2. mach_absolute_time vs CLOCK_MONOTONIC(_RAW) / CLOCK_UPTIME_RAW / gettimeofday over 2 s (rates should be 1.000);
 *   3. timed waits of 21.333 ms (1024 frames at 48 kHz) the ways the HAL's I/O thread can wait (pthread cond timed
 *      wait relative/absolute, mach_wait_until, usleep), plain and as a time-constraint (real-time) thread:
 *      how long each wait really took, by the mach clock and by CLOCK_MONOTONIC_RAW.
 *   tprobe            (about 15 s)
 *   tprobe -t NAME|PID  that process's threads: scheduling policy (1 timeshare, 2 round robin, 4 FIFO), base / current
 *                     priority, whether a time-constraint (real-time) policy is set, CPU use, name. Needs
 *                     task_for_pid: sign with tsample.plist.
 *   tprobe -c 0|1     set kern.timer.coalescing_enabled (root), then time the plain waits again.
 *   tprobe -b         2026-11-27: what one clock read costs (mach_absolute_time, mach_continuous_time,
 *                     clock_gettime_nsec_np), 1,000,000 of each, with kern.sweet_fast_abstime as it is.
 * MIT, as userland/.
 */
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

static mach_timebase_info_data_t tb;

static double abs_ns(uint64_t t) { return (double)t * tb.numer / tb.denom; }

static double raw_ns(clockid_t c)
{
	struct timespec ts;
	clock_gettime(c, &ts);
	return ts.tv_sec * 1e9 + ts.tv_nsec;
}

static void sys64(const char *name)
{
	uint64_t v = 0;
	size_t l = sizeof(v);
	if (sysctlbyname(name, &v, &l, NULL, 0) != 0) {
		printf("  %-22s (unavailable)\n", name);
		return;
	}
	printf("  %-22s %llu (%zu bytes)\n", name, l == 4 ? (unsigned long long)(uint32_t)v : (unsigned long long)v, l);
}

#define WAIT_NS 21333333ull
#define N 40

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static void one(const char *how, int kind)
{
	double sum_a = 0, sum_r = 0, max_a = 0, min_a = 1e18;
	for (int i = 0; i < N; i++) {
		uint64_t a0 = mach_absolute_time();
		double r0 = raw_ns(CLOCK_MONOTONIC_RAW);
		if (kind == 0) {
			struct timespec rel = { 0, (long)WAIT_NS };
			pthread_mutex_lock(&m);
			pthread_cond_timedwait_relative_np(&cv, &m, &rel);
			pthread_mutex_unlock(&m);
		} else if (kind == 1) {
			struct timeval now;
			gettimeofday(&now, NULL);
			long long ns = (long long)now.tv_usec * 1000 + (long long)WAIT_NS;
			struct timespec abs = { now.tv_sec + ns / 1000000000, ns % 1000000000 };
			pthread_mutex_lock(&m);
			pthread_cond_timedwait(&cv, &m, &abs);
			pthread_mutex_unlock(&m);
		} else if (kind == 2) {
			mach_wait_until(a0 + (uint64_t)(WAIT_NS * (double)tb.denom / tb.numer));
		} else {
			usleep(WAIT_NS / 1000);
		}
		double da = abs_ns(mach_absolute_time() - a0) / 1e6, dr = (raw_ns(CLOCK_MONOTONIC_RAW) - r0) / 1e6;
		sum_a += da;
		sum_r += dr;
		if (da > max_a)
			max_a = da;
		if (da < min_a)
			min_a = da;
	}
	printf("  %-34s asked 21.333 ms: mach avg %7.3f (min %7.3f max %7.3f), raw avg %7.3f ms\n", how, sum_a / N, min_a,
	    max_a, sum_r / N);
}

static void waits(const char *who)
{
	char b[80];
	static const char *const names[] = { "cond_timedwait_relative_np", "cond_timedwait (absolute)", "mach_wait_until",
		"usleep" };
	for (int k = 0; k < 4; k++) {
		snprintf(b, sizeof(b), "%s %s", who, names[k]);
		one(b, k);
	}
}

static void *rt(void *arg)
{
	(void)arg;
	/* like the HAL's I/O thread: period 1024 frames, computation ~ a quarter, constraint = period */
	thread_time_constraint_policy_data_t p;
	double ticks_per_ns = (double)tb.denom / tb.numer;
	p.period = (uint32_t)(WAIT_NS * ticks_per_ns);
	p.computation = (uint32_t)(WAIT_NS / 4 * ticks_per_ns);
	p.constraint = (uint32_t)(WAIT_NS * ticks_per_ns);
	p.preemptible = 1;
	kern_return_t kr = thread_policy_set(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&p,
	    THREAD_TIME_CONSTRAINT_POLICY_COUNT);
	printf("time-constraint policy: %s (%d)\n", kr == KERN_SUCCESS ? "set" : "REFUSED", kr);
	waits("RT");
	return NULL;
}

static int find_pid(const char *arg)
{
	if (isdigit((unsigned char)arg[0]))
		return atoi(arg);
	int mib[3] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL};
	size_t len = 0;
	if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0)
		return -1;
	len += 64 * sizeof(struct kinfo_proc);
	struct kinfo_proc *kp = malloc(len);
	if (!kp || sysctl(mib, 3, kp, &len, NULL, 0) != 0)
		return -1;
	int pid = -1;
	for (size_t i = 0; i < len / sizeof(*kp); i++)
		if (strncmp(kp[i].kp_proc.p_comm, arg, MAXCOMLEN) == 0) {
			pid = kp[i].kp_proc.p_pid;
			break;
		}
	free(kp);
	return pid;
}

static int threads(const char *arg)
{
	int pid = find_pid(arg);
	task_t task;
	if (pid <= 0 || task_for_pid(mach_task_self(), pid, &task) != KERN_SUCCESS) {
		fprintf(stderr, "tprobe: no task for %s (pid %d)\n", arg, pid);
		return 1;
	}
	thread_act_array_t th;
	mach_msg_type_number_t n;
	if (task_threads(task, &th, &n) != KERN_SUCCESS)
		return 1;
	printf("%s pid %d: %u threads\n", arg, pid, n);
	printf("  #  policy base cur max  RT(period/comp/constr us, preempt)  cpu%%  name\n");
	for (unsigned i = 0; i < n; i++) {
		thread_basic_info_data_t bi;
		mach_msg_type_number_t c = THREAD_BASIC_INFO_COUNT;
		thread_extended_info_data_t ei;
		mach_msg_type_number_t ce = THREAD_EXTENDED_INFO_COUNT;
		memset(&ei, 0, sizeof(ei));
		if (thread_info(th[i], THREAD_BASIC_INFO, (thread_info_t)&bi, &c) != KERN_SUCCESS)
			continue;
		thread_info(th[i], THREAD_EXTENDED_INFO, (thread_info_t)&ei, &ce);
		thread_time_constraint_policy_data_t tc;
		mach_msg_type_number_t ct = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
		boolean_t def = FALSE;
		char rt[64] = "-";
		if (thread_policy_get(th[i], THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&tc, &ct, &def) == KERN_SUCCESS &&
		    !def)
			snprintf(rt, sizeof(rt), "%.0f/%.0f/%.0f %u", abs_ns(tc.period) / 1e3, abs_ns(tc.computation) / 1e3,
			    abs_ns(tc.constraint) / 1e3, tc.preemptible);
		printf("  %-2u %6d %4d %3d %3d  %-34s %5.1f  %s\n", i, ei.pth_policy ? ei.pth_policy : bi.policy,
		    ei.pth_priority, ei.pth_curpri, ei.pth_maxpriority, rt, bi.cpu_usage / 10.0, ei.pth_name);
		mach_port_deallocate(mach_task_self(), th[i]);
	}
	return 0;
}

static void bench(void)
{
	enum { N = 1000000 };
	int fast = -1;
	size_t l = sizeof(fast);
	volatile uint64_t sink = 0;

	sysctlbyname("kern.sweet_fast_abstime", &fast, &l, NULL, 0);
	printf("kern.sweet_fast_abstime %d\n", fast);
	for (int pass = 0; pass < 2; pass++) {
		double r0 = raw_ns(CLOCK_MONOTONIC_RAW);
		for (int i = 0; i < N; i++)
			sink += mach_absolute_time();
		double r1 = raw_ns(CLOCK_MONOTONIC_RAW);
		for (int i = 0; i < N; i++)
			sink += mach_continuous_time();
		double r2 = raw_ns(CLOCK_MONOTONIC_RAW);
		for (int i = 0; i < N; i++)
			sink += clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
		double r3 = raw_ns(CLOCK_MONOTONIC_RAW);
		printf("pass %d: mach_absolute_time %.0f ns, mach_continuous_time %.0f ns, clock_gettime_nsec_np(UPTIME_RAW) "
		    "%.0f ns per call\n", pass + 1, (r1 - r0) / N, (r2 - r1) / N, (r3 - r2) / N);
	}
	(void)sink;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	mach_timebase_info(&tb);
	if (argc > 1 && !strcmp(argv[1], "-b")) {
		bench();
		return 0;
	}
	if (argc > 2 && !strcmp(argv[1], "-t"))
		return threads(argv[2]);
	if (argc > 2 && !strcmp(argv[1], "-c")) {
		int v = atoi(argv[2]), old = -1;
		size_t l = sizeof(old);
		int rc = sysctlbyname("kern.timer.coalescing_enabled", &old, &l, &v, sizeof(v));
		printf("kern.timer.coalescing_enabled %d -> %d: %s\n", old, v, rc ? strerror(errno) : "ok");
		waits("plain");
		return rc != 0;
	}
	printf("mach_timebase_info %u/%u = %.4f ns per tick = %.6f MHz\n", tb.numer, tb.denom, (double)tb.numer / tb.denom,
	    1e3 * tb.denom / tb.numer);
	sys64("hw.tbfrequency");
	sys64("hw.cpufrequency");
	sys64("hw.busfrequency");
	sys64("kern.timer.coalescing_enabled");
	struct clockinfo ci;
	size_t l = sizeof(ci);
	if (sysctlbyname("kern.clockrate", &ci, &l, NULL, 0) == 0)
		printf("  kern.clockrate         hz %d tick %d profhz %d\n", ci.hz, ci.tick, ci.profhz);

	/* the clocks against each other over 2 s */
	uint64_t a0 = mach_absolute_time(), c0 = mach_continuous_time();
	double m0 = raw_ns(CLOCK_MONOTONIC), r0 = raw_ns(CLOCK_MONOTONIC_RAW), u0 = raw_ns(CLOCK_UPTIME_RAW),
	       w0 = raw_ns(CLOCK_REALTIME);
	sleep(2);
	double da = abs_ns(mach_absolute_time() - a0), dc = abs_ns(mach_continuous_time() - c0);
	printf("over 2 s by mach_absolute_time: %.6f s; continuous %.6f; MONOTONIC %.6f; MONOTONIC_RAW %.6f; UPTIME_RAW "
	    "%.6f; REALTIME %.6f\n", da / 1e9, dc / 1e9, (raw_ns(CLOCK_MONOTONIC) - m0) / 1e9,
	    (raw_ns(CLOCK_MONOTONIC_RAW) - r0) / 1e9, (raw_ns(CLOCK_UPTIME_RAW) - u0) / 1e9,
	    (raw_ns(CLOCK_REALTIME) - w0) / 1e9);

	waits("plain");
	pthread_t t;
	pthread_create(&t, NULL, rt, NULL);
	pthread_join(t, NULL);
	return 0;
}

/*
 * libsweet-atcsfix.dylib: keeps locationd alive on a phone without a modem (MobileGestalt IsVirtualDevice).
 *
 * locationd's ATCommandStudio client (libATCommandStudioDynamic, +0x17164) creates a connection to CommCenter's
 * AT service, com.apple.commcenter.atcs.xpc; with no telephony product (IsVirtualDevice) the client object does
 * not take it, never resumes it, and the creator's xpc_release() then drops the last reference of a suspended
 * connection -- a libxpc trap (SIGTRAP in xpc_release). Here that one connection gets one extra reference, so
 * it is leaked (suspended, never used) instead. Inserted by sweet-locd (launchd strips DYLD_* from jobs).
 * MIT, as userland/.
 */
#include <os/log.h>
#include <string.h>
#include <xpc/xpc.h>

/* the SDK marks it unavailable on iOS; the system has it (as vibtest.c does for IORegistryEntrySetCFProperties) */
extern xpc_connection_t sweet_real_create(const char *name, dispatch_queue_t queue, uint64_t flags)
    __asm__("_xpc_connection_create_mach_service");

static xpc_connection_t
sweet_create_mach_service(const char *name, dispatch_queue_t queue, uint64_t flags)
{
	xpc_connection_t c = sweet_real_create(name, queue, flags);

	if (c != NULL && name != NULL && strcmp(name, "com.apple.commcenter.atcs.xpc") == 0) {
		static unsigned told;

		xpc_retain(c);
		if (told++ < 3) {
			os_log(OS_LOG_DEFAULT, "SWEET ATCSFIX: %{public}s: one extra reference (no modem)", name);
		}
	}
	return c;
}

__attribute__((used)) static const struct {
	const void *replacement, *original;
} interpose_create __attribute__((section("__DATA,__interpose"))) = {
	(const void *)sweet_create_mach_service, (const void *)sweet_real_create
};

__attribute__((constructor)) static void
sweet_atcsfix_loaded(void)
{
	os_log(OS_LOG_DEFAULT, "SWEET ATCSFIX: loaded");
}

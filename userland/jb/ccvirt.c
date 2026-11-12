/*
 * libsweet-ccvirt.dylib: CommCenter without a modem, for CommCenter alone.
 *
 * CommCenter (iPhone build) asks libTelephonyUtilDynamic which telephony product this is; for D22 (MobileGestalt
 * HWModelStr) that is the Mav17 modem, so libTelephonyBasebandDynamic waits for the AppleBaseband driver and throws
 * "Unable to communicate with AppleBaseband driver". With MobileGestalt IsVirtualDevice true the product is
 * "unknown" (a virtual device: no modem) and CommCenter runs -- but that answer is system-wide, and locationd then
 * crashes in its modem-GPS client (libATCommandStudio). So this sets the same "unknown" product inside CommCenter
 * only: libTelephonyUtil keeps it in a 12-byte global filled once by a dispatch_once block (20H392 cache,
 * unslid): product at 0x1e1fd0d60, its once predicate at 0x1e1fd0d40, the block literal at 0x1e1fd0728 whose
 * invoke is 0x1937c7d9c (writes zeros there for a virtual device). Marking that once as done with the product
 * left zero is what IsVirtualDevice does. The block's invoke pointer is checked first, so on another build this
 * does nothing. Inserted by sweet-cc (launchd strips DYLD_* from jobs). MIT, as userland/.
 */
#include <mach/mach.h>
#include <os/log.h>
#include <stdint.h>
#include <string.h>

/* dyld (private header): the shared cache's slid start and size */
extern const void *_dyld_get_shared_cache_range(size_t *length);

#define CACHE_BASE    0x180000000ull
#define PRODUCT       0x1e1fd0d60ull
#define PRODUCT_ONCE  0x1e1fd0d40ull
#define BLOCK_INVOKE  0x1e1fd0738ull
#define INVOKE        0x1937c7d9cull

__attribute__((constructor)) static void
sweet_ccvirt(void)
{
	size_t len = 0;
	const void *start = _dyld_get_shared_cache_range(&len);

	if (start == NULL) {
		os_log(OS_LOG_DEFAULT, "SWEET CCVIRT: no shared cache");
		return;
	}
	uint64_t slide = (uint64_t)(uintptr_t)start - CACHE_BASE;
	volatile uint64_t *invoke = (volatile uint64_t *)(uintptr_t)(BLOCK_INVOKE + slide);
	volatile long *once = (volatile long *)(uintptr_t)(PRODUCT_ONCE + slide);
	volatile uint32_t *product = (volatile uint32_t *)(uintptr_t)(PRODUCT + slide);

	if ((*invoke & 0xfffffffffull) != INVOKE + slide) {
		os_log(OS_LOG_DEFAULT, "SWEET CCVIRT: not the 20H392 libTelephonyUtil (block invoke %#llx); nothing done",
		    (unsigned long long)*invoke);
		return;
	}
	if (*once != 0) {
		os_log(OS_LOG_DEFAULT, "SWEET CCVIRT: product already computed (%u %u %u); nothing done", product[0], product[1],
		    product[2]);
		return;
	}
	/* the predicate and the product are shared-cache __DATA: copy-on-write, writable; make sure before writing */
	vm_address_t page = (vm_address_t)(uintptr_t)once & ~(vm_address_t)vm_page_mask;
	if (vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) !=
	    KERN_SUCCESS) {
		os_log(OS_LOG_DEFAULT, "SWEET CCVIRT: cannot make %#lx writable; nothing done", (unsigned long)page);
		return;
	}
	product[0] = product[1] = product[2] = 0;
	*once = ~0l;
	os_log(OS_LOG_DEFAULT, "SWEET CCVIRT: telephony product set to unknown (no modem) in this process");
}

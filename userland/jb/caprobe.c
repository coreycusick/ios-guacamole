/*
 * caprobe: CoreAnimation render-server checks (PosterBoard's wallpaper snapshots come back black).
 *   caprobe            where the CARenderServer* entry points are (unslid, for disassembly) and the server port
 *   caprobe displays   the render server's display names (CADisplay) -- snapshots name the display to render
 *   caprobe accel      can this system scale/copy IOSurfaces with IOSurfaceAccelerator (the 2D scaler CoreAnimation
 *                      uses for surface transfers)? Copies a grey surface into a black one and checks
 *   caprobe shot NAME  ask the render server to render display NAME into a new 1125x2436 IOSurface, as a
 *                      snapshot / screenshot does, and say how much of it is black
 * FrontBoardServices calls CARenderServerCreateSnapshots(0, ...); port 0 -> CARenderServerGetServerPort(NULL), and
 * the request names a display (CFString, CARenderServerRenderDisplayLayer... fails at once without one).
 * MIT, as userland/.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern const void *_dyld_get_shared_cache_range(size_t *length);

static void *qc;

static void
where(void)
{
	size_t len = 0;
	uintptr_t slide = (uintptr_t)_dyld_get_shared_cache_range(&len) - 0x180000000ul;
	const char *names[] = { "CARenderServerCreateSnapshots", "CARenderServerGetServerPort", "CARenderServerRenderDisplay",
		"CARenderServerRenderDisplayLayerWithTransformAndTimeOffset", "CARenderServerSnapshot", NULL };
	printf("slide %#lx\n", (unsigned long)slide);
	for (int i = 0; names[i]; i++) {
		void *p = dlsym(qc, names[i]);
		printf("%s %p (unslid %#lx)\n", names[i], p, p ? (unsigned long)((uintptr_t)p - slide) : 0ul);
	}
	mach_port_t (*getport)(const char *) = (mach_port_t (*)(const char *))dlsym(qc, "CARenderServerGetServerPort");
	if (getport)
		printf("CARenderServerGetServerPort(NULL) = %#x\n", getport(NULL));
}

static void
displays(void)
{
	Class cls = objc_getClass("CADisplay");
	id list = cls ? ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("displays")) : nil;
	long n = list ? ((long (*)(id, SEL))objc_msgSend)(list, sel_registerName("count")) : 0;
	printf("%ld CADisplay(s)\n", n);
	for (long i = 0; i < n; i++) {
		id d = ((id (*)(id, SEL, unsigned long))objc_msgSend)(list, sel_registerName("objectAtIndex:"), (unsigned long)i);
		id name = ((id (*)(id, SEL))objc_msgSend)(d, sel_registerName("name"));
		id dn = ((id (*)(id, SEL))objc_msgSend)(d, sel_registerName("deviceName"));
		unsigned id_ = ((unsigned (*)(id, SEL))objc_msgSend)(d, sel_registerName("displayId"));
		char a[128] = "", b[128] = "";
		if (name) CFStringGetCString((CFStringRef)name, a, sizeof(a), kCFStringEncodingUTF8);
		if (dn) CFStringGetCString((CFStringRef)dn, b, sizeof(b), kCFStringEncodingUTF8);
		printf("  display %ld: name \"%s\" deviceName \"%s\" id %u\n", i, a, b, id_);
	}
}

static IOSurfaceRef
new_surface(int w, int h)
{
	int bpe = 4, fmt = 'BGRA';
	CFNumberRef nw = CFNumberCreate(NULL, kCFNumberIntType, &w), nh = CFNumberCreate(NULL, kCFNumberIntType, &h),
	    nb = CFNumberCreate(NULL, kCFNumberIntType, &bpe), nf = CFNumberCreate(NULL, kCFNumberIntType, &fmt);
	const void *k[] = { kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement, kIOSurfacePixelFormat };
	const void *v[] = { nw, nh, nb, nf };
	CFDictionaryRef props = CFDictionaryCreate(NULL, k, v, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	return IOSurfaceCreate(props);
}

static int
accel(void)
{
	void *lib = dlopen("/System/Library/PrivateFrameworks/IOSurfaceAccelerator.framework/IOSurfaceAccelerator", RTLD_NOW);
	int (*create)(CFAllocatorRef, uint32_t, void **) = lib ? (int (*)(CFAllocatorRef, uint32_t, void **))dlsym(lib, "IOSurfaceAcceleratorCreate") : NULL;
	int (*transfer)(void *, IOSurfaceRef, IOSurfaceRef, CFDictionaryRef, void *, void *, void *) =
	    lib ? (int (*)(void *, IOSurfaceRef, IOSurfaceRef, CFDictionaryRef, void *, void *, void *))dlsym(lib, "IOSurfaceAcceleratorTransferSurface") : NULL;
	if (!create || !transfer) {
		printf("IOSurfaceAccelerator: library %p create %p transfer %p\n", lib, (void *)create, (void *)transfer);
		return 1;
	}
	void *acc = NULL;
	int kr = create(kCFAllocatorDefault, 0, &acc);
	printf("IOSurfaceAcceleratorCreate = %#x, accelerator %p\n", kr, acc);
	if (kr != 0 || acc == NULL)
		return 1;
	IOSurfaceRef src = new_surface(256, 256), dst = new_surface(256, 256);
	IOSurfaceLock(src, 0, NULL);
	memset(IOSurfaceGetBaseAddress(src), 0x80, IOSurfaceGetAllocSize(src));
	IOSurfaceUnlock(src, 0, NULL);
	kr = transfer(acc, src, dst, NULL, NULL, NULL, NULL);
	IOSurfaceLock(dst, kIOSurfaceLockReadOnly, NULL);
	const uint8_t *p = IOSurfaceGetBaseAddress(dst);
	printf("IOSurfaceAcceleratorTransferSurface = %#x; destination pixel %02x %02x %02x %02x (80 80 80 80 = copied)\n", kr, p[0], p[1], p[2], p[3]);
	IOSurfaceUnlock(dst, kIOSurfaceLockReadOnly, NULL);
	return 0;
}

static int
shot(const char *name)
{
	int w = 1125, h = 2436, bpe = 4, fmt = 'BGRA';
	CFNumberRef nw = CFNumberCreate(NULL, kCFNumberIntType, &w), nh = CFNumberCreate(NULL, kCFNumberIntType, &h),
	    nb = CFNumberCreate(NULL, kCFNumberIntType, &bpe), nf = CFNumberCreate(NULL, kCFNumberIntType, &fmt);
	const void *k[] = { kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement, kIOSurfacePixelFormat };
	const void *v[] = { nw, nh, nb, nf };
	CFDictionaryRef props = CFDictionaryCreate(NULL, k, v, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	IOSurfaceRef sf = IOSurfaceCreate(props);
	if (!sf) {
		printf("no IOSurface\n");
		return 1;
	}
	Boolean (*render)(mach_port_t, CFStringRef, IOSurfaceRef, int, int) =
	    (Boolean (*)(mach_port_t, CFStringRef, IOSurfaceRef, int, int))dlsym(qc, "CARenderServerRenderDisplay");
	CFStringRef dn = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
	Boolean ok = render ? render(0, dn, sf, 0, 0) : 0;
	IOSurfaceLock(sf, kIOSurfaceLockReadOnly, NULL);
	const uint8_t *base = IOSurfaceGetBaseAddress(sf);
	size_t row = IOSurfaceGetBytesPerRow(sf);
	unsigned black = 0;
	for (int j = 0; j < 8; j++)
		for (int i = 0; i < 8; i++) {
			const uint8_t *p = base + (size_t)(h * (2 * j + 1) / 16) * row + (size_t)(w * (2 * i + 1) / 16) * 4;
			black += p[0] < 8 && p[1] < 8 && p[2] < 8;
		}
	IOSurfaceUnlock(sf, kIOSurfaceLockReadOnly, NULL);
	printf("CARenderServerRenderDisplay(0, \"%s\") = %d; %u/64 black (surface %#x)\n", name, ok, black, IOSurfaceGetID(sf));
	return 0;
}

int
main(int argc, char **argv)
{
	qc = dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore", RTLD_NOW);
	if (!qc) {
		printf("no QuartzCore\n");
		return 1;
	}
	if (argc > 1 && strcmp(argv[1], "displays") == 0) {
		displays();
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "accel") == 0)
		return accel();
	if (argc > 2 && strcmp(argv[1], "shot") == 0)
		return shot(argv[2]);
	where();
	return 0;
}

/*
 * sbprobe: looks at UIKit's status bar server from a command line, to learn the iOS 16 layout of the private
 * status bar data (StatusBarRawData / StatusBarOverrideData) before writing a "No SIM" override.
 *   sbprobe            print the shared cache slide, the IMPs of UIStatusBarServer's class methods, and a hex
 *                      dump of +getStatusBarData (the data SpringBoard posted: the time string shows where the
 *                      strings start)
 * MIT, as userland/.
 */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>

extern const void *_dyld_get_shared_cache_range(size_t *length);

static void
dump(const unsigned char *p, size_t n)
{
	for (size_t i = 0; i < n; i += 16) {
		printf("%04zx:", i);
		for (size_t j = 0; j < 16; j++)
			printf(" %02x", p[i + j]);
		printf("  ");
		for (size_t j = 0; j < 16; j++)
			putchar(p[i + j] >= 32 && p[i + j] < 127 ? p[i + j] : '.');
		putchar('\n');
	}
}

int
main(int argc, char **argv)
{
	(void)argc; (void)argv;
	@autoreleasepool {
		size_t len = 0;
		const void *start = _dyld_get_shared_cache_range(&len);
		printf("slide %#lx\n", (unsigned long)((uintptr_t)start - 0x180000000ul));
		dlopen("/System/Library/PrivateFrameworks/UIKitCore.framework/UIKitCore", RTLD_NOW);
		Class c = objc_getClass("UIStatusBarServer");
		if (c == Nil) {
			printf("no UIStatusBarServer\n");
			return 1;
		}
		unsigned n = 0;
		Method *m = class_copyMethodList(object_getClass(c), &n);
		for (unsigned i = 0; i < n; i++)
			printf("+%s %#lx %s\n", sel_getName(method_getName(m[i])), (unsigned long)method_getImplementation(m[i]),
			    method_getTypeEncoding(m[i]));
		SEL g = sel_registerName("getStatusBarData");
		if ([c respondsToSelector:g]) {
			const unsigned char *raw = ((const unsigned char *(*)(id, SEL))objc_msgSend)(c, g);
			printf("getStatusBarData -> %p\n", raw);
			if (raw != NULL)
				dump(raw, 3072);
		}
	}
	return 0;
}

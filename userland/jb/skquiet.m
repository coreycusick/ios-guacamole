/*
 * libsweet-skquiet.dylib (2026-11-27): an app's App Store receipt refresh ends at once as if its sign-in sheet had been
 * cancelled, with no sheet.
 *
 * With no Apple Account on the phone, -[SKReceiptRefreshRequest start] puts up iOS's Apple Account sign-in sheet.
 * Balatro asks for one at launch ("[IAP] No receipt found, refreshing..."), and its SDL keyboard observer crashes when
 * the sheet's text field brings the keyboard up; closed quickly enough, the game runs. Here -start answers the request's
 * delegate with request:didFailWithError: (SKErrorDomain, SKErrorPaymentCancelled), which is what the app gets when the
 * sheet is cancelled by hand. Nothing else changes: no receipt is made up, the app sees the same failure it already
 * handles, and every other StoreKit call goes to StoreKit as before.
 *
 * Loaded by an LC_LOAD_DYLIB of the app's executable: EXTRA_DYLIBS=/usr/lib/libsweet-skquiet.dylib tools/angle-app.sh
 * (or tools/insert-dylib.py). A line goes to /var/local/gl.log when that file exists. MIT, as userland/.
 */
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
note(const char *text)
{
	int fd = open("/var/local/gl.log", O_WRONLY | O_APPEND);
	if (fd >= 0) {
		char line[256];
		int n = snprintf(line, sizeof(line), "SK[%d %s]: %s\n", getpid(), getprogname(), text);
		write(fd, line, n > 0 && n < (int)sizeof(line) ? (size_t)n : sizeof(line) - 1);
		close(fd);
	}
}

static void
quiet_start(id self, SEL selector)
{
	(void)selector;
	note("receipt refresh answered as cancelled (no sign-in sheet)");
	id request = self;
	dispatch_async(dispatch_get_main_queue(), ^{
		id delegate = ((id (*)(id, SEL))objc_msgSend)(request, sel_registerName("delegate"));
		SEL fail = sel_registerName("request:didFailWithError:");
		if (delegate != nil && [delegate respondsToSelector:fail]) {
			NSError *error = [NSError errorWithDomain:@"SKErrorDomain" code:2 /* SKErrorPaymentCancelled */ userInfo:nil];
			((void (*)(id, SEL, id, id))objc_msgSend)(delegate, fail, request, error);
		}
	});
}

__attribute__((constructor)) static void
skquiet_init(void)
{
	/* StoreKit may not be loaded yet: the class is looked up again once it is */
	Class cls = objc_getClass("SKReceiptRefreshRequest");
	if (cls == Nil) {
		void *handle = dlopen("/System/Library/Frameworks/StoreKit.framework/StoreKit", RTLD_LAZY);
		(void)handle;
		cls = objc_getClass("SKReceiptRefreshRequest");
	}
	Method start = cls ? class_getInstanceMethod(cls, sel_registerName("start")) : NULL;
	if (start == NULL) {
		note("no SKReceiptRefreshRequest -start: nothing changed");
		return;
	}
	/* only the subclass: other SKRequests (product lists) keep SKRequest's -start */
	if (!class_addMethod(cls, sel_registerName("start"), (IMP)quiet_start, method_getTypeEncoding(start))) {
		method_setImplementation(start, (IMP)quiet_start);
	}
	note("SKReceiptRefreshRequest -start replaced");
}

/*
 * glprobe: why OpenGL ES apps fail on iOS 27 (Balatro "cannot use OpenGL", Geometry Dash strlen(NULL) at launch,
 * Angry Birds Classic C++ throw at launch). iOS 27's OpenGL ES is OpenGLES.framework -> GLEngine.bundle ->
 * AppleMetalGLRenderer (GL on Metal), all in the shared cache; OpenGLES finds the GPU as IOAcceleratorES.
 * This asks each step in turn, one line at a time (flushed: if one crashes, the last line says where):
 *   the Metal device; dlopen of GLEngine and AppleMetalGLRenderer; EAGLContext for ES 3 / 2 / 1;
 *   then on the first context that works: glGetString, an offscreen 64x64 framebuffer cleared to a colour and read back.
 *   glprobe [-v | -n]   (-n: no Metal device first)
 * Read-only apart from one small clear on the GPU. MIT, as userland/.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#import <objc/runtime.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAY(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

static void try_dlopen(const char *path, const char *sym)
{
	void *h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
	if (h == NULL) {
		SAY("dlopen %s: FAILED: %s", path, dlerror());
		return;
	}
	SAY("dlopen %s: ok; %s %s", path, sym, dlsym(h, sym) ? "found" : "MISSING");
}

static const char *gls(GLenum e)
{
	const GLubyte *s = glGetString(e);
	return s ? (const char *)s : "(NULL)";
}

int main(int argc, char **argv)
{
	/* -v: also the step-by-step OpenGLES / GLEngine / renderer calls by hand (their arguments are reconstructed) */
	const int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
	/* -n: no Metal device in this process (ANGLE: Turnip's darwin backend and a second GPU client in one process) */
	const int no_metal = argc > 1 && strcmp(argv[1], "-n") == 0;
	@autoreleasepool {
		id<MTLDevice> dev = no_metal ? nil : MTLCreateSystemDefaultDevice();
		SAY("Metal device: %s", dev ? dev.name.UTF8String : "NONE");
		try_dlopen("/System/Library/Frameworks/OpenGLES.framework/OpenGLES", "EAGLGetVersion");
		try_dlopen("/System/Library/Frameworks/OpenGLES.framework/GLEngine.bundle/GLEngine", "gliInitializeLibrary");
		try_dlopen("/System/Library/Extensions/AppleMetalGLRenderer.bundle/AppleMetalGLRenderer", "gldCreateDevice");

		/*
		 * AppleMetalGLRenderer's GLDDeviceRec::init: MTLCreateSystemDefaultDevice, then default.metallib from its
		 * bundle (newDefaultLibraryWithBundle), then newFunctionWithName for its built-in shaders ("clearVertex" ...).
		 * Any of these failing makes the renderer refuse, and every EAGLContext init returns nil.
		 */
		if (!no_metal) {
		NSString *bp = @"/System/Library/Extensions/AppleMetalGLRenderer.bundle";
		NSArray *files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:bp error:nil];
		SAY("AppleMetalGLRenderer.bundle: %s", files ? [files componentsJoinedByString:@" "].UTF8String : "(cannot list)");
		NSBundle *b = [NSBundle bundleWithPath:bp];
		SAY("NSBundle: %s, default.metallib %s", b ? "ok" : "nil",
		    [b pathForResource:@"default" ofType:@"metallib"].UTF8String ?: "NOT FOUND");
		if (dev != nil && b != nil) {
			NSError *err = nil;
			id<MTLLibrary> lib = [dev newDefaultLibraryWithBundle:b error:&err];
			SAY("newDefaultLibraryWithBundle: %s %s", lib ? "ok" : "nil", err ? err.description.UTF8String : "");
			if (lib != nil) {
				NSArray<NSString *> *names = lib.functionNames;
				SAY("  %lu functions", (unsigned long)names.count);
				unsigned bad = 0;
				for (NSString *n in names) {
					id<MTLFunction> fn = [lib newFunctionWithName:n];
					if (fn == nil) {
						if (bad++ < 10)
							SAY("  newFunctionWithName %s: nil", n.UTF8String);
					}
				}
				SAY("  functions that returned nil: %u of %lu", bad, (unsigned long)names.count);
				SAY("  clearVertex: %s", [lib newFunctionWithName:@"clearVertex"] ? "ok" : "nil");
			}
		}

		}       /* !no_metal */

		if (verbose) {
		/*
		 * OpenGLES's eagl_init, by hand: GLEngine's gliInitializeLibrary(&accel, &flag, 1, 0, 0x40000000) runs
		 * libGFXShared's gfxInitializeLibrary, which should leave AppleMetalGLRenderer on the plugin list.
		 * gfxInitializeLibrary returns at once if something in the process initialised it before (gfx_ref_count).
		 */
		{
			void *gfx = dlopen("/System/Library/Frameworks/OpenGLES.framework/libGFXShared.dylib", RTLD_LAZY);
			void *(*plugins)(void) = gfx ? dlsym(gfx, "gfxGetPlugins") : NULL;
			void *gle = dlopen("/System/Library/Frameworks/OpenGLES.framework/GLEngine.bundle/GLEngine", RTLD_LAZY);
			void (*gliInit)(void *, void *, uint32_t, void *, uint32_t) = gle ? dlsym(gle, "gliInitializeLibrary") : NULL;
			SAY("before gliInitializeLibrary: gfxGetPlugins %p", plugins ? plugins() : (void *)-1);
			static io_service_t a0;
			static char f0;
			a0 = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOAcceleratorES"));
			if (gliInit != NULL) {
				gliInit(&a0, &f0, 1, NULL, 0x40000000);
				SAY("after gliInitializeLibrary(&accel %#x, 1): gfxGetPlugins %p, flag %d", a0,
				    plugins ? plugins() : (void *)-1, f0);
			}
			/*
			 * eagl_init's next two steps: gliGetVersion(&flag, &a, &b) must return nonzero, then
			 * gliChoosePixelFormat(&pf, attribs) must return 0 with a pixel format. attribs: eagl_init's constant
			 * (OpenGLES 0x24ed89690 unslid, 0x28 bytes; slide from EAGLGetVersion at 0x24ed88d98).
			 */
			int (*gliVer)(char *, uint32_t *, uint32_t *) = gle ? dlsym(gle, "gliGetVersion") : NULL;
			int (*gliPF)(void **, const uint32_t *) = gle ? dlsym(gle, "gliChoosePixelFormat") : NULL;
			void *ev = dlsym(RTLD_DEFAULT, "EAGLGetVersion");
			if (gliVer != NULL) {
				uint32_t va = 0, vb = 0;
				char fl = 0;
				int r = gliVer(&fl, &va, &vb);
				SAY("gliGetVersion -> %d (nonzero needed), flag %d, %u %u", r, fl, va, vb);
			}
			if (gliPF != NULL && ev != NULL) {
				uintptr_t slide = (uintptr_t)ev - 0x24ed88d98ull;
				const uint32_t *attr = (const uint32_t *)(0x24ed89690ull + slide);
				uint32_t copy[10];
				memcpy(copy, attr, 32);
				copy[8] = 0;
				copy[9] = 0;
				SAY("eagl attribs: %u %u %u %u %u %u %u %u", copy[0], copy[1], copy[2], copy[3], copy[4], copy[5],
				    copy[6], copy[7]);
				void *pf = NULL;
				int r = gliPF(&pf, copy);
				SAY("gliChoosePixelFormat -> %d (0 needed; 10014 = 0x271e bad args, 10015 = no plugins), pf %p", r, pf);
				/*
				 * -[EAGLContext commonInitWithAPI:properties:] then calls gliCreateContextWithShared(&ctx, pixel format,
				 * shared state (NULL: new), &dispatch, ctx+0x2088, flags from OpenGLES's table at 0x24ed89678
				 * [api - 1]); nonzero -> nil. Errors: 0x2712 bad pixel format, 0x2719 share mismatch,
				 * 0x271e bad args, 0x2723 command buffer, anything else from the renderer (gldCreateContext).
				 */
				int (*gliCreate)(void **, void *, void *, void *, void *, uint32_t) =
				    gle ? dlsym(gle, "gliCreateContextWithShared") : NULL;
				if (gliCreate != NULL && r == 0 && pf != NULL) {
					const uint32_t *tbl = (const uint32_t *)(0x24ed89678ull + slide);
					for (int api = 3; api >= 1; api--) {
						void *gctx = NULL, *disp = NULL;
						void *area = calloc(1, 0x4000);
						SAY("gliCreateContextWithShared(API %d, flags %#x) ...", api, tbl[api - 1]);
						int cr = gliCreate(&gctx, pf, NULL, &disp, area, tbl[api - 1]);
						SAY("  -> %d (%#x), context %p", cr, cr, gctx);
					}
				}
			}
		}

		/*
		 * libGFXShared's gfxInitializeLibrary keeps the renderer only if gldGetVersion answers 4 / 1 / 16 with a renderer
		 * id whose bits 0xffff00ff are zero, all 129 gld* names resolve (they do: all exported), and gldCreateDevice
		 * (out, display mask, out) returns 0. Ask the renderer the same two questions here.
		 */
		{
			void *h = dlopen("/System/Library/Extensions/AppleMetalGLRenderer.bundle/AppleMetalGLRenderer", RTLD_LAZY);
			int (*getVersion)(uint32_t *, uint32_t *, uint32_t *, uint32_t *) = h ? dlsym(h, "gldGetVersion") : NULL;
			void (*initLib)(void *, void *, uint32_t, void *, void *) = h ? dlsym(h, "gldInitializeLibrary") : NULL;
			int (*createDevice)(void **, uint32_t, void **) = h ? dlsym(h, "gldCreateDevice") : NULL;
			/*
			 * As OpenGLES's eagl_init: the first IOAcceleratorES service, then gliInitializeLibrary(&accel, &flag, 1, ...)
			 * -> gfxInitializeLibrary -> gldInitializeLibrary(services, flags, display mask 1, ...).
			 */
			static io_service_t accel;
			static char accelFlag;
			accel = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOAcceleratorES"));
			io_name_t cls = "";
			if (accel != 0)
				IOObjectGetClass(accel, cls);
			CFTypeRef gles = accel ? IORegistryEntryCreateCFProperty(accel, CFSTR("IOGLESMetalBundleName"), kCFAllocatorDefault, 0) : NULL;
			SAY("IOAcceleratorES service %#x, class %s, IOGLESMetalBundleName %s", accel, cls,
			    gles ? [(__bridge NSString *)gles description].UTF8String : "(none)");
			if (gles != NULL)
				CFRelease(gles);
			if (initLib != NULL)
				initLib(&accel, &accelFlag, 1, NULL, NULL);   /* gldGetVersion reads what this stores (crashed without it) */
			if (getVersion != NULL) {
				uint32_t a = 0, v = 0, c = 0, id = 0;
				int r = getVersion(&a, &v, &c, &id);
				SAY("gldGetVersion -> %d: %u / %u / %u, renderer id %#x (libGFXShared wants nonzero return, 4 / 1 / 16, "
				    "id & 0xffff00ff == 0)", r, a, v, c, id);
			}
			if (initLib != NULL && createDevice != NULL) {
				void *d1 = NULL, *d2 = NULL;
				SAY("gldCreateDevice(display mask 1) ...");
				int r = createDevice(&d1, 1, &d2);
				SAY("  -> %d (0 = ok), %p %p", r, d1, d2);
			}
		}

		}       /* verbose */

		EAGLContext *ctx = nil;
		EAGLRenderingAPI apis[] = { kEAGLRenderingAPIOpenGLES3, kEAGLRenderingAPIOpenGLES2, kEAGLRenderingAPIOpenGLES1 };
		for (int i = 0; i < 3; i++) {
			SAY("EAGLContext initWithAPI:%d ...", (int)apis[i]);
			EAGLContext *c = [[EAGLContext alloc] initWithAPI:apis[i]];
			SAY("  -> %s", c ? "ok" : "nil");
			if (c != nil && ctx == nil)
				ctx = c;
		}
		if (ctx == nil) {
			void *(*getPlugins)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT, "gfxGetPlugins");
			SAY("gfxGetPlugins: %p (NULL = no GL renderer plugin loaded)", getPlugins ? getPlugins() : (void *)-1);
			SAY("no EAGLContext: OpenGL ES apps cannot start");
			return 1;
		}
		SAY("setCurrentContext (API %d): %d", (int)ctx.API, [EAGLContext setCurrentContext:ctx]);
		SAY("GL_VENDOR   %s", gls(GL_VENDOR));
		SAY("GL_RENDERER %s", gls(GL_RENDERER));
		SAY("GL_VERSION  %s", gls(GL_VERSION));
		SAY("GL_EXTENSIONS length %zu", strlen(gls(GL_EXTENSIONS)));

		GLuint fbo = 0, rb = 0;
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glGenRenderbuffers(1, &rb);
		glBindRenderbuffer(GL_RENDERBUFFER, rb);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 64);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
		SAY("framebuffer status 0x%x (0x8cd5 = complete), glGetError 0x%x", glCheckFramebufferStatus(GL_FRAMEBUFFER),
		    glGetError());
		glViewport(0, 0, 64, 64);
		glClearColor(1.0f, 0.5f, 0.25f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		SAY("clear issued, glFinish ...");
		glFinish();
		unsigned char px[4] = { 0 };
		glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		SAY("read back %u %u %u %u (expected about 255 128 64 255), glGetError 0x%x", px[0], px[1], px[2], px[3],
		    glGetError());
		glDeleteRenderbuffers(1, &rb);
		glDeleteFramebuffers(1, &fbo);
		[EAGLContext setCurrentContext:nil];
		SAY("done");
	}
	return 0;
}

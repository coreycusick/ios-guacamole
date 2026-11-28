/*
 * The OpenGL ES library behind eagl.c when that library is ANGLE (Google's OpenGL ES 1 / 2 / 3 on Vulkan, BSD) built
 * for iOS with its Vulkan back end only, on Turnip (libvulkan_freedreno.dylib in NativeAdrenoMetal.bundle): an app's
 * OpenGL ES without Metal or Apple's GL-on-Metal renderer in between (2026-11-27).
 *
 * As gl_osmesa.c: a context of a version of OpenGL ES, made the calling thread's, taken away again, a name for the log.
 * ANGLE's contexts come through EGL. Nothing here draws into a window: each context is made current on a small pbuffer
 * and draws into the app's renderbuffers (eagl.c), which -presentRenderbuffer: reads back into IOSurfaces.
 * redwood_gl_surface_texture answers 0 (no IOSurface-backed textures on ANGLE's Vulkan back end): eagl.c copies.
 *
 * Links against ANGLE's libGLESv2.dylib only (/usr/lib/angle/), whose EGL_ functions are its EGL (see below).
 * MIT, as userland/.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "redwood_gl.h"

/* EGL/egl.h, EGL/eglext.h, EGL/eglext_angle.h */
typedef void *EGLDisplay, *EGLContext, *EGLSurface, *EGLConfig;
typedef int32_t EGLint;
typedef unsigned EGLBoolean;
typedef unsigned EGLenum;
typedef intptr_t EGLAttrib;
#define EGL_DEFAULT_DISPLAY             ((void *)0)
#define EGL_NO_CONTEXT                  ((EGLContext)0)
#define EGL_NO_SURFACE                  ((EGLSurface)0)
#define EGL_NONE                        0x3038
#define EGL_RED_SIZE                    0x3024
#define EGL_GREEN_SIZE                  0x3023
#define EGL_BLUE_SIZE                   0x3022
#define EGL_ALPHA_SIZE                  0x3021
#define EGL_DEPTH_SIZE                  0x3025
#define EGL_STENCIL_SIZE                0x3026
#define EGL_SURFACE_TYPE                0x3033
#define EGL_PBUFFER_BIT                 0x0001
#define EGL_RENDERABLE_TYPE             0x3040
#define EGL_OPENGL_ES_BIT               0x0001
#define EGL_OPENGL_ES2_BIT              0x0004
#define EGL_OPENGL_ES3_BIT              0x0040
#define EGL_WIDTH                       0x3057
#define EGL_HEIGHT                      0x3056
#define EGL_CONTEXT_MAJOR_VERSION       0x3098
#define EGL_CONTEXT_MINOR_VERSION       0x30FB
#define EGL_OPENGL_ES_API               0x30A0
#define EGL_PLATFORM_ANGLE_ANGLE        0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE   0x3203
#define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE            0x3450
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE            0x3209
#define EGL_PLATFORM_ANGLE_NATIVE_PLATFORM_TYPE_ANGLE   0x348F
#define EGL_PLATFORM_SURFACELESS_MESA                   0x31DD
#define EGL_EXTENSIONS                  0x3055
#define EGL_VERSION                     0x3054
#define GL_RENDERER                     0x1f01
#define GL_VERSION                      0x1f02

/*
 * ANGLE's libEGL is only a loader that dlopens libGLESv2 from @executable_path/Frameworks/libGLESv2.framework (an iOS app's
 * own copy), which a game we load into does not have. libGLESv2 exports the same EGL functions itself as EGL_Name:
 * call those, and do not link libEGL at all.
 */
#define eglGetPlatformDisplay   EGL_GetPlatformDisplay
#define eglInitialize           EGL_Initialize
#define eglBindAPI              EGL_BindAPI
#define eglChooseConfig         EGL_ChooseConfig
#define eglCreatePbufferSurface EGL_CreatePbufferSurface
#define eglCreateContext        EGL_CreateContext
#define eglMakeCurrent          EGL_MakeCurrent
#define eglDestroyContext       EGL_DestroyContext
#define eglDestroySurface       EGL_DestroySurface
#define eglGetError             EGL_GetError
#define eglQueryString          EGL_QueryString
#define eglGetProcAddress       EGL_GetProcAddress
extern EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native, const EGLAttrib *attributes);
extern EGLBoolean eglInitialize(EGLDisplay display, EGLint *major, EGLint *minor);
extern EGLBoolean eglBindAPI(EGLenum api);
extern EGLBoolean eglChooseConfig(EGLDisplay display, const EGLint *attributes, EGLConfig *configs, EGLint size, EGLint *count);
extern EGLSurface eglCreatePbufferSurface(EGLDisplay display, EGLConfig config, const EGLint *attributes);
extern EGLContext eglCreateContext(EGLDisplay display, EGLConfig config, EGLContext share, const EGLint *attributes);
extern EGLBoolean eglMakeCurrent(EGLDisplay display, EGLSurface draw, EGLSurface read, EGLContext context);
extern EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext context);
extern EGLBoolean eglDestroySurface(EGLDisplay display, EGLSurface surface);
extern EGLint eglGetError(void);
extern const char *eglQueryString(EGLDisplay display, EGLint name);
extern void (*eglGetProcAddress(const char *name))(void);

struct redwood_gl {
	EGLContext context;
	EGLSurface surface;
	int version;
};

static EGLDisplay display;
static EGLConfig configs[2];                    /* [0] OpenGL ES 2 / 3, [1] OpenGL ES 1 */

static void
say_gl(const char *what)
{
	fprintf(stderr, "redwood-gl (ANGLE): %s: EGL error %#x\n", what, eglGetError());
}

/*
 * ANGLE's Vulkan display with no window system (headless / surfaceless, as the build has it): only pbuffers.
 * The Vulkan driver is Turnip (the build points ANGLE at libvulkan_freedreno.dylib).
 */
static int
display_ready(void)
{
	static int tried, ready;

	if (tried) {
		return ready;
	}
	tried = 1;
	const EGLAttrib attributes[] = {
		EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
		EGL_PLATFORM_ANGLE_NATIVE_PLATFORM_TYPE_ANGLE, EGL_PLATFORM_SURFACELESS_MESA,
		EGL_NONE
	};
	display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attributes);
	if (display == NULL) {
		/* a build whose Vulkan display has no surfaceless type: its default one */
		const EGLAttrib plain[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE, EGL_NONE };
		display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, plain);
	}
	EGLint major = 0, minor = 0;
	if (display == NULL || !eglInitialize(display, &major, &minor)) {
		say_gl("no Vulkan display");
		return 0;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint es23[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	const EGLint es1[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	EGLint count = 0;
	if (!eglChooseConfig(display, es23, &configs[0], 1, &count) || count < 1) {
		say_gl("no OpenGL ES 2/3 pbuffer configuration");
		return 0;
	}
	count = 0;
	if (!eglChooseConfig(display, es1, &configs[1], 1, &count) || count < 1) {
		configs[1] = NULL;      /* OpenGL ES 1 refused below */
	}
	fprintf(stderr, "redwood-gl (ANGLE): EGL %d.%d, %s\n", major, minor, eglQueryString(display, EGL_VERSION));
	ready = 1;
	return 1;
}

struct redwood_gl *
redwood_gl_create(int version)
{
	if (!display_ready() || version < 1 || version > 3) {
		return NULL;
	}
	EGLConfig config = configs[version == 1 ? 1 : 0];
	if (config == NULL) {
		return NULL;
	}
	struct redwood_gl *gl = calloc(1, sizeof(*gl));
	if (gl == NULL) {
		return NULL;
	}
	const EGLint size[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
	const EGLint attributes[] = { EGL_CONTEXT_MAJOR_VERSION, version, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
	gl->version = version;
	gl->surface = eglCreatePbufferSurface(display, config, size);
	gl->context = gl->surface ? eglCreateContext(display, config, EGL_NO_CONTEXT, attributes) : EGL_NO_CONTEXT;
	if (gl->context == EGL_NO_CONTEXT) {
		say_gl(version == 1 ? "no OpenGL ES 1 context" : "no OpenGL ES 2/3 context");
		if (gl->surface != EGL_NO_SURFACE) {
			eglDestroySurface(display, gl->surface);
		}
		free(gl);
		return NULL;
	}
	return gl;
}

int
redwood_gl_make_current(struct redwood_gl *gl)
{
	if (gl == NULL) {
		return display == NULL || eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}
	if (!eglMakeCurrent(display, gl->surface, gl->surface, gl->context)) {
		say_gl("eglMakeCurrent");
		return 0;
	}
	return 1;
}

void
redwood_gl_destroy(struct redwood_gl *gl)
{
	eglDestroyContext(display, gl->context);
	eglDestroySurface(display, gl->surface);
	free(gl);
}

const char *
redwood_gl_library(void)
{
	return "ANGLE (OpenGL ES on Vulkan, Turnip)";
}

/* No OpenGL ES 1 context to be had: eagl.c refuses API 1 then */
int
redwood_gl_supports_es1(void)
{
	return display_ready() && configs[1] != NULL;
}

/* A function of the library by name (OES framebuffer functions for an OpenGL ES 1 context) */
void *
redwood_gl_proc(const char *name)
{
	return (void *)eglGetProcAddress(name);
}

/* ANGLE's Vulkan back end makes no texture of an IOSurface's memory: eagl.c reads frames back instead */
int
redwood_gl_surface_texture(struct redwood_gl *gl, uint32_t surface)
{
	(void)gl;
	(void)surface;
	return 0;
}

#if !defined(ANDROID) && !defined(AMIGAOS4)
#include <execinfo.h>
#endif
#include <fcntl.h>

#include "../gl/init.h"

#ifdef USE_FBIO
#include <linux/fb.h>
#endif
#ifdef PANDORA
#include <sys/socket.h>
#include <sys/un.h>
#endif
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include "glx.h"
#include "utils.h"
#include "../gl/gl.h"
#include "../glx/streaming.h"
#include "khash.h"
#include "hardext.h"
#include "../gl/debug.h"
#ifdef AMIGAOS4
#include "../agl/amigaos.h"
#endif

#ifndef AliasExport
#define AliasExport(name)   __attribute__((alias(name))) __attribute__((visibility("default")))
#endif



//#define DEBUG
#ifdef DEBUG
#pragma GCC optimize 0
#define DBG(a) a
#else
#define DBG(a)
#endif

#ifndef EGL_GL_COLORSPACE_KHR
#define EGL_GL_COLORSPACE_KHR                   0x309D
#define EGL_GL_COLORSPACE_SRGB_KHR              0x3089
#define EGL_GL_COLORSPACE_LINEAR_KHR            0x308A
#endif

#ifndef NOEGL
static bool eglInitialized = false;
static EGLDisplay eglDisplay;
static EGLSurface eglSurface;
static EGLConfig eglConfigs[1];
#endif
static int glx_default_depth=0;
#ifdef PANDORA
static struct sockaddr_un sun;
static int sock = -2;
#endif

#ifndef NOX11
typedef struct {
    int Width; 
    int Height; 
    EGLContext Context; 
    EGLSurface Surface;
    EGLConfig  Config;
    int Depth; 
    Display *dpy; 
    int Type; GC gc; 
    XImage* frame; 
} glx_buffSize;

//PBuffer should work under ANDROID / NOX11
static GLXPbuffer *pbufferlist = NULL;
static glx_buffSize *pbuffersize = NULL;
static int pbufferlist_cap = 0;
static int pbufferlist_size = 0;
static int isPBuffer(GLXDrawable drawable) {
    for (int i=0; i<pbufferlist_size; i++)
        if(pbufferlist[i]==(GLXPbuffer)drawable)
            return i+1;
    return 0;
}
void BlitEmulatedPixmap();
int createPBuffer(Display * dpy, const EGLint * egl_attribs, EGLSurface* Surface, EGLContext* Context, EGLConfig* Config, int redBits, int greenBits, int blueBits, int alphaBits, int samplebuffers, int samples);
GLXPbuffer addPixBuffer(Display *dpy, EGLSurface surface, EGLConfig Config, int Width, int Height, EGLContext Context, Pixmap pixmap, int depth, int emulated);

static Display *g_display = NULL;
static GLXContext glxContext = NULL;
static GLXContext fbContext = NULL;
static GLuint current_fb = 0;

#endif //NOX11
void gl4es_getMainFBSize(GLint* width, GLint* height) {
#ifndef NOEGL
    // noegl, no updating of framebuffer size
    DBG(printf("gl4es_getMainFBSize() %dx%d -> ", *width, *height);)
    LOAD_EGL(eglQuerySurface);
    egl_eglQuerySurface(eglDisplay, glxContext->eglSurface, EGL_WIDTH, width);
    egl_eglQuerySurface(eglDisplay, glxContext->eglSurface, EGL_HEIGHT, height);
    DBG(printf("%dx%d (%s)\n", *width, *height, PrintEGLError(0));)
#endif
}

#ifndef NOEGL
// hmm...
static EGLContext eglContext  = EGL_NO_CONTEXT;
#endif

static int fbcontext_count = 0;

#ifdef USE_FBIO
#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif
static int fbdev = -1;
#endif

static int  g_width=0, g_height=0;
static int swapinterval = 1;    // default value. Also, should be tracked by drawable...
static int minswap=0;
static int maxswap=1;
// **** RPI stuffs ****
static bool g_bcmhost = false;
static bool g_bcm_active = false;
void (*bcm_host_init)();
void (*bcm_host_deinit)();
#ifndef ANDROID
#include "rpi.h"
#endif
// ***** end of RPI stuffs ****

// Generic create native window to use with "LIBGL_FB=1" (so with EGL_DEFAULT_DISPLAY and without X11)
static void* create_native_window(int w, int h) {
#if !defined(ANDROID) && !defined(AMIGAOS4)
    if(bcm_host) return create_rpi_window(w, h);
#endif

    return NULL;
}

#define SHUT(a) if(!globals4es.nobanner) a

#ifndef NOEGL
static EGLint egl_context_attrib_es2[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
};

static EGLint egl_context_attrib[] = {
    EGL_NONE
};
#endif


extern void* egl;
int globales2 = 0;
// GLState management
void* NewGLState(void* shared_glstate, int es2only);
void DeleteGLState(void* oldstate);
void ActivateGLState(void* new_glstate);

typedef struct {
    int drawable;
#ifndef NOEGL
    EGLSurface surface;
#endif
    int PBuffer;
} map_drawable_t;
KHASH_MAP_INIT_INT(mapdrawable, map_drawable_t*)
khash_t(mapdrawable) *MapDrawable = NULL;

int8_t CheckEGLErrors() {
#ifndef NOEGL
    EGLenum error;
    const char *errortext = PrintEGLError(1);
    
    if (errortext) {
        LOGE("LIBGL: ERROR: EGL Error detected: %s (0x%X)\n", errortext, error);
        return 1;
    }
#endif
    return 0;
}
#ifndef NOX11
static int get_config_default(Display *display, int attribute, int *value) {
    switch (attribute) {
        case GLX_USE_GL:
        case GLX_RGBA:
        case GLX_DOUBLEBUFFER:
        case GLX_X_RENDERABLE:
            *value = 1;
            break;
        case GLX_LEVEL:
        case GLX_STEREO:
            *value = 0;
            break;
        case GLX_AUX_BUFFERS:
            *value = 0;
            break;
        case GLX_RED_SIZE:
            *value = 5;
            break;
        case GLX_GREEN_SIZE:
            *value = 6;
            break;
        case GLX_BLUE_SIZE:
            *value = 5;
            break;
        case GLX_ALPHA_SIZE:
            *value = 8;
            break;
        case GLX_DEPTH_SIZE:
            *value = 16;
            break;
        case GLX_STENCIL_SIZE:
            *value = 8;
            break;
        case GLX_ACCUM_RED_SIZE:
        case GLX_ACCUM_GREEN_SIZE:
        case GLX_ACCUM_BLUE_SIZE:
        case GLX_ACCUM_ALPHA_SIZE:
            *value = 0;
            break;
        case GLX_TRANSPARENT_TYPE:
            *value = GLX_NONE;
            break;
        case GLX_RENDER_TYPE:
            *value = GLX_RGBA_TYPE;
            break;
        case GLX_VISUAL_ID:
            *value = gl4es_glXChooseVisual(display, 0, NULL)->visualid;
            //*value = 1;
            break;
        case GLX_FBCONFIG_ID:
            *value = 1;
            break;
        case GLX_DRAWABLE_TYPE:
            *value = GLX_WINDOW_BIT;
            break;
        case GLX_BUFFER_SIZE:
             *value = 16;
            break;
        case GLX_X_VISUAL_TYPE:
        case GLX_CONFIG_CAVEAT:
        case GLX_SAMPLE_BUFFERS:
        case GLX_SAMPLES:
            *value = 0;
            break;
        case GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB:
            *value = hardext.srgb;
            break;
        default:
            DBG(printf(" => Unknown attrib\n");)
            LOGD("LIBGL: unknown attrib %i\n", attribute);
            *value = 0;
            return 1;
    }
    //DBG(printf(" -> 0x%04X\n", *value);)
    return 0;
}

static void init_display(Display *display) {
    LOAD_EGL(eglGetDisplay);
    
    if (! g_display) {
        g_display = display;//XOpenDisplay(NULL);
    }
    if (globals4es.usefb || globals4es.usepbuffer) {
        eglDisplay = egl_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    } else {
		eglDisplay = egl_eglGetDisplay(display);
    }
}
#endif //NOX11
static void init_vsync() {
#ifdef USE_FBIO
    fbdev = open("/dev/fb0", O_RDONLY);
    if (fbdev < 0) {
        LOGE("LIBGL: Could not open /dev/fb0 for vsync.\n");
    }
#endif
}

static void xrefresh() {
    int dummy = system("xrefresh");
}

#ifdef PANDORA
static void pandora_reset_gamma() {
    if(globals4es.gamma>0.0f)
        system("sudo /usr/pandora/scripts/op_gamma.sh 0");
}
void pandora_set_gamma() {
     {
        char buf[50];
        if(globals4es.gamma>0.0f)
            sprintf(buf, "sudo /usr/pandora/scripts/op_gamma.sh %.2f", globals4es.gamma);
        else
            sprintf(buf, "sudo /usr/pandora/scripts/op_gamma.sh 0");
        int dummy = system(buf);
    }
}
#endif

static void signal_handler(int sig) {
    if (globals4es.xrefresh)
        xrefresh();
#ifdef PANDORA
    pandora_reset_gamma();
#endif

#ifdef BCMHOST
    if (g_bcm_active) {
        g_bcm_active = false;
        bcm_host_deinit();
    }
#endif
#if !defined(ANDROID) && !defined(AMIGAOS4)
    if (globals4es.stacktrace) {
        switch (sig) {
            case SIGBUS:
            case SIGFPE:
            case SIGILL:
            case SIGSEGV: {
                void *array[10];
                size_t size = backtrace(array, 10);
                if (! size) {
                    LOGD("LIBGL: No stacktrace. Compile with -funwind-tables.\n");
                } else {
                    LOGD("LIBGL: Stacktrace: %zd\n", size);
                    backtrace_symbols_fd(array, size, 2);
                }
                break;
            }
        }
    }
#endif
    signal(sig, SIG_DFL);
    raise(sig);
}
#ifdef PANDORA
static void init_liveinfo() {
    static const char socket_name[] = "\0liveinfo";
    sock = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (sock == -1) {
        // no socket, so LiveInfo probably not active
        return;
    }

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    memcpy(sun.sun_path, socket_name, sizeof(socket_name));
    // send a test string
    const char test_string[] = "gl: fpsinfo";
    if (sendto(sock, test_string, strlen(test_string), 0,(struct sockaddr *)&sun, sizeof(sun))<0) {
        // error, so probably not present
        close(sock);
        sock=-1;
    } else
        fcntl(sock, F_SETFL, O_NONBLOCK);
}

#endif

void glx_init() {
    // init map_drawable
    int ret;
    MapDrawable = kh_init(mapdrawable);
    kh_put(mapdrawable, MapDrawable, 1, &ret);
    kh_del(mapdrawable, MapDrawable, 1);
#ifndef AMIGAOS4
    // if ok, grab the init/deinit functions
    if (bcm_host) {
        bcm_host_init = dlsym(bcm_host, "bcm_host_init");
        bcm_host_deinit = dlsym(bcm_host, "bcm_host_deinit");
        if (bcm_host_init && bcm_host_deinit) {
            g_bcmhost = true;
#ifndef ANDROID
            rpi_init();
#endif
        }
    }
#endif
    if (globals4es.xrefresh || globals4es.stacktrace) 
    {
        // TODO: a bit gross. Maybe look at this: http://stackoverflow.com/a/13290134/293352
        signal(SIGBUS, signal_handler);
        signal(SIGFPE, signal_handler);
        //signal(SIGILL, signal_handler);
        signal(SIGSEGV, signal_handler);
        if (globals4es.xrefresh) {
            signal(SIGINT, signal_handler);
            signal(SIGQUIT, signal_handler);
            signal(SIGTERM, signal_handler);
        }
        if (globals4es.xrefresh)
            atexit(xrefresh);
#if !defined(ANDROID) && !defined(AMIGAOS4)
#endif //!ANDROID && !AMIGAOS4
    }
#ifdef PANDORA
    atexit(pandora_reset_gamma);
#elif defined(BCMHOST)
    atexit(bcm_host_deinit);
#elif defined(AMIGAOS4)
    atexit(os4CloseLib);
#endif
    //V-Sync
    if (globals4es.vsync)
        init_vsync();
#ifdef PANDORA

    init_liveinfo();
    if (sock>-1) {
        SHUT(LOGD("LIBGL: LiveInfo detected, fps will be shown\n"));
    }
#endif
}

#ifndef NOX11
static XVisualInfo *latest_visual = NULL;
static GLXFBConfig latest_glxfbconfig = NULL;

GLXContext gl4es_glXCreateContext(Display *display,
                            XVisualInfo *visual,
                            GLXContext shareList,
                            Bool isDirect) {
    DBG(printf("glXCreateContext(%p, %p, %p, %i), latest_visual=%p, fbcontext_count=%d", display, visual, shareList, isDirect, latest_visual, fbcontext_count);)

    static struct __GLXFBConfigRec default_glxfbconfig;
    GLXFBConfig glxfbconfig;

    if(visual==latest_visual)
        glxfbconfig = latest_glxfbconfig;
    else {
        glxfbconfig = &default_glxfbconfig;
        memset(glxfbconfig, 0, sizeof(struct __GLXFBConfigRec));
        default_glxfbconfig.redBits = (visual==0)?0:(visual->depth==16)?5:8;
        default_glxfbconfig.greenBits = (visual==0)?0:(visual->depth==16)?6:8;
        default_glxfbconfig.blueBits = (visual==0)?0:(visual->depth==16)?5:8;
        default_glxfbconfig.alphaBits = (visual==0)?0:(visual->depth!=32)?0:8;
        default_glxfbconfig.depthBits = 16;
        default_glxfbconfig.stencilBits = 8;
        default_glxfbconfig.doubleBufferMode = 1;
    }
    int depthBits = glxfbconfig->depthBits;
#ifdef PANDORA
    if(depthBits==32)
        depthBits = (glxfbconfig->stencilBits==8 && hardext.esversion==2)?24:16;
    if(depthBits==24 && glxfbconfig->stencilBits==8 && !(globals4es.usefbo || globals4es.usepbuffer || hardext.esversion==2))
        depthBits = 16;
    else if(depthBits==16 && glxfbconfig->stencilBits==8 && hardext.esversion==2)
        depthBits = 24;
#endif    

    DBG(printf("Creating R:%d G:%d B:%d A:%d Depth:%d Stencil:%d Multisample:%d/%d\n", glxfbconfig->redBits, glxfbconfig->greenBits, glxfbconfig->blueBits, glxfbconfig->alphaBits, depthBits, glxfbconfig->stencilBits, glxfbconfig->nMultiSampleBuffers, glxfbconfig->multiSampleSize);)
    EGLint configAttribs[] = {
#ifdef PANDORA
        EGL_RED_SIZE, 5,
        EGL_GREEN_SIZE, 6,
        EGL_BLUE_SIZE, 5,
#else
        EGL_RED_SIZE, glxfbconfig->redBits,
        EGL_GREEN_SIZE, glxfbconfig->greenBits,
        EGL_BLUE_SIZE, glxfbconfig->blueBits,
        EGL_ALPHA_SIZE, (hardext.eglnoalpha)?0:glxfbconfig->alphaBits,
#endif
        EGL_DEPTH_SIZE, depthBits,
        EGL_RENDERABLE_TYPE, (hardext.esversion==1)?EGL_OPENGL_ES_BIT:EGL_OPENGL_ES2_BIT,
        //EGL_BUFFER_SIZE, depthBits,
        EGL_STENCIL_SIZE, glxfbconfig->stencilBits,

        EGL_SAMPLE_BUFFERS, glxfbconfig->nMultiSampleBuffers,
        EGL_SAMPLES, glxfbconfig->multiSampleSize,

        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_NONE
    };
    if (globals4es.usefb)
        ++fbcontext_count;

#ifdef BCMHOST
    if (! g_bcm_active) {
        g_bcm_active = true;
        bcm_host_init();
    }
#endif

    LOAD_EGL(eglMakeCurrent);
    LOAD_EGL(eglDestroyContext);
    LOAD_EGL(eglDestroySurface);
    LOAD_EGL(eglBindAPI);
    LOAD_EGL(eglInitialize);
    LOAD_EGL(eglCreateContext);
    LOAD_EGL(eglChooseConfig);
    LOAD_EGL(eglQueryString);
    
    GLXContext fake = malloc(sizeof(struct __GLXContextRec));
	memset(fake, 0, sizeof(struct __GLXContextRec));

    // make an egl context here...
    EGLBoolean result;
    if (eglDisplay == NULL || eglDisplay == EGL_NO_DISPLAY) {
        init_display(display);
        if (eglDisplay == EGL_NO_DISPLAY) {
            DBG(printf(" => %p\n", 0);)
            CheckEGLErrors();
            LOGE("LIBGL: Unable to create EGL display.\n");
            free(fake);
            return 0;
        }
    }

    // first time?
    if (eglInitialized == false) {
        egl_eglBindAPI(EGL_OPENGL_ES_API);
        result = egl_eglInitialize(eglDisplay, NULL, NULL);
        if (result != EGL_TRUE) {
            DBG(printf(" => %p\n", 0);)
            CheckEGLErrors();
            LOGE("LIBGL: Unable to initialize EGL display.\n");
            free(fake);
            return 0;
        }
        eglInitialized = true;
    }

    int configsFound;
	result = egl_eglChooseConfig(eglDisplay, configAttribs, fake->eglConfigs, 1, &configsFound);

    CheckEGLErrors();
    if (result != EGL_TRUE || configsFound == 0) {
        DBG(printf(" => %p\n", 0);)
        LOGE("LIBGL: No EGL configs found (depth=%d, stencil=%d).\n", depthBits, glxfbconfig->stencilBits);
        free(fake);
        return 0;
    }
    EGLContext shared = (shareList)?shareList->eglContext:EGL_NO_CONTEXT;
	fake->eglContext = egl_eglCreateContext(eglDisplay, fake->eglConfigs[0], shared, (hardext.esversion==1)?egl_context_attrib:egl_context_attrib_es2);

    CheckEGLErrors();

    // need to return a glx context pointing at it
    fake->display = display;
    fake->direct = true;
    fake->xid = 1;  //TODO: Proper handling of that id...
    fake->contextType = 0;  //Window
    fake->doublebuff = glxfbconfig->doubleBufferMode;
#ifdef PANDORA
    fake->rbits = 5; fake->gbits=6; fake->bbits=5; fake->abits=0;
#else
    fake->rbits = (visual==0)?8:(visual->depth==16)?5:8,
    fake->gbits= (visual==0)?8:(visual->depth==16)?6:8,
    fake->bbits= (visual==0)?8:(visual->depth==16)?5:8,
    fake->abits= (visual==0)?8:(visual->depth!=32)?0:8,
#endif
    fake->samples = 0; fake->samplebuffers = 0;
    fake->shared = (shareList)?shareList->glstate:NULL;

    DBG(printf(" => %p\n", fake);)
    return fake;
}

GLXContext createPBufferContext(Display *display, GLXContext shareList, GLXFBConfig config) {

    EGLint configAttribs[] = {
        EGL_RED_SIZE, (config)?config->redBits:0,
        EGL_GREEN_SIZE, (config)?config->greenBits:0,
        EGL_BLUE_SIZE, (config)?config->blueBits:0,
        EGL_ALPHA_SIZE, (hardext.eglnoalpha)?0:((config)?config->alphaBits:0),
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, (hardext.esversion==1)?EGL_OPENGL_ES_BIT:EGL_OPENGL_ES2_BIT,
        EGL_SAMPLE_BUFFERS, (config)?config->nMultiSampleBuffers:0,
        EGL_SAMPLES, (config)?config->multiSampleSize:0,
        EGL_NONE
    };

    LOAD_EGL(eglBindAPI);
    LOAD_EGL(eglInitialize);
    LOAD_EGL(eglChooseConfig);
    LOAD_EGL(eglCreateContext);
    LOAD_EGL(eglGetConfigAttrib);

    // Check that the config is for PBuffer
    if(config->drawableType&GLX_PBUFFER_BIT!=GLX_PBUFFER_BIT)
        return 0;

    // Init what need to be done
    EGLBoolean result;
    if (eglDisplay == NULL || eglDisplay == EGL_NO_DISPLAY) {
        init_display(display);

        if (eglDisplay == EGL_NO_DISPLAY) {
            LOGE("LIBGL: Unable to create EGL display.\n");
            return 0;
        }
    }

    // first time?
    if (eglInitialized == false) {
        egl_eglBindAPI(EGL_OPENGL_ES_API);
        result = egl_eglInitialize(eglDisplay, NULL, NULL);
        if (result != EGL_TRUE) {
            CheckEGLErrors();
            LOGE("LIBGL: Unable to initialize EGL display.\n");
            return 0;
        }
        eglInitialized = true;
    }

	// select a configuration
    int configsFound;
    static EGLConfig pbufConfigs[1];
    result = egl_eglChooseConfig(eglDisplay, configAttribs, pbufConfigs, 1, &configsFound);

    CheckEGLErrors();
    if (result != EGL_TRUE || configsFound == 0) {
        LOGE("LIBGL: No EGL configs found.\n");
        return 0;
    }

    EGLContext shared = (shareList)?shareList->eglContext:EGL_NO_CONTEXT;
    
    GLXContext fake = malloc(sizeof(struct __GLXContextRec));
	memset(fake, 0, sizeof(struct __GLXContextRec));
    fake->es2only = globales2;
    fake->shared = (shareList)?shareList->glstate:NULL;
    fake->eglConfigs[0] = pbufConfigs[0];

	fake->eglContext = egl_eglCreateContext(eglDisplay, fake->eglConfigs[0], shared, (hardext.esversion==1)?egl_context_attrib:egl_context_attrib_es2);

    CheckEGLErrors();

    // need to return a glx context pointing at it
    fake->display = display;
    fake->direct = true;
    fake->xid = 1;  //TODO: Proper handling of that id...
    fake->contextType = 1;  //PBuffer
    egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_RED_SIZE, &fake->rbits);
    egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_GREEN_SIZE, &fake->gbits);
    egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_BLUE_SIZE, &fake->bbits);
    egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_ALPHA_SIZE, &fake->abits);
    egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_DEPTH_SIZE, &fake->depth);
    egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_STENCIL_SIZE, &fake->stencil);
    egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_SAMPLES, &fake->samples);
    egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_SAMPLE_BUFFERS, &fake->samplebuffers);

    DBG(printf(" => return PBufferContext %p (context->shared=%p)\n", fake, fake->shared);)
    return fake;
}

GLXContext gl4es_glXCreateContextAttribsARB(Display *display, GLXFBConfig config,
                                      GLXContext share_context, Bool direct,
                                      const int *attrib_list) {
    DBG(printf("glXCreateContextAttribsARB(%p, %p, %p, %d) ", display, config, share_context, direct);if(config)printf("config is RGBA:%d%d%d%d, depth=%d, stencil=%d, drawable=%d\n", config->redBits, config->greenBits, config->blueBits, config->alphaBits, config->depthBits, config->stencilBits, config->drawableType); else printf("\n");)
    if(config && config->drawableType==GLX_PBUFFER_BIT) {
        return createPBufferContext(display, share_context, config);
    } else {
        EGLint configAttribs[] = {
#ifdef PANDORA
            EGL_RED_SIZE, (config->drawableType)==GLX_PIXMAP_BIT?config->redBits:5,
            EGL_GREEN_SIZE, (config->drawableType)==GLX_PIXMAP_BIT?config->greenBits:6,
            EGL_BLUE_SIZE, (config->drawableType)==GLX_PIXMAP_BIT?config->blueBits:5,
            EGL_ALPHA_SIZE, (config->drawableType)==GLX_PIXMAP_BIT?config->alphaBits:0,
#else
            EGL_RED_SIZE, config->redBits,
            EGL_GREEN_SIZE, config->greenBits,
            EGL_BLUE_SIZE, config->blueBits,
            EGL_ALPHA_SIZE, (hardext.eglnoalpha)?0:config->alphaBits,
#endif
            EGL_DEPTH_SIZE, config->depthBits,
            EGL_STENCIL_SIZE, config->stencilBits,
            EGL_SAMPLES, config->multiSampleSize,
            EGL_SAMPLE_BUFFERS, config->nMultiSampleBuffers,
            EGL_RENDERABLE_TYPE, (hardext.esversion==1)?EGL_OPENGL_ES_BIT:EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, (config->drawableType)==GLX_PIXMAP_BIT?EGL_PIXMAP_BIT:(EGL_WINDOW_BIT | EGL_PBUFFER_BIT),
            EGL_NONE
        };
        if (globals4es.usefb)
            ++fbcontext_count;
#ifdef BCMHOST
        if (! g_bcm_active) {
            g_bcm_active = true;
            bcm_host_init();
        }
#endif

        LOAD_EGL(eglMakeCurrent);
        LOAD_EGL(eglDestroyContext);
        LOAD_EGL(eglDestroySurface);
        LOAD_EGL(eglBindAPI);
        LOAD_EGL(eglInitialize);
        LOAD_EGL(eglCreateContext);
        LOAD_EGL(eglChooseConfig);
        LOAD_EGL(eglQueryString);
        LOAD_EGL(eglGetConfigAttrib);

        GLXContext fake = malloc(sizeof(struct __GLXContextRec));
        memset(fake, 0, sizeof(struct __GLXContextRec));
        fake->es2only = globales2;

        fake->shared = (share_context)?share_context->glstate:NULL;

        // make an egl context here...
        EGLBoolean result;
        if (eglDisplay == NULL || eglDisplay == EGL_NO_DISPLAY) {
            init_display(display);
            if (eglDisplay == EGL_NO_DISPLAY) {
                LOGE("LIBGL: Unable to create EGL display.\n");
                return fake;
            }
        }

        // first time?
        if (eglInitialized == false) {
            egl_eglBindAPI(EGL_OPENGL_ES_API);
            result = egl_eglInitialize(eglDisplay, NULL, NULL);
            if (result != EGL_TRUE) {
                CheckEGLErrors();
                LOGE("LIBGL: Unable to initialize EGL display.\n");
                return fake;
            }
            eglInitialized = true;
        }

        int configsFound;
        result = egl_eglChooseConfig(eglDisplay, configAttribs, fake->eglConfigs, 1, &configsFound);

        CheckEGLErrors();
        if (result != EGL_TRUE || configsFound == 0) {
            LOGE("LIBGL: No EGL configs found.\n");
            return fake;
        }
        EGLContext shared = (share_context)?share_context->eglContext:EGL_NO_CONTEXT;
        fake->eglContext = egl_eglCreateContext(eglDisplay, fake->eglConfigs[0], shared, (hardext.esversion==1)?egl_context_attrib:egl_context_attrib_es2);

        CheckEGLErrors();

        // need to return a glx context pointing at it
        fake->display = display;
        fake->direct = true;
        fake->xid = 1;  //TODO: Proper handling of that id...
        fake->contextType = (config->drawableType)==GLX_PIXMAP_BIT?2:0;  //Pixmap:Window
        fake->doublebuff = config->doubleBufferMode;

        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_RED_SIZE, &fake->rbits);
        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_GREEN_SIZE, &fake->gbits);
        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_BLUE_SIZE, &fake->bbits);
        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_ALPHA_SIZE, &fake->abits);
        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_DEPTH_SIZE, &fake->depth);
        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_STENCIL_SIZE, &fake->stencil);
        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_SAMPLES, &fake->samples);
        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_SAMPLE_BUFFERS, &fake->samplebuffers);
        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_MIN_SWAP_INTERVAL, &minswap);
        egl_eglGetConfigAttrib(eglDisplay, fake->eglConfigs[0], EGL_MAX_SWAP_INTERVAL, &maxswap);
        DBG(printf(" => return %p (context->shared=%p)\n", fake, fake->shared);)
        return fake;
    }
}

void gl4es_glXDestroyContext(Display *display, GLXContext ctx) {
    DBG(printf("glXDestroyContext(%p, %p), fbcontext_count=%d, ctx_type=%d\n", display, ctx, fbcontext_count, (ctx)?ctx->contextType:0);)
    if(glxContext==ctx)
        glxContext = NULL;
    if(globals4es.usefb)
        --fbcontext_count;
    if (ctx->eglContext) {
        if (globals4es.usefbo && ctx->contextType==0) {
            deleteMainFBO(ctx->glstate);
        }

        DeleteGLState(ctx->glstate);
        
        LOAD_EGL(eglDestroyContext);
        LOAD_EGL(eglDestroySurface);
        
		EGLBoolean result = egl_eglDestroyContext(eglDisplay, ctx->eglContext);
        ctx->eglContext = 0;
        if (ctx->eglSurface != 0) {
            if(globals4es.usefb!=1 /*|| !fbcontext_count*/) { // this ressource leak is left on purpose (Pandora driver doesn't seems to like to many Creation of the surface)
                egl_eglDestroySurface(eglDisplay, ctx->eglSurface);
                eglSurface = 0;
            }
			ctx->eglSurface = 0;
        }

        if (result != EGL_TRUE) {
            CheckEGLErrors();
            LOGE("LIBGL: Failed to destroy EGL context.\n");
        }
        /*if (fbdev >= 0) {
            close(fbdev);
            fbdev = -1;
        }*/
    }
        
    free(ctx);
    return;
}

Display *gl4es_glXGetCurrentDisplay() {
    DBG(printf("glXGetCurrentDisplay()\n");)
    if (g_display && eglContext) {
        return g_display;
    }

    return XOpenDisplay(NULL);
}

XVisualInfo *gl4es_glXChooseVisual(Display *display,
                             int screen,
                             int *attributes) {
    DBG(printf("glXChooseVisual(%p, %d, %p)\n", display, screen, attributes);)
    // apparently can't trust the Display I'm passed?
/*
    if (g_display == NULL) {
        g_display = XOpenDisplay(NULL);
    }
*/
    glx_default_depth = XDefaultDepth(display, screen);
    if (glx_default_depth != 16 && glx_default_depth != 24  && glx_default_depth != 32)
        LOGD("LIBGL: unusual desktop color depth %d\n", glx_default_depth);

    XVisualInfo *visual = (XVisualInfo *)malloc(sizeof(XVisualInfo));
    if (!XMatchVisualInfo(display, screen, glx_default_depth, TrueColor, visual)) {
        LOGE("LIBGL: XMatchVisualInfo failed in glXChooseVisual\n");
        return NULL;
    }

    // create and store the glxConfig that goes with thoses attributes
    latest_visual = visual;
    int count = 1;
    latest_glxfbconfig = gl4es_glXChooseFBConfig(display, screen, attributes, &count)[0];

    return visual;
}

/*
EGL_BAD_MATCH is generated if draw or read are not compatible with context
or if context is set to EGL_NO_CONTEXT and draw or read are not set to
EGL_NO_SURFACE, or if draw or read are set to EGL_NO_SURFACE and context is
not set to EGL_NO_CONTEXT.
*/

Bool gl4es_glXMakeCurrent(Display *display,
                    GLXDrawable drawable,
                    GLXContext context) {
    DBG(printf("glXMakeCurrent(%p, %p, %p), isPBuffer(drawable)=%d, context->drawable=%p, context->eglSurface=%p(%p), context->doublebuff=%d\n", display, drawable, context, isPBuffer(drawable), context?context->drawable:0, context?context->eglSurface:0, eglSurface, context?context->doublebuff:0);)
    LOAD_EGL(eglMakeCurrent);
    LOAD_EGL(eglDestroySurface);
    LOAD_EGL(eglCreateWindowSurface);
    LOAD_EGL(eglQuerySurface);
#ifdef NOX11
    int created = 0;
#else
    int created = (context)?isPBuffer(drawable):0;
#endif
    EGLContext eglContext = EGL_NO_CONTEXT;
    EGLSurface eglSurf = 0;
    EGLConfig eglConfig = 0;
    // flush current context if exist...
    if(glxContext && glxContext->glstate) {
        /*if(!context && !glxContext->doublebuff && !glxContext->contextType) {
            gl4es_glXSwapBuffers(display, glxContext->drawable);
        } else*/
            gl4es_glFlush();
    }
    if(context && glxContext==context && context->drawable==drawable) {
        DBG(printf(" => True\n");)
        //same context, all is done bye
        DBG(printf("Same context and drawable, doing nothing\n");)
        return true;
    }
    if(globals4es.fbomakecurrent && glxContext && glxContext->glstate) {
       current_fb = gl4es_getCurrentFBO();
       if(current_fb) {
            if(hardext.vendor&VEND_ARM)
                gl4es_glFinish(); //MALI seems to need a flush commandbefore unbinding the Framebuffer here
            LOAD_GLES2_OR_OES(glBindFramebuffer);
            gles_glBindFramebuffer(GL_FRAMEBUFFER, 0);
       }
    }
    if(context) {
        eglContext = context->eglContext;
        if(context->drawable==drawable && context->eglSurface) {
            // same-same, recycling...
            eglSurf = context->eglSurface;
        } else {
            // new one
            if(created) {
#ifndef NOX11
                eglSurf = context->eglSurface = pbuffersize[created-1].Surface; //(EGLSurface)drawable;
                eglContext = context->eglContext = pbuffersize[created-1].Context;    // this context is ok for the PBuffer
                eglConfig = context->eglConfigs[0] = pbuffersize[created-1].Config;
                /*if (context->contextType != pbuffersize[created-1].Type) {    // Context / buffer not aligned, create a new glstate tracker
                    if(context->glstate)
                        DeleteGLState(context->glstate);
                    context->glstate = NULL;
                    context->shared = NULL;
                }*/
#endif
            } else {
                EGLint attrib_list[5];
                int cnt=0;
                if(!context->doublebuff) {
                    attrib_list[cnt++] = EGL_RENDER_BUFFER;
                    attrib_list[cnt++] = EGL_SINGLE_BUFFER;
                }
                if(globals4es.glx_surface_srgb){
                    attrib_list[cnt++] = EGL_GL_COLORSPACE_KHR;
                    attrib_list[cnt++] = EGL_GL_COLORSPACE_SRGB_KHR;
                }
                attrib_list[cnt++] = EGL_NONE;

                unsigned int width = 0, height = 0, depth = 0;
#ifndef NOX11
                if(globals4es.usepbuffer || (globals4es.usefb && bcm_host)) {
                    // Get Window size and all...
                    unsigned int border;
                    Window root;
                    int x, y;
                    XGetGeometry(display, drawable, &root, &x, &y, &width, &height, &border, &depth);
                    DBG(printf("XGetGeometry gives %dx%d for drawable %p\n", width, height, drawable);)
                }
                if(globals4es.usepbuffer) {
                    //let's create a PBuffer attributes
                    EGLint egl_attribs[10];	// should be enough
                    int i = 0;
                    egl_attribs[i++] = EGL_WIDTH;
                    egl_attribs[i++] = width;
                    egl_attribs[i++] = EGL_HEIGHT;
                    egl_attribs[i++] = height;
                    egl_attribs[i++] = EGL_NONE;

                    if(createPBuffer(display, egl_attribs, &eglSurf, &eglContext, &eglConfig, (depth>16)?8:5, (depth==15)?5:(depth>16)?8:6, (depth>16)?8:5, (depth==32)?8:0, context->samplebuffers, context->samples)==0) {
                        // fail too, abort
                        SHUT(LOGE("LIBGL: PBuffer creation failed too\n"));
                        return 0;
                    }
                    int Width, Height;

                    egl_eglQuerySurface(eglDisplay,eglSurf,EGL_WIDTH,&Width);
                    egl_eglQuerySurface(eglDisplay,eglSurf,EGL_HEIGHT,&Height);

                    addPixBuffer(display, eglSurf, eglConfig, Width, Height, eglContext, drawable, depth, 2);
                    context->eglSurface = eglSurf;
                    context->eglContext = eglContext;
                    // update, that context is a created emulated one...
                    created = isPBuffer(drawable); 
                } else
#endif
                {
                    if(globals4es.usefb || globals4es.usefbo) {
                        if(eglSurface) // cannot create multiple eglSurface for the same Framebuffer?
                            eglSurf = context->eglSurface = eglSurface;
                        else {
                            eglSurface = eglSurf = context->eglSurface = egl_eglCreateWindowSurface(eglDisplay, context->eglConfigs[0], (EGLNativeWindowType)create_native_window(width,height), attrib_list);
                            if(!eglSurf) {
                                DBG(printf("LIBGL: Warning, EeglSurf is null\n");)
                                CheckEGLErrors();
                            }
                        }
                    } else {
                        if(context->eglSurface)
                            egl_eglDestroySurface(eglDisplay, context->eglSurface);
                        eglSurf = context->eglSurface = egl_eglCreateWindowSurface(eglDisplay, context->eglConfigs[0], drawable, attrib_list);
                        if(!eglSurf) {
                            DBG(printf("LIBGL: Warning, eglSurf is null\n");)
                            CheckEGLErrors();
                        }
                    }
                }
            }
        }
        eglSurf = context->eglSurface;
        eglContext = context->eglContext;
    }
    EGLBoolean result = egl_eglMakeCurrent(eglDisplay, eglSurf, eglSurf, eglContext);
    DBG(printf("LIBGL: eglMakeCurrent(%p, %p, %p, %p)\n", eglDisplay, eglSurf, eglSurf, eglContext);)
    CheckEGLErrors();
    glxContext = context;
    if(!result) {
        // error switching context, don't change glstate and abort...
        DBG(printf(" => False\n");)
        return false;
    }
    // update MapDrawable
    {
        int ret;
        khint_t k = kh_get(mapdrawable, MapDrawable, drawable);
        map_drawable_t* map = NULL;
        if (k == kh_end(MapDrawable)){
            k = kh_put(mapdrawable, MapDrawable, drawable, &ret);
            map = kh_value(MapDrawable, k) = (map_drawable_t*)malloc(sizeof(map_drawable_t));
            map->drawable = drawable;
        } else {
            map = kh_value(MapDrawable, k);
        }
        map->surface = eglSurf;
        map->PBuffer = created;

    }
    
    if (context) {
        if(!context->glstate) {
            context->glstate = NewGLState(context->shared, context->es2only);
            if(created && pbuffersize[created-1].Type >= 3) {
                ((glstate_t*)context->glstate)->emulatedPixmap = created;
                ((glstate_t*)context->glstate)->emulatedWin = pbuffersize[created-1].Type==4?1:0;
            }
        }
        context->drawable = drawable;

        ActivateGLState(context->glstate);
#ifdef PANDORA
        if(!created) pandora_set_gamma();
#endif

        CheckEGLErrors();
        if (result) {
            if (globals4es.usefbo && !created) {
                // get size of the surface...
                egl_eglQuerySurface(eglDisplay,eglSurf,EGL_WIDTH,&g_width);
                egl_eglQuerySurface(eglDisplay,eglSurf,EGL_HEIGHT,&g_height);
#if defined(PANDORA) || defined(CHIP)
                if(getenv("LIBGL_FBO")) {
                    int fbo_width, fbo_height;
                    if(sscanf(getenv("LIBGL_FBO"), "%dx%d", &fbo_width, &fbo_height)==2) {
                        SHUT(printf("LIBGL: Forcing FBO size %dx%d (%dx%d)\n", fbo_width, fbo_height, g_width, g_height);)
                        g_width = fbo_width; 
                        g_height = fbo_height;
                    }
                }
#endif
                // create the main_fbo...
                createMainFBO(g_width, g_height);
            }
            if(globals4es.fbomakecurrent && gl4es_getCurrentFBO())
                gl4es_setCurrentFBO();
             // finished
            DBG(printf(" => True\n");)
            return true;
        }
        DBG(printf(" => False\n");)
        return false;
    }
    DBG(printf(" => True\n");)
    return true;
}

Bool gl4es_glXMakeContextCurrent(Display *display, int drawable,
                           int readable, GLXContext context) {
    DBG(printf("glXMakeContextCurrent(%p, %X, %X, %p)\n", display, drawable, readable, context);)
    return gl4es_glXMakeCurrent(display, drawable, context);
}

void gl4es_glXSwapBuffers(Display *display,
                    int drawable) {
    static int frames = 0;
    DBG(printf("glXSwapBuffers(%p, 0x%X)\n", display, drawable);)
    LOAD_EGL(eglSwapBuffers);
    // TODO: what if active context is not on the drawable?
    realize_textures();
    if (glstate->list.active){
        flush();
    }
    if (glstate->raster.bm_drawing)
        bitmap_flush();
    EGLSurface surface = eglSurface;
    int PBuffer = 0;
    {
        // get MapDrawable surface
        khint_t k = kh_get(mapdrawable, MapDrawable, drawable);
        map_drawable_t* map = NULL;
        if (k != kh_end(MapDrawable)){
            map = kh_value(MapDrawable, k);
            surface = map->surface;
            PBuffer = map->PBuffer;
        }
    }
#ifdef USE_FBIO
    if (globals4es.vsync && fbdev >= 0 && PBuffer==0) {
        // TODO: can I just return if I don't meet vsync over multiple frames?
        // this will just block otherwise.
        int arg = 0;
        for (int i = 0; i < swapinterval; i++) {
            ioctl(fbdev, FBIO_WAITFORVSYNC, &arg);
        }
    }
#endif
    if (globals4es.usefbo && PBuffer==0) {
        unbindMainFBO();
        int x = 0, y = 0;
        unsigned int width = 0, height = 0;
#ifndef NOX11
        {
            unsigned int border, depth;
            Window root;
            XWindowAttributes xwa;
            XGetGeometry(display, drawable, &root, &x, &y, &width, &height, &border, &depth); // get geometry (relative to window)
            XTranslateCoordinates( display, drawable, root, 0, 0, &x, &y, &root ); // translate to get x,y absolute to screen
        }
#endif
        DBG(printf("blitMainFBO(%d, %d, %u, %u)\n", x, y, width, height);)
        blitMainFBO(x, y, width, height);
        // blit the main_fbo before swap
    }
    // check emulated Pixmap
    if(PBuffer && glstate->emulatedPixmap) {
        LOAD_GLES(glFlush);
        gles_glFlush();
        BlitEmulatedPixmap();
    } else
        egl_eglSwapBuffers(eglDisplay, surface);
    //CheckEGLErrors();     // not sure it's a good thing to call a eglGetError() after all eglSwapBuffers, performance wize (plus result is discarded anyway)
#ifdef PANDORA
    if (globals4es.showfps || (sock>-1))
#else
    if (globals4es.showfps) 
#endif
    {
        // framerate counter
        static float avg, fps = 0;
        static int frame1, last_frame, frame, now, current_frames;
        struct timeval out;
        gettimeofday(&out, NULL);
        now = out.tv_sec;
        frame++;
        current_frames++;

        if (frame == 1) {
            frame1 = now;
        } else if (frame1 < now) {
            if (last_frame < now) {
                float change = current_frames / (float)(now - last_frame);
                float weight = 0.7;
                if (! fps) {
                    fps = change;
                } else {
                    fps = (1 - weight) * fps + weight * change;
                }
                current_frames = 0;

                avg = frame / (float)(now - frame1);
                if (globals4es.showfps) LOGD("LIBGL: fps: %.2f, avg: %.2f\n", fps, avg);
#ifdef PANDORA
                if (sock>-1) {
                    char tmp[60];
                    snprintf(tmp, 60, "gl:  %2.2f", fps);
                    sendto(sock, tmp, strlen(tmp), 0,(struct sockaddr *)&sun, sizeof(sun));                    
                }
#endif
            }
        }
        last_frame = now;
    }
    if (globals4es.usefbo && PBuffer==0) {
        bindMainFBO();
    }
}

int gl4es_glXGetConfig(Display *display,
                 XVisualInfo *visual,
                 int attribute,
                 int *value) {
    return get_config_default(display, attribute, value);
}

const char *gl4es_glXQueryExtensionsString(Display *display, int screen) {
    DBG(printf("glXQueryExtensionString(%p, %d)\n", display, screen);)
    const char *extensions = 
        "GLX_ARB_create_context "
        "GLX_ARB_create_context_profile "
        "GLX_ARB_get_proc_address "
        "GLX_ARB_multisample "
        "GLX_SGI_swap_control "
        "GLX_MESA_swap_control "
        "GLX_EXT_swap_control "
        "GLX_EXT_create_context_es2_profile ";
    //TODO: make this string parametrable, to remo ES2 profile if not on ES2 Backend?
    return extensions;
}

const char *gl4es_glXQueryServerString(Display *display, int screen, int name) {
    DBG(printf("glXQueryServerString(%p, %d, %d)\n", display, screen, name);)
    switch (name) {
        case GLX_VENDOR: return "ptitSeb";
        case GLX_VERSION: return "1.4 GL4ES";
        case GLX_EXTENSIONS: return gl4es_glXQueryExtensionsString(display, screen);
    }
    return 0;    
}

Bool gl4es_glXQueryExtension(Display *display, int *errorBase, int *eventBase) {
    DBG(printf("glXQueryExtension(%p, %p, %p)\n", display, errorBase, eventBase);)
    if (errorBase)
        *errorBase = 0;

    if (eventBase)
        *eventBase = 0;

    return true;
}

Bool gl4es_glXQueryVersion(Display *display, int *major, int *minor) {
    DBG(printf("glXQueryVersion(%p, %p, %p)\n", display, major, minor);)
    // TODO: figure out which version we want to pretend to implement
    *major = 1;
    *minor = 4;
    return true;
}

const char *gl4es_glXGetClientString(Display *display, int name) {
    DBG(printf("glXGetClientString(%p, %d)\n", display, name);)
    switch (name) {
        case GLX_VENDOR: return "ptitSeb";
        case GLX_VERSION: return "1.4 GL4ES";
        case GLX_EXTENSIONS: return gl4es_glXQueryExtensionsString(display, 0);
    }
    return 0;    
}

int gl4es_glXQueryContext( Display *dpy, GLXContext ctx, int attribute, int *value ) {
    DBG(printf("glXQueryContext(%p, %p, %d, %p)\n", dpy, ctx, attribute, value);)
	*value=0;
	if (ctx) switch (attribute) {
		case GLX_FBCONFIG_ID: *value=ctx->xid; break;
		case GLX_RENDER_TYPE: *value=GLX_RGBA_TYPE; break;
		case GLX_SCREEN: break;			// screen n# is always 0
	}
    return 0;
}

// stubs for glfw (GLX 1.3)
GLXContext gl4es_glXGetCurrentContext() {
    DBG(printf("glXGetCurrentContext()\n");)

	return glxContext;
}

GLXFBConfig *gl4es_glXChooseFBConfig(Display *display, int screen,
                       const int *attrib_list, int *count) {
    DBG(printf("glXChooseFBConfig(%p, %d, %p, %p)\n", display, screen, attrib_list, count);)
    // this is not really good. A static table of all config should be build, and then a filter done according to attribs...
    static struct __GLXFBConfigRec currentConfig[8];
    static int idx = 0;
    *count = 1;
    GLXFBConfig *configs = (GLXFBConfig *)malloc(sizeof(GLXFBConfig));
    configs[0] = &currentConfig[idx];
    idx=(idx+1)%8;
    memset(configs[0], 0, sizeof(struct __GLXFBConfigRec));
    // fill that config with some of the attrib_list info...
    configs[0]->drawableType = GLX_WINDOW_BIT;
    configs[0]->screen = 0;
    configs[0]->maxPbufferWidth = configs[0]->maxPbufferHeight = 2048;
    configs[0]->redBits = configs[0]->greenBits = configs[0]->blueBits = configs[0]->alphaBits = 0;
    configs[0]->nMultiSampleBuffers = 0; configs[0]->multiSampleSize = 0;
    configs[0]->depthBits = 16; configs[0]->stencilBits = 8;
    //configs[0]->doubleBufferMode = 1; //need to force doublebuff?
    if(attrib_list) {
		int i = 0;
		while(attrib_list[i]!=0) {
			switch(attrib_list[i++]) {
				case GLX_RED_SIZE:
					configs[0]->redBits = attrib_list[i++];
					if(configs[0]->redBits==GLX_DONT_CARE) configs[0]->redBits = 0;
                    if(configs[0]->redBits>0 && configs[0]->redBits<5) configs[0]->redBits = 5;
                    DBG(printf("FBConfig redBits=%d\n", configs[0]->redBits);)
					break;
				case GLX_GREEN_SIZE:
					configs[0]->greenBits = attrib_list[i++];
					if(configs[0]->greenBits==GLX_DONT_CARE) configs[0]->greenBits = 0;
                    if(configs[0]->greenBits>0 && configs[0]->greenBits<5) configs[0]->greenBits = 6;
                    DBG(printf("FBConfig greenBits=%d\n", configs[0]->greenBits);)
					break;
				case GLX_BLUE_SIZE:
					configs[0]->blueBits = attrib_list[i++];
					if(configs[0]->blueBits==GLX_DONT_CARE) configs[0]->blueBits = 0;
                    if(configs[0]->blueBits>0 && configs[0]->blueBits<5) configs[0]->blueBits = 5;
                    DBG(printf("FBConfig blueBits=%d\n", configs[0]->blueBits);)
					break;
				case GLX_ALPHA_SIZE:
					configs[0]->alphaBits = attrib_list[i++];
					if(configs[0]->alphaBits==GLX_DONT_CARE) configs[0]->alphaBits = 0;
                    DBG(printf("FBConfig alphaBits=%d\n", configs[0]->alphaBits);)
					break;
                case GLX_DEPTH_SIZE:
					configs[0]->depthBits = attrib_list[i++];
					if(configs[0]->depthBits==GLX_DONT_CARE) configs[0]->depthBits = 0;
                    DBG(printf("FBConfig depthBits=%d\n", configs[0]->depthBits);)
					break;
                case GLX_STENCIL_SIZE:
					configs[0]->stencilBits = attrib_list[i++];
					if(configs[0]->stencilBits==GLX_DONT_CARE) configs[0]->stencilBits = 0;
                    DBG(printf("FBConfig stencilBits=%d\n", configs[0]->stencilBits);)
					break;
                case GLX_DRAWABLE_TYPE:
                    configs[0]->drawableType = attrib_list[i++];
                    DBG(printf("FBConfig drawableType=%d\n", configs[0]->drawableType);)
                    break;
                case GLX_SAMPLE_BUFFERS:
                    configs[0]->nMultiSampleBuffers = attrib_list[i++];
                    DBG(printf("FBConfig multisampleBuffers=%d\n", configs[0]->nMultiSampleBuffers);)
                    break;
                case GLX_SAMPLES:
                    configs[0]->multiSampleSize = attrib_list[i++];
                    DBG(printf("FBConfig multiSampleSize=%d\n", configs[0]->multiSampleSize);)
                    break;
                case GLX_DOUBLEBUFFER: // not a single value
                    configs[0]->doubleBufferMode = attrib_list[i++];
                    DBG(printf("FBConfig doubleBufferMode=%d\n", configs[0]->doubleBufferMode);)
                    break;
                case GLX_RGBA: // single value
                    configs[0]->rgbMode = 1;
                    DBG(printf("FBConfig rgba=%d\n", configs[0]->rgbMode);)
                    break;
                case GLX_X_RENDERABLE:
                    ++i; //value ignored
                    DBG(printf("FBConfig renderable=%d\n", attrib_list[i-1]);)
                    break;
                default:
                    ++i;
				// discard other stuffs
			}
		}
	}
		
    return configs;
}

GLXFBConfig *gl4es_glXGetFBConfigs(Display *display, int screen, int *count) {
    DBG(printf("glXGetFBConfigs(%p, %d, %p)\n", display, screen, count);)
    // this is wrong! The config table should be a static one built according to EGL Config capabilities...
    *count = 1;
    // this is to only do 1 malloc instead of 1 for the array and one for the element...
    GLXFBConfig *configs = (GLXFBConfig *)malloc(sizeof(GLXFBConfig) + sizeof(struct __GLXFBConfigRec));
    configs[0] = (GLXFBConfig)((char*)(&configs[0])+sizeof(GLXFBConfig));
    memset(configs[0], 0, sizeof(struct __GLXFBConfigRec));
    configs[0]->drawableType = GLX_WINDOW_BIT | GLX_PBUFFER_BIT;
    configs[0]->redBits = configs[0]->greenBits = configs[0]->blueBits = configs[0]->alphaBits = 8; 
    configs[0]->depthBits = 24; configs[0]->stencilBits = 8;
    configs[0]->multiSampleSize = 0; configs[0]->nMultiSampleBuffers = 0;
    return configs;
}

int gl4es_glXGetFBConfigAttrib(Display *display, GLXFBConfig config, int attribute, int *value) {
    DBG(printf("glXGetFBConfigAttrib(%p, %p, 0x%04X, %p)", display, config, attribute, value);)
    if(!config) {
        return get_config_default(display, attribute, value);
    }

    switch (attribute) {
        case GLX_RGBA:
            *value = config->alphaBits>0?1:0;
        case GLX_RED_SIZE:
            *value = config->redBits;
            break;
        case GLX_GREEN_SIZE:
            *value = config->greenBits;
            break;
        case GLX_BLUE_SIZE:
            *value = config->blueBits;
            break;
        case GLX_ALPHA_SIZE:
            *value = config->alphaBits;
            break;
        case GLX_DEPTH_SIZE:
            *value = config->depthBits;
            break;
        case GLX_STENCIL_SIZE:
            *value = config->stencilBits;
            break;
        case GLX_ACCUM_RED_SIZE:
        case GLX_ACCUM_GREEN_SIZE:
        case GLX_ACCUM_BLUE_SIZE:
        case GLX_ACCUM_ALPHA_SIZE:
            *value = 0;
            break;
        case GLX_TRANSPARENT_TYPE:
            *value = GLX_NONE;
            break;
        case GLX_RENDER_TYPE:
            *value = GLX_RGBA_BIT;
            break;
        case GLX_VISUAL_ID:
            *value = gl4es_glXChooseVisual(display, 0, NULL)->visualid; //config->associatedVisualId;
            //*value = 1;
            break;
        case GLX_FBCONFIG_ID:
            *value = 1;
            break;
        case GLX_DRAWABLE_TYPE:
            *value = GLX_WINDOW_BIT; //config->drawableType;
            break;
        case GLX_X_VISUAL_TYPE:
        case GLX_CONFIG_CAVEAT:
            *value = 0;
            break;
        case GLX_SAMPLE_BUFFERS:
            *value = config->nMultiSampleBuffers;
            break;
        case GLX_SAMPLES:
            *value = config->multiSampleSize;
            break;
        case GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB:
            *value = hardext.srgb;
            break;
        case GLX_DOUBLEBUFFER:
            *value = config->doubleBufferMode;
            break;
        default:
            return get_config_default(display, attribute, value);
   }
   DBG(printf(" => 0x%04X\n", *value);)
   return Success;
}

XVisualInfo *gl4es_glXGetVisualFromFBConfig(Display *display, GLXFBConfig config) {
    DBG(printf("glXGetVisualFromFBConfig(%p, %p)\n", display, config);)
    /*if (g_display == NULL) {
        g_display = XOpenDisplay(NULL);
    }*/
    if (glx_default_depth==0)
        glx_default_depth = XDefaultDepth(display, 0);
    XVisualInfo *visual = (XVisualInfo *)malloc(sizeof(XVisualInfo));
    XMatchVisualInfo(display, 0, glx_default_depth, TrueColor, visual);
    return visual;
}

GLXContext gl4es_glXCreateNewContext(Display *display, GLXFBConfig config,
                               int render_type, GLXContext share_list,
                               Bool is_direct) {
    DBG(printf("glXCreateNewContext(%p, %p, %d, %p, %i), drawableType=0x%02X\n", display, config, render_type, share_list, is_direct, (config)?config->drawableType:0);)
    if(render_type!=GLX_RGBA_TYPE)
        return 0;
    if(config && config->drawableType==GLX_PBUFFER_BIT) {
        return createPBufferContext(display, share_list, config);
    } else
        return gl4es_glXCreateContextAttribsARB(display, config, share_list, is_direct, NULL);
        //return glXCreateContext(display, 0, share_list, is_direct);
}
#endif //NOX11

void gl4es_glXSwapInterval(int interval) {
    DBG(printf("glXSwapInterval(%i)\n", interval);)
#ifdef NOEGL
    // nothing
#elif defined(NOX11)
    LOAD_EGL(eglSwapInterval);
    egl_eglSwapInterval(eglDisplay, swapinterval);
#elif defined(USE_FBIO)
    if (! globals4es.vsync)
        LOGD("LIBGL: Enable LIBGL_VSYNC=1 if you want to use vsync.\n");
    swapinterval = interval;
#else
    if(glxContext) {
        LOAD_EGL(eglSwapInterval);
        egl_eglSwapInterval(eglDisplay, swapinterval);
        CheckEGLErrors();
        if(interval<minswap || interval>maxswap) {
            SHUT(printf("LIBGL: Warning, Swap Interval %d is out of possible values %d, %d\n", interval, minswap, maxswap);)
        } else
            swapinterval = interval;
    } else {
        DBG(printf("LIBGL: glXSwapInterval called before Context is current.\n");)
        swapinterval = interval;
    }
#endif
}

#ifndef NOX11
void gl4es_glXSwapIntervalEXT(Display *display, int drawable, int interval) {
    gl4es_glXSwapInterval(interval);
}

// misc stubs
void gl4es_glXCopyContext(Display *display, GLXContext src, GLXContext dst, GLuint mask) {
    DBG(printf("glXCopyContext(%p, %p, %p, %04X)\n", display, src, dst, mask);)
	// mask is ignored for now, but should include glPushAttrib / glPopAttrib
	memcpy(dst, src, sizeof(struct __GLXContextRec));
}

Window gl4es_glXCreateWindow(Display *display, GLXFBConfig config, Window win, int *attrib_list) {
    // should return GLXWindo
    DBG(printf("glXCreateWindow(%p, %p, %d, %p)\n", display, config, win, attrib_list);)
    return win;
}
void gl4es_glXDestroyWindow(Display *display, void *win) {
    // really wants a GLXWindow
    DBG(printf("glXDestroyWindow(%p, %p)\n", display, win);)

} 

GLXDrawable gl4es_glXGetCurrentDrawable() {
    DBG(printf("glXGetCurrentDrawable()\n");)
	if (glxContext) 
		return glxContext->drawable; 
	else 
		return 0;
} // this should actually return GLXDrawable.

Bool gl4es_glXIsDirect(Display * display, GLXContext ctx) {
    DBG(printf("glXIsDirect(%p, %p)\n", display, ctx);)
    return true;
}

void gl4es_glXUseXFont(Font font, int first, int count, int listBase) {
    DBG(printf("glXUseXFont(%p, %d, %d, %d)\n", font, first, count, listBase);)
	/* Mostly from MesaGL-9.0.1 
	 * 
	 */
	// First get current Display and Window
	XFontStruct *fs;
	unsigned int max_width, max_height, max_bm_width, max_bm_height;
    Pixmap pixmap;
    XGCValues values;
    GC gc;
    int i;
    unsigned long valuemask;
	GLubyte *bm;
	Display *dpy;
	Window win;
    if (globals4es.usefb) {
        dpy = g_display;
        win = RootWindow(dpy, XDefaultScreen(dpy));
    } else {
        dpy = glxContext->display;
        win = glxContext->drawable;		//TODO, check that drawable is a window and not a pixmap ?
    }

	// Grab font params
	fs = XQueryFont(dpy, font);
    if (!fs) {
      LOGE("LIBGL: error, no font set before call to glXUseFont\n");
      return;
    }
	max_width = fs->max_bounds.rbearing - fs->min_bounds.lbearing;
    max_height = fs->max_bounds.ascent + fs->max_bounds.descent;
    max_bm_width = (max_width + 7) / 8;
    max_bm_height = max_height;

    bm = (GLubyte *)malloc((max_bm_width * max_bm_height) * sizeof(GLubyte));
    if (!bm) {
       XFreeFontInfo(NULL, fs, 1);
       return;
    }
    // Save GL texture parameters
    GLint swapbytes, lsbfirst, rowlength;
    GLint skiprows, skippixels, alignment;
    gl4es_glGetIntegerv(GL_UNPACK_SWAP_BYTES, &swapbytes);
    gl4es_glGetIntegerv(GL_UNPACK_LSB_FIRST, &lsbfirst);
    gl4es_glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowlength);
    gl4es_glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skiprows);
    gl4es_glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skippixels);
    gl4es_glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
	// Set Safe Texture params
	gl4es_glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
    gl4es_glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    gl4es_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl4es_glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    gl4es_glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    gl4es_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	// Create GC and Pixmap
	pixmap = XCreatePixmap(dpy, win, 10, 10, 1);
    values.foreground = BlackPixel(dpy, DefaultScreen(dpy));
    values.background = WhitePixel(dpy, DefaultScreen(dpy));
    values.font = fs->fid;
    valuemask = GCForeground | GCBackground | GCFont;
    gc = XCreateGC(dpy, pixmap, valuemask, &values);
    XFreePixmap(dpy, pixmap);
	// Loop each chars
    for (i = 0; i < count; i++) {
       unsigned int width, height, bm_width, bm_height;
       GLfloat x0, y0, dx, dy;
       XCharStruct *ch;
       int x, y;
       unsigned int c = first + i;
       int list = listBase + i;
       int valid;

       /* check on index validity and get the bounds */
       ch = isvalid(fs, c);
       if (!ch) {
          ch = &fs->max_bounds;
          valid = 0;
       }
       else {
          valid = 1;
       }
      /* glBitmap()' parameters:
          straight from the glXUseXFont(3) manpage.  */
       width = ch->rbearing - ch->lbearing;
       height = ch->ascent + ch->descent;
       x0 = -ch->lbearing;
       y0 = ch->descent - 1;
       dx = ch->width;
       dy = 0;
       /* X11's starting point.  */
       x = -ch->lbearing;
       y = ch->ascent;
       /* Round the width to a multiple of eight.  We will use this also
         for the pixmap for capturing the X11 font.  This is slightly
         inefficient, but it makes the OpenGL part real easy.  */
       bm_width = (width + 7) / 8;
       bm_height = height;
       gl4es_glNewList(list, GL_COMPILE);
       if (valid && (bm_width > 0) && (bm_height > 0)) {

          memset(bm, '\0', bm_width * bm_height);
          fill_bitmap(dpy, win, gc, bm_width, bm_height, x, y, c, bm);

          gl4es_glBitmap(width, height, x0, y0, dx, dy, bm);
       }
       else {
          gl4es_glBitmap(0, 0, 0.0, 0.0, dx, dy, NULL);
       }
       gl4es_glEndList();
    }

	// Free GC & Pixmap
    free(bm);
    XFreeFontInfo(NULL, fs, 1);
    XFreeGC(dpy, gc);

    // Restore saved packing modes.
    gl4es_glPixelStorei(GL_UNPACK_SWAP_BYTES, swapbytes);
    gl4es_glPixelStorei(GL_UNPACK_LSB_FIRST, lsbfirst);
    gl4es_glPixelStorei(GL_UNPACK_ROW_LENGTH, rowlength);
    gl4es_glPixelStorei(GL_UNPACK_SKIP_ROWS, skiprows);
    gl4es_glPixelStorei(GL_UNPACK_SKIP_PIXELS, skippixels);
    gl4es_glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
	// All done
}
#endif //NOX11
void gl4es_glXWaitGL() {}
void gl4es_glXWaitX() {}
void gl4es_glXReleaseBuffersMESA() {}

#ifndef NOX11
/* TODO proper implementation */
int gl4es_glXQueryDrawable(Display *dpy, GLXDrawable draw, int attribute, unsigned int *value) {
    DBG(printf("glXQueryDrawable(%p, %p", dpy, draw);)
    int pbuf=isPBuffer(draw);
    *value = 0;
    int width = 800;
    int height = 480;
#ifndef NOX11
    if(!pbuf && (attribute==GLX_WIDTH || attribute==GLX_HEIGHT)) {
        // Get Window size and all...
        unsigned int border, depth;
        Window root;
        int x, y;
        XGetGeometry(dpy, draw, &root, &x, &y, &width, &height, &border, &depth);
    }
#endif                
    switch(attribute) {
        case GLX_WIDTH:
            *value = (pbuf)?pbuffersize[pbuf-1].Width:width;
            DBG(printf("(%d), GLX_WIDTH, %p = %d)\n", pbuf, value, *value);)
            return 1;
        case GLX_HEIGHT:
            *value = (pbuf)?pbuffersize[pbuf-1].Height:height;
            DBG(printf("(%d), GLX_HEIGHT, %p = %d)\n", pbuf, value, *value);)
            return 1;
        case GLX_PRESERVED_CONTENTS:
            if(pbuf) *value = 1;
            DBG(printf("(%d), GLX_PRESERVED_CONTENTS, %p = %d)\n", pbuf, value, *value);)
            return 1;
        case GLX_LARGEST_PBUFFER:
            if(pbuf) *value = 0;
            DBG(printf("(%d), GLX_LARGEST_PBUFFER, %p = %d)\n", pbuf, value, *value);)
            return 1;
        case GLX_FBCONFIG_ID:
            *value = 1;
            DBG(printf("(%d), GLX_FBCONFIG_ID, %p = %d)\n", pbuf, value, *value);)
            return 1;
        case GLX_SWAP_INTERVAL_EXT:
            *value = swapinterval;
            DBG(printf("(%d), GLX_SWAP_INTERVAL_EXT, %p = %d)\n", pbuf, value, *value);)
            return 1;
        case GLX_MAX_SWAP_INTERVAL_EXT:
            *value = maxswap; // fake, should eglQuery the Config for EGL_MAX_SWAP_INTERVAL (and EGL_MIN_SWAP_INTERVAL)
            DBG(printf("(%d), GLX_MAX_SWAP_INTERVAL_EXT, %p = %d)\n", pbuf, value, *value);)
            return 1;
    }
    DBG(printf("(%d), %04x, %p)\n", pbuf, attribute, value);)
    return 0;
}

GLXPbuffer addPBuffer(EGLSurface surface, int Width, int Height, EGLContext Context, EGLConfig Config)
{
    if(pbufferlist_cap<=pbufferlist_size) {
        pbufferlist_cap += 4;
        pbufferlist = (GLXPbuffer*)realloc(pbufferlist, sizeof(GLXPbuffer)*pbufferlist_cap);
        pbuffersize = (glx_buffSize*)realloc(pbuffersize, sizeof(glx_buffSize)*pbufferlist_cap);
    }
    pbufferlist[pbufferlist_size] = (GLXPbuffer)surface;
    pbuffersize[pbufferlist_size].Width = Width;
    pbuffersize[pbufferlist_size].Height = Height;
    pbuffersize[pbufferlist_size].Context = Context;
    pbuffersize[pbufferlist_size].Surface = surface;
    pbuffersize[pbufferlist_size].Config = Config;
    pbuffersize[pbufferlist_size].gc = NULL;
    pbuffersize[pbufferlist_size].Type = 1; // 1 = pbuffer
    return pbufferlist[pbufferlist_size++];
}
void delPBuffer(int j)
{
    LOAD_EGL(eglDestroyContext);
    pbufferlist[j] = 0;
    pbuffersize[j].Width = 0;
    pbuffersize[j].Height = 0;
    pbuffersize[j].gc = 0;
    egl_eglDestroyContext(eglDisplay, pbuffersize[j].Context);
    CheckEGLErrors();
    // should pack, but I think it's useless for common use 
}

void gl4es_glXDestroyPbuffer(Display * dpy, GLXPbuffer pbuf) {
    DBG(printf("glxDestroyPBuffer(%p, %p)\n", dpy, pbuf);)
    LOAD_EGL(eglDestroySurface);
    int j=0;
    while(j<pbufferlist_size && pbufferlist[j]!=pbuf) j++;
    if(j==pbufferlist_size) {
        DBG(printf("PBuff not found in pbufferlist\n");)
        return;
    }
        // delete de Surface
    EGLSurface surface = (EGLSurface)pbufferlist[j];
    egl_eglDestroySurface(eglDisplay, surface);
    CheckEGLErrors();

    delPBuffer(j);
}

int createPBuffer(Display * dpy, const EGLint * egl_attribs, EGLSurface* Surface, EGLContext* Context, EGLConfig *Config, int redBits, int greenBits, int blueBits, int alphaBits, int samplebuffers, int samples) {
    LOAD_EGL(eglChooseConfig);
    LOAD_EGL(eglCreatePbufferSurface);
    LOAD_EGL(eglInitialize);
    LOAD_EGL(eglBindAPI);
    LOAD_EGL(eglCreateContext);

    EGLint configAttribs[] = {
        EGL_RED_SIZE, redBits,
        EGL_GREEN_SIZE, greenBits,
        EGL_BLUE_SIZE, blueBits,
        EGL_ALPHA_SIZE, (hardext.eglnoalpha)?0:alphaBits,
        EGL_DEPTH_SIZE, 1,
        EGL_STENCIL_SIZE, 1,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, (hardext.esversion==1)?EGL_OPENGL_ES_BIT:EGL_OPENGL_ES2_BIT,
        EGL_SAMPLE_BUFFERS, samplebuffers,
        EGL_SAMPLES, samples,
        EGL_NONE
    };

    // Init what need to be done
    EGLBoolean result;
    if (eglDisplay == NULL || eglDisplay == EGL_NO_DISPLAY) {
        init_display((globals4es.usefb || globals4es.usepbuffer)?g_display:dpy);
        if (eglDisplay == EGL_NO_DISPLAY) {
            CheckEGLErrors();
            LOGD("LIBGL: Unable to create EGL display.\n");
            return 0;
        }
    }

    // first time?
    if (eglInitialized == false) {
        egl_eglBindAPI(EGL_OPENGL_ES_API);
        result = egl_eglInitialize(eglDisplay, NULL, NULL);
        if (result != EGL_TRUE) {
            CheckEGLErrors();
            LOGD("LIBGL: Unable to initialize EGL display.\n");
            return 0;
        }
        eglInitialized = true;
    }

	// select a configuration
    int configsFound;

    result = egl_eglChooseConfig(eglDisplay, configAttribs, Config, 1, &configsFound);

    CheckEGLErrors();
    if (result != EGL_TRUE || configsFound == 0) {
        LOGD("LIBGL: No EGL configs found.\n");
        return 0;
    }

	// now, create the PBufferSurface
    (*Surface) = egl_eglCreatePbufferSurface(eglDisplay, Config[0], egl_attribs);

    if((*Surface)==EGL_NO_SURFACE) {
        CheckEGLErrors();
        LOGD("LIBGL: Error creating PBuffer\n");
        return 0;
    }
    (*Context) = egl_eglCreateContext(eglDisplay, Config[0], EGL_NO_CONTEXT, (hardext.esversion==1)?egl_context_attrib:egl_context_attrib_es2);
    CheckEGLErrors();

    return 1;
}

GLXPbuffer gl4es_glXCreatePbuffer(Display * dpy, GLXFBConfig config, const int * attrib_list) {
    DBG(printf("glXCreatePbuffer(%p, %p, %p)\n", dpy, config, attrib_list);)
    LOAD_EGL(eglQuerySurface);

	EGLSurface Surface = 0;
    EGLContext Context = 0;
    EGLConfig Config[1];
	//let's create a PBuffer attributes
	EGLint egl_attribs[128];	// should be enough
	int i = 0;
	if(attrib_list) {
		int j = 0;
		while(attrib_list[j]!=0) {
			switch(attrib_list[j++]) {
				case GLX_PBUFFER_WIDTH:
					egl_attribs[i++] = EGL_WIDTH;
					egl_attribs[i++] = attrib_list[j++];
					break;
				case GLX_PBUFFER_HEIGHT:
					egl_attribs[i++] = EGL_HEIGHT;
					egl_attribs[i++] = attrib_list[j++];
					break;
				case GLX_LARGEST_PBUFFER:
					egl_attribs[i++] = EGL_LARGEST_PBUFFER;
                    egl_attribs[i++] = (attrib_list[j++])?EGL_TRUE:EGL_FALSE;
					break;
				case GLX_PRESERVED_CONTENTS:
                    j++;
					// ignore this one
					break;
				//nothing, ignore unknown attribs
			}
		}
	}
    egl_attribs[i++] = EGL_NONE;

    // Check that the config is for PBuffer
    if(config->drawableType&GLX_PBUFFER_BIT!=GLX_PBUFFER_BIT)
        return 0;


    if(createPBuffer(dpy, egl_attribs, &Surface, &Context, Config, config->redBits, config->greenBits, config->blueBits, config->alphaBits, config->nMultiSampleBuffers, config->multiSampleSize)==0) {
        return 0;
    }

    int Width, Height;

    egl_eglQuerySurface(eglDisplay,Surface,EGL_WIDTH,&Width);
    egl_eglQuerySurface(eglDisplay,Surface,EGL_HEIGHT,&Height);

    return addPBuffer(Surface, Width, Height, Context, Config[1]);
}

GLXPbuffer addPixBuffer(Display *dpy, EGLSurface surface, EGLConfig Config, int Width, int Height, EGLContext Context, Pixmap pixmap, int depth, int emulated)
{
    if(pbufferlist_cap<=pbufferlist_size) {
        pbufferlist_cap += 4;
        pbufferlist = (GLXPbuffer*)realloc(pbufferlist, sizeof(GLXPbuffer)*pbufferlist_cap);
        pbuffersize = (glx_buffSize*)realloc(pbuffersize, sizeof(glx_buffSize)*pbufferlist_cap);
    }
    pbufferlist[pbufferlist_size] = (GLXPbuffer)pixmap;
    pbuffersize[pbufferlist_size].Width = Width;
    pbuffersize[pbufferlist_size].Height = Height;
    pbuffersize[pbufferlist_size].Context = Context;
    pbuffersize[pbufferlist_size].Surface = surface;
    pbuffersize[pbufferlist_size].Config = Config;
    pbuffersize[pbufferlist_size].Depth = depth;
    pbuffersize[pbufferlist_size].dpy = dpy;
    pbuffersize[pbufferlist_size].gc = (emulated)?XCreateGC(dpy, pixmap, 0, NULL):NULL;
    pbuffersize[pbufferlist_size].frame = NULL;

    pbuffersize[pbufferlist_size].Type = 2+emulated;    //2 = pixmap, 3 = emulated pixmap, 4 = emulated win
    return pbufferlist[pbufferlist_size++];
}
void delPixBuffer(int j)
{
    LOAD_EGL(eglDestroyContext);
    if(pbuffersize[j].gc)
        XFree(pbuffersize[j].gc);
    if(pbuffersize[j].frame) {
        XDestroyImage(pbuffersize[j].frame);
    }
    pbufferlist[j] = 0;
    pbuffersize[j].Width = 0;
    pbuffersize[j].Height = 0;
    pbuffersize[j].Depth = 0;
    pbuffersize[j].dpy = 0;
    pbuffersize[j].gc = 0;
    pbuffersize[j].Surface = 0;
    egl_eglDestroyContext(eglDisplay, pbuffersize[j].Context);
    CheckEGLErrors();
    // should pack, but I think it's useless for common use 
}

int createPixBuffer(Display * dpy, int bpp, const EGLint * egl_attribs, NativePixmapType nativepixmap, EGLSurface* Surface, EGLContext* Context) {
    LOAD_EGL(eglChooseConfig);
    LOAD_EGL(eglCreatePixmapSurface);
    LOAD_EGL(eglInitialize);
    LOAD_EGL(eglBindAPI);
    LOAD_EGL(eglCreateContext);

    EGLint configAttribs[] = {
        EGL_RED_SIZE, (bpp>16)?8:5,
        EGL_GREEN_SIZE, (bpp==15)?5:(bpp>16)?8:6,
        EGL_BLUE_SIZE, (bpp>16)?8:5,
        EGL_ALPHA_SIZE, (bpp==32)?8:0,
        EGL_DEPTH_SIZE, 1,      // some depth
        EGL_STENCIL_SIZE, 1,    // some stencil too
        EGL_SURFACE_TYPE, EGL_PIXMAP_BIT,
        EGL_RENDERABLE_TYPE, (hardext.esversion==1)?EGL_OPENGL_ES_BIT:EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    // Init what need to be done
    EGLBoolean result;
    if (eglDisplay == NULL || eglDisplay == EGL_NO_DISPLAY) {
        init_display((globals4es.usefb || globals4es.usepbuffer)?g_display:dpy);
        if (eglDisplay == EGL_NO_DISPLAY) {
            CheckEGLErrors();
            LOGE("LIBGL: Unable to create EGL display.\n");
            return 0;
        }
    }

    // first time?
    if (eglInitialized == false) {
        egl_eglBindAPI(EGL_OPENGL_ES_API);
        result = egl_eglInitialize(eglDisplay, NULL, NULL);
        if (result != EGL_TRUE) {
            CheckEGLErrors();
            LOGE("LIBGL: Unable to initialize EGL display.\n");
            return 0;
        }
        eglInitialized = true;
    }

	// select a configuration
    int configsFound;
    static EGLConfig pixbufConfigs[1];
    result = egl_eglChooseConfig(eglDisplay, configAttribs, pixbufConfigs, 1, &configsFound);

    CheckEGLErrors();
    if (result != EGL_TRUE || configsFound == 0) {
        LOGE("LIBGL: No EGL configs found.\n");
        return 0;
    }

	// now, create the PixmapSurface
    (*Surface) = egl_eglCreatePixmapSurface(eglDisplay, pixbufConfigs[0], nativepixmap,egl_attribs);

    if((*Surface)==EGL_NO_SURFACE) {
        CheckEGLErrors();
        LOGE("LIBGL: Error creating PixmapSurface\n");
        return 0;
    }

    (*Context) = egl_eglCreateContext(eglDisplay, pixbufConfigs[0], EGL_NO_CONTEXT, (hardext.esversion==1)?egl_context_attrib:egl_context_attrib_es2);
    CheckEGLErrors();

    return 1;
}

GLXPixmap gl4es_glXCreateGLXPixmap(Display *display, XVisualInfo * visual, Pixmap pixmap) {
    DBG(printf("glXCreateGLXPixmap(%p, %p, %p)\n", display, visual, pixmap);)
    LOAD_EGL(eglQuerySurface);

	EGLSurface Surface = 0;
    EGLContext Context = 0;
    EGLConfig  Config[1] = {0};
    //first, analyse PixMap to get it's dimensions and color depth...
    unsigned int width, height, border, depth;
    Window root;
    int x, y;
    int emulated = 0;
    XGetGeometry(display, pixmap, &root, &x, &y, &width, &height, &border, &depth);
    // let's try to create a PixmapSurface directly
    if(globals4es.usefb || createPixBuffer(display, depth, NULL, (NativePixmapType)pixmap, &Surface, &Context)==0) {
        // fail, so emulate with a PBuffer
        SHUT(LOGE("LIBGL: Pixmap creation failed, trying PBuffer instead\n"));
        //let's create a PixBuffer attributes
        EGLint egl_attribs[10];	// should be enough
        int i = 0;
        egl_attribs[i++] = EGL_WIDTH;
        egl_attribs[i++] = width;
        egl_attribs[i++] = EGL_HEIGHT;
        egl_attribs[i++] = height;
        egl_attribs[i++] = EGL_NONE;

        if(createPBuffer(display, egl_attribs, &Surface, &Context, Config, (depth>16)?8:5, (depth==15)?5:(depth>16)?8:6, (depth>16)?8:5, (depth==32)?8:0, 0, 0)==0) {
            // fail too, abort
            SHUT(LOGE("LIBGL: PBuffer creation failed too\n"));
            return 0;
        }
        emulated = 1;

    }
    int Width, Height;

    egl_eglQuerySurface(eglDisplay,Surface,EGL_WIDTH,&Width);
    egl_eglQuerySurface(eglDisplay,Surface,EGL_HEIGHT,&Height);

    return addPixBuffer(display, Surface, Config[0], Width, Height, Context, pixmap, depth, emulated);
}

GLXPixmap gl4es_glXCreatePixmap(Display * dpy, GLXFBConfig config, Pixmap pixmap, const int * attrib_list) {
    DBG(printf("glXCreatePixmap(%p, %p, %p, %p)\n", dpy, config, pixmap, attrib_list);)
    // Check that the config is for PBuffer
    if(config->drawableType&GLX_PIXMAP_BIT!=GLX_PIXMAP_BIT)
        return 0;
    
    return gl4es_glXCreateGLXPixmap(dpy, NULL, pixmap);
}


void gl4es_glXDestroyGLXPixmap(Display *display, void *pixmap) {
    DBG(printf("glXDestroyGLXPixmap(%p, %p)\n", display, pixmap);)
    LOAD_EGL(eglDestroySurface);
    int j=0;
    while(j<pbufferlist_size && pbufferlist[j]!=(GLXPbuffer)pixmap) j++;
    if(j==pbufferlist_size)
        return;
        // delete de Surface
    EGLSurface surface = pbuffersize[j].Surface;// (EGLSurface)pbufferlist[j];
    egl_eglDestroySurface(eglDisplay, surface);
    CheckEGLErrors();

    delPixBuffer(j);
}

void gl4es_glXDestroyPixmap(Display *display, void *pixmap) {
    DBG(printf("glXDestroyPixmap(%p, %p)\n", display, pixmap);)
    gl4es_glXDestroyGLXPixmap(display, pixmap);
}


void actualBlit(int reverse, int Width, int Height, int Depth, 
                    Display *dpy, Pixmap drawable, GC gc, XImage* frame,
                    uintptr_t pix, void* tmp) {
const int sbuf = Width * Height * (Depth==16?2:4);
#ifdef PANDORA
    if (tmp) {
        if(reverse) {
            int stride = Width * 2;
            uintptr_t src_pos = (uintptr_t)tmp;
            uintptr_t dst_pos = (uintptr_t)pix+sbuf-stride;
            for (int i = 0; i < Height; i++) {
                for (int j = 0; j < Width; j++) {
                    *(GLushort*)dst_pos = ((GLushort)(((char*)src_pos)[0]&0xf8)>>(3)) | ((GLushort)(((char*)src_pos)[1]&0xfc)<<(5-2)) | ((GLushort)(((char*)src_pos)[2]&0xf8)<<(11-3));
                    src_pos += 4;
                    dst_pos += 2;
                }
                dst_pos -= 2*stride;
            }
        } else
            pixel_convert(tmp, (void**)&pix, Width, Height, GL_BGRA, GL_UNSIGNED_BYTE, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0, glstate->texture.unpack_align);
    } else
#endif
        if(reverse) {
            int stride = Width * (Depth==16?2:4);
            uintptr_t end=(uintptr_t)pix+sbuf-stride;
            uintptr_t beg=(uintptr_t)pix;
            void* const tmp = (void*)(pix+sbuf);
            for (; beg < end; beg+=stride, end-=stride) {
                memcpy(tmp, (void*)end, stride);
                memcpy((void*)end, (void*)beg, stride);
                memcpy((void*)beg, tmp, stride);
            }
        }

    // blit
    XPutImage(dpy, drawable, gc, frame, 0, 0, 0, 0, Width, Height);
}

void BlitEmulatedPixmap() {
    if(!glstate->emulatedPixmap)
        return;

    Pixmap drawable = (Pixmap)pbufferlist[glstate->emulatedPixmap-1];

    glx_buffSize *buff = &pbuffersize[glstate->emulatedPixmap-1]; 

    int Width = buff->Width;
    int Height = buff->Height;
    int Depth = buff->Depth;
    Display *dpy = buff->dpy;
    GC gc = buff->gc;
    // the reverse stuff can probably be better!
    int reverse = buff->Type==4?1:0;
    const int sbuf = Width * Height * (Depth==16?2:4);
    XImage* frame = buff->frame;

    // grab the size of the drawable if it has changed
    if(reverse) {
        // Get Window size and all...
        unsigned int width, height, border, depth;
        Window root;
        int x, y;
        XGetGeometry(dpy, drawable, &root, &x, &y, &width, &height, &border, &depth);
        if(width!=Width || height!=Height || depth!=Depth) {
            LOAD_EGL(eglCreatePbufferSurface);
            LOAD_EGL(eglDestroySurface);
            LOAD_EGL(eglMakeCurrent);
            LOAD_EGL(eglChooseConfig);
            // destroy old stuff
            XSync(dpy, False);  // synch seems needed before the DestroyImage...
            if(frame) XDestroyImage(frame);
            buff->frame = 0;
            

            //let's create a PBuffer attributes
            EGLint egl_attribs[10];	// should be enough
            int i = 0;
            egl_attribs[i++] = EGL_WIDTH;
            egl_attribs[i++] = width;
            egl_attribs[i++] = EGL_HEIGHT;
            egl_attribs[i++] = height;
            egl_attribs[i++] = EGL_NONE;

            EGLint configAttribs[] = {
                EGL_RED_SIZE, (depth>16)?8:5,
                EGL_GREEN_SIZE, (depth==15)?5:((depth>16)?8:6),
                EGL_BLUE_SIZE, (depth>16)?8:5,
                EGL_ALPHA_SIZE, (depth==32)?8:0,
                EGL_DEPTH_SIZE, 1,
                EGL_STENCIL_SIZE, 1,
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                EGL_RENDERABLE_TYPE, (hardext.esversion==1)?EGL_OPENGL_ES_BIT:EGL_OPENGL_ES2_BIT,
                EGL_NONE
            };

            DBG(printf("LIBGL: Recreate PBuffer %dx%dx%d => %dx%dx%d\n", Width, Height, Depth, width, height, depth);)

            int configsFound;
            static EGLConfig pbufConfigs[1];
            egl_eglChooseConfig(eglDisplay, configAttribs, pbufConfigs, 1, &configsFound);
            CheckEGLErrors();

            EGLSurface Surface = egl_eglCreatePbufferSurface(eglDisplay, pbufConfigs[0], egl_attribs);
            CheckEGLErrors();

            egl_eglMakeCurrent(eglDisplay, Surface, Surface, buff->Context);
            CheckEGLErrors();

            egl_eglDestroySurface(eglDisplay, buff->Surface);
            CheckEGLErrors();
            glxContext->eglSurface = buff->Surface = Surface;
            glxContext->eglConfigs[0] = pbufConfigs[0];
            buff->Width = width;
            buff->Height = height;
            buff->Depth = depth;
            return;
        }
    }

    // create things if needed
    if(!buff->frame) {
        int sz = Width*(Height+reverse)*(Depth==16?2:4);
#ifdef PANDORA
        if(hardext.esversion==1 && Depth==16) {
            sz += Width*Height*4;
        }
#endif
        buff->frame = XCreateImage(dpy, NULL /*visual*/, Depth, ZPixmap, 0, malloc(sz), Width, Height, (Depth==16)?16:32, 0);
    }

    if (!frame) {
        return;
    }
    uintptr_t pix=(uintptr_t)frame->data;

    // grab framebuffer
    void* tmp = NULL;
#ifdef PANDORA
    LOAD_GLES(glReadPixels);
    if(hardext.esversion==1) {
        if(Depth==16) {
            tmp = (void*)(pix + Width*Height*2);
            gles_glReadPixels(0, 0, Width, Height, GL_BGRA, GL_UNSIGNED_BYTE, tmp);
        } else {
            gles_glReadPixels(0, 0, Width, Height, GL_BGRA, GL_UNSIGNED_BYTE, (void*)pix);
        }
    } else 
    if(Depth==16)
        gles_glReadPixels(0, 0, Width, Height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (void*)pix);
    else
#endif
    gl4es_glReadPixels(0, 0, Width, Height, (Depth==16)?GL_RGB:GL_BGRA, (Depth==16)?GL_UNSIGNED_SHORT_5_6_5:GL_UNSIGNED_BYTE, (void*)pix);

    actualBlit(reverse, Width, Height, Depth, dpy, drawable, gc, frame, pix, tmp);

}

GLXContext gl4es_glXCreateContextAttribs(Display *dpy, GLXFBConfig config, GLXContext share_context, Bool direct, const int *attrib_list) {
    DBG(printf("glXCreateContextAttribs(%p, %p, %p, %d, %p)\n", dpy, config, share_context, direct, attrib_list);)
    int ask_es = 0;
    int ask_shaders = 0;
    int majver = 0, minver = 0;
    int flags = 0;
    int mask = 0;
    const int *attr = attrib_list;
    while (attr && (*attr)!= 0) {
        int name = (*(attr++));
        int pair = (*(attr++));
        switch(name) {
            case GLX_CONTEXT_MAJOR_VERSION_ARB:
                majver = pair;
                break;
            case GLX_CONTEXT_MINOR_VERSION_ARB:
                minver = pair;
                break;
            case GLX_CONTEXT_FLAGS_ARB:
                flags = pair;
                break;
            case GLX_CONTEXT_PROFILE_MASK_ARB:
                mask = pair;
                break;
            default: {
                DBG(printf(" unknow Attrib %04X (value=%d)\n", name, pair);)
            }
        }   
    }
    if(majver*10+minver != 0) {
        DBG(printf(" Required context version %d.%d\n", majver, minver);)
        if(majver*10+minver>21) {
            LOGE("LIBGL: Asked for unsupported context version %d.%d\n", majver, minver);
            return 0;
        }
        if(majver*10+minver>globals4es.gl) {
            LOGE("LIBGL: Asked for unsupported context version %d.%d (max version is %d.%d)\n", majver, minver, globals4es.gl/10, globals4es.gl%10);
            return 0;
        }
        if((mask&GLX_CONTEXT_ES2_PROFILE_BIT_EXT) && hardext.esversion<2) {
            LOGE("LIBGL: Asked for ES2 compatible context on GLES1.1 Backend\n");
            return 0;
        }
    }
    if(mask&GLX_CONTEXT_ES2_PROFILE_BIT_EXT)
        globales2 = 1;
    GLXContext context = gl4es_glXCreateNewContext(dpy, config, GLX_RGBA_TYPE, share_context, direct);
    globales2 = 0;
    return context;
}

#endif //NOX11

// New export the Alias
#ifndef NOX11
GLXContext glXCreateContext(Display *display, XVisualInfo *visual, GLXContext shareList, Bool isDirect) AliasExport("gl4es_glXCreateContext");
GLXContext glXCreateContextAttribsARB(Display *display, GLXFBConfig config, GLXContext share_context, Bool direct, const int *attrib_list) AliasExport("gl4es_glXCreateContextAttribsARB");
void glXDestroyContext(Display *display, GLXContext ctx) AliasExport("gl4es_glXDestroyContext");
Display *glXGetCurrentDisplay() AliasExport("gl4es_glXGetCurrentDisplay");
XVisualInfo *glXChooseVisual(Display *display, int screen, int *attributes) AliasExport("gl4es_glXChooseVisual");
Bool glXMakeCurrent(Display *display, GLXDrawable drawable, GLXContext context) AliasExport("gl4es_glXMakeCurrent");
Bool glXMakeContextCurrent(Display *display, int drawable, int readable, GLXContext context) AliasExport("gl4es_glXMakeContextCurrent");
void glXSwapBuffers(Display *display, int drawable) AliasExport("gl4es_glXSwapBuffers");
int glXGetConfig(Display *display, XVisualInfo *visual, int attribute, int *value) AliasExport("gl4es_glXGetConfig");
const char *glXQueryExtensionsString(Display *display, int screen) AliasExport("gl4es_glXQueryExtensionsString");
const char *glXQueryServerString(Display *display, int screen, int name) AliasExport("gl4es_glXQueryServerString");
Bool glXQueryExtension(Display *display, int *errorBase, int *eventBase) AliasExport("gl4es_glXQueryExtension");
Bool glXQueryVersion(Display *display, int *major, int *minor) AliasExport("gl4es_glXQueryVersion");
const char *glXGetClientString(Display *display, int name) AliasExport("gl4es_glXGetClientString");
int glXQueryContext( Display *dpy, GLXContext ctx, int attribute, int *value) AliasExport("gl4es_glXQueryContext");
GLXContext glXGetCurrentContext() AliasExport("gl4es_glXGetCurrentContext");
GLXFBConfig *glXChooseFBConfig(Display *display, int screen, const int *attrib_list, int *count) AliasExport("gl4es_glXChooseFBConfig");
GLXFBConfig *glXChooseFBConfigSGIX(Display *display, int screen, const int *attrib_list, int *count) AliasExport("gl4es_glXChooseFBConfig");
GLXFBConfig *glXGetFBConfigs(Display *display, int screen, int *count) AliasExport("gl4es_glXGetFBConfigs");
int glXGetFBConfigAttrib(Display *display, GLXFBConfig config, int attribute, int *value) AliasExport("gl4es_glXGetFBConfigAttrib");
XVisualInfo *glXGetVisualFromFBConfig(Display *display, GLXFBConfig config) AliasExport("gl4es_glXGetVisualFromFBConfig");
GLXContext glXCreateNewContext(Display *display, GLXFBConfig config, int render_type, GLXContext share_list, Bool is_direct) AliasExport("gl4es_glXCreateNewContext");
void glXSwapIntervalEXT(Display *display, int drawable, int interval) AliasExport("gl4es_glXSwapIntervalEXT");
void glXCopyContext(Display *display, GLXContext src, GLXContext dst, GLuint mask) AliasExport("gl4es_glXCopyContext");
Window glXCreateWindow(Display *display, GLXFBConfig config, Window win, int *attrib_list) AliasExport("gl4es_glXCreateWindow");
void glXDestroyWindow(Display *display, void *win) AliasExport("gl4es_glXDestroyWindow");
GLXDrawable glXGetCurrentDrawable() AliasExport("gl4es_glXGetCurrentDrawable");
Bool glXIsDirect(Display * display, GLXContext ctx) AliasExport("gl4es_glXIsDirect");
void glXUseXFont(Font font, int first, int count, int listBase) AliasExport("gl4es_glXUseXFont");
int glXQueryDrawable(Display *dpy, GLXDrawable draw, int attribute, unsigned int *value) AliasExport("gl4es_glXQueryDrawable");
void glXDestroyPbuffer(Display * dpy, GLXPbuffer pbuf) AliasExport("gl4es_glXDestroyPbuffer");
GLXPbuffer glXCreatePbuffer(Display * dpy, GLXFBConfig config, const int * attrib_list) AliasExport("gl4es_glXCreatePbuffer");
GLXPixmap glXCreateGLXPixmap(Display *display, XVisualInfo * visual, Pixmap pixmap) AliasExport("gl4es_glXCreateGLXPixmap");
GLXPixmap glXCreatePixmap(Display * dpy, GLXFBConfig config, Pixmap pixmap, const int * attrib_list) AliasExport("gl4es_glXCreatePixmap");
void glXDestroyGLXPixmap(Display *display, void *pixmap) AliasExport("gl4es_glXDestroyGLXPixmap");
void glXDestroyPixmap(Display *display, void *pixmap) AliasExport("gl4es_glXDestroyPixmap");
GLXContext glXCreateContextAttribs(Display *dpy, GLXFBConfig config, GLXContext share_context, Bool direct, const int *attrib_list) AliasExport("gl4es_glXCreateContextAttribs");
#endif

void glXSwapInterval(int interval) AliasExport("gl4es_glXSwapInterval");
void glXSwapIntervalMESA(int interval) AliasExport("gl4es_glXSwapInterval");
void glXSwapIntervalSGI(int interval) AliasExport("gl4es_glXSwapInterval");

void glXWaitGL() AliasExport("gl4es_glXWaitGL");
void glXWaitX() AliasExport("gl4es_glXWaitX");
void glXReleaseBuffersMESA() AliasExport("gl4es_glXReleaseBuffersMESA");

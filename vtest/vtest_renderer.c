/**************************************************************************
 *
 * Copyright (C) 2015 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include "virglrenderer.h"
#include <sys/uio.h>
#include "vtest.h"
#include "vtest_protocol.h"
#include "util/u_debug.h"
#include "ring.h"
#include <epoxy/egl.h>
#include "vrend_renderer.h"
#include "vrend_object.h"
#define X11
#ifdef X11
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <epoxy/glx.h>
#elif defined ANDROID_JNI
#include <jni.h>
#include <android/native_window.h>
#include <android/log.h>
#define printf(...) __android_log_print(ANDROID_LOG_DEBUG, "virgl", __VA_ARGS__) 
#endif
#define FL_RING (1<<0)
#define FL_GLX (1<<1)
#define FL_GLES (1<<2)
#define FL_OVERLAY (1<<3)



pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
//EGLDisplay disp;

struct dt_record
{
uint32_t used;
EGLSurface egl_surf;
int fb_id;
int res_id;
#ifdef ANDROID_JNI
jobject java_surf;
#elif defined X11
Drawable x11_win;
#endif

};
struct vtest_renderer {
  // pipe
  int fd;
  int flags;
  ring_t ring;

  // egl
  EGLDisplay egl_display;
  EGLConfig egl_conf;
  EGLContext egl_ctx;
  EGLSurface egl_fake_surf;

  // renderer
  int ctx_id;
  int fence_id;
  int last_fence;
  
  struct dt_record dts[32];
#ifdef X11
  // x11
  Drawable x11_fake_win;
  Display *x11_dpy;

  // glx
  GLXFBConfig* fbConfigs;
  GLXPbuffer pbuffer;
  GLXContext glx_ctx;
#elif defined ANDROID_JNI
  struct jni_s
  {
    jclass cls;
    JNIEnv *env;
    jmethodID create;
    jmethodID get_surface;
    jmethodID set_rect;
    jmethodID destroy;
  } jni;
#endif
};


void *create_renderer(int in_fd, int ctx_id);

#ifdef ANDROID_JNI
static void *renderer_thread(void *arg)
{
    int fd = *(int*)arg;
    static int ctx_id = 0;
    ctx_id++;
    printf("renderer thread\n");
    run_renderer(fd, ctx_id);
    return NULL;
}


JNIEXPORT jint JNICALL Java_common_overlay_nativeOpen(JNIEnv *env, jclass cls)
{
  disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
return vtest_open_socket("/data/media/0/multirom/roms/Linux4TegraR231/root/tmp/.virgl_test");
}



JNIEXPORT void JNICALL Java_common_overlay_nativeUnlink(JNIEnv *env, jclass cls)
{
unlink("/data/media/0/multirom/roms/Linux4TegraR231/root/tmp/.virgl_test");
}
JNIEXPORT jint JNICALL Java_common_overlay_nativeAccept(JNIEnv *env, jclass cls, jint fd)
{
return wait_for_socket_accept(fd);
}
JNIEXPORT jint JNICALL Java_common_overlay_nativeInit(JNIEnv *env, jclass cls, jstring settings)
{
return 0;
}

JNIEXPORT void JNICALL Java_common_overlay_nativeRun(JNIEnv *env, jclass cls, jint fd)
{
//int fd1;

  //volatile int fd2 = fd;
  static int ctx_id;
      ctx_id++;
  struct vtest_renderer *r = create_renderer( fd, ctx_id);
  r->jni.env = env;
  r->jni.cls = cls;
  r->jni.create = (*env)->GetStaticMethodID(env,cls, "create", "(IIII)Landroid/view/SurfaceView;");
  r->jni.get_surface = (*env)->GetStaticMethodID(env,cls, "get_surface", "(Landroid/view/SurfaceView;)Landroid/view/Surface;");
  r->jni.set_rect = (*env)->GetStaticMethodID(env,cls, "set_rect", "(Landroid/view/SurfaceView;IIIII)V");
  r->jni.destroy = (*env)->GetStaticMethodID(env,cls, "destroy", "(Landroid/view/SurfaceView;)V");
  //int fd = vtest_open_socket("/data/media/0/multirom/roms/Linux4TegraR231/root/tmp/.virgl_test");
  renderer_loop(r);
  //exit(0);
}
#endif
static void vtest_write_fence(void *cookie, uint32_t fence_id_in)
{
  struct vtest_renderer *r = cookie;
  r->last_fence = fence_id_in;
}

static bool vtest_egl_has_extension_in_string(const char *haystack, const char *needle)
{
   const unsigned needle_len = strlen(needle);

   if (needle_len == 0)
      return false;

   while (true) {
      const char *const s = strstr(haystack, needle);

      if (s == NULL)
         return false;

      if (s[needle_len] == ' ' || s[needle_len] == '\0') {
         return true;
      }

      /* strstr found an extension whose name begins with
       * needle, but whose name is not equal to needle.
       * Restart the search at s + needle_len so that we
       * don't just find the same extension again and go
       * into an infinite loop.
       */
      haystack = s + needle_len;
   }

   return false;
}

static bool vtest_egl_init(struct vtest_renderer *d, bool surfaceless, bool gles)
{
   static EGLint conf_att[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 1,
      EGL_GREEN_SIZE, 1,
      EGL_BLUE_SIZE, 1,
      EGL_ALPHA_SIZE, 0,
      EGL_NONE,
   };
   static const EGLint ctx_att[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };
   EGLBoolean b;
   EGLenum api;
   EGLint major, minor, n;
   const char *extension_list;

   if (gles)
      conf_att[3] = EGL_OPENGL_ES_BIT;

   if (surfaceless) {
      conf_att[1] = EGL_PBUFFER_BIT;
   }
   
   const char *client_extensions = eglQueryString (NULL, EGL_EXTENSIONS);
#ifdef X11
   if( !d->x11_dpy) d->x11_dpy = XOpenDisplay(NULL);
   d->egl_display = eglGetDisplay(d->x11_dpy);
#else
   d->egl_display =disp;// eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
   if (!d->egl_display)
      goto fail;

   b = eglInitialize(d->egl_display, &major, &minor);
   if (!b)
      goto fail;

   extension_list = eglQueryString(d->egl_display, EGL_EXTENSIONS);

   fprintf(stderr, "EGL major/minor: %d.%d\n", major, minor);
   fprintf(stderr, "EGL version: %s\n",
           eglQueryString(d->egl_display, EGL_VERSION));
   fprintf(stderr, "EGL vendor: %s\n",
           eglQueryString(d->egl_display, EGL_VENDOR));
   fprintf(stderr, "EGL extensions: %s\n", extension_list);
#if 0
   d->have_mesa_drm_image = false;
   d->have_mesa_dma_buf_img_export = false;
   if (vtest_egl_has_extension_in_string(extension_list, "EGL_MESA_drm_image"))
      d->have_mesa_drm_image = true;

   if (vtest_egl_has_extension_in_string(extension_list, "EGL_MESA_image_dma_buf_export"))
      d->have_mesa_dma_buf_img_export = true;
#endif
   if (gles)
      api = EGL_OPENGL_ES_API;
   else
      api = EGL_OPENGL_API;
   b = eglBindAPI(api);
   if (!b)
      goto fail;

   b = eglChooseConfig(d->egl_display, conf_att, &d->egl_conf,
                       1, &n);

   if (!b || n != 1)
      goto fail;

   d->egl_ctx = eglCreateContext(d->egl_display,
                                 d->egl_conf,
                                 EGL_NO_CONTEXT,
                                 ctx_att);
   if (!d->egl_ctx)
      goto fail;

    static EGLint const window_attribute_list[] = {
        EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
        EGL_NONE,
    };
#ifdef X11
    d->x11_fake_win = XCreateSimpleWindow(d->x11_dpy, RootWindow(d->x11_dpy, 0), 0, 0, 100, 100, 0, BlackPixel(d->x11_dpy, 0),BlackPixel(d->x11_dpy, 0));
    d->egl_fake_surf = eglCreateWindowSurface(d->egl_display, d->egl_conf, d->x11_fake_win, window_attribute_list);
#elif defined ANDROID_JNI
struct vtest_renderer *r = d;
       jobject surf = (*r->jni.env)->CallStaticObjectMethod(r->jni.env, r->jni.cls, r->jni.get_surface,(*r->jni.env)->CallStaticObjectMethod(r->jni.env, r->jni.cls, r->jni.create, 0, 0, 0, 0));
       if(surf == 0)exit(0);
       ANativeWindow *window = ANativeWindow_fromSurface(r->jni.env, surf);
       int format;
       eglGetConfigAttrib(d->egl_display, d->egl_conf, EGL_NATIVE_VISUAL_ID, &format);
       ANativeWindow_setBuffersGeometry(window, 0, 0, format);
       d->egl_fake_surf = eglCreateWindowSurface(d->egl_display, d->egl_conf, window, 0);
//    d->egl_fake_surf = EGL_NO_SURFACE;
#endif
   eglMakeCurrent(d->egl_display, d->egl_fake_surf, d->egl_fake_surf, d->egl_ctx);
 //  eglMakeCurrent(d->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, d->egl_ctx);
   return true;
 fail:
   return false;
}

static void vtest_egl_destroy(struct vtest_renderer *d)
{
   eglMakeCurrent(d->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                  EGL_NO_CONTEXT);
   eglDestroyContext(d->egl_display, d->egl_ctx);
   eglTerminate(d->egl_display);
   free(d);
}

static virgl_renderer_gl_context vtest_egl_create_context(void *cookie, int scanout_idx, struct virgl_renderer_gl_ctx_param *param)
{
   struct vtest_renderer *ve = cookie;
   EGLContext eglctx;
   EGLint ctx_att[] = {
      EGL_CONTEXT_CLIENT_VERSION, param->major_ver,
      EGL_CONTEXT_MINOR_VERSION_KHR, param->minor_ver,
      EGL_NONE
   };


   eglctx = eglCreateContext(ve->egl_display,
                             ve->egl_conf,
                             param->shared ? eglGetCurrentContext() : 
//EGL_NO_CONTEXT,
ve->egl_ctx,
                             ctx_att);

  
 printf("create_context %d %d %d %d %x\n", scanout_idx, param->shared, param->major_ver, param->minor_ver, eglctx);

   return (virgl_renderer_gl_context)eglctx;
}

static void vtest_egl_destroy_context(void *cookie, virgl_renderer_gl_context ctx)
{
   struct vtest_renderer *ve = cookie;
   EGLContext eglctx = (EGLContext)ctx;

   printf("destroy_context %x\n", ctx);

   eglDestroyContext(ve->egl_display, eglctx);
}

static int vtest_egl_make_context_current(void *cookie, int scanout_idx, virgl_renderer_gl_context ctx)
{
   struct vtest_renderer *ve = cookie;
   EGLContext eglctx = (EGLContext)ctx;
   if( ctx == ve->egl_ctx )
       return eglMakeCurrent(ve->egl_display, ve->egl_fake_surf ,ve->egl_fake_surf, eglctx);
   else
       return eglMakeCurrent(ve->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, eglctx);

}
#ifdef X11
/// TODO: debug it with various implementations
static struct thread_shared
{
    Display *x11_dpy;
    GLXFBConfig* fbConfigs;
} shared;


static bool vtest_glx_init(struct vtest_renderer *d)
{
   if( !shared.x11_dpy )
   {
       int visualAttribs[] = { None };
       int numberOfFramebufferConfigurations = 0;

       shared.x11_dpy = XOpenDisplay(NULL);
       shared.fbConfigs = glXChooseFBConfig(shared.x11_dpy, DefaultScreen(shared.x11_dpy),
                                    visualAttribs, &numberOfFramebufferConfigurations);
   }
   d->x11_dpy = shared.x11_dpy;
   d->fbConfigs = shared.fbConfigs;

   int pbufferAttribs[] = {
      GLX_PBUFFER_WIDTH,  32,
      GLX_PBUFFER_HEIGHT, 32,
      None
   };
   d->pbuffer = glXCreatePbuffer(d->x11_dpy, d->fbConfigs[0], pbufferAttribs);
   d->x11_fake_win = XCreateSimpleWindow(d->x11_dpy, RootWindow(d->x11_dpy, 0), 0, 0, 100, 100, 0, BlackPixel(d->x11_dpy, 0),BlackPixel(d->x11_dpy, 0));
   d->glx_ctx = glXCreateNewContext(d->x11_dpy, d->fbConfigs[0], GLX_RGBA_TYPE, NULL, True);
   glXMakeContextCurrent(d->x11_dpy, d->x11_fake_win, d->x11_fake_win, d->glx_ctx);
   return d;
}

static void vtest_glx_destroy(struct vtest_renderer *d)
{
   XFree(d->fbConfigs);
   glXDestroyPbuffer(d->x11_dpy, d->pbuffer);
   XCloseDisplay(d->x11_dpy);
}

static virgl_renderer_gl_context vtest_glx_create_context(void *cookie, int scanout_idx, struct virgl_renderer_gl_ctx_param *vparams)
{
   struct vtest_renderer *d = cookie;
   int context_attribs[] = {
      GLX_CONTEXT_MAJOR_VERSION_ARB, vparams->major_ver,
      GLX_CONTEXT_MINOR_VERSION_ARB, vparams->minor_ver,
      None
   };

   GLXContext ctx =
      glXCreateContextAttribsARB(d->x11_dpy, d->fbConfigs[0],
                                 vparams->shared ? glXGetCurrentContext() : d->glx_ctx,
                                 True, context_attribs);
   return (virgl_renderer_gl_context)ctx;
}

static void vtest_glx_destroy_context(void *cookie, virgl_renderer_gl_context ctx)
{
   struct vtest_renderer *d = cookie;

   glXDestroyContext(d->x11_dpy, ctx);
}

static int vtest_glx_make_context_current(void *cookie, int scanout_idx, virgl_renderer_gl_context ctx)
{
   struct vtest_renderer *d = cookie;

   if( ctx == d->glx_ctx )
      return glXMakeContextCurrent(d->x11_dpy, d->x11_fake_win, d->x11_fake_win, ctx);

   return glXMakeContextCurrent(d->x11_dpy, d->pbuffer, d->pbuffer, ctx);
}

/*virgl_renderer_gl_context vtest_glx_get_current_context(struct vtest_renderer *d)
{
   return glXGetCurrentContext();
}*/
#endif
struct virgl_renderer_callbacks vtest_cbs = {
    .version = 1,
    .write_fence = vtest_write_fence,
    .create_gl_context = vtest_egl_create_context,
    .destroy_gl_context = vtest_egl_destroy_context,
    .make_current = vtest_egl_make_context_current,
};

/*struct virgl_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};*/

static int vtest_wait_for_fd_read(struct vtest_renderer *r)
{
   fd_set read_fds;
   int ret;

   if(r->ring.size) return 0;

   FD_ZERO(&read_fds);
   FD_SET(r->fd, &read_fds);

   ret = select(r->fd + 1, &read_fds, NULL, NULL, NULL);
   if (ret < 0)
      return ret;

   if (FD_ISSET(r->fd, &read_fds)) {
      return 0;
   }
   return -1;
}

static int vtest_block_write(struct vtest_renderer *r, void *buf, int size)
{
   void *ptr = buf;
   int left;
   int ret;

   if( r->ring.size )
      return ring_write( &r->ring, buf, size );

   left = size;
   do {
      ret = write(r->fd, ptr, left);
      if (ret < 0)
         return -errno;
      left -= ret;
      ptr += ret;
   } while (left);
   return size;
}

static int vtest_block_read(struct vtest_renderer *r, void *buf, int size)
{
   void *ptr = buf;
   int left;
   int ret;
   static int savefd = -1;


   if( r->ring.size )
       return ring_read(&r->ring, buf, size);

   left = size;
   do {
      ret = read(r->fd, ptr, left);
      if (ret <= 0)
	return ret == -1 ? -errno : 0;
      left -= ret;
      ptr += ret;
   } while (left);
   if (getenv("VTEST_SAVE")) {
      if (savefd == -1) {
         savefd = open(getenv("VTEST_SAVE"),
                       O_CLOEXEC|O_CREAT|O_WRONLY|O_TRUNC|O_DSYNC, S_IRUSR|S_IWUSR);
         if (savefd == -1) {
            perror("error opening save file");
            exit(1);
         }
      }
      if (write(savefd, buf, size) != size) {
         perror("failed to save");
         exit(1);
      }
   }
   return size;
}

static int vtest_create_renderer(struct vtest_renderer *r, uint32_t length)
{
    char *vtestname;
    int ret;
    int ctx = 0;

    if (getenv("VTEST_USE_GLX"))
       r->flags |= FL_GLX;

    if (getenv("VTEST_USE_EGL_SURFACELESS")) {
        if (ctx & VIRGL_RENDERER_USE_GLX) {
            fprintf(stderr, "Cannot use surfaceless with GLX.\n");
            return -1;
        }
        ctx |= VIRGL_RENDERER_USE_SURFACELESS;
    }

    if (1||getenv("VTEST_USE_GLES")) {
        if (ctx & VIRGL_RENDERER_USE_GLX) {
            fprintf(stderr, "Cannot use GLES with GLX.\n");
            return -1;
        }
        ctx |= VIRGL_RENDERER_USE_GLES;
        r->flags |= FL_GLES;
    }

    
    if( !(r->flags & FL_GLX) ) vtest_egl_init(r, false,!!(ctx & VIRGL_RENDERER_USE_GLES));
#ifdef X11
    else
    {
    vtest_cbs.create_gl_context = vtest_glx_create_context;
    vtest_cbs.destroy_gl_context = vtest_glx_destroy_context;
    vtest_cbs.make_current = vtest_glx_make_context_current;

      vtest_glx_init(r);
     }
#endif
    ret = virgl_renderer_init(r, ctx | VIRGL_RENDERER_THREAD_SYNC, &vtest_cbs);
    if (ret) {
      fprintf(stderr, "failed to initialise renderer.\n");
      return -1;
    }

    vtestname = calloc(1, length + 1);
    if (!vtestname)
      return -1;

    ret = vtest_block_read(r, vtestname, length);
    if (ret != (int)length) {
       ret = -1;
       goto end;
    }

    ret = virgl_renderer_context_create(r->ctx_id, strlen(vtestname), vtestname);

end:
    free(vtestname);
    return ret;
}

static void vtest_destroy_renderer(struct vtest_renderer *r)
{
  virgl_renderer_context_destroy(r->ctx_id);
  virgl_renderer_cleanup(r);
}

static int vtest_send_caps2(struct vtest_renderer *r)
{
    uint32_t hdr_buf[2];
    void *caps_buf;
    int ret;
    uint32_t max_ver, max_size;

    virgl_renderer_get_cap_set(2, &max_ver, &max_size);

    if (max_size == 0)
	return -1;
    caps_buf = malloc(max_size);
    if (!caps_buf)
	return -1;

    virgl_renderer_fill_caps(2, 1, caps_buf);

    hdr_buf[0] = max_size + 1;
    hdr_buf[1] = 2;
    ret = vtest_block_write(r, hdr_buf, 8);
    if (ret < 0)
	goto end;
    vtest_block_write(r, caps_buf, max_size);
    if (ret < 0)
	goto end;

end:
    free(caps_buf);
    return 0;
}

static int vtest_send_caps(struct vtest_renderer *r)
{
    uint32_t  max_ver, max_size;
    void *caps_buf;
    uint32_t hdr_buf[2];
    int ret;

    virgl_renderer_get_cap_set(1, &max_ver, &max_size);

    caps_buf = malloc(max_size);
    if (!caps_buf)
	return -1;
    
    virgl_renderer_fill_caps(1, 1, caps_buf);

    hdr_buf[0] = max_size + 1;
    hdr_buf[1] = 1;
    ret = vtest_block_write(r, hdr_buf, 8);
    if (ret < 0)
       goto end;
    vtest_block_write(r, caps_buf, max_size);
    if (ret < 0)
       goto end;

end:
    free(caps_buf);
    return 0;
}



static int vtest_dt_cmd(struct vtest_renderer *r)
{
    uint32_t flush_buf[VCMD_DT_SIZE];
    struct virgl_renderer_resource_create_args args;
    int ret;
    uint32_t cmd, x, y, w, h, handle;
    uint32_t drawable, id;
    static int use_overlay;

    ret = vtest_block_read(r, &flush_buf, sizeof(flush_buf));
    if (ret != sizeof(flush_buf))
	return -1;

    //EGLContext ctx = eglGetCurrentContext();
    
    drawable = flush_buf[VCMD_DT_DRAWABLE];
    x = flush_buf[VCMD_DT_X];
    y = flush_buf[VCMD_DT_Y];
    w = flush_buf[VCMD_DT_WIDTH];
    h = flush_buf[VCMD_DT_HEIGHT];
    id = flush_buf[VCMD_DT_ID];
    cmd = flush_buf[VCMD_DT_CMD];
    handle = flush_buf[VCMD_DT_HANDLE];

    printf("dt_cmd %d %d %d %d %d %d %d %d\n", cmd, x, y, w, h, id, handle, drawable);
    
    struct dt_record *dt = &r->dts[id]; 

    if( cmd == VCMD_DT_CMD_CREATE )
    {

#ifdef X11
    if( !dt->x11_win )
    {
        static EGLint const window_attribute_list[] = {
            EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
            EGL_NONE,
        };

	dt->x11_win = drawable;
	if( getenv("VTEST_OVERLAY") )
	{
	use_overlay = 1;
	dt->x11_win = XCreateSimpleWindow(r->x11_dpy, RootWindow(r->x11_dpy, 0), x, y, w, h, 0, BlackPixel(r->x11_dpy, 0),BlackPixel(r->x11_dpy, 0));
	XRectangle rect;
	XserverRegion region = XFixesCreateRegion(r->x11_dpy, &rect, 1);
	Atom window_type = XInternAtom(r->x11_dpy, "_NET_WM_WINDOW_TYPE", False);
	long value = XInternAtom(r->x11_dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	XChangeProperty(r->x11_dpy, (Window)dt->x11_win, window_type, XA_ATOM, 32, PropModeReplace, (unsigned char *) &value, 1);
    XFixesSetWindowShapeRegion(r->x11_dpy, (Window)dt->x11_win, ShapeInput, 0, 0, region);
	XFixesDestroyRegion(r->x11_dpy, region);
	XSetWindowAttributes attributes;
	attributes.override_redirect = True;
	XChangeWindowAttributes(r->x11_dpy,dt->x11_win,CWOverrideRedirect,&attributes);
	XMapWindow(r->x11_dpy, (Window)dt->x11_win);
	XFlush(r->x11_dpy);
	}
	if(!(r->flags &FL_GLX))dt->egl_surf = eglCreateWindowSurface(r->egl_display, r->egl_conf, dt->x11_win, window_attribute_list);
    }
    if((r->flags &FL_GLX)) glXMakeContextCurrent(r->x11_dpy, dt->x11_win, dt->x11_win, r->glx_ctx);
    else 
#elif defined ANDROID_JNI
    if(!dt->egl_surf)
    {
       dt->java_surf = (*r->jni.env)->CallStaticObjectMethod(r->jni.env, r->jni.cls, r->jni.create, x, y, w, h);
       jobject surf = (*r->jni.env)->CallStaticObjectMethod(r->jni.env, r->jni.cls, r->jni.get_surface, dt->java_surf);
       ANativeWindow *window = ANativeWindow_fromSurface(r->jni.env, surf);
       int format;
       eglGetConfigAttrib(r->egl_display, r->egl_conf, EGL_NATIVE_VISUAL_ID, &format);
       ANativeWindow_setBuffersGeometry(window, 0, 0, format);
       dt->egl_surf = eglCreateWindowSurface(r->egl_display, r->egl_conf, window, 0);
       //r->egl_drawable_surf = r->egl_fake_surf;
    }
#else
// useless
dt->egl_surf = EGL_NO_SURFACE;
#endif
return 0;
}
else if(cmd == VCMD_DT_CMD_DESTROY)
    {
#ifdef ANDROID_JNI
       if( dt->java_surf )
       	  (*r->jni.env)->CallStaticVoidMethod(r->jni.env, r->jni.cls, r->jni.destroy, dt->java_surf);
       dt->java_surf = 0;
#elif defined X11
        if(use_overlay)
        {
            XUnmapWindow(r->x11_dpy, dt->x11_win);
            XDestroyWindow(r->x11_dpy, dt->x11_win);
        }
        if(r->flags &FL_GLX)
        {
            glXMakeCurrent(r->x11_dpy, r->x11_fake_win, r->glx_ctx);
            dt->x11_win = 0;
        }
        else
#endif
       {
       eglMakeCurrent( r->egl_display, r->egl_fake_surf, r->egl_fake_surf, r->egl_ctx);
       eglDestroySurface(r->egl_display, dt->egl_surf);
       dt->egl_surf = 0;
        }

       return 0;
    }
else if(cmd == VCMD_DT_CMD_SET_RECT)
{
#ifdef ANDROID_JNI
       (*r->jni.env)->CallStaticVoidMethod(r->jni.env, r->jni.cls, r->jni.set_rect, dt->java_surf,x,y,w,h,drawable);
#elif defined X11
       if( use_overlay )
       {
       if( drawable )
       {
           XMapWindow( r->x11_dpy, dt->x11_win );
           XMoveResizeWindow(r->x11_dpy,dt->x11_win, x, y, w, h );
       }
       else XUnmapWindow( r->x11_dpy, dt->x11_win);
       }
#endif

       return 0;
}
    if( cmd != VCMD_DT_CMD_FLUSH )
    return -1;
	eglMakeCurrent(r->egl_display, dt->egl_surf, dt->egl_surf, r->egl_ctx);
    if(!dt->fb_id)
	glGenFramebuffersEXT(1,&dt->fb_id);

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, dt->fb_id );

    // use internal API here to get texture id
    if( handle != dt->res_id)
    {
        struct vrend_resource *res;

        res = vrend_resource_lookup(handle,0);//vrend_renderer_ctx_res_lookup(vrend_lookup_renderer_ctx(1), flush_buf[VCMD_FLUSH_HANDLE]);

        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                GL_TEXTURE_2D, res->id, 0);
        dt->res_id = handle;
    }

    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);

    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, 0);
    glBlitFramebuffer(x,y+h,w+x,y,x,y,w+x,h+y,GL_COLOR_BUFFER_BIT,GL_NEAREST);
#ifdef X11
    if((r->flags &FL_GLX)) glXSwapBuffers(r->x11_dpy, dt->x11_win);
    else
#endif
    eglSwapBuffers(r->egl_display, dt->egl_surf);
    return 0;
}

static int vtest_create_resource(struct vtest_renderer *r)
{
    uint32_t res_create_buf[VCMD_RES_CREATE_SIZE];
    struct virgl_renderer_resource_create_args args;
    int ret;

    ret = vtest_block_read(r, &res_create_buf, sizeof(res_create_buf));
    if (ret != sizeof(res_create_buf))
	return -1;
	
    args.handle = res_create_buf[VCMD_RES_CREATE_RES_HANDLE];
    args.target = res_create_buf[VCMD_RES_CREATE_TARGET];
    args.format = res_create_buf[VCMD_RES_CREATE_FORMAT];
    args.bind = res_create_buf[VCMD_RES_CREATE_BIND];

    args.width = res_create_buf[VCMD_RES_CREATE_WIDTH];
    args.height = res_create_buf[VCMD_RES_CREATE_HEIGHT];
    args.depth = res_create_buf[VCMD_RES_CREATE_DEPTH];
    args.array_size = res_create_buf[VCMD_RES_CREATE_ARRAY_SIZE];
    args.last_level = res_create_buf[VCMD_RES_CREATE_LAST_LEVEL];
    args.nr_samples = res_create_buf[VCMD_RES_CREATE_NR_SAMPLES];
    args.flags = 0;

    ret = virgl_renderer_resource_create(&args, NULL, 0);

    virgl_renderer_ctx_attach_resource(r->ctx_id, args.handle);
    return ret;
}

static int vtest_resource_unref(struct vtest_renderer *r)
{
    uint32_t res_unref_buf[VCMD_RES_UNREF_SIZE];
    int ret;
    uint32_t handle;

    ret = vtest_block_read(r, &res_unref_buf, sizeof(res_unref_buf));
    if (ret != sizeof(res_unref_buf))
      return -1;

    handle = res_unref_buf[VCMD_RES_UNREF_RES_HANDLE];
    virgl_renderer_ctx_attach_resource(r->ctx_id, handle);
    virgl_renderer_resource_unref(handle);
    return 0;
}

static int vtest_submit_cmd(struct vtest_renderer *r, uint32_t length_dw)
{
    uint32_t *cbuf;
    int ret;

    if (length_dw > UINT_MAX / 4)
       return -1;

    cbuf = malloc(length_dw * 4);
    if (!cbuf)
	return -1;

    ret = vtest_block_read(r, cbuf, length_dw * 4);
    if (ret != (int)length_dw * 4) {
       free(cbuf);
       return -1;
    }

    virgl_renderer_submit_cmd(cbuf, r->ctx_id, length_dw);

    free(cbuf);
    return 0;
}

#define DECODE_TRANSFER \
  do {							\
  handle = thdr_buf[VCMD_TRANSFER_RES_HANDLE];		\
  level = thdr_buf[VCMD_TRANSFER_LEVEL];		\
  stride = thdr_buf[VCMD_TRANSFER_STRIDE];		\
  layer_stride = thdr_buf[VCMD_TRANSFER_LAYER_STRIDE];	\
  box.x = thdr_buf[VCMD_TRANSFER_X];			\
  box.y = thdr_buf[VCMD_TRANSFER_Y];			\
  box.z = thdr_buf[VCMD_TRANSFER_Z];			\
  box.w = thdr_buf[VCMD_TRANSFER_WIDTH];		\
  box.h = thdr_buf[VCMD_TRANSFER_HEIGHT];		\
  box.d = thdr_buf[VCMD_TRANSFER_DEPTH];		\
  data_size = thdr_buf[VCMD_TRANSFER_DATA_SIZE];		\
  } while(0)


static int vtest_transfer_get(struct vtest_renderer *r, UNUSED uint32_t length_dw)
{
    uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
    int ret;
    int level;
    uint32_t stride, layer_stride, handle;
    struct virgl_box box;
    uint32_t data_size;
    void *ptr;
    struct iovec iovec;

    ret = vtest_block_read(r, thdr_buf, VCMD_TRANSFER_HDR_SIZE * 4);
    if (ret != VCMD_TRANSFER_HDR_SIZE * 4)
      return ret;

    DECODE_TRANSFER;

    ptr = malloc(data_size);
    if (!ptr)
      return -ENOMEM;

    iovec.iov_len = data_size;
    iovec.iov_base = ptr;
    ret = virgl_renderer_transfer_read_iov(handle,
				     r->ctx_id,
				     level,
				     stride,
				     layer_stride,
				     &box,
				     0,
				     &iovec, 1);
    if (ret)
      fprintf(stderr," transfer read failed %d\n", ret);
    ret = vtest_block_write(r, ptr, data_size);

    free(ptr);
    return ret < 0 ? ret : 0;
}

static int vtest_transfer_put(struct vtest_renderer *r, UNUSED uint32_t length_dw)
{
    uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
    int ret;
    int level;
    uint32_t stride, layer_stride, handle;
    struct virgl_box box;
    uint32_t data_size;
    void *ptr;
    struct iovec iovec;

    ret = vtest_block_read(r, thdr_buf, VCMD_TRANSFER_HDR_SIZE * 4);
    if (ret != VCMD_TRANSFER_HDR_SIZE * 4)
      return ret;

    DECODE_TRANSFER;

    ptr = malloc(data_size);
    if (!ptr)
      return -ENOMEM;

    ret = vtest_block_read(r, ptr, data_size);
    if (ret < 0)
      return ret;

    iovec.iov_len = data_size;
    iovec.iov_base = ptr;
    ret = virgl_renderer_transfer_write_iov(handle,
					    r->ctx_id,
					    level,
					    stride,
					    layer_stride,
					    &box,
					    0,
					    &iovec, 1);
    if (ret)
      fprintf(stderr," transfer write failed %d\n", ret);
    free(ptr);
    return 0;
}

static int vtest_resource_busy_wait(struct vtest_renderer *r)
{
  uint32_t bw_buf[VCMD_BUSY_WAIT_SIZE];
  int ret, fd;
  int flags;
  uint32_t hdr_buf[VTEST_HDR_SIZE];
  uint32_t reply_buf[1];
  bool busy = false;
  ret = vtest_block_read(r, &bw_buf, sizeof(bw_buf));
  if (ret != sizeof(bw_buf))
    return -1;

  /*  handle = bw_buf[VCMD_BUSY_WAIT_HANDLE]; unused as of now */
  flags = bw_buf[VCMD_BUSY_WAIT_FLAGS];

  if (flags == VCMD_BUSY_WAIT_FLAG_WAIT) {
    do {
       if (r->last_fence == (r->fence_id - 1))
          break;

       fd = virgl_renderer_get_poll_fd();
       if (fd != -1)
          vtest_wait_for_fd_read(r);
       virgl_renderer_poll();
    } while (1);
    busy = false;
  } else {
    busy = r->last_fence != (r->fence_id - 1);
  }

  hdr_buf[VTEST_CMD_LEN] = 1;
  hdr_buf[VTEST_CMD_ID] = VCMD_RESOURCE_BUSY_WAIT;
  reply_buf[0] = busy ? 1 : 0;

  ret = vtest_block_write(r, hdr_buf, sizeof(hdr_buf));
  if (ret < 0)
    return ret;

  ret = vtest_block_write(r, reply_buf, sizeof(reply_buf));
  if (ret < 0)
    return ret;

  return 0;
}

static int vtest_renderer_create_fence(struct vtest_renderer *r)
{
  virgl_renderer_create_fence(r->fence_id++, r->ctx_id);
  return 0;
}

static int vtest_poll()
{
  virgl_renderer_poll();
  return 0;
}




void *create_renderer(int in_fd, int ctx_id)
{
    struct vtest_renderer *r = calloc(1, sizeof(struct vtest_renderer));

    r->ctx_id = ctx_id;
    r->fence_id = 1;
    r->fd = in_fd;

//    vtest_glx_init(r);
    return r;
}


int run_renderer(int fd, int ctx_id)
{
    struct vtest_renderer *r = create_renderer(fd, ctx_id);
    const char ring =getenv( "VTEST_RING" );
    if(ring )
    {
       ring_setup( &r->ring, r->fd, ring );
       ring_server_handshake( &r->ring );
    }
    return renderer_loop(r);
}



int renderer_loop( void *d)
{
    int ret;
    uint32_t header[VTEST_HDR_SIZE];
    bool inited = false;
    struct vtest_renderer *r = d;
    EGLContext ctx = 0;
    EGLSurface surf = 0;
    //EGLDisplay disp = 0;
    //struct vtest_renderer *r = calloc(1, sizeof(struct vtest_renderer));
/*
    r->ctx_id = ctx_id;
    r->fence_id = 1;
    r->fd = in_fd;*/

//    glXMakeContextCurrent(r->x11_dpy, r->x11_fake_win, r->x11_fake_win, r->glx_ctx);


again:
    ret = vtest_wait_for_fd_read(r);
    if (ret < 0)
      goto fail;

    ret = vtest_block_read(r, &header, sizeof(header));
 //   pthread_mutex_lock(&mutex);
  //  if(ctx)
  //  eglMakeCurrent(disp, surf, surf, ctx);

    if (ret == 8) {
      if (!inited) {
	if (header[1] != VCMD_CREATE_RENDERER)
	  goto fail;
	ret = vtest_create_renderer(r, header[0]);
	inited = true;
      }
      vtest_poll();
      switch (header[1]) {
      case VCMD_GET_CAPS:
	ret = vtest_send_caps(r);
	break;
      case VCMD_RESOURCE_CREATE:
	ret = vtest_create_resource(r);
	break;
      case VCMD_RESOURCE_UNREF:
	ret = vtest_resource_unref(r);
	break;
      case VCMD_SUBMIT_CMD:
	ret = vtest_submit_cmd(r, header[0]);
	break;
      case VCMD_TRANSFER_GET:
	ret = vtest_transfer_get(r, header[0]);
	break;
      case VCMD_TRANSFER_PUT:
	ret = vtest_transfer_put(r, header[0]);
	break;
      case VCMD_RESOURCE_BUSY_WAIT:
        vtest_renderer_create_fence(r);
	ret = vtest_resource_busy_wait(r);
	break;
      case VCMD_GET_CAPS2:
	ret = vtest_send_caps2(r);
	break;
      case VCMD_DT_COMMAND:
	ret = vtest_dt_cmd(r);
	break;
      default:
	break;
      }
   //   ctx = eglGetCurrentContext();
    //  disp = eglGetCurrentDisplay();
   //   surf = EGL_NO_SURFACE;
  //    eglMakeCurrent( disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
//pthread_mutex_unlock(&mutex);
      if (ret < 0) {
	goto fail;
      }
      goto again;
    }
    if (ret <= 0) {
      goto fail;
    }
fail:
    fprintf(stderr, "socket failed - closing renderer\n");

    vtest_destroy_renderer(r);
    close(r->fd);
    free(r);

    return 0;
}


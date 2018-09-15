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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include "virglrenderer.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/uio.h>
#include "vtest.h"
#include "vtest_protocol.h"
#include "util/u_debug.h"
#include "ring.h"
#include <epoxy/egl.h>
#include "vrend_renderer.h"
#include "vrend_object.h"
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
static int ctx_id = 1;
static int fence_id = 1;

static int last_fence;
static void vtest_write_fence(UNUSED void *cookie, uint32_t fence_id_in)
{
  last_fence = fence_id_in;
}

static EGLSurface drawable_surf;
static EGLContext drawable_ctx;
static Drawable drawable_win;
static EGLConfig drawable_conf;
static Display *dpy;
struct vtest_egl {
   EGLDisplay egl_display;
   EGLConfig egl_conf;
   EGLContext egl_ctx;
   bool have_mesa_drm_image;
   bool have_mesa_dma_buf_img_export;
};

static bool virgl_egl_has_extension_in_string(const char *haystack, const char *needle)
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

static struct vtest_egl *vtest_egl_init(bool surfaceless, bool gles)
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
   struct vtest_egl *d;

   d = malloc(sizeof(struct vtest_egl));
   if (!d)
      return NULL;

   if (gles)
      conf_att[3] = EGL_OPENGL_ES_BIT;

   if (surfaceless) {
      conf_att[1] = EGL_PBUFFER_BIT;
   }
   if( !dpy) dpy = XOpenDisplay(NULL);
   const char *client_extensions = eglQueryString (NULL, EGL_EXTENSIONS);
   d->egl_display = eglGetDisplay(dpy);

   if (!d->egl_display)
      goto fail;

   b = eglInitialize(d->egl_display, &major, &minor);
   if (!b)
      goto fail;

   extension_list = eglQueryString(d->egl_display, EGL_EXTENSIONS);
#ifdef VIRGL_EGL_DEBUG
   fprintf(stderr, "EGL major/minor: %d.%d\n", major, minor);
   fprintf(stderr, "EGL version: %s\n",
           eglQueryString(d->egl_display, EGL_VERSION));
   fprintf(stderr, "EGL vendor: %s\n",
           eglQueryString(d->egl_display, EGL_VENDOR));
   fprintf(stderr, "EGL extensions: %s\n", extension_list);
#endif
   /* require surfaceless context */
   if (!virgl_egl_has_extension_in_string(extension_list, "EGL_KHR_surfaceless_context"))
      goto fail;

   d->have_mesa_drm_image = false;
   d->have_mesa_dma_buf_img_export = false;
   if (virgl_egl_has_extension_in_string(extension_list, "EGL_MESA_drm_image"))
      d->have_mesa_drm_image = true;

   if (virgl_egl_has_extension_in_string(extension_list, "EGL_MESA_image_dma_buf_export"))
      d->have_mesa_dma_buf_img_export = true;

   if (gles)
      api = EGL_OPENGL_ES_API;
   else
      api = EGL_OPENGL_API;
   b = eglBindAPI(api);
   if (!b)
      goto fail;

   b = eglChooseConfig(d->egl_display, conf_att, &d->egl_conf,
                       1, &n);
   drawable_conf = d->egl_conf;

   if (!b || n != 1)
      goto fail;

   d->egl_ctx = eglCreateContext(d->egl_display,
                                 d->egl_conf,
                                 EGL_NO_CONTEXT,
                                 ctx_att);
   if (!d->egl_ctx)
      goto fail;


   eglMakeCurrent(d->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                  d->egl_ctx);
   return d;
 fail:
   free(d);
   return NULL;
}

static void vtest_egl_destroy(struct vtest_egl *d)
{
   eglMakeCurrent(d->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                  EGL_NO_CONTEXT);
   eglDestroyContext(d->egl_display, d->egl_ctx);
   eglTerminate(d->egl_display);
   free(d);
}

static virgl_renderer_gl_context vtest_egl_create_context(void *cookie, int scanout_idx, struct virgl_renderer_gl_ctx_param *param)
{
   struct vtest_egl *ve = cookie;
   EGLContext eglctx;
   EGLint ctx_att[] = {
      EGL_CONTEXT_CLIENT_VERSION, param->major_ver,
      EGL_CONTEXT_MINOR_VERSION_KHR, param->minor_ver,
      EGL_NONE
   };
   eglctx = eglCreateContext(ve->egl_display,
                             ve->egl_conf,
                             param->shared ? eglGetCurrentContext() : EGL_NO_CONTEXT,
                             ctx_att);

   printf("create_context %d %d %d %d %x\n", scanout_idx, param->shared, param->major_ver, param->minor_ver, eglctx);

   return (virgl_renderer_gl_context)eglctx;
}

static void vtest_egl_destroy_context(void *cookie, virgl_renderer_gl_context ctx)
{
   struct vtest_egl *ve = cookie;
   EGLContext eglctx = (EGLContext)ctx;

   printf("destroy_context %x\n", ctx);

   eglDestroyContext(ve->egl_display, eglctx);
}

static int vtest_egl_make_context_current(void *cookie, int scanout_idx, virgl_renderer_gl_context ctx)
{
   struct vtest_egl *ve = cookie;
   EGLContext eglctx = (EGLContext)ctx;
//   printf("make_current %d %x\n", scanout_idx, ctx);
   if( ctx == drawable_ctx )
   {
//       printf("flush drawable make current %d\n", drawable_win );
   return eglMakeCurrent(ve->egl_display, drawable_surf , drawable_surf,
                         eglctx);

   }

   return eglMakeCurrent(ve->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         eglctx);
}

struct virgl_renderer_callbacks vtest_cbs = {
    .version = 1,
    .write_fence = vtest_write_fence,
   .create_gl_context = vtest_egl_create_context,
   .destroy_gl_context = vtest_egl_destroy_context,
   .make_current = vtest_egl_make_context_current,
};

struct vtest_renderer {
  int in_fd;
  int out_fd;
  int use_ring;
  ring_t ring;
};

struct vtest_renderer renderer;

/*struct virgl_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};*/

int vtest_wait_for_fd_read(int fd)
{
   fd_set read_fds;
   int ret;

   if( !renderer.ring.size )
   {
       static int ring;
       if( !ring )
       {
           if(getenv( "VTEST_RING" ) )
           {
               ring_setup( &renderer.ring, fd );
               ring_server_handshake( &renderer.ring );
               return 0;
           }
           ring = 1;
       }
   }
   else return 0;

   FD_ZERO(&read_fds);
   FD_SET(fd, &read_fds);

   ret = select(fd + 1, &read_fds, NULL, NULL, NULL);
   if (ret < 0)
      return ret;

   if (FD_ISSET(fd, &read_fds)) {
      return 0;
   }
   return -1;
}

static int vtest_block_write(int fd, void *buf, int size)
{
   void *ptr = buf;
   int left;
   int ret;

   if( renderer.ring.size )
      return ring_write( &renderer.ring, buf, size );

   left = size;
   do {
      ret = write(fd, ptr, left);
      if (ret < 0)
         return -errno;
      left -= ret;
      ptr += ret;
   } while (left);
   return size;
}

int vtest_block_read(int fd, void *buf, int size)
{
   void *ptr = buf;
   int left;
   int ret;
   static int savefd = -1;


   if( renderer.ring.size )
       return ring_read(&renderer.ring, buf, size);

   left = size;
   do {
      ret = read(fd, ptr, left);
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

int vtest_create_renderer(int in_fd, int out_fd, uint32_t length)
{
    char *vtestname;
    int ret;
    int ctx = 0;

    renderer.in_fd = in_fd;
    renderer.out_fd = out_fd;

//    if (getenv("VTEST_USE_GLX"))
//       ctx = VIRGL_RENDERER_USE_GLX;

    if (getenv("VTEST_USE_EGL_SURFACELESS")) {
        if (ctx & VIRGL_RENDERER_USE_GLX) {
            fprintf(stderr, "Cannot use surfaceless with GLX.\n");
            return -1;
        }
        ctx |= VIRGL_RENDERER_USE_SURFACELESS;
    }

    if (getenv("VTEST_USE_GLES")) {
        if (ctx & VIRGL_RENDERER_USE_GLX) {
            fprintf(stderr, "Cannot use GLES with GLX.\n");
            return -1;
        }
        ctx |= VIRGL_RENDERER_USE_GLES;
    }

    ret = virgl_renderer_init(vtest_egl_init(0,!!(ctx & VIRGL_RENDERER_USE_GLES)),
                              ctx | VIRGL_RENDERER_THREAD_SYNC, &vtest_cbs);
    if (ret) {
      fprintf(stderr, "failed to initialise renderer.\n");
      return -1;
    }

    vtestname = calloc(1, length + 1);
    if (!vtestname)
      return -1;

    ret = vtest_block_read(renderer.in_fd, vtestname, length);
    if (ret != (int)length) {
       ret = -1;
       goto end;
    }

    ret = virgl_renderer_context_create(ctx_id, strlen(vtestname), vtestname);

end:
    free(vtestname);
    return ret;
}

void vtest_destroy_renderer(void)
{
  virgl_renderer_context_destroy(ctx_id);
  virgl_renderer_cleanup(&renderer);
  renderer.in_fd = -1;
  renderer.out_fd = -1;
}

int vtest_send_caps2(void)
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
    ret = vtest_block_write(renderer.out_fd, hdr_buf, 8);
    if (ret < 0)
	goto end;
    vtest_block_write(renderer.out_fd, caps_buf, max_size);
    if (ret < 0)
	goto end;

end:
    free(caps_buf);
    return 0;
}

int vtest_send_caps(void)
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
    ret = vtest_block_write(renderer.out_fd, hdr_buf, 8);
    if (ret < 0)
       goto end;
    vtest_block_write(renderer.out_fd, caps_buf, max_size);
    if (ret < 0)
       goto end;

end:
    free(caps_buf);
    return 0;
}


int vtest_flush_frontbuffer(void)
{
    uint32_t flush_buf[VCMD_FLUSH_SIZE];
    struct virgl_renderer_resource_create_args args;
    int ret;
    uint32_t w_x, w_y, x, y, w, h;
    Drawable drawable;
    static int use_overlay;

    ret = vtest_block_read(renderer.in_fd, &flush_buf, sizeof(flush_buf));
    if (ret != sizeof(flush_buf))
	return -1;

//    printf("flush drawable %x\n", flush_buf[VCMD_FLUSH_DRAWABLE]);
    EGLContext ctx = eglGetCurrentContext();
    drawable = flush_buf[VCMD_FLUSH_DRAWABLE];
    x = flush_buf[VCMD_FLUSH_X];
    y = flush_buf[VCMD_FLUSH_Y];
    w = flush_buf[VCMD_FLUSH_WIDTH];
    h = flush_buf[VCMD_FLUSH_HEIGHT];
    w_x = flush_buf[VCMD_FLUSH_W_X];
    w_y = flush_buf[VCMD_FLUSH_W_Y];

    if( ctx != drawable_ctx )
    {
        static EGLint const window_attribute_list[] = {
            EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
            EGL_NONE,
        };
        drawable_ctx = ctx;
	drawable_win = flush_buf[VCMD_FLUSH_DRAWABLE];
	if( getenv("VTEST_OVERLAY") )
	{
	use_overlay = 1;
	drawable_win = XCreateSimpleWindow(dpy, RootWindow(dpy, 0), w_x, w_y, w, h, 0, BlackPixel(dpy, 0),BlackPixel(dpy, 0));
	XRectangle rect;
	XserverRegion region = XFixesCreateRegion(dpy, &rect, 1);
	Atom window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	long value = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	XChangeProperty(dpy, (Window)drawable_win, window_type, XA_ATOM, 32, PropModeReplace, (unsigned char *) &value, 1);
	XFixesSetWindowShapeRegion(dpy, (Window)drawable_win, ShapeInput, 0, 0, region);
	XFixesDestroyRegion(dpy, region);
	XSetWindowAttributes attributes;
	attributes.override_redirect = True;
	XChangeWindowAttributes(dpy,drawable,CWOverrideRedirect,&attributes);
	XMapWindow(dpy, (Window)drawable_win);
	XFlush(dpy);
	}
	drawable_surf = eglCreateWindowSurface(eglGetCurrentDisplay(), drawable_conf, drawable_win, window_attribute_list);
	eglMakeCurrent(eglGetCurrentDisplay(), drawable_surf, drawable_surf, ctx);
	
    }
//    struct vrend_resource *res = vrend_resource_lookup(flush_buf[1], 0);
    int buf;
//	eglMakeCurrent(eglGetCurrentDisplay(), drawable_surf, drawable_surf, ctx);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &buf);
//    printf("res:%d\n",buf);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, 0);
/*    glClearColor(255,0,0,255);
    glClear(GL_COLOR_BUFFER_BIT);*/
    
    glBlitFramebuffer(x,y+h,w+x,y,x,y,w+x,h+y,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    if( use_overlay )
      XMoveWindow(dpy,drawable_win,w_x,w_y);
    eglSwapBuffers(eglGetCurrentDisplay(), drawable_surf);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, buf);

	
    return 0;
}

int vtest_create_resource(void)
{
    uint32_t res_create_buf[VCMD_RES_CREATE_SIZE];
    struct virgl_renderer_resource_create_args args;
    int ret;

    ret = vtest_block_read(renderer.in_fd, &res_create_buf, sizeof(res_create_buf));
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

    virgl_renderer_ctx_attach_resource(ctx_id, args.handle);
    return ret;
}

int vtest_resource_unref(void)
{
    uint32_t res_unref_buf[VCMD_RES_UNREF_SIZE];
    int ret;
    uint32_t handle;

    ret = vtest_block_read(renderer.in_fd, &res_unref_buf, sizeof(res_unref_buf));
    if (ret != sizeof(res_unref_buf))
      return -1;

    handle = res_unref_buf[VCMD_RES_UNREF_RES_HANDLE];
    virgl_renderer_ctx_attach_resource(ctx_id, handle);
    virgl_renderer_resource_unref(handle);
    return 0;
}

int vtest_submit_cmd(uint32_t length_dw)
{
    uint32_t *cbuf;
    int ret;

    if (length_dw > UINT_MAX / 4)
       return -1;

    cbuf = malloc(length_dw * 4);
    if (!cbuf)
	return -1;

    ret = vtest_block_read(renderer.in_fd, cbuf, length_dw * 4);
    if (ret != (int)length_dw * 4) {
       free(cbuf);
       return -1;
    }

    virgl_renderer_submit_cmd(cbuf, ctx_id, length_dw);

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


int vtest_transfer_get(UNUSED uint32_t length_dw)
{
    uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
    int ret;
    int level;
    uint32_t stride, layer_stride, handle;
    struct virgl_box box;
    uint32_t data_size;
    void *ptr;
    struct iovec iovec;

    ret = vtest_block_read(renderer.in_fd, thdr_buf, VCMD_TRANSFER_HDR_SIZE * 4);
    if (ret != VCMD_TRANSFER_HDR_SIZE * 4)
      return ret;

    DECODE_TRANSFER;

    ptr = malloc(data_size);
    if (!ptr)
      return -ENOMEM;

    iovec.iov_len = data_size;
    iovec.iov_base = ptr;
    ret = virgl_renderer_transfer_read_iov(handle,
				     ctx_id,
				     level,
				     stride,
				     layer_stride,
				     &box,
				     0,
				     &iovec, 1);
    if (ret)
      fprintf(stderr," transfer read failed %d\n", ret);
    ret = vtest_block_write(renderer.out_fd, ptr, data_size);

    free(ptr);
    return ret < 0 ? ret : 0;
}

int vtest_transfer_put(UNUSED uint32_t length_dw)
{
    uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
    int ret;
    int level;
    uint32_t stride, layer_stride, handle;
    struct virgl_box box;
    uint32_t data_size;
    void *ptr;
    struct iovec iovec;

    ret = vtest_block_read(renderer.in_fd, thdr_buf, VCMD_TRANSFER_HDR_SIZE * 4);
    if (ret != VCMD_TRANSFER_HDR_SIZE * 4)
      return ret;

    DECODE_TRANSFER;

    ptr = malloc(data_size);
    if (!ptr)
      return -ENOMEM;

    ret = vtest_block_read(renderer.in_fd, ptr, data_size);
    if (ret < 0)
      return ret;

    iovec.iov_len = data_size;
    iovec.iov_base = ptr;
    ret = virgl_renderer_transfer_write_iov(handle,
					    ctx_id,
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

int vtest_resource_busy_wait(void)
{
  uint32_t bw_buf[VCMD_BUSY_WAIT_SIZE];
  int ret, fd;
  int flags;
  uint32_t hdr_buf[VTEST_HDR_SIZE];
  uint32_t reply_buf[1];
  bool busy = false;
  ret = vtest_block_read(renderer.in_fd, &bw_buf, sizeof(bw_buf));
  if (ret != sizeof(bw_buf))
    return -1;

  /*  handle = bw_buf[VCMD_BUSY_WAIT_HANDLE]; unused as of now */
  flags = bw_buf[VCMD_BUSY_WAIT_FLAGS];

  if (flags == VCMD_BUSY_WAIT_FLAG_WAIT) {
    do {
       if (last_fence == (fence_id - 1))
          break;

       fd = virgl_renderer_get_poll_fd();
       if (fd != -1)
          vtest_wait_for_fd_read(fd);
       virgl_renderer_poll();
    } while (1);
    busy = false;
  } else {
    busy = last_fence != (fence_id - 1);
  }

  hdr_buf[VTEST_CMD_LEN] = 1;
  hdr_buf[VTEST_CMD_ID] = VCMD_RESOURCE_BUSY_WAIT;
  reply_buf[0] = busy ? 1 : 0;

  ret = vtest_block_write(renderer.out_fd, hdr_buf, sizeof(hdr_buf));
  if (ret < 0)
    return ret;

  ret = vtest_block_write(renderer.out_fd, reply_buf, sizeof(reply_buf));
  if (ret < 0)
    return ret;

  return 0;
}

int vtest_renderer_create_fence(void)
{
  virgl_renderer_create_fence(fence_id++, ctx_id);
  return 0;
}

int vtest_poll(void)
{
  virgl_renderer_poll();
  return 0;
}

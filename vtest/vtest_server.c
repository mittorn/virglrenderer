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
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>

#include "util.h"
#include "util/u_memory.h"
#include "vtest.h"
#include "vtest_protocol.h"
#include "virglrenderer.h"
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

struct vtest_client
{
   int in_fd;
   int out_fd;
   struct vtest_input input;

   struct vtest_context *context;
};

struct vtest_server
{
   const char *socket_name;
   int socket;
   const char *read_file;

   const char *render_device;

   bool do_fork;
   bool loop;

   bool use_glx;
   bool use_egl_surfaceless;
   bool use_gles;

   int ctx_flags;

   struct vtest_client client;
};

struct vtest_server server = {
   .socket_name = VTEST_DEFAULT_SOCKET_NAME,
   .socket = -1,

   .read_file = NULL,

   .render_device = 0,
   .do_fork = true,
   .loop = true,

   .ctx_flags = 0,

   .client = {
      .in_fd = -1,
      .out_fd = -1,
      .input = { { -1 }, NULL },
      .context = NULL,
   },
};

static void vtest_server_getenv(void);
static void vtest_server_parse_args(int argc, char **argv);
static void vtest_server_set_signal_child(void);
static void vtest_server_set_signal_segv(void);
static void vtest_server_open_read_file(void);
static void vtest_server_open_socket(void);
static void vtest_server_run_renderer(struct vtest_client *client);
static void vtest_server_wait_for_socket_accept(void);
static void vtest_server_tidy_fds(void);
static void vtest_server_close_socket(void);
static int vtest_client_dispatch_commands(struct vtest_client *client);


int main(int argc, char **argv)
{
#ifdef __AFL_LOOP
while (__AFL_LOOP(1000)) {
#endif

   vtest_server_getenv();
   vtest_server_parse_args(argc, argv);

   if (server.read_file != NULL) {
      vtest_server_open_read_file();
      goto start;
   }

   if (server.do_fork) {
      vtest_server_set_signal_child();
   }

   vtest_server_open_socket();
restart:
   vtest_server_wait_for_socket_accept();

start:
   if (server.do_fork) {
      /* fork a renderer process */
      if (fork() == 0) {
         vtest_server_set_signal_segv();
         vtest_server_run_renderer(&server.client);
         exit(0);
      }
   } else {
      vtest_server_set_signal_segv();
      vtest_server_run_renderer(&server.client);
   }

   vtest_server_tidy_fds();

   if (server.loop) {
      goto restart;
   }

   vtest_server_close_socket();

#ifdef __AFL_LOOP
}
#endif
}

#define OPT_NO_FORK 'f'
#define OPT_NO_LOOP_OR_FORK 'l'
#define OPT_USE_GLX 'x'
#define OPT_USE_EGL_SURFACELESS 's'
#define OPT_USE_GLES 'e'
#define OPT_RENDERNODE 'r'

static void vtest_server_parse_args(int argc, char **argv)
{
   int ret;

   static struct option long_options[] = {
      {"no-fork",             no_argument, NULL, OPT_NO_FORK},
      {"no-loop-or-fork",     no_argument, NULL, OPT_NO_LOOP_OR_FORK},
      {"use-glx",             no_argument, NULL, OPT_USE_GLX},
      {"use-egl-surfaceless", no_argument, NULL, OPT_USE_EGL_SURFACELESS},
      {"use-gles",            no_argument, NULL, OPT_USE_GLES},
      {"rendernode",          required_argument, NULL, OPT_RENDERNODE},
      {0, 0, 0, 0}
   };

   /* getopt_long stores the option index here. */
   int option_index = 0;

   do {
      ret = getopt_long(argc, argv, "", long_options, &option_index);

      switch (ret) {
      case -1:
         break;
      case OPT_NO_FORK:
         server.do_fork = false;
         break;
      case OPT_NO_LOOP_OR_FORK:
         server.do_fork = false;
         server.loop = false;
         break;
      case OPT_USE_GLX:
         server.use_glx = true;
         break;
      case OPT_USE_EGL_SURFACELESS:
         server.use_egl_surfaceless = true;
         break;
      case OPT_USE_GLES:
         server.use_gles = true;
         break;
      case OPT_RENDERNODE:
         server.render_device = optarg;
         break;
      default:
         printf("Usage: %s [--no-fork] [--no-loop-or-fork] [--use-glx] "
                "[--use-egl-surfaceless] [--use-gles] [--rendernode <dev>]"
                " [file]\n", argv[0]);
         exit(EXIT_FAILURE);
         break;
      }

   } while (ret >= 0);

   if (optind < argc) {
      server.read_file = argv[optind];
      server.loop = false;
      server.do_fork = false;
   }

   server.ctx_flags = VIRGL_RENDERER_USE_EGL;
   if (server.use_glx) {
      if (server.use_egl_surfaceless || server.use_gles) {
         fprintf(stderr, "Cannot use surfaceless or GLES with GLX.\n");
         exit(EXIT_FAILURE);
      }
      server.ctx_flags = VIRGL_RENDERER_USE_GLX;
   } else {
      if (server.use_egl_surfaceless)
         server.ctx_flags |= VIRGL_RENDERER_USE_SURFACELESS;
      if (server.use_gles)
         server.ctx_flags |= VIRGL_RENDERER_USE_GLES;
   }
}

static void vtest_server_getenv(void)
{
   server.use_glx = getenv("VTEST_USE_GLX") != NULL;
   server.use_egl_surfaceless = getenv("VTEST_USE_EGL_SURFACELESS") != NULL;
   server.use_gles = getenv("VTEST_USE_GLES") != NULL;
   server.render_device = getenv("VTEST_RENDERNODE");
}

static void handler(int sig, siginfo_t *si, void *unused)
{
   (void)sig; (void)si, (void)unused;

   printf("SIGSEGV!\n");
   exit(EXIT_FAILURE);
}

static void vtest_server_set_signal_child(void)
{
   struct sigaction sa;
   int ret;

   memset(&sa, 0, sizeof(sa));
   sigemptyset(&sa.sa_mask);
   sa.sa_handler = SIG_IGN;
   sa.sa_flags = 0;

   ret = sigaction(SIGCHLD, &sa, NULL);
   if (ret == -1) {
      perror("Failed to set SIGCHLD");
      exit(1);
   }
}

static void vtest_server_set_signal_segv(void)
{
   struct sigaction sa;
   int ret;

   memset(&sa, 0, sizeof(sa));
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_SIGINFO;
   sa.sa_sigaction = handler;

   ret = sigaction(SIGSEGV, &sa, NULL);
   if (ret == -1) {
      perror("Failed to set SIGSEGV");
      exit(1);
   }
}

static void vtest_server_open_read_file(void)
{
   struct vtest_client *client = &server.client;
   int ret;

   ret = open(server.read_file, O_RDONLY);
   if (ret == -1) {
      perror(NULL);
      exit(1);
   }
   client->in_fd = ret;
   client->input.data.fd = client->in_fd;
   client->input.read = vtest_block_read;

   ret = open("/dev/null", O_WRONLY);
   if (ret == -1) {
      perror(NULL);
      exit(1);
   }
   client->out_fd = ret;
}

static void vtest_server_open_socket(void)
{
   struct sockaddr_un un;

   server.socket = socket(PF_UNIX, SOCK_STREAM, 0);
   if (server.socket < 0) {
      goto err;
   }

   memset(&un, 0, sizeof(un));
   un.sun_family = AF_UNIX;

   snprintf(un.sun_path, sizeof(un.sun_path), "%s", server.socket_name);

   unlink(un.sun_path);

   if (bind(server.socket, (struct sockaddr *)&un, sizeof(un)) < 0) {
      goto err;
   }

   if (listen(server.socket, 1) < 0){
      goto err;
   }

   return;

err:
   perror("Failed to setup socket.");
   exit(1);
}

static void vtest_server_wait_for_socket_accept(void)
{
   struct vtest_client *client = &server.client;
   fd_set read_fds;
   int new_fd;
   int ret;
   FD_ZERO(&read_fds);
   FD_SET(server.socket, &read_fds);

   ret = select(server.socket + 1, &read_fds, NULL, NULL, NULL);
   if (ret < 0) {
      perror("Failed to select on socket!");
      exit(1);
   }

   if (!FD_ISSET(server.socket, &read_fds)) {
      perror("Odd state in fd_set.");
      exit(1);
   }

   new_fd = accept(server.socket, NULL, NULL);
   if (new_fd < 0) {
      perror("Failed to accept socket.");
      exit(1);
   }

   client->in_fd = new_fd;
   client->out_fd = new_fd;
   client->input.data.fd = client->in_fd;
   client->input.read = vtest_block_read;
}

static void vtest_server_run_renderer(struct vtest_client *client)
{
   int err, ret;

   ret = vtest_init_renderer(server.ctx_flags, server.render_device);
   if (ret) {
      return;
   }

   do {
      ret = vtest_wait_for_fd_read(client->in_fd);
      if (ret < 0) {
         err = 1;
         break;
      }

      err = vtest_client_dispatch_commands(client);
      if (err) {
         break;
      }
   } while (1);

   fprintf(stderr, "socket failed (%d) - closing renderer\n", err);

   if (client->context) {
      vtest_destroy_context(client->context);
      client->context = NULL;
   }

   vtest_cleanup_renderer();
}

typedef int (*vtest_cmd_fptr_t)(uint32_t);

static const vtest_cmd_fptr_t vtest_commands[] = {
   NULL /* CMD ids starts at 1 */,
   vtest_send_caps,
   vtest_create_resource,
   vtest_resource_unref,
   vtest_transfer_get,
   vtest_transfer_put,
   vtest_submit_cmd,
   vtest_resource_busy_wait,
   NULL, /* VCMD_CREATE_RENDERER is a specific case */
   vtest_send_caps2,
   vtest_ping_protocol_version,
   vtest_protocol_version,
   vtest_create_resource2,
   vtest_transfer_get2,
   vtest_transfer_put2,
};

static int vtest_client_dispatch_commands(struct vtest_client *client)
{
   int ret;
   uint32_t header[VTEST_HDR_SIZE];

   ret = client->input.read(&client->input, &header, sizeof(header));
   if (ret < 0 || (size_t)ret < sizeof(header)) {
      return 2;
   }

   if (!client->context) {
      /* The first command MUST be VCMD_CREATE_RENDERER */
      if (header[1] != VCMD_CREATE_RENDERER) {
         return 3;
      }

      ret = vtest_create_context(&client->input, client->out_fd,
                                 header[0], &client->context);
      if (ret < 0) {
         return 4;
      }
      printf("%s: client context created.\n", __func__);
      vtest_set_current_context(client->context);
      vtest_poll();

      return 0;
   }

   vtest_poll();
   if (header[1] <= 0 || header[1] >= ARRAY_SIZE(vtest_commands)) {
      return 5;
   }

   if (vtest_commands[header[1]] == NULL) {
      return 6;
   }

   ret = vtest_commands[header[1]](header[0]);
   if (ret < 0) {
      return 7;
   }

   /* GL draws are fenced, while possible fence creations are too */
   if (header[1] == VCMD_SUBMIT_CMD || header[1] == VCMD_RESOURCE_CREATE ||
       header[1] == VCMD_RESOURCE_CREATE2)
      vtest_renderer_create_fence();

   return 0;
}

static void vtest_server_tidy_fds(void)
{
   struct vtest_client *client = &server.client;

   // out_fd will be closed by the in_fd clause if they are the same.
   if (client->out_fd == client->in_fd) {
      client->out_fd = -1;
   }

   if (client->in_fd != -1) {
      close(client->in_fd);
      client->in_fd = -1;
      client->input.read = NULL;
   }

   if (client->out_fd != -1) {
      close(client->out_fd);
      client->out_fd = -1;
   }
}

static void vtest_server_close_socket(void)
{
   if (server.socket != -1) {
      close(server.socket);
      server.socket = -1;
   }
}

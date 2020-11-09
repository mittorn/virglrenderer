/**************************************************************************
 *
 * Copyright (C) 2019 Chromium.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "virgl_util.h"

#include <errno.h>
#ifdef HAVE_EVENTFD_H
#include <sys/eventfd.h>
#endif
#include <unistd.h>

#include "util/u_pointer.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if ENABLE_TRACING == TRACE_WITH_PERFETTO
#include <vperfetto-min.h>
#endif

unsigned hash_func_u32(void *key)
{
   intptr_t ip = pointer_to_intptr(key);
   return (unsigned)(ip & 0xffffffff);
}

int compare_func(void *key1, void *key2)
{
   if (key1 < key2)
      return -1;
   if (key1 > key2)
      return 1;
   else
      return 0;
}

bool has_eventfd(void)
{
#ifdef HAVE_EVENTFD_H
   return true;
#else
   return false;
#endif
}

int create_eventfd(unsigned int initval)
{
#ifdef HAVE_EVENTFD_H
   return eventfd(initval, EFD_CLOEXEC | EFD_NONBLOCK);
#else
   return -1;
#endif
}

int write_eventfd(int fd, uint64_t val)
{
   const char *buf = (const char *)&val;
   size_t count = sizeof(val);
   ssize_t ret = 0;

   while (count) {
      ret = write(fd, buf, count);
      if (ret < 0) {
         if (errno == EINTR)
            continue;
         break;
      }
      count -= ret;
      buf += ret;
   }

   return count ? -1 : 0;
}

void flush_eventfd(int fd)
{
    ssize_t len;
    uint64_t value;
    do {
       len = read(fd, &value, sizeof(value));
    } while ((len == -1 && errno == EINTR) || len == sizeof(value));
}

#if ENABLE_TRACING == TRACE_WITH_PERFETTO
void trace_init(void)
{
   struct vperfetto_min_config config = {
      .init_flags = VPERFETTO_INIT_FLAG_USE_SYSTEM_BACKEND,
            .filename = NULL,
            .shmem_size_hint_kb = 32 * 1024,
   };

   vperfetto_min_startTracing(&config);
}

char *trace_begin(const char* format, ...)
{
   char buffer[1024];
   va_list args;
   va_start (args, format);
   vsnprintf (buffer, sizeof(buffer), format, args);
   va_end (args);
   vperfetto_min_beginTrackEvent_VMM(buffer);
   return (void *)1;
}

void trace_end(char **dummy)
{
   (void)dummy;
   vperfetto_min_endTrackEvent_VMM();
}
#endif

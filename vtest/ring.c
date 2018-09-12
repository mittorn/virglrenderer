/* ring.c
 * inspired by slaeshjag's Glouija
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "ring.h"

#define RING_SIZE (1024 * 4096 * 8)
#define ALIGN4(x) (((x) & 3) ? (((x) & ~3) + 4) : (x))

#define SPINLOCK_YIELD 2000
#define SPINLOCK_COUNT 10000
#define SPINLOCK_LOOP(cond) \
    for (int i = 0; i < SPINLOCK_COUNT && (cond); i++) { \
        if (i % SPINLOCK_YIELD == 0) sched_yield(); \
    }
#define SYNC_WHILE(cond) \
    while ((cond)) { \
        SPINLOCK_LOOP(cond); \
        while ((cond)) ring_wait(ring); \
    }

void ring_wait(ring_t *ring) {
    char c[32];
    int res;
    if (__sync_lock_test_and_set(ring->waiting, 1)) {
//        fprintf(stderr, "warning: both sides waiting?\n");
        ring_post(ring);
        return;
    }
    while( ( res = read(ring->fd, c, 32) ) == 32 )
    if( res < 0 )
        exit(0);
}

void ring_post(ring_t *ring) {
    // TODO: check if waiting first, or use nonblocking IO?
    if (__sync_bool_compare_and_swap(ring->waiting, 1, 0)) {
        char c = 0;
        write(ring->fd, &c, 1);
    }
}

#if 0
void ring_wait_read(ring_t *ring) {
    SYNC_WHILE(*ring->dir != ring->me);
    SYNC_WHILE(*ring->mark == *ring->write && *ring->wrap == 0);
}
#endif

void ring_sync_write(ring_t *ring)
{
    while( (*ring->write_out& (ring->size - 1)) != (*ring->read_out& (ring->size - 1)) )
        sched_yield();
}


int ring_write_partial(ring_t *ring, const void *buf, size_t bufsize) {
    uint32_t writemark = *ring->write_out& (ring->size - 1);
    uint32_t readmark = *ring->read_out& (ring->size - 1);
    uint32_t freespace = (readmark - writemark - 1) & (ring->size - 1);

    if(bufsize > freespace)
        bufsize = freespace;

    memcpy( ring->buf_out + writemark, buf, bufsize );

    *ring->write_out = (*ring->write_out + bufsize) & (ring->size - 1);

    if( writemark == readmark )
        ring_post(ring);

    return bufsize;
}

int ring_write(ring_t *ring, const void *buf, size_t bufsize) {
    int bufs = bufsize;

    while( bufsize )
    {
        int ret = ring_write_partial(ring, buf, bufsize);

        bufsize -= ret;
        buf += ret;

        if(bufsize)
           sched_yield();
    }

    return bufs;

}


int ring_read_partial(ring_t *ring, void *buf, size_t bufsize) {

    uint32_t writemark = *ring->write_in& (ring->size - 1);
    uint32_t readmark = *ring->read_in& (ring->size - 1);
    uint32_t readsize = (writemark - readmark) & (ring->size - 1);

    if( readsize == 0 )
    {
        ring_wait(ring);
        writemark = *ring->write_in& (ring->size - 1);
        readmark = *ring->read_in& (ring->size - 1);
        readsize = (writemark - readmark) & (ring->size - 1);
    }

    if(bufsize > readsize)
       bufsize = readsize;

    if( readsize > bufsize )
       readsize = bufsize;

    memcpy( buf, ring->buf_in + readmark, readsize );

    *ring->read_in += readsize;
    *ring->read_in &= ring->size - 1;

    return readsize;
}

int ring_read(ring_t *ring, void *buf, size_t bufsize) {
    int bufs = bufsize;

    while( bufsize )
    {
        int ret = ring_read_partial(ring, buf, bufsize);

        bufsize -= ret;
        buf += ret;

        if(bufsize)
           sched_yield();
    }

    return bufs;

}


static const size_t cache_line_size() {
    size_t size;
#ifdef __linux__
    size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
#elif __APPLE__
    size_t ret_size = sizeof(size_t);
    sysctlbyname("hw.cachelinesize", &size, &ret_size, 0, 0);
#endif
    if (size == 0) size = 64;
    return size;
}

static void ring_set_pointers(ring_t *ring, void *header, size_t cache_line, int server) {
    int i = 0;
#define next_line (header + cache_line * i++)
if( server ) {
    ring->read_in    = next_line;
    ring->write_in   = next_line;
    ring->read_out    = next_line;
    ring->write_out   = next_line;
}
else
{
    ring->read_out    = next_line;
    ring->write_out   = next_line;
    ring->read_in    = next_line;
    ring->write_in   = next_line;
}
    ring->waiting = next_line;
#undef next_line
}

void ring_setup(ring_t *ring, int sync_fd) {
    ring->fd = sync_fd;
}

static void *ring_map(ring_t *ring, int fd, uint32_t header_size, uint32_t ring_size, uint32_t line_size, int server) {
    void *base = mmap(NULL, header_size + ring_size * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        return MAP_FAILED;
    }
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_SHARED | MAP_FIXED;
    void *header   = mmap(base,                                   header_size, prot, flags, fd, 0);
    void *buf_in      = mmap(base + header_size,                  ring_size,   prot, flags, fd, header_size);
    void *overflow_in = mmap(base + header_size + ring_size,      ring_size,   prot, flags, fd, header_size);
    void *buf_out      = mmap(base + header_size + ring_size * 2, ring_size,   prot, flags, fd, header_size + ring_size);
    void *overflow_out = mmap(base + header_size + ring_size * 3, ring_size,   prot, flags, fd, header_size + ring_size);

    if (header == MAP_FAILED || overflow_in == MAP_FAILED || overflow_out == MAP_FAILED ) {
        return MAP_FAILED;
    }
    ring_set_pointers(ring, header, line_size, server);
    if( server ) // swap read/write regions
        ring->buf_in = buf_in, ring->buf_out = buf_out;
    else
        ring->buf_in = buf_out, ring->buf_out = buf_in;

    ring->size = ring_size;
    return base;
}

#define write_check(fd, ptr, size) do { if (write(fd, ptr, size) < size) { printf("remote write failed\n"); return -1; }} while (0)
#define read_check(fd, ptr, size) do { if (read(fd, ptr, size) < size) { printf("remote read failed\n"); return -1; }} while (0)

uint32_t MAGIC = 0xBEEFCAFE;

int ring_server_handshake(ring_t *ring) {
//    ring->me = 0;
    // check magic number
    // endianness is explicitly ignored for this part
    // so endian disparity will fail handshake for now
    uint32_t magic = MAGIC;
    write_check(ring->fd, &magic, 4);
    read_check(ring->fd, &magic, 4);
    if (magic != MAGIC) {
        fprintf(stderr, "server: magic mismatch\n");
        return -1;
    }

    // always use server's cache line size
    uint32_t line_size = htonl(cache_line_size());
    write_check(ring->fd, &line_size, 4);
    line_size = ntohl(line_size);

    // always use server's page size
    uint32_t page_size = htonl(getpagesize());
    write_check(ring->fd, &page_size, 4);
    page_size = ntohl(page_size);

    int mask = page_size - 1;
    int header_size = ((line_size * 5) + mask) & ~mask;
    int ring_size = (RING_SIZE + mask) & ~mask;

    // get shm name
    char name[33] = {0};
    read_check(ring->fd, name, 32);
    if (strlen(name) == 0) {
        fprintf(stderr, "server: failed to get shm name\n");
        return -1;
    }

    // set up shm
    int fd = shm_open(name, O_RDWR, 0700);
    void *addr = ring_map(ring, fd, header_size, ring_size, line_size, 1);
    if (addr == MAP_FAILED) {
        shm_unlink(name);
        fprintf(stderr, "server: mapping failed from shm '%s'\n", name);
        return -1;
    }
    shm_unlink(name);
    ring->size = RING_SIZE;
    return 0;
}

int ring_client_handshake(ring_t *ring, char *title) {
//    ring->me = 1;
    // check magic number
    uint32_t magic = 0;
    read_check(ring->fd, &magic, 4);
    if (magic != MAGIC) {
        fprintf(stderr, "client: magic mismatch\n");
        return -1;
    }
    write_check(ring->fd, &magic, 4);

    // negotiate cache line size
    uint32_t line_size = 0;
    read_check(ring->fd, &line_size, 4);
    line_size = ntohl(line_size);

    // negotiate page size
    uint32_t page_size = 0;
    read_check(ring->fd, &page_size, 4);
    page_size = ntohl(page_size);

    int mask = page_size - 1;
    int header_size = ((line_size * 5) + mask) & ~mask;
    int ring_size = (RING_SIZE + mask) & ~mask;

    // set up shm
    int i = 0;
    int fd = -1;
    char buf[32] = {0};
    while (fd < 0) {
        snprintf(buf, 32, "/%s.%d", title, i++);
        fd = shm_open(buf, O_RDWR | O_CREAT, 0700);
        if (i > 65535) {
            memset(buf, 0, 32);
            // write a null name so the other side knows we failed
            write_check(ring->fd, buf, 32);
            fprintf(stderr, "client: failed to shm_open() 65535 times, giving up.\n");
            return -1;
        }
    }
    ftruncate(fd, header_size + ring_size * 2);

    // map our memory
    void *addr = ring_map(ring, fd, header_size, ring_size, line_size, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "client: mmap failed\n");
        return -1;
    }
    memset(addr, 0, header_size + ring_size * 4);

    // write shm name
    write_check(ring->fd, buf, 32);
    return 0;
}

#ifndef RING_H
#define RING_H

#include <stdint.h>

typedef struct ring_s {
    volatile uint32_t *read_in, *write_in, *wait_in;
    volatile uint32_t *read_out, *write_out, *wait_out;
    volatile uint32_t *waiting;
    // file descriptor for setup/sync
    uint32_t fd;
    void *buf_in, *buf_out;
    size_t size;
    char shm_prefix[256];
} ring_t;

int ring_read_partial(ring_t *ring, void *buf, size_t reqsize);
int ring_write_partial(ring_t *ring, const void *buf, size_t reqsize);
int ring_read(ring_t *ring, void *buf, size_t reqsize);
int ring_write(ring_t *ring, const void *buf, size_t reqsize);
void ring_wait(ring_t *ring);
void ring_sync_write(ring_t *ring);
void ring_post(ring_t *ring);
void ring_setup(ring_t *ring, int sync_fd, const char *shm_prefix);
int ring_server_handshake(ring_t *ring);
int ring_client_handshake(ring_t *ring, char *title);

#endif

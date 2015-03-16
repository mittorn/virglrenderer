#ifndef VTEST_H
#define VTEST_H

#include <errno.h>
int vtest_create_renderer(int fd);

int vtest_send_caps(void);

int vtest_create_resource(void);
int vtest_resource_unref(void);
int vtest_submit_cmd(uint32_t length_dw);

int vtest_transfer_get(uint32_t length_dw);
int vtest_transfer_put(uint32_t length_dw);

int vtest_block_read(int fd, void *buf, int size);

int vtest_resource_busy_wait(void);
int vtest_renderer_create_fence(void);
int vtest_poll(void);
#endif


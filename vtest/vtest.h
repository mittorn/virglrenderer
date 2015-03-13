#ifndef VTEST_H
#define VTEST_H

int vtest_create_renderer(int fd);

int vtest_send_caps(void);

int vtest_create_resource(void);
int vtest_resource_unref(void);
#endif


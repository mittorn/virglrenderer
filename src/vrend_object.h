#ifndef VREND_OBJECT_H
#define VREND_OBJECT_H

#include "virgl_protocol.h"

void vrend_object_init_resource_table(void);
void vrend_object_fini_resource_table(void);

struct grend_context;

struct util_hash_table *vrend_object_init_ctx_table(void);
void vrend_object_fini_ctx_table(struct util_hash_table *ctx_hash);

void vrend_object_remove(struct util_hash_table *handle_hash, uint32_t handle, enum virgl_object_type obj);
void *vrend_object_lookup(struct util_hash_table *handle_hash, uint32_t handle, enum virgl_object_type obj);
uint32_t vrend_object_insert(struct util_hash_table *handle_hash, void *data, uint32_t length, uint32_t handle, enum virgl_object_type type);

/* resources are global */
void *vrend_resource_insert(void *data, uint32_t length, uint32_t handle);
void vrend_resource_remove(uint32_t handle);
void *vrend_resource_lookup(uint32_t handle, uint32_t ctx_id);

void vrend_object_set_destroy_callback(int type, void (*cb)(void *));

void vrend_object_dumb_ctx_table(struct util_hash_table *ctx_hash);
void graw_renderer_dump_resource(void *data);
#endif

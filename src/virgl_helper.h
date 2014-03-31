#ifndef VIRGL_HELPER_H
#define VIRGL_HELPER_H

#define GREND_EXPORT  __attribute__((visibility("default")))

/* add helpers for local renderers - not used by remote viewers */
#define VIRGL_HELPER_Y_0_TOP (1 << 0)
GREND_EXPORT void virgl_helper_scanout_info(int idx,
                                            uint32_t tex_id,
                                            uint32_t flags,
                                            int x, int y,
                                            uint32_t width, uint32_t height);
GREND_EXPORT void virgl_helper_flush_scanout(int idx,
                                             int x, int y,
                                             uint32_t width, uint32_t height);

#endif

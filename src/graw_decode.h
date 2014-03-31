#ifndef GRAW_DECODE_H
#define GRAW_DECODE_H

struct graw_decoder_state {
   uint32_t *buf;
   uint32_t buf_total;
   uint32_t buf_offset;
};


void graw_decode_transfer(uint32_t *data, uint32_t ndw);
void graw_decode_get_transfer(uint32_t *data, uint32_t ndw);
#endif


#ifndef GRAW_IOV_H
#define GRAW_IOV_H

/* stolen from qemu for now until later integration */
struct virgl_iovec {
   void *iov_base;
   size_t iov_len;
};

typedef void (*IOCallback)(void *cookie, unsigned int doff, void *src, int len);

size_t graw_iov_size(const struct virgl_iovec *iov, const unsigned int iov_cnt);
size_t graw_iov_from_buf(const struct virgl_iovec *iov, unsigned int iov_cnt,
                    size_t offset, const void *buf, size_t bytes);
size_t graw_iov_to_buf(const struct virgl_iovec *iov, const unsigned int iov_cnt,
                  size_t offset, void *buf, size_t bytes);

size_t graw_iov_to_buf_cb(const struct virgl_iovec *iov, const unsigned int iov_cnt,
                          size_t offset, size_t bytes, IOCallback iocb, void *cookie);

#endif

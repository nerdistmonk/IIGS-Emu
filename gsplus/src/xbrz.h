#ifndef XBRZ_H
#define XBRZ_H

#include <stdint.h>

/* 2x upscale of an ARGB8888 frame. src is w x h pixels with src_stride pixels
 * per row; dst is contiguous and must hold (2*w)*(2*h) pixels. */
void xbrz_scale2x(const uint32_t *src, int src_stride, uint32_t *dst, int w, int h);

#endif /* XBRZ_H */

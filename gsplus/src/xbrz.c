/* 2x corner-blend upscaler, ported from ProSystemSE's SDL/xbrz.cpp (GPL-2.0
 * or later). Only the active 2x path is kept; the unused colour-distance
 * helpers from the original were dropped. */

#include "xbrz.h"

static uint32_t
alpha_blend2(uint32_t p1, uint32_t p2)
{
	/* 50/50 average per channel without overflowing into the next channel. */
	return (((p1 ^ p2) & 0xFEFEFEFEu) >> 1) + (p1 & p2);
}

void
xbrz_scale2x(const uint32_t *src, int src_stride, uint32_t *dst, int w, int h)
{
	int	x, y, xp, xn, yp, yn;
	int	outp = w * 2;
	uint32_t	B, D, E, F, H;
	uint32_t	e0, e1, e2, e3;
	uint32_t	*out;

	for(y = 0; y < h; y++) {
		yp = (y > 0) ? (y - 1) : 0;
		yn = (y + 1 < h) ? (y + 1) : (h - 1);
		for(x = 0; x < w; x++) {
			xp = (x > 0) ? (x - 1) : 0;
			xn = (x + 1 < w) ? (x + 1) : (w - 1);

			B = src[yp * src_stride + x];	/* above */
			D = src[y * src_stride + xp];	/* left */
			E = src[y * src_stride + x];	/* centre */
			F = src[y * src_stride + xn];	/* right */
			H = src[yn * src_stride + x];	/* below */

			e0 = E;
			e1 = E;
			e2 = E;
			e3 = E;
			if((D == B) && (D != H) && (B != F)) {
				e0 = alpha_blend2(E, D);
			}
			if((B == F) && (B != D) && (F != H)) {
				e1 = alpha_blend2(E, F);
			}
			if((H == D) && (H != F) && (D != B)) {
				e2 = alpha_blend2(E, D);
			}
			if((F == H) && (F != B) && (H != D)) {
				e3 = alpha_blend2(E, F);
			}

			out = dst + (y * 2) * outp + (x * 2);
			out[0] = e0;
			out[1] = e1;
			out[outp] = e2;
			out[outp + 1] = e3;
		}
	}
}

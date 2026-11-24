/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*      This code is covered by the GNU GPL v3                        */
/**********************************************************************/

/* Minimal, dependency-free PNG writer.
 *
 * GSplus deliberately links no PNG/zlib library for screenshots, so this file
 * emits a complete, valid PNG entirely by hand. The one trick is that PNG's
 * IDAT data is a zlib stream, which normally implies DEFLATE compression -- but
 * DEFLATE permits *stored* (uncompressed) blocks (BTYPE=00). We use only those,
 * so no compressor is needed. The output is larger than a compressed PNG but is
 * read correctly by every PNG decoder. We still must compute the two checksums
 * the format requires: CRC-32 over each chunk and Adler-32 over the zlib data.
 *
 * write_png_rgba() takes 8-bit RGBA, top-to-bottom, tightly packed (no padding).
 * write_png_gray1() takes 1-bit packed pixels (bit 1 = white, bit 0 = black),
 * rows padded to whole bytes, top-to-bottom.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defc.h"

/* ---- CRC-32 (PNG chunk checksum, IEEE 802.3 polynomial 0xEDB88320) ------- */

static unsigned long g_png_crc_table[256];
static int g_png_crc_table_done = 0;

static void
png_crc_table_init(void)
{
	unsigned long c;
	int n, k;

	for(n = 0; n < 256; n++) {
		c = (unsigned long)n;
		for(k = 0; k < 8; k++) {
			c = (c & 1) ? (0xedb88320UL ^ (c >> 1)) : (c >> 1);
		}
		g_png_crc_table[n] = c;
	}
	g_png_crc_table_done = 1;
}

/* Continue a running CRC-32 over `buf`; pass 0xffffffff as the seed. */
static unsigned long
png_crc_update(unsigned long c, const unsigned char *buf, size_t len)
{
	size_t i;

	if(!g_png_crc_table_done) {
		png_crc_table_init();
	}
	for(i = 0; i < len; i++) {
		c = g_png_crc_table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
	}
	return c;
}

/* ---- chunk emission ------------------------------------------------------ */

static void
png_put_be32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)(v);
}

/* Write one PNG chunk: length, 4-char type, data, then CRC over type+data. */
static int
png_write_chunk(FILE *f, const char *type, const unsigned char *data,
		size_t len)
{
	unsigned char hdr[8];
	unsigned char crcbuf[4];
	unsigned long crc;

	png_put_be32(hdr, (unsigned long)len);
	memcpy(hdr + 4, type, 4);
	if(fwrite(hdr, 1, 8, f) != 8) {
		return -1;
	}
	if(len && fwrite(data, 1, len, f) != len) {
		return -1;
	}
	/* CRC covers the type field and the data, but not the length. */
	crc = png_crc_update(0xffffffffUL, (const unsigned char *)type, 4);
	crc = png_crc_update(crc, data, len) ^ 0xffffffffUL;
	png_put_be32(crcbuf, crc);
	if(fwrite(crcbuf, 1, 4, f) != 4) {
		return -1;
	}
	return 0;
}

/* ---- zlib wrapper using only stored (uncompressed) DEFLATE blocks -------- */

/* Build a zlib stream around `raw`/`raw_len` made entirely of stored blocks.
 * Returns a malloc'd buffer (caller frees) and sets *out_len, or NULL on OOM. */
static unsigned char *
png_zlib_store(const unsigned char *raw, size_t raw_len, size_t *out_len)
{
	const size_t MAXBLK = 65535;		/* stored-block length is 16-bit */
	size_t nblocks, cap, pos, off;
	unsigned char *out;
	unsigned long adler, s1, s2;
	size_t i;

	nblocks = (raw_len + MAXBLK - 1) / MAXBLK;
	if(nblocks == 0) {
		nblocks = 1;			/* one empty final block */
	}
	/* 2 zlib header + per block (1 BFINAL/BTYPE + 2 LEN + 2 NLEN) + data
	 * + 4 Adler-32. */
	cap = 2 + nblocks * 5 + raw_len + 4;
	out = malloc(cap);
	if(!out) {
		return NULL;
	}

	pos = 0;
	out[pos++] = 0x78;			/* CMF: deflate, 32K window */
	out[pos++] = 0x01;			/* FLG: (0x7801 % 31 == 0) */

	off = 0;
	do {
		size_t blk = raw_len - off;
		int final;
		if(blk > MAXBLK) {
			blk = MAXBLK;
		}
		final = (off + blk >= raw_len);
		out[pos++] = final ? 1 : 0;	/* BFINAL bit, BTYPE=00 (stored) */
		out[pos++] = (unsigned char)(blk & 0xff);
		out[pos++] = (unsigned char)((blk >> 8) & 0xff);
		out[pos++] = (unsigned char)(~blk & 0xff);	/* NLEN = ~LEN */
		out[pos++] = (unsigned char)((~blk >> 8) & 0xff);
		if(blk) {
			memcpy(out + pos, raw + off, blk);
			pos += blk;
		}
		off += blk;
	} while(off < raw_len);

	/* Adler-32 of the uncompressed data, stored big-endian. */
	s1 = 1;
	s2 = 0;
	for(i = 0; i < raw_len; i++) {
		s1 = (s1 + raw[i]) % 65521;
		s2 = (s2 + s1) % 65521;
	}
	adler = (s2 << 16) | s1;
	png_put_be32(out + pos, adler);
	pos += 4;

	*out_len = pos;
	return out;
}

/* ---- shared emitter ------------------------------------------------------ */

/* Wrap `raw` (filtered scanlines, each row prefixed by a filter byte) in a
 * stored zlib stream and write it as a PNG at `path`. `depth` and `ctype`
 * go straight into IHDR. */
static int
png_emit(const char *path, const unsigned char *raw, size_t rawlen,
	int w, int h, unsigned char depth, unsigned char ctype)
{
	static const unsigned char sig[8] =
		{ 137, 80, 78, 71, 13, 10, 26, 10 };
	unsigned char ihdr[13];
	unsigned char *zlib;
	size_t zlen;
	FILE	*f;
	int	rc;

	zlib = png_zlib_store(raw, rawlen, &zlen);
	if(!zlib) {
		return -1;
	}

	f = fopen(path, "wb");
	if(!f) {
		free(zlib);
		return -1;
	}

	rc = 0;
	if(fwrite(sig, 1, 8, f) != 8) {
		rc = -1;
	}

	/* IHDR: width, height, bit depth, colour type, default
	 * compression/filter/interlace. */
	png_put_be32(ihdr + 0, (unsigned long)w);
	png_put_be32(ihdr + 4, (unsigned long)h);
	ihdr[8] = depth;
	ihdr[9] = ctype;
	ihdr[10] = 0;
	ihdr[11] = 0;
	ihdr[12] = 0;

	if(!rc) { rc = png_write_chunk(f, "IHDR", ihdr, sizeof(ihdr)); }
	if(!rc) { rc = png_write_chunk(f, "IDAT", zlib, zlen); }
	if(!rc) { rc = png_write_chunk(f, "IEND", NULL, 0); }

	free(zlib);
	if(fclose(f) != 0) {
		rc = -1;
	}
	return rc;
}

/* ---- public entry points ------------------------------------------------- */

/* Write `rgba` (w*h pixels, 8-bit RGBA, top-to-bottom, no row padding) to a
 * PNG at `path`. Returns 0 on success, non-zero on failure. */
int
write_png_rgba(const char *path, const unsigned char *rgba, int w, int h)
{
	unsigned char *raw;
	size_t rawlen, rowbytes;
	int	y, rc;

	if(!rgba || w <= 0 || h <= 0) {
		return -1;
	}

	/* Filtered raw scanlines: each row is a 0x00 filter byte (None) followed
	 * by w*4 RGBA bytes. */
	rowbytes = (size_t)w * 4;
	rawlen = (size_t)h * (rowbytes + 1);
	raw = malloc(rawlen);
	if(!raw) {
		return -1;
	}
	for(y = 0; y < h; y++) {
		unsigned char *dst = raw + (size_t)y * (rowbytes + 1);
		*dst++ = 0x00;			/* filter type: None */
		memcpy(dst, rgba + (size_t)y * rowbytes, rowbytes);
	}

	rc = png_emit(path, raw, rawlen, w, h, 8, 6);
	free(raw);
	return rc;
}

/* Write `bits` (w*h pixels, 1 bit each, packed MSB-first, rows padded to
 * whole bytes) to a greyscale PNG at `path`. Bit 1 is white, bit 0 is black.
 * Returns 0 on success, non-zero on failure. */
int
write_png_gray1(const char *path, const unsigned char *bits, int w, int h)
{
	unsigned char *raw;
	size_t rawlen, rowbytes;
	int	y, rc;

	if(!bits || w <= 0 || h <= 0) {
		return -1;
	}

	rowbytes = ((size_t)w + 7) / 8;
	rawlen = (size_t)h * (rowbytes + 1);
	raw = malloc(rawlen);
	if(!raw) {
		return -1;
	}
	for(y = 0; y < h; y++) {
		unsigned char *dst = raw + (size_t)y * (rowbytes + 1);
		*dst++ = 0x00;			/* filter type: None */
		memcpy(dst, bits + (size_t)y * rowbytes, rowbytes);
	}

	rc = png_emit(path, raw, rawlen, w, h, 1, 0);
	free(raw);
	return rc;
}

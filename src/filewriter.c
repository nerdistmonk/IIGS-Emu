/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*      This code is covered by the GNU GPL v3                        */
/**********************************************************************/

/* FileWriter virtual printer.
 *
 * Emulates an Apple ImageWriter II on a serial port. The guest's bytes are
 * parsed as ImageWriter commands and rendered onto a page at 160 x 144 dots
 * per inch. The page keeps one byte per dot: the ribbon colors that printed
 * it, 0 for paper. Each finished page is written as <stem>.png (a pixel
 * printout, 1-bit when it is all black, otherwise RGB for color ribbons) and
 * <stem>.txt (text only, Mac Roman, CR line endings; written only when the
 * page has text, so bitmap-only pages have just the .png).
 *
 * A page ends on FF, when paper runs past the page length, when the port
 * closes, and at emulator exit. A page with no ink writes no files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "defc.h"
#include "filewriter_font.h"

extern char *g_cfg_ssdir;

#define FW_CANVAS_DPI_X	160		/* canvas pixels per inch, across */
#define FW_CANVAS_DPI_Y	144		/* canvas rows per inch = paper steps */
#define FW_PAGE_W	1360		/* 8.5 in of canvas */
#define FW_PRINT_W	8.0		/* printable width, inches */
#define FW_MAX_LEN	9999		/* longest page, paper steps */
#define FW_TXT_COLS	160
#define FW_TXT_ROWS	10000
#define FW_DOT_PX	2		/* a 1/72 in dot row spans 2 canvas rows */
#define FW_SPILL_ROWS	(FW_DOT_PX * 9)	/* a glyph's height: the most ink that can cross the page end */
#define FW_CANVAS_ROWS	(FW_MAX_LEN + FW_SPILL_ROWS)
#define FW_DEF_DPI	96		/* ESC E, Elite: 12 cpi */
#define FW_DEF_LEN	(11 * FW_CANVAS_DPI_Y)
#define FW_DEF_SPACING	24		/* 6 lpi */
#define FW_IDLE_FRAMES	60		/* frames without a byte before saving, about 1 s */
#define FW_JOB_END_FRAMES	300	/* frames without a byte that end a job, about 5 s */
#define FW_EPS		1e-9
#define FW_MAX_TABS	32		/* tab stops that can be set */
#define FW_MAX_COLS	160		/* widest column number: 160 dpi (ESC P) */
#define FW_CUSTOM_NARROW	8	/* widest custom character in dots, ESC - */
#define FW_CUSTOM_WIDE	16		/* widest custom character in dots, ESC + */

#define FW_REG_A_LF_FULL	0x20	/* LF when the line is full */
#define FW_REG_A_PRINT_CMDS	0x40	/* CR/LF/FF act; clear = only CR acts */
#define FW_REG_A_LF_AFTER_CR	0x80
#define FW_REG_B_IGNORE_8TH	0x20

/* Ribbon colors, one bit each. A dot holds the bits of the ink that printed
 * it; overprinting sets more than one. */
#define FW_INK_Y	0x01
#define FW_INK_M	0x02
#define FW_INK_C	0x04
#define FW_INK_K	0x08

/* ESC K digit to ribbon ink, Table 8-6. Digits above 6 select nothing. */
static const unsigned char fw_ribbon_ink[7] = {
	FW_INK_K,
	FW_INK_Y,
	FW_INK_M,
	FW_INK_C,
	FW_INK_Y | FW_INK_M,
	FW_INK_Y | FW_INK_C,
	FW_INK_M | FW_INK_C,
};

enum {
	FW_IDLE,
	FW_ESC,
	FW_ARGS,
	FW_UNDERSCORE,
	FW_DATA,
	FW_LIST,
	FW_PICK,	/* after ESC ' or ESC ., the key follows */
	FW_PICK_KEY,	/* key read, waiting to see whether ESC S follows */
	FW_PICK_ESC,	/* key and ESC read, waiting for S */
	FW_LOAD_KEY,	/* after ESC I: a key, or CTRL-D to end */
	FW_LOAD_WIDTH,
	FW_LOAD_DATA
};

typedef struct {
	int		open;
	unsigned char	*canvas;	/* FW_PAGE_W x FW_CANVAS_ROWS, 0 = paper, else FW_INK_* bits */
	unsigned char	*txt;		/* FW_TXT_ROWS x FW_TXT_COLS, 0 = blank */
	int		txt_maxrow;	/* last transcript row used, -1 if none */
	int		dirty;		/* page has ink or text */
	char		stem[64];	/* file stem once the page has been saved, "" before */
	int		unsaved;	/* page changed since it was last saved */
	int		idle_frames;	/* frames since the last byte */
	int		job_idle_frames;	/* frames since the last byte, for job end */
	int		page_len;	/* paper steps */
	int		vy;		/* paper position, canvas rows */
	int		trow;		/* transcript row of the current line */
	double		hx;		/* head position, inches from paper left */
	double		margin;		/* left margin, inches */
	int		dpi;		/* pitch, dots per inch */
	int		spacing;	/* line feed step, paper steps */
	int		reverse;	/* reverse line feed */
	int		bold;		/* ESC ! boldface: glyph columns are printed twice */
	int		dw;		/* CTRL-N double-width: each dot column is printed twice */
	int		ul;		/* ESC X underline: the bottom dot row is printed */
	int		nlq;		/* ESC a 2 NLQ: text is struck twice */
	int		mt;		/* ESC & MouseText: codes 0x40-0x5F print MouseText glyphs */
	int		cfont;		/* 0 standard, 1 custom (ESC ' or ESC .), 2 high-ASCII custom (ESC *) */
	int		cmax;		/* custom character width limit in dots: 8 (ESC -) or 16 (ESC +) */
	unsigned char	cw[256];	/* custom character width by key, 0 = none loaded */
	unsigned short	cdat[256][FW_CUSTOM_WIDE];	/* custom character columns, bit r = dot row r */
	unsigned char	pick_key;	/* key given after ESC ', held until the next byte is known */
	int		lkey;		/* load: key of the character being loaded */
	int		lwidth;		/* load: width in dots of that character */
	int		lcol;		/* load: columns read so far */
	int		lbot;		/* load: lowercase width code, so the column uses the bottom 8 wires */
	int		lstore;		/* load: the character is kept, not skipped */
	double		tshift;		/* inches of double-width growth on this line, for the transcript */
	unsigned char	ink;		/* ribbon color that new dots take, FW_INK_* bits */
	int		cr_insert;	/* insert CR before LF, FF, CTRL-_ n */
	unsigned char	reg_a;
	unsigned char	reg_b;
	int		prev_cr;	/* the previous byte was CR */
	int		last_cr;	/* the current byte is CR */
	int		us_ok;		/* CTRL-_ came right after CR */
	int		state;
	int		cmd;		/* ESC command letter */
	const char	*fmt;		/* argument layout, see fw_cmd_format() */
	int		fidx;		/* current field of fmt */
	int		fdigits;	/* digits read in the current numeric field */
	int		fval;		/* value of the current numeric field */
	int		fv[2];		/* completed fields */
	int		data_left;	/* graphics data bytes still expected */
	double		tab_in[FW_MAX_TABS];	/* tab stops, inches from paper left, ascending */
	int		tab_n;
	int		lst[FW_MAX_COLS];	/* in-range columns of an ESC ( or ESC ) list */
	int		lst_n;
	int		lst_last;	/* last list number, -1 before the first */
	int		lst_digit;	/* the current list number has a digit, not only spaces */
	int		lst_bad;	/* the current list is malformed; skip to its period */
} FwPort;

static FwPort	g_fw[2];
static int	g_fw_seq = 0;
static pid_t	g_fw_owner_pid;
static int	g_fw_atexit_set = 0;

/* ---- output files ------------------------------------------------------- */

static void
fw_build_path(char *buf, size_t buflen, const char *stem, const char *ext)
{
	const char *dir = g_cfg_ssdir;
	const char *sep = "";
	size_t len;

	if(!dir) {
		dir = "";
	}
	len = strlen(dir);
	if(len && dir[len - 1] != '/') {
		sep = "/";
	}
	snprintf(buf, buflen, "%s%s%s%s", dir, sep, stem, ext);
}

/* Pick a "FileWriter-<timestamp>-<seq>" stem that doesn't already exist. */
static void
fw_new_stem(char *stem, size_t n)
{
	char path[1024];
	char ts[32];
	time_t now;
	struct tm *tmv;
	FILE *f;

	now = time(NULL);
	tmv = localtime(&now);
	if(tmv) {
		strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", tmv);
	} else {
		snprintf(ts, sizeof(ts), "%lld", (long long)now);
	}
	do {
		g_fw_seq++;
		snprintf(stem, n, "FileWriter-%s-%d", ts, g_fw_seq);
		fw_build_path(path, sizeof(path), stem, ".png");
		f = fopen(path, "rb");
		if(f) {
			fclose(f);
		}
	} while(f);
}

static int
fw_row_end(const FwPort *p, int r)
{
	const unsigned char *row = p->txt + (size_t)r * FW_TXT_COLS;
	int c;

	for(c = FW_TXT_COLS - 1; c >= 0; c--) {
		if(row[c]) {
			return c;
		}
	}
	return -1;
}

/* Rows run top to bottom, each ends with CR, trailing spaces are dropped. */
static void
fw_write_txt(const FwPort *p, const char *path)
{
	FILE *f;
	int last_row = -1;
	int r, c, end;

	f = fopen(path, "wb");
	if(!f) {
		printf("FileWriter: cannot write %s\n", path);
		return;
	}
	for(r = p->txt_maxrow; r >= 0 && last_row < 0; r--) {
		if(fw_row_end(p, r) >= 0) {
			last_row = r;
		}
	}
	for(r = 0; r <= last_row; r++) {
		const unsigned char *row = p->txt + (size_t)r * FW_TXT_COLS;

		end = fw_row_end(p, r);
		for(c = 0; c <= end; c++) {
			fputc(row[c] ? row[c] : ' ', f);
		}
		fputc('\r', f);
	}
	fclose(f);
}

/* RGB of one canvas byte: each ink in `bits` multiplies white paper, so
 * overprinted inks darken. */
static void
fw_ink_rgb(unsigned char bits, unsigned char rgb[3])
{
	static const unsigned char ink[4][3] = {
		{ 255, 242, 0 },	/* yellow */
		{ 236, 0, 140 },	/* magenta */
		{ 0, 174, 239 },	/* cyan */
		{ 0, 0, 0 },		/* black */
	};
	int i, c;

	rgb[0] = rgb[1] = rgb[2] = 255;
	for(i = 0; i < 4; i++) {
		if(bits & (1 << i)) {
			for(c = 0; c < 3; c++) {
				rgb[c] = (unsigned char)(rgb[c] * ink[i][c] / 255);
			}
		}
	}
}

/* Save the page as a PNG. A page with only black ink is written 1-bit
 * (black on white); a page with any color ink is written as RGB. */
static int
fw_save_png(const FwPort *p, const char *path)
{
	size_t stride = FW_PAGE_W / 8;
	unsigned char *bits;
	int y, x, rc, color = 0;

	for(y = 0; y < p->page_len && !color; y++) {
		const unsigned char *row = p->canvas + (size_t)y * FW_PAGE_W;

		for(x = 0; x < FW_PAGE_W; x++) {
			if(row[x] & (FW_INK_Y | FW_INK_M | FW_INK_C)) {
				color = 1;
				break;
			}
		}
	}
	if(color) {
		unsigned char pal[16][3];
		unsigned char *rgba = malloc((size_t)FW_PAGE_W * p->page_len * 4);

		if(!rgba) {
			return -1;
		}
		for(x = 0; x < 16; x++) {
			fw_ink_rgb((unsigned char)x, pal[x]);
		}
		for(y = 0; y < p->page_len; y++) {
			const unsigned char *row = p->canvas + (size_t)y * FW_PAGE_W;
			unsigned char *out = rgba + (size_t)y * FW_PAGE_W * 4;

			for(x = 0; x < FW_PAGE_W; x++) {
				const unsigned char *c = pal[row[x] & 0x0f];

				out[0] = c[0];
				out[1] = c[1];
				out[2] = c[2];
				out[3] = 255;
				out += 4;
			}
		}
		rc = write_png_rgba(path, rgba, FW_PAGE_W, p->page_len);
		free(rgba);
		return rc;
	}

	bits = calloc(stride, (size_t)p->page_len);
	if(!bits) {
		return -1;
	}
	for(y = 0; y < p->page_len; y++) {
		const unsigned char *row = p->canvas + (size_t)y * FW_PAGE_W;
		unsigned char *out = bits + (size_t)y * stride;

		for(x = 0; x < FW_PAGE_W; x++) {
			if(!row[x]) {
				out[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
			}
		}
	}
	rc = write_png_gray1(path, bits, FW_PAGE_W, p->page_len);
	free(bits);
	return rc;
}

/* Save the page under its stem. The first save picks the stem; later saves
 * of the same page overwrite the same files. */
static void
fw_write_page(FwPort *p)
{
	char path[1024];

	if(!p->stem[0]) {
		fw_new_stem(p->stem, sizeof(p->stem));
	}

	fw_build_path(path, sizeof(path), p->stem, ".png");
	if(fw_save_png(p, path) != 0) {
		printf("FileWriter: cannot write %s\n", path);
	} else {
		printf("FileWriter: wrote %s\n", path);
	}

	if(p->txt_maxrow >= 0) {
		fw_build_path(path, sizeof(path), p->stem, ".txt");
		fw_write_txt(p, path);
	}
	p->unsaved = 0;
}

static void
fw_clear_page(FwPort *p)
{
	if(p->txt_maxrow >= 0) {
		memset(p->txt, 0, (size_t)(p->txt_maxrow + 1) * FW_TXT_COLS);
	}
	p->txt_maxrow = -1;
	memset(p->canvas, 0, (size_t)FW_PAGE_W * FW_CANVAS_ROWS);
	p->dirty = 0;
	p->stem[0] = '\0';
	p->unsaved = 0;
}

/* Count the canvas rows from page_len down that hold ink: the lower part of
 * a glyph that crossed the page bottom. Scans only FW_SPILL_ROWS rows, the
 * most a glyph can reach past the bottom. */
static int
fw_spill_rows(const FwPort *p)
{
	int y, x;

	for(y = p->page_len + FW_SPILL_ROWS - 1; y >= p->page_len; y--) {
		const unsigned char *row = p->canvas + (size_t)y * FW_PAGE_W;

		for(x = 0; x < FW_PAGE_W; x++) {
			if(row[x]) {
				return y - p->page_len + 1;
			}
		}
	}
	return 0;
}

/* Write the page, then move any spill to the top of a fresh page. */
static void
fw_page_end(FwPort *p)
{
	unsigned char spill[FW_SPILL_ROWS * FW_PAGE_W];
	int k;

	if(!p->dirty) {
		return;
	}
	k = fw_spill_rows(p);
	memcpy(spill, p->canvas + (size_t)p->page_len * FW_PAGE_W, (size_t)k * FW_PAGE_W);
	fw_write_page(p);
	fw_clear_page(p);
	if(k > 0) {
		memcpy(p->canvas, spill, (size_t)k * FW_PAGE_W);
		p->dirty = 1;
		p->unsaved = 1;
		p->idle_frames = 0;
	}
}

/* Write everything left, including a carried spill, which becomes a page
 * of its own. */
static void
fw_flush_port(FwPort *p)
{
	while(p->dirty) {
		fw_page_end(p);
	}
}

/* ---- drawing ------------------------------------------------------------ */

/* Blacken canvas rows y0..y0+h-1 across x0..x1-1. Rows past page_len are kept
 * so fw_page_end can carry them onto the next page. */
static void
fw_ink(FwPort *p, int x0, int x1, int y0, int h)
{
	int x, y;
	int y1 = y0 + h;

	if(x0 < 0) {
		x0 = 0;
	}
	if(x1 > FW_PAGE_W) {
		x1 = FW_PAGE_W;
	}
	if(y0 < 0) {
		y0 = 0;
	}
	if(y1 > FW_CANVAS_ROWS) {
		y1 = FW_CANVAS_ROWS;
	}
	if(x0 >= x1 || y0 >= y1) {
		return;
	}
	for(y = y0; y < y1; y++) {
		unsigned char *row = p->canvas + (size_t)y * FW_PAGE_W;

		for(x = x0; x < x1; x++) {
			row[x] |= p->ink;
		}
	}
	p->dirty = 1;
}

/* One dot column at x_in (inches). Bit r of mask is dot row r, 2 canvas rows
 * apart, so bit 0 is the top dot. */
static void
fw_column(FwPort *p, double x_in, unsigned int mask, int nrows)
{
	int x0, x1, r;

	if(!mask) {
		return;
	}
	x0 = (int)(x_in * FW_CANVAS_DPI_X + 0.5);
	x1 = (int)((x_in + 1.0 / p->dpi) * FW_CANVAS_DPI_X + 0.5);
	for(r = 0; r < nrows; r++) {
		if(mask & (1u << r)) {
			fw_ink(p, x0, x1, p->vy + FW_DOT_PX * r, FW_DOT_PX);
		}
	}
}

static void
fw_txt_store(FwPort *p, unsigned char ch)
{
	int row = p->trow;
	int col = (int)((p->hx - p->tshift) * (p->dpi / 8.0) + 0.5);

	if(col < 0) {
		col = 0;
	}
	if(col >= FW_TXT_COLS) {
		col = FW_TXT_COLS - 1;
	}
	p->txt[(size_t)row * FW_TXT_COLS + col] = ch;
	if(row > p->txt_maxrow) {
		p->txt_maxrow = row;
	}
	p->dirty = 1;
}

/* Head returns to the margin, starting a new transcript line. */
static void
fw_home(FwPort *p)
{
	p->hx = p->margin;
	p->tshift = 0.0;
}

/* ---- paper and head motion ---------------------------------------------- */

static void
fw_motion(FwPort *p, int lines)
{
	int step = lines * p->spacing;

	if(p->reverse) {
		p->vy -= step;
		if(p->vy < 0) {
			p->vy = 0;
		}
		p->trow -= lines;
		if(p->trow < 0) {
			p->trow = 0;
		}
		return;
	}
	p->vy += step;
	p->trow += lines;
	if(p->trow >= FW_TXT_ROWS) {
		p->trow = FW_TXT_ROWS - 1;
	}
	if(p->vy >= p->page_len) {
		fw_page_end(p);
		p->vy %= p->page_len;
		p->trow = 0;
	}
}

/* A character or column that doesn't fit ends the line. The head returns to
 * the margin, and paper advances only if LF-when-full is set. */
static void
fw_wrap_check(FwPort *p, double adv)
{
	if(p->hx + adv > FW_PRINT_W + FW_EPS) {
		fw_home(p);
		if(p->reg_a & FW_REG_A_LF_FULL) {
			fw_motion(p, 1);
		}
	}
}

/* Prints one character cell or graphics column. cols[] holds ncols dot
 * columns, bit r of each mask is dot row r. adv is the advance in dots at the
 * current pitch. Double width prints each dot column twice. Boldface and NLQ
 * strike the cell again one canvas dot to the right. Underline (text only)
 * prints the bottom dot row across the whole advance, spaces included. tch is
 * the transcript character, 0 for none. */
static void
fw_cell(FwPort *p, const unsigned int *cols, int ncols, int nrows, int adv,
	int text, unsigned char tch)
{
	const int f = p->dw ? 2 : 1;
	const int strikes = 1 + p->bold + (text && p->nlq);
	const double width = (double)adv * f / p->dpi;
	int k, j, s, i;

	fw_wrap_check(p, width);
	for(k = 0; k < ncols; k++) {
		for(j = 0; j < f; j++) {
			double x = p->hx + (double)(k * f + j) / p->dpi;

			for(s = 0; s < strikes; s++) {
				fw_column(p, x + s / (double)FW_CANVAS_DPI_X, cols[k], nrows);
			}
		}
	}
	if(text && p->ul) {
		for(i = 0; i < adv * f; i++) {
			double x = p->hx + i / (double)p->dpi;

			for(s = 0; s < 1 + p->bold; s++) {
				fw_column(p, x + s / (double)FW_CANVAS_DPI_X, 1u << 8, 9);
			}
		}
	}
	if(text && tch && tch != ' ') {
		fw_txt_store(p, tch);
	}
	if(text && p->dw) {
		p->tshift += adv / (double)p->dpi;
	}
	p->hx += width;
}

/* A custom character is text, and its advance is its width in dots. A key
 * with no character loaded prints a blank standard-width cell. A negative key
 * has no custom character and also prints blank. */
static void
fw_custom_cell(FwPort *p, int key)
{
	unsigned int cols[FW_CUSTOM_WIDE] = {0};
	int w = 0, k;

	if(key >= 0) {
		w = p->cw[key];
		for(k = 0; k < w; k++) {
			cols[k] = p->cdat[key][k];
		}
	}
	fw_cell(p, cols, w, 9, w ? w : 8, 1, 0);
}

static void
fw_text(FwPort *p, unsigned char ch)
{
	unsigned int cols[7];
	const unsigned char *glyph;
	int mt, k, r;

	if(ch < 0x20 || ch == 0x7f) {
		return;
	}
	if(p->cfont == 2) {
		fw_custom_cell(p, (ch >= 32 && ch <= 111) ? ch + 128 : -1);
		return;
	}
	if(p->cfont == 1) {
		fw_custom_cell(p, ch);
		return;
	}
	mt = p->mt && ch >= 0x40 && ch <= 0x5f;
	glyph = mt ? fw_mousetext[ch - 0x40] : fw_glyphs[ch - 32];
	for(k = 0; k < 7; k++) {
		cols[k] = 0;
		for(r = 0; r < 9; r++) {
			if(glyph[r] & (0x40 >> k)) {
				cols[k] |= 1u << r;
			}
		}
	}
	fw_cell(p, cols, 7, 9, 8, 1, mt ? 0 : ch);
}

static void
fw_gfx_column(FwPort *p, unsigned char mask)
{
	unsigned int col = mask;

	fw_cell(p, &col, 1, 8, 1, 0, 0);
}

static void
fw_cr(FwPort *p)
{
	fw_home(p);
	p->last_cr = 1;
	if(p->reg_a & FW_REG_A_LF_AFTER_CR) {
		fw_motion(p, 1);
	}
}

/* LF and CTRL-_ n. CR-only mode (reg A bit 6 clear) acts only right after a
 * CR. With CR insertion on, the head returns to the margin first. */
static void
fw_feed_cmd(FwPort *p, int lines, int after_cr)
{
	if(!(p->reg_a & FW_REG_A_PRINT_CMDS) && !after_cr) {
		return;
	}
	if(p->cr_insert) {
		fw_home(p);
	}
	fw_motion(p, lines);
}

static void
fw_ff(FwPort *p, int after_cr)
{
	if(!(p->reg_a & FW_REG_A_PRINT_CMDS) && !after_cr) {
		return;
	}
	if(p->cr_insert) {
		fw_home(p);
	}
	fw_page_end(p);
	p->vy = 0;
	p->trow = 0;
}

/* Backspace moves back one character cell, two when double width is on. */
static void
fw_backspace(FwPort *p)
{
	const double adv = 8.0 / p->dpi;

	p->hx -= p->dw ? 2.0 * adv : adv;
	if(p->dw) {
		p->tshift -= adv;
	}
	if(p->hx < p->margin) {
		fw_home(p);
	}
}

/* ---- commands ----------------------------------------------------------- */

/* If the head is already past a shorter new page length, the current page
 * ends at its old length (so text above the head is kept) and the head moves
 * to the top of a fresh one. */
static void
fw_set_page_len(FwPort *p, int len)
{
	if(p->vy >= len) {
		fw_page_end(p);
		p->vy = 0;
		p->trow = 0;
	}
	p->page_len = len;
}

static void
fw_soft_reset(FwPort *p)
{
	p->margin = 0.0;
	fw_home(p);
	p->tab_n = 0;
	p->dpi = FW_DEF_DPI;
	p->spacing = FW_DEF_SPACING;
	p->reverse = 0;
	p->bold = 0;
	p->dw = 0;
	p->ul = 0;
	p->nlq = 0;
	p->mt = 0;
	p->cfont = 0;
	p->ink = FW_INK_K;
	p->cr_insert = 1;
	p->reg_a &= (unsigned char)~(FW_REG_A_LF_AFTER_CR | FW_REG_A_LF_FULL);
	p->reg_a |= FW_REG_A_PRINT_CMDS;
	fw_set_page_len(p, FW_DEF_LEN);
}

/* Tab columns count from the paper's left edge at the current pitch. */
static double
fw_col_in(const FwPort *p, int col)
{
	return (col - 1) * 8.0 / p->dpi;
}

static int
fw_tab_find(const FwPort *p, double x)
{
	int i;

	for(i = 0; i < p->tab_n; i++) {
		if(p->tab_in[i] - x < FW_EPS && x - p->tab_in[i] < FW_EPS) {
			return i;
		}
	}
	return -1;
}

static void
fw_tab_add(FwPort *p, int col)
{
	double x;
	int i;

	if(col < 1 || col > p->dpi || p->tab_n >= FW_MAX_TABS) {
		return;
	}
	x = fw_col_in(p, col);
	if(fw_tab_find(p, x) >= 0) {
		return;
	}
	for(i = p->tab_n; i > 0 && p->tab_in[i - 1] > x; i--) {
		p->tab_in[i] = p->tab_in[i - 1];
	}
	p->tab_in[i] = x;
	p->tab_n++;
}

static void
fw_tab_remove(FwPort *p, int col)
{
	int i = fw_tab_find(p, fw_col_in(p, col));

	if(i < 0) {
		return;
	}
	for(; i + 1 < p->tab_n; i++) {
		p->tab_in[i] = p->tab_in[i + 1];
	}
	p->tab_n--;
}

static void
fw_tab_next(FwPort *p)
{
	int i;

	for(i = 0; i < p->tab_n; i++) {
		if(p->tab_in[i] > p->hx + FW_EPS) {
			p->hx = p->tab_in[i];
			return;
		}
	}
}

static int
fw_pitch_dpi(int c)
{
	switch(c) {
	case 'n': return 72;
	case 'N': return 80;
	case 'E': return 96;
	case 'e': return 107;
	case 'q': return 120;
	case 'Q': return 136;
	case 'p': return 144;	/* proportional, approximated as 8-dot cells */
	case 'P': return 160;	/* proportional, approximated as 8-dot cells */
	}
	return 0;
}

static int
fw_is_simple(int c)
{
	return fw_pitch_dpi(c) != 0 || c == 'A' || c == 'B' ||
		c == 'f' || c == 'r' || c == 'c' || c == '~' || c == '!' || c == '"' ||
		c == 'X' || c == 'Y' || c == 'M' || c == '&' || c == '$' ||
		c == '-' || c == '+' || c == 'I' || c == '*';
}

/* Argument layout after ESC + letter. Each character is a field: a digit
 * gives an ASCII numeral of that many digits, 'r' gives one raw byte. */
static const char *
fw_cmd_format(int c)
{
	switch(c) {
	case 'T': return "2";
	case 'L': return "3";
	case 'F': return "4";
	case 'H': return "4";
	case 'D': return "rr";
	case 'Z': return "rr";
	case 'R': return "3r";
	case 'G': return "4";
	case 'S': return "4";
	case 'g': return "3";
	case 'V': return "4r";
	case 'l': return "1";
	case 'u': return "3";
	case 'a': return "1";	/* print quality: 2 selects NLQ, 0 and 1 correspondence and draft */
	case 'K': return "1";	/* ribbon color */
	case 's': return "1";	/* proportional dot spacing, read and ignored */
	case 'm': return "1";	/* NLQ off; digit read and ignored */
	}
	return NULL;
}

static void
fw_cmd_exec(FwPort *p)
{
	p->state = FW_IDLE;
	p->data_left = 0;
	switch(p->cmd) {
	case 'T':
		if(p->fv[0] >= 1) {
			p->spacing = p->fv[0];
		}
		break;
	case 'L':
		p->margin = p->fv[0] * 8.0 / p->dpi;
		if(p->margin > FW_PRINT_W) {
			p->margin = FW_PRINT_W;
		}
		break;
	case 'F':
		p->hx = p->margin + p->fv[0] / (double)p->dpi;
		break;
	case 'H':
		if(p->fv[0] >= 1) {
			fw_set_page_len(p, p->fv[0]);
		}
		break;
	case 'D':
		p->reg_a |= (unsigned char)p->fv[0];
		p->reg_b |= (unsigned char)p->fv[1];
		break;
	case 'Z':
		p->reg_a &= (unsigned char)~p->fv[0];
		p->reg_b &= (unsigned char)~p->fv[1];
		break;
	case 'R': {
		unsigned char c = (unsigned char)p->fv[1];
		int i;

		if(p->reg_b & FW_REG_B_IGNORE_8TH) {
			c &= 0x7f;
		}
		if(c >= 0x20) {
			for(i = 0; i < p->fv[0]; i++) {
				fw_text(p, c);
			}
		}
		break;
	}
	case 'G':
	case 'S':
		p->data_left = p->fv[0];
		break;
	case 'g':
		p->data_left = p->fv[0] * 8;
		break;
	case 'V': {
		int i;

		for(i = 0; i < p->fv[0]; i++) {
			fw_gfx_column(p, (unsigned char)p->fv[1]);
		}
		break;
	}
	case 'A':
		p->spacing = 24;
		break;
	case 'B':
		p->spacing = 18;
		break;
	case 'f':
		p->reverse = 0;
		break;
	case 'r':
		p->reverse = 1;
		break;
	case 'c':
		fw_soft_reset(p);
		break;
	case 'u':
		fw_tab_add(p, p->fv[0]);
		break;
	case '~':
		p->tab_n = 0;
		break;
	case '!':
		p->bold = 1;
		break;
	case '"':
		p->bold = 0;
		break;
	case 'X':
		p->ul = 1;
		break;
	case 'Y':
		p->ul = 0;
		break;
	case 'M':
		p->nlq = 1;
		break;
	case 'm':
		p->nlq = 0;
		break;
	case '&':
		p->mt = 1;
		p->cfont = 0;
		break;
	case '$':
		p->mt = 0;
		p->cfont = 0;
		break;
	case '*':
		p->mt = 0;
		p->cfont = 2;
		break;
	case '-':
	case '+':
		p->cmax = p->cmd == '-' ? FW_CUSTOM_NARROW : FW_CUSTOM_WIDE;
		memset(p->cw, 0, sizeof(p->cw));
		p->reg_b &= (unsigned char)~FW_REG_B_IGNORE_8TH;
		break;
	case 'I':
		p->state = FW_LOAD_KEY;
		break;
	case 'a':
		if(p->fv[0] == 2) {
			p->nlq = 1;
		} else if(p->fv[0] == 0 || p->fv[0] == 1) {
			p->nlq = 0;
		}
		break;
	case 'K':
		if(p->fv[0] <= 6) {
			p->ink = fw_ribbon_ink[p->fv[0]];
		}
		break;
	case 's':
		break;
	case 'l':
		if(p->fv[0] <= 1) {
			p->cr_insert = (p->fv[0] == 0);
		}
		break;
	default:
		if(fw_pitch_dpi(p->cmd)) {
			p->dpi = fw_pitch_dpi(p->cmd);
		}
		break;
	}
	if(p->data_left > 0) {
		p->state = FW_DATA;
	}
}

static void
fw_list_start(FwPort *p)
{
	p->state = FW_LIST;
	p->fval = 0;
	p->fdigits = 0;
	p->lst_n = 0;
	p->lst_last = -1;
	p->lst_digit = 0;
	p->lst_bad = 0;
}

static void
fw_esc_byte(FwPort *p, unsigned char b)
{
	const char *fmt = fw_cmd_format(b);

	p->cmd = b;
	if(fmt) {
		p->fmt = fmt;
		p->fidx = 0;
		p->fdigits = 0;
		p->fval = 0;
		p->fv[0] = 0;
		p->fv[1] = 0;
		p->state = FW_ARGS;
	} else if(b == '(' || b == ')') {
		fw_list_start(p);
	} else if(b == '\'' || b == '.') {
		p->state = FW_PICK;
	} else if(fw_is_simple(b)) {
		fw_cmd_exec(p);
	} else {
		p->state = FW_IDLE;
	}
}

static void
fw_idle_byte(FwPort *p, unsigned char b)
{
	switch(b) {
	case 0x08:
		fw_backspace(p);
		break;
	case 0x09:
		fw_tab_next(p);
		break;
	case 0x0a:
		fw_feed_cmd(p, 1, p->prev_cr);
		break;
	case 0x0c:
		fw_ff(p, p->prev_cr);
		break;
	case 0x0d:
		fw_cr(p);
		break;
	case 0x0e:
		p->dw = 1;
		break;
	case 0x0f:
		p->dw = 0;
		break;
	case 0x1b:
		p->state = FW_ESC;
		break;
	case 0x1f:
		p->us_ok = p->prev_cr;
		p->state = FW_UNDERSCORE;
		break;
	default:
		if(b >= 0x20 && b != 0x7f) {
			fw_text(p, b);
		}
		break;
	}
}

static void
fw_list_end(FwPort *p)
{
	int i;

	p->state = FW_IDLE;
	if(p->cmd == '(') {
		p->tab_n = 0;
		for(i = 0; i < p->lst_n; i++) {
			fw_tab_add(p, p->lst[i]);
		}
	} else {
		for(i = 0; i < p->lst_n; i++) {
			fw_tab_remove(p, p->lst[i]);
		}
	}
}

/* Parses an ESC ( or ESC ) list: three-digit numerals, leading zeros may be
 * spaces, ascending, comma-separated, ended by a period. A malformed list is
 * ignored in full: its digits, spaces and commas are skipped up to the period.
 * Any other byte ends the command and is handled normally. */
static void
fw_list_byte(FwPort *p, unsigned char b)
{
	int listch = b == ' ' || b == ',' || (b >= '0' && b <= '9');

	if(p->lst_bad) {
		if(b == '.') {
			p->state = FW_IDLE;
		} else if(!listch) {
			p->state = FW_IDLE;
			fw_idle_byte(p, b);
		}
		return;
	}
	if(b == ' ' && !p->lst_digit && p->fdigits < 3) {
		p->fdigits++;
		return;
	}
	if(b >= '0' && b <= '9' && p->fdigits < 3) {
		p->fval = p->fval * 10 + (b - '0');
		p->lst_digit = 1;
		p->fdigits++;
		return;
	}
	if((b == ',' || b == '.') && p->fdigits == 3 && p->lst_digit && p->fval > p->lst_last) {
		p->lst_last = p->fval;
		if(p->fval >= 1 && p->fval <= p->dpi && p->lst_n < FW_MAX_COLS) {
			p->lst[p->lst_n++] = p->fval;
		}
		p->fval = 0;
		p->fdigits = 0;
		p->lst_digit = 0;
		if(b == '.') {
			fw_list_end(p);
		}
		return;
	}
	if(b == '.' && p->lst_last < 0 && p->fdigits == 0) {
		fw_list_end(p);
		return;
	}
	if(b == '.') {
		p->state = FW_IDLE;
	} else if(listch) {
		p->lst_bad = 1;
	} else {
		p->state = FW_IDLE;
		fw_idle_byte(p, b);
	}
}

static void
fw_arg_byte(FwPort *p, unsigned char b)
{
	char f = p->fmt[p->fidx];

	if(f == 'r') {
		p->fv[p->fidx++] = b;
	} else {
		int width = f - '0';

		if(b == ' ') {
			b = '0';		/* leading zeros may be spaces */
		}
		if(b < '0' || b > '9') {
			/* Not a numeral: drop the command and handle b normally. */
			p->state = FW_IDLE;
			fw_idle_byte(p, b);
			return;
		}
		p->fval = p->fval * 10 + (b - '0');
		if(++p->fdigits == width) {
			p->fv[p->fidx++] = p->fval;
			p->fval = 0;
			p->fdigits = 0;
		}
	}
	if(p->fmt[p->fidx] == '\0') {
		fw_cmd_exec(p);
	}
}

static void
fw_us_byte(FwPort *p, unsigned char b)
{
	p->state = FW_IDLE;
	if(b >= '1' && b <= '?') {
		fw_feed_cmd(p, b - '0', p->us_ok);
	}
}

static void
fw_data_byte(FwPort *p, unsigned char b)
{
	fw_gfx_column(p, b);
	if(--p->data_left <= 0) {
		p->state = FW_IDLE;
	}
}

static int
fw_is_load(const FwPort *p)
{
	return p->state == FW_LOAD_KEY || p->state == FW_LOAD_WIDTH ||
		p->state == FW_LOAD_DATA;
}

/* ESC - keeps 32-126 and 160-239; ESC + keeps only 32-126. */
static int
fw_key_loadable(const FwPort *p, int key)
{
	if(key >= 32 && key <= 126) {
		return 1;
	}
	return p->cmax == FW_CUSTOM_NARROW && key >= 160 && key <= 239;
}

/* ESC I, then KEY, WIDTH and DATA per character; CTRL-D ends the load. A
 * character that cannot be kept (bad key, too wide) still has its data read,
 * so the next character stays in step. */
static void
fw_load_byte(FwPort *p, unsigned char b)
{
	if(p->state == FW_LOAD_KEY) {
		if(b == 0x04) {
			p->state = FW_IDLE;
		} else {
			p->lkey = b;
			p->state = FW_LOAD_WIDTH;
		}
	} else if(p->state == FW_LOAD_WIDTH) {
		int w = 0;

		p->lbot = 0;
		if(b >= 'A' && b <= 'P') {
			w = b - 'A' + 1;
		} else if(b >= 'a' && b <= 'p') {
			w = b - 'a' + 1;
			p->lbot = 1;
		}
		p->lwidth = w;
		p->lcol = 0;
		p->lstore = w > 0 && w <= p->cmax && fw_key_loadable(p, p->lkey);
		if(p->lstore) {
			p->cw[p->lkey] = (unsigned char)w;
			memset(p->cdat[p->lkey], 0, sizeof(p->cdat[p->lkey]));
		}
		p->state = w > 0 ? FW_LOAD_DATA : FW_LOAD_KEY;
	} else {
		if(p->lstore) {
			p->cdat[p->lkey][p->lcol] = p->lbot ? (unsigned short)(b << 1) : b;
		}
		if(++p->lcol >= p->lwidth) {
			p->state = FW_LOAD_KEY;
		}
	}
}

static void fw_dispatch(FwPort *p, unsigned char b);

/* ESC ' and ESC . select custom characters. The switch is held until the next
 * byte shows whether it is a single symbol (ESC ' key ESC S), which leaves the
 * font alone. */
static void
fw_pick_switch(FwPort *p)
{
	p->cfont = 1;
	p->mt = 0;
	p->state = FW_IDLE;
}

static void
fw_pick_byte(FwPort *p, unsigned char b)
{
	if(b < 0x20 || b == 0x7f) {
		fw_pick_switch(p);
		fw_dispatch(p, b);
	} else {
		p->pick_key = b;
		p->state = FW_PICK_KEY;
	}
}

static void
fw_pick_key_byte(FwPort *p, unsigned char b)
{
	if(b == 0x1b) {
		p->state = FW_PICK_ESC;
		return;
	}
	fw_pick_switch(p);
	fw_dispatch(p, p->pick_key);
	fw_dispatch(p, b);
}

static void
fw_pick_esc_byte(FwPort *p, unsigned char b)
{
	if(b == 'S') {
		fw_custom_cell(p, p->pick_key);
		p->state = FW_IDLE;
		return;
	}
	fw_pick_switch(p);
	fw_dispatch(p, p->pick_key);
	fw_dispatch(p, 0x1b);
	fw_dispatch(p, b);
}

static void
fw_dispatch(FwPort *p, unsigned char b)
{
	switch(p->state) {
	case FW_DATA:
		fw_data_byte(p, b);
		break;
	case FW_ARGS:
		fw_arg_byte(p, b);
		break;
	case FW_LIST:
		fw_list_byte(p, b);
		break;
	case FW_ESC:
		fw_esc_byte(p, b);
		break;
	case FW_UNDERSCORE:
		fw_us_byte(p, b);
		break;
	case FW_PICK:
		fw_pick_byte(p, b);
		break;
	case FW_PICK_KEY:
		fw_pick_key_byte(p, b);
		break;
	case FW_PICK_ESC:
		fw_pick_esc_byte(p, b);
		break;
	case FW_LOAD_KEY:
	case FW_LOAD_WIDTH:
	case FW_LOAD_DATA:
		fw_load_byte(p, b);
		break;
	default:
		fw_idle_byte(p, b);
		break;
	}
}

/* ---- public interface --------------------------------------------------- */

/* exit() skips main()'s flush, so flush at exit too. The config's fork()ed
 * child inherits g_fw and runs exit(), so only the owning process flushes. */
static void
fw_atexit(void)
{
	if(getpid() == g_fw_owner_pid) {
		filewriter_flush_all();
	}
}

void
filewriter_open(int port)
{
	unsigned char *canvas;
	unsigned char *txt;
	FwPort *p;

	if(port < 0 || port > 1) {
		return;
	}
	if(!g_fw_atexit_set) {
		g_fw_owner_pid = getpid();
		g_fw_atexit_set = 1;
		atexit(fw_atexit);
	}
	filewriter_close(port);

	canvas = calloc((size_t)FW_PAGE_W * FW_CANVAS_ROWS, 1);
	txt = calloc((size_t)FW_TXT_ROWS, FW_TXT_COLS);
	if(!canvas || !txt) {
		free(canvas);
		free(txt);
		printf("FileWriter: out of memory\n");
		return;
	}

	p = &g_fw[port];
	memset(p, 0, sizeof(*p));
	p->open = 1;
	p->canvas = canvas;
	p->ink = FW_INK_K;
	p->cmax = FW_CUSTOM_NARROW;
	p->txt = txt;
	p->txt_maxrow = -1;
	p->page_len = FW_DEF_LEN;
	p->dpi = FW_DEF_DPI;
	p->spacing = FW_DEF_SPACING;
	p->cr_insert = 1;
	p->reg_a = FW_REG_A_PRINT_CMDS;
	p->reg_b = 0;
}

void
filewriter_close(int port)
{
	FwPort *p;

	if(port < 0 || port > 1) {
		return;
	}
	p = &g_fw[port];
	if(!p->open) {
		return;
	}
	fw_flush_port(p);
	free(p->canvas);
	free(p->txt);
	p->canvas = NULL;
	p->txt = NULL;
	p->open = 0;
}

void
filewriter_put(int port, unsigned char b)
{
	FwPort *p;
	int raw_arg;

	if(port < 0 || port > 1) {
		return;
	}
	p = &g_fw[port];
	if(!p->open) {
		return;
	}

	p->unsaved = 1;
	p->idle_frames = 0;
	p->job_idle_frames = 0;
	p->prev_cr = p->last_cr;
	p->last_cr = 0;

	raw_arg = (p->state == FW_ARGS && p->fmt[p->fidx] == 'r');
	if((p->reg_b & FW_REG_B_IGNORE_8TH) && p->state != FW_DATA && !raw_arg && !fw_is_load(p)) {
		b &= 0x7f;
	}

	fw_dispatch(p, b);
}

void
filewriter_flush_all(void)
{
	int i;

	for(i = 0; i < 2; i++) {
		if(g_fw[i].open) {
			fw_flush_port(&g_fw[i]);
		}
	}
}

/* A long pause ends the job: write what is left and start the next job on a
 * fresh sheet at the top and left margin. */
static void
fw_end_job(FwPort *p)
{
	fw_flush_port(p);
	fw_home(p);
	p->vy = 0;
	p->trow = 0;
}

/* Called once per emulated frame. A page that has gone FW_IDLE_FRAMES
 * frames without a byte is saved, so a crash during a pause does not lose it. */
void
filewriter_tick(void)
{
	int i;
	FwPort *p;

	for(i = 0; i < 2; i++) {
		p = &g_fw[i];
		if(!p->open || !p->dirty) {
			continue;
		}
		if(p->unsaved && ++p->idle_frames >= FW_IDLE_FRAMES) {
			fw_write_page(p);
		}
		if(++p->job_idle_frames >= FW_JOB_END_FRAMES) {
			fw_end_job(p);
		}
	}
}

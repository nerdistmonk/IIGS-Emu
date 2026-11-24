/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*      This code is covered by the GNU GPL v3                        */
/**********************************************************************/

/* FileWriter virtual printer.
 *
 * Emulates an Apple ImageWriter II on a serial port. The guest's bytes are
 * parsed as ImageWriter commands and rendered onto a 1-bit page at 160 x 144
 * dots per inch. Each finished page is written as <stem>.png (a pixel
 * printout) and <stem>.txt (text only, Mac Roman, CR line endings; written
 * only when the page has text, so bitmap-only pages have just the .png).
 *
 * A page ends on FF, when paper runs past the page length, when the port
 * closes, and at emulator exit. A page with no ink writes no files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "defc.h"
#include "filewriter_font.h"

extern char *g_cfg_ssdir;

#define FW_CANVAS_DPI_X	160		/* canvas pixels per inch, across */
#define FW_CANVAS_DPI_Y	144		/* canvas rows per inch = paper steps */
#define FW_PAGE_W	1360		/* 8.5 in of canvas */
#define FW_STRIDE	(FW_PAGE_W / 8)
#define FW_PRINT_W	8.0		/* printable width, inches */
#define FW_MAX_LEN	9999		/* longest page, paper steps */
#define FW_TXT_COLS	160
#define FW_TXT_ROWS	10000
#define FW_DOT_PX	2		/* a 1/72 in dot row spans 2 canvas rows */
#define FW_DEF_DPI	96		/* ESC E, Elite: 12 cpi */
#define FW_DEF_LEN	(11 * FW_CANVAS_DPI_Y)
#define FW_DEF_SPACING	24		/* 6 lpi */
#define FW_EPS		1e-9

#define FW_REG_A_LF_FULL	0x20	/* LF when the line is full */
#define FW_REG_A_PRINT_CMDS	0x40	/* CR/LF/FF act; clear = only CR acts */
#define FW_REG_A_LF_AFTER_CR	0x80
#define FW_REG_B_IGNORE_8TH	0x20

enum {
	FW_IDLE,
	FW_ESC,
	FW_ARGS,
	FW_UNDERSCORE,
	FW_DATA
};

typedef struct {
	int		open;
	unsigned char	*canvas;	/* FW_STRIDE x FW_MAX_LEN, 1 = paper */
	unsigned char	*txt;		/* FW_TXT_ROWS x FW_TXT_COLS, 0 = blank */
	int		txt_maxrow;	/* last transcript row used, -1 if none */
	int		dirty;		/* page has ink or text */
	int		page_len;	/* paper steps */
	int		vy;		/* paper position, canvas rows */
	int		trow;		/* transcript row of the current line */
	double		hx;		/* head position, inches from paper left */
	double		margin;		/* left margin, inches */
	int		dpi;		/* pitch, dots per inch */
	int		spacing;	/* line feed step, paper steps */
	int		reverse;	/* reverse line feed */
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
} FwPort;

static FwPort	g_fw[2];
static int	g_fw_seq = 0;

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

static void
fw_write_page(FwPort *p)
{
	char stem[64];
	char path[1024];

	fw_new_stem(stem, sizeof(stem));

	fw_build_path(path, sizeof(path), stem, ".png");
	if(write_png_gray1(path, p->canvas, FW_PAGE_W, p->page_len) != 0) {
		printf("FileWriter: cannot write %s\n", path);
	} else {
		printf("FileWriter: wrote %s\n", path);
	}

	if(p->txt_maxrow >= 0) {
		fw_build_path(path, sizeof(path), stem, ".txt");
		fw_write_txt(p, path);
	}
}

static void
fw_clear_page(FwPort *p)
{
	if(p->txt_maxrow >= 0) {
		memset(p->txt, 0, (size_t)(p->txt_maxrow + 1) * FW_TXT_COLS);
	}
	p->txt_maxrow = -1;
	memset(p->canvas, 0xff, (size_t)FW_STRIDE * FW_MAX_LEN);
	p->dirty = 0;
}

static void
fw_page_end(FwPort *p)
{
	if(p->dirty) {
		fw_write_page(p);
		fw_clear_page(p);
	}
}

/* ---- drawing ------------------------------------------------------------ */

/* Blacken canvas rows y0..y0+h-1 across x0..x1-1, clipped to the page. */
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
	if(y1 > p->page_len) {
		y1 = p->page_len;
	}
	if(x0 >= x1 || y0 >= y1) {
		return;
	}
	for(y = y0; y < y1; y++) {
		unsigned char *row = p->canvas + (size_t)y * FW_STRIDE;

		for(x = x0; x < x1; x++) {
			row[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
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
	int col = (int)(p->hx * (p->dpi / 8.0) + 0.5);

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
		p->hx = p->margin;
		if(p->reg_a & FW_REG_A_LF_FULL) {
			fw_motion(p, 1);
		}
	}
}

static void
fw_text(FwPort *p, unsigned char ch)
{
	const double adv = 8.0 / p->dpi;
	int k, r;

	if(ch < 0x20 || ch == 0x7f) {
		return;
	}
	fw_wrap_check(p, adv);
	for(k = 0; k < 7; k++) {
		unsigned int mask = 0;

		for(r = 0; r < 9; r++) {
			if(fw_glyphs[ch - 32][r] & (0x40 >> k)) {
				mask |= 1u << r;
			}
		}
		fw_column(p, p->hx + k / (double)p->dpi, mask, 9);
	}
	if(ch != ' ') {
		fw_txt_store(p, ch);
	}
	p->hx += adv;
}

static void
fw_gfx_column(FwPort *p, unsigned char mask)
{
	const double adv = 1.0 / p->dpi;

	fw_wrap_check(p, adv);
	fw_column(p, p->hx, mask, 8);
	p->hx += adv;
}

static void
fw_cr(FwPort *p)
{
	p->hx = p->margin;
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
		p->hx = p->margin;
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
		p->hx = p->margin;
	}
	fw_page_end(p);
	p->vy = 0;
	p->trow = 0;
}

static void
fw_backspace(FwPort *p)
{
	p->hx -= 8.0 / p->dpi;
	if(p->hx < p->margin) {
		p->hx = p->margin;
	}
}

/* ---- commands ----------------------------------------------------------- */

/* If the head is already past a shorter new page length, the current page
 * ends and the head moves to the top of a fresh one. */
static void
fw_set_page_len(FwPort *p, int len)
{
	p->page_len = len;
	if(p->vy >= p->page_len) {
		fw_page_end(p);
		p->vy = 0;
		p->trow = 0;
	}
}

static void
fw_soft_reset(FwPort *p)
{
	p->margin = 0.0;
	p->hx = 0.0;
	p->dpi = FW_DEF_DPI;
	p->spacing = FW_DEF_SPACING;
	p->reverse = 0;
	p->cr_insert = 1;
	p->reg_a &= (unsigned char)~(FW_REG_A_LF_AFTER_CR | FW_REG_A_LF_FULL);
	fw_set_page_len(p, FW_DEF_LEN);
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
		c == 'f' || c == 'r' || c == 'c';
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
		break;			/* TAB: no tab stops are set */
	case 0x0a:
		fw_feed_cmd(p, 1, p->prev_cr);
		break;
	case 0x0c:
		fw_ff(p, p->prev_cr);
		break;
	case 0x0d:
		fw_cr(p);
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

/* ---- public interface --------------------------------------------------- */

void
filewriter_open(int port)
{
	unsigned char *canvas;
	unsigned char *txt;
	FwPort *p;

	if(port < 0 || port > 1) {
		return;
	}
	filewriter_close(port);

	canvas = malloc((size_t)FW_STRIDE * FW_MAX_LEN);
	txt = calloc((size_t)FW_TXT_ROWS, FW_TXT_COLS);
	if(!canvas || !txt) {
		free(canvas);
		free(txt);
		printf("FileWriter: out of memory\n");
		return;
	}
	memset(canvas, 0xff, (size_t)FW_STRIDE * FW_MAX_LEN);

	p = &g_fw[port];
	memset(p, 0, sizeof(*p));
	p->open = 1;
	p->canvas = canvas;
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
	fw_page_end(p);
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

	p->prev_cr = p->last_cr;
	p->last_cr = 0;

	raw_arg = (p->state == FW_ARGS && p->fmt[p->fidx] == 'r');
	if((p->reg_b & FW_REG_B_IGNORE_8TH) && p->state != FW_DATA && !raw_arg) {
		b &= 0x7f;
	}

	switch(p->state) {
	case FW_DATA:
		fw_data_byte(p, b);
		break;
	case FW_ARGS:
		fw_arg_byte(p, b);
		break;
	case FW_ESC:
		fw_esc_byte(p, b);
		break;
	case FW_UNDERSCORE:
		fw_us_byte(p, b);
		break;
	default:
		fw_idle_byte(p, b);
		break;
	}
}

void
filewriter_flush_all(void)
{
	int i;

	for(i = 0; i < 2; i++) {
		if(g_fw[i].open) {
			fw_page_end(&g_fw[i]);
		}
	}
}

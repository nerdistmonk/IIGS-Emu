/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*                    Copyright 2002-2024 Kent Dickey                 */
/*                    Copyright 2025-2026 GSplus Contributors         */
/*                                                                    */
/*      This code is covered by the GNU GPL v3                        */
/*      See the file COPYING.txt or https://www.gnu.org/licenses/     */
/**********************************************************************/

/* Host clipboard bridge.  This file has no SDL code: the SDL side registers
 * get and set callbacks with clip_set_host_hooks, the same way input_map.c
 * reaches the window through imap_set_host_hook.
 *
 * GS/OS paste: when the host clipboard holds text that differs from the last
 * synced text, a guest GetScrap, GetScrapHandle, or GetScrapSize is redirected
 * to the 65816 shim in the host-only RAM bank (clipboard_shim.asm).  The shim
 * loads the text into the desk scrap and then runs the native call. */

#include "defc.h"

extern int g_cur_a2_stat;

#define CLIP_SCRAP_MAX		65536
#define CLIP_TOOL_ZERO_SCRAP	0x0b16	/* Scrap Manager ZeroScrap */
#define CLIP_TOOL_PUT_SCRAP	0x0c16	/* Scrap Manager PutScrap */
#define CLIP_TOOL_GET_SCRAP	0x0d16	/* Scrap Manager GetScrap */
#define CLIP_TOOL_GET_SCRAP_HANDLE	0x0e16	/* Scrap Manager GetScrapHandle */
#define CLIP_TOOL_GET_SCRAP_SIZE	0x0f16	/* Scrap Manager GetScrapSize */
#define CLIP_SCRAP_TYPE_TEXT	0x0000
#define CLIP_TOOLBOX_ENTRY	0xe10000	/* DISPATCH1 */
#define CLIP_TOOLBOX_ENTRY2	0xe10004	/* DISPATCH2 */
#define CLIP_SHIM_LEN		0x110	/* shim LEN word, host RAM offset */
#define CLIP_SHIM_TOOL		0x114	/* shim tool number word, host RAM offset */
#define CLIP_SHIM_STAGE		0x1000	/* shim text staging, host RAM offset */
#define CLIP_STAGE_MAX		0xe000

/* Assembled from clipboard_shim.asm (64tass -b).  Copied to host RAM before
 * each redirected Scrap Manager read. */
static const byte clip_shim_code[] = {
	0x08, 0xe2, 0x20, 0x68, 0x8f, 0x0c, 0x01, 0xfb, 0x68, 0x8f, 0x00, 0x01,
	0xfb, 0x68, 0x8f, 0x01, 0x01, 0xfb, 0x68, 0x8f, 0x02, 0x01, 0xfb, 0x68,
	0x8f, 0x04, 0x01, 0xfb, 0x68, 0x8f, 0x05, 0x01, 0xfb, 0x68, 0x8f, 0x06,
	0x01, 0xfb, 0x68, 0x8f, 0x07, 0x01, 0xfb, 0x68, 0x8f, 0x08, 0x01, 0xfb,
	0x68, 0x8f, 0x09, 0x01, 0xfb, 0xc2, 0x10, 0xa2, 0x16, 0x0b, 0x22, 0x00,
	0x00, 0xe1, 0xf4, 0x00, 0x00, 0xc2, 0x20, 0xaf, 0x10, 0x01, 0xfb, 0x48,
	0xe2, 0x20, 0xf4, 0x00, 0x00, 0xf4, 0xfb, 0x00, 0xf4, 0x00, 0x10, 0xc2,
	0x10, 0xa2, 0x16, 0x0c, 0x22, 0x00, 0x00, 0xe1, 0xe2, 0x20, 0xaf, 0x09,
	0x01, 0xfb, 0x48, 0xaf, 0x08, 0x01, 0xfb, 0x48, 0xaf, 0x07, 0x01, 0xfb,
	0x48, 0xaf, 0x06, 0x01, 0xfb, 0x48, 0xaf, 0x05, 0x01, 0xfb, 0x48, 0xaf,
	0x04, 0x01, 0xfb, 0x48, 0xc2, 0x30, 0xaf, 0x14, 0x01, 0xfb, 0xaa, 0x22,
	0x00, 0x00, 0xe1, 0xe2, 0x20, 0xaf, 0x02, 0x01, 0xfb, 0x48, 0xaf, 0x01,
	0x01, 0xfb, 0x48, 0xaf, 0x00, 0x01, 0xfb, 0x48, 0xaf, 0x0c, 0x01, 0xfb,
	0x48, 0x28, 0x6b,
};

static char	g_scrap_text[CLIP_SCRAP_MAX + 1];
static int	g_scrap_len = 0;
static char	*g_last_host = 0;	/* host text as of the last sync */
static char	*(*g_host_get)(void) = 0;
static int	(*g_host_set)(const char *text) = 0;

void
clip_set_host_hooks(char *(*get)(void), int (*set)(const char *text))
{
	g_host_get = get;
	g_host_set = set;
}

static char *
clip_dup(const char *text)
{
	char	*copy;
	size_t	len;

	len = strlen(text);
	copy = malloc(len + 1);
	if(copy) {
		memcpy(copy, text, len + 1);
	}
	return copy;
}

/* Sends text to the host clipboard and records it as the synced text, so the
 * next GetScrap sees the guest side as current. */
int
clip_publish_text(const char *text)
{
	char	*copy;

	if(!g_host_set || !g_host_set(text)) {
		return 0;
	}
	copy = clip_dup(text);
	if(copy) {
		free(g_last_host);
		g_last_host = copy;
	}
	return 1;
}

static int
clip_is_io(word32 addr)
{
	word32	bank, lo;

	bank = (addr >> 16) & 0xff;
	lo = addr & 0xffff;
	if((bank != 0x00) && (bank != 0x01) && (bank != 0xe0) && (bank != 0xe1)) {
		return 0;
	}
	return (lo >= 0xc000) && (lo < 0xc100);
}

static word32
clip_read_byte(word32 addr)
{
	return get_memory_c(addr) & 0xff;
}

/* Reads a little-endian value of nbytes at stack offset off, bank 00. */
static word32
clip_stack_val(word32 sp, word32 off, int nbytes)
{
	word32	val;
	int	i;

	val = 0;
	for(i = nbytes - 1; i >= 0; i--) {
		val = (val << 8) | clip_read_byte((sp + off + i) & 0xffff);
	}
	return val;
}

/* Mac Roman $80-$FF as Unicode.  GS/OS dead-key accents arrive in this
 * encoding (verified: Option+E e leaves $8E on the scrap).  $DB is the
 * currency sign from the 1984 table, not the later euro sign. */
static const word16 clip_mac_high[128] = {
	0x00c4, 0x00c5, 0x00c7, 0x00c9, 0x00d1, 0x00d6, 0x00dc, 0x00e1,
	0x00e0, 0x00e2, 0x00e4, 0x00e3, 0x00e5, 0x00e7, 0x00e9, 0x00e8,
	0x00ea, 0x00eb, 0x00ed, 0x00ec, 0x00ee, 0x00ef, 0x00f1, 0x00f3,
	0x00f2, 0x00f4, 0x00f6, 0x00f5, 0x00fa, 0x00f9, 0x00fb, 0x00fc,
	0x2020, 0x00b0, 0x00a2, 0x00a3, 0x00a7, 0x2022, 0x00b6, 0x00df,
	0x00ae, 0x00a9, 0x2122, 0x00b4, 0x00a8, 0x2260, 0x00c6, 0x00d8,
	0x221e, 0x00b1, 0x2264, 0x2265, 0x00a5, 0x00b5, 0x2202, 0x2211,
	0x220f, 0x03c0, 0x222b, 0x00aa, 0x00ba, 0x03a9, 0x00e6, 0x00f8,
	0x00bf, 0x00a1, 0x00ac, 0x221a, 0x0192, 0x2248, 0x2206, 0x00ab,
	0x00bb, 0x2026, 0x00a0, 0x00c0, 0x00c3, 0x00d5, 0x0152, 0x0153,
	0x2013, 0x2014, 0x201c, 0x201d, 0x2018, 0x2019, 0x00f7, 0x25ca,
	0x00ff, 0x0178, 0x2044, 0x00a4, 0x2039, 0x203a, 0xfb01, 0xfb02,
	0x2021, 0x00b7, 0x201a, 0x201e, 0x2030, 0x00c2, 0x00ca, 0x00c1,
	0x00cb, 0x00c8, 0x00cd, 0x00ce, 0x00cf, 0x00cc, 0x00d3, 0x00d4,
	0xf8ff, 0x00d2, 0x00da, 0x00db, 0x00d9, 0x0131, 0x02c6, 0x02dc,
	0x00af, 0x02d8, 0x02d9, 0x02da, 0x00b8, 0x02dd, 0x02db, 0x02c7,
};

/* Writes the UTF-8 encoding of cp to dst and returns its length (1-3). */
static int
clip_utf8_put(char *dst, word32 cp)
{
	if(cp < 0x80) {
		dst[0] = cp;
		return 1;
	}
	if(cp < 0x800) {
		dst[0] = 0xc0 | (cp >> 6);
		dst[1] = 0x80 | (cp & 0x3f);
		return 2;
	}
	dst[0] = 0xe0 | (cp >> 12);
	dst[1] = 0x80 | ((cp >> 6) & 0x3f);
	dst[2] = 0x80 | (cp & 0x3f);
	return 3;
}

/* Decodes the UTF-8 sequence at p and returns its Mac Roman byte, or '?' when
 * the character has no Mac Roman form.  *used gets the bytes consumed. */
static int
clip_mac_from_utf8(const byte *p, int *used)
{
	word32	cp;
	int	n, i;

	if((p[0] & 0xe0) == 0xc0) {
		n = 2;
		cp = p[0] & 0x1f;
	} else if((p[0] & 0xf0) == 0xe0) {
		n = 3;
		cp = p[0] & 0x0f;
	} else if((p[0] & 0xf8) == 0xf0) {
		n = 4;
		cp = p[0] & 0x07;
	} else {
		*used = 1;
		return '?';
	}
	for(i = 1; i < n; i++) {
		if((p[i] & 0xc0) != 0x80) {
			*used = i;
			return '?';
		}
		cp = (cp << 6) | (p[i] & 0x3f);
	}
	*used = n;
	for(i = 0; i < 128; i++) {
		if(clip_mac_high[i] == cp) {
			return 0x80 + i;
		}
	}
	return '?';
}

static void
clip_append_scrap(word32 src, word32 num_bytes)
{
	word32	i, addr;
	int	c;

	for(i = 0; (i < num_bytes) && (i < CLIP_SCRAP_MAX); i++) {
		if(g_scrap_len >= CLIP_SCRAP_MAX) {
			break;
		}
		addr = (src + i) & 0xffffff;
		if(clip_is_io(addr)) {
			break;
		}
		c = clip_read_byte(addr);
		if(c >= 0x80) {
			if(g_scrap_len + 3 > CLIP_SCRAP_MAX) {
				break;
			}
			g_scrap_len += clip_utf8_put(&g_scrap_text[g_scrap_len],
						clip_mac_high[c - 0x80]);
			continue;
		}
		if(c == 0x0d) {
			c = '\n';
		} else if(((c < 0x20) && (c != '\t')) || (c == 0x7f)) {
			continue;
		}
		g_scrap_text[g_scrap_len++] = c;
	}
	g_scrap_text[g_scrap_len] = 0;
}

/* Called from the JSL handler with the post-push stack pointer.  The return
 * address sits at sp+1..sp+3, so the Pascal parameters of PutScrap are at:
 *   sp+4 srcPtr (4 bytes), sp+8 scrapType (2), sp+10 numBytes (4).
 * Only reads memory.  publish is 0 for the shim's own calls. */
static void
clip_observe_toolbox(word32 xreg, word32 stack, int publish)
{
	word32	sp, scrap_type, num_bytes, src;

	sp = stack & 0xffff;
	switch(xreg & 0xffff) {
	case CLIP_TOOL_ZERO_SCRAP:
		g_scrap_len = 0;
		g_scrap_text[0] = 0;
		break;
	case CLIP_TOOL_PUT_SCRAP:
		scrap_type = clip_stack_val(sp, 8, 2);
		if(scrap_type != CLIP_SCRAP_TYPE_TEXT) {
			break;
		}
		src = clip_stack_val(sp, 4, 2) |
				(clip_stack_val(sp, 6, 1) << 16);
		num_bytes = clip_stack_val(sp, 10, 4);
		clip_append_scrap(src, num_bytes);
		if(publish && (g_scrap_len > 0)) {
			clip_publish_text(g_scrap_text);
		}
		break;
	default:
		break;
	}
}

/* Normalizes text for the GS/OS scrap: CR line ends, printable ASCII, tabs as
 * spaces, and UTF-8 letters mapped to Mac Roman where possible ('?' otherwise).
 * Writes the shim text and LEN word, returns the length. */
static int
clip_stage_text(const char *text)
{
	byte		*dst;
	const byte	*p;
	int		c, n;

	dst = g_host_ram_ptr + CLIP_SHIM_STAGE;
	n = 0;
	for(p = (const byte *)text; *p && (n < CLIP_STAGE_MAX); p++) {
		c = *p;
		if(c == '\r') {
			if(p[1] == '\n') {
				p++;
			}
			c = '\r';
		} else if(c == '\n') {
			c = '\r';
		} else if(c == '\t') {
			c = ' ';
		} else if(c >= 0x80) {
			int	used;

			c = clip_mac_from_utf8(p, &used);
			p += used - 1;
		} else if((c < 0x20) || (c >= 0x7f)) {
			continue;
		}
		dst[n++] = c;
	}
	g_host_ram_ptr[CLIP_SHIM_LEN] = n & 0xff;
	g_host_ram_ptr[CLIP_SHIM_LEN + 1] = (n >> 8) & 0xff;
	return n;
}

/* Returns the staged length when host text is newer than the last sync, or 0
 * to leave the guest scrap alone. */
static int
clip_stage_host(void)
{
	char	*host;

	if(!g_host_get) {
		return 0;
	}
	host = g_host_get();
	if(host == 0) {
		return 0;
	}
	if((host[0] == 0) || (g_last_host && !strcmp(host, g_last_host))) {
		free(host);
		return 0;
	}
	free(g_last_host);
	g_last_host = host;
	return clip_stage_text(host);
}

static int
clip_is_scrap_read(word32 tool)
{
	return (tool == CLIP_TOOL_GET_SCRAP) ||
		(tool == CLIP_TOOL_GET_SCRAP_HANDLE) ||
		(tool == CLIP_TOOL_GET_SCRAP_SIZE);
}

/* Called from the JSL and JML handlers before the jump.  Returns the address to
 * jump to: target, or the shim when a guest GetScrap, GetScrapHandle, or
 * GetScrapSize should import host text.  ret_bank is the caller's program bank.
 * A JML caller pushes its own return address after its parameters, so the
 * parameter offsets match the JSL case.  The shim's own calls (host RAM bank)
 * are never redirected or published. */
word32
clip_toolbox_call(word32 xreg, word32 stack, word32 psr, word32 ret_bank,
								word32 target)
{
	word32	sp;
	int	from_guest;

	target &= 0xffffff;
	if(((target != CLIP_TOOLBOX_ENTRY) && (target != CLIP_TOOLBOX_ENTRY2)) ||
			(psr & 0x100)) {
		return target;
	}
	sp = stack & 0xffff;
	from_guest = (ret_bank != (HOST_RAM_BASE >> 16));
	if(from_guest && g_host_ram_ptr && clip_is_scrap_read(xreg & 0xffff) &&
			(clip_stack_val(sp, 4, 2) == CLIP_SCRAP_TYPE_TEXT) &&
			clip_stage_host()) {
		memcpy(g_host_ram_ptr, clip_shim_code, sizeof(clip_shim_code));
		g_host_ram_ptr[CLIP_SHIM_TOOL] = xreg & 0xff;
		g_host_ram_ptr[CLIP_SHIM_TOOL + 1] = (xreg >> 8) & 0xff;
		return HOST_RAM_BASE;
	}
	clip_observe_toolbox(xreg, stack, from_guest);
	return target;
}

/* Returns the visible text with trailing blank lines removed, or NULL when no
 * text is shown (graphics without MIX, or SHR). The result lives in a shared
 * buffer. */
const char *
clip_copy_text_screen(void)
{
	char	*text;
	int	len, first_row;

	// SHR overrides the TEXT switch on screen, so the text page may be stale.
	if(g_cur_a2_stat & ALL_STAT_SUPER_HIRES) {
		return 0;
	}
	if(g_cur_a2_stat & ALL_STAT_TEXT) {
		first_row = 0;
	} else if(g_cur_a2_stat & ALL_STAT_MIX_T_GR) {
		// MIX shows text only in the bottom four rows (line 160 and up).
		first_row = 20;
	} else {
		return 0;
	}
	text = cfg_text_screen_str(first_row);
	len = strlen(text);
	while((len > 0) && (text[len-1] == '\n')) {
		len--;
	}
	text[len] = 0;
	return text;
}

/* Feeds host text through the ADB keystroke paste buffer.  Each non-ASCII
 * UTF-8 character becomes one '?'.  Returns 1 if the buffer filled before the
 * whole text fit. */
int
clip_paste_text(const char *text)
{
	const byte	*p;
	int		c;

	for(p = (const byte *)text; *p; p++) {
		c = *p;
		if(c == '\t') {
			c = ' ';
		} else if(c >= 0x80) {
			while((p[1] & 0xc0) == 0x80) {
				p++;
			}
			c = '?';
		} else if(((c < 0x20) && (c != '\r') && (c != '\n')) ||
							(c == 0x7f)) {
			continue;
		}
		if(adb_paste_add_buf(c)) {
			return 1;
		}
	}
	return 0;
}

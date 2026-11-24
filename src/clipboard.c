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
#define CLIP_TOOLBOX_ENTRY	0xe10000
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
		c = clip_read_byte(addr) & 0x7f;
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

/* Normalizes text for the GS/OS scrap: CR line ends, 7-bit printable ASCII,
 * tabs as spaces.  Writes the shim text and LEN word, returns the length. */
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

/* Called from the JSL handler before the jump.  Returns the address to jump to:
 * target, or the shim when a guest GetScrap, GetScrapHandle, or GetScrapSize
 * should import host text.  ret_bank
 * is the caller's program bank; the shim's own calls (host RAM bank) are never
 * redirected or published. */
word32
clip_toolbox_jsl(word32 xreg, word32 stack, word32 psr, word32 ret_bank,
								word32 target)
{
	word32	sp;
	int	from_guest;

	if(((target & 0xffffff) != CLIP_TOOLBOX_ENTRY) || (psr & 0x100)) {
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

/* Returns the visible text screen with trailing blank lines removed, or NULL
 * when the display is not in text mode. The result lives in a shared buffer. */
const char *
clip_copy_text_screen(void)
{
	char	*text;
	int	len;

	// SHR overrides the TEXT switch on screen, so the text page may be stale.
	if((!(g_cur_a2_stat & ALL_STAT_TEXT)) ||
			(g_cur_a2_stat & ALL_STAT_SUPER_HIRES)) {
		return 0;
	}
	text = cfg_text_screen_str();
	len = strlen(text);
	while((len > 0) && (text[len-1] == '\n')) {
		len--;
	}
	text[len] = 0;
	return text;
}

/* Feeds host text through the ADB keystroke paste buffer.  Returns 1 if the
 * buffer filled before the whole text fit. */
int
clip_paste_text(const char *text)
{
	const byte	*p;
	int		c;

	for(p = (const byte *)text; *p; p++) {
		c = *p;
		if(c == '\t') {
			c = ' ';
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

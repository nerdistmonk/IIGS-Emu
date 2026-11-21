/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Copyright 2025-2026 GSplus Contributors         */
/*                                                                    */
/*      This code is covered by the GNU GPL v3                        */
/*      See the file COPYING.txt or https://www.gnu.org/licenses/     */
/**********************************************************************/

/*
 * Host input mapping.
 *
 * Every host input is bound here: SDL keys, mouse buttons, gamepad buttons
 * and gamepad axes map to an IIgs ADB keycode, the IIgs mouse button, a
 * game-port control, or an emulator action.  The ADB core only sees IIgs
 * input.  This file has no SDL dependency; sdl_driver.c passes raw values.
 *
 * Map file (input.map beside config.kegs, or -map <file>), one binding per
 * line:
 *
 *     source = target [invert]
 *
 * '#' starts a comment.  Names are case-insensitive.  A line for a source
 * replaces the built-in binding for that same source.  Bad lines print a
 * warning and are skipped.
 *
 * Sources:
 *     key:NAME        SDL scancode name without SDL_SCANCODE_, e.g.
 *                     key:TAB, key:KP_1, key:LGUI, key:F11, or key:0x2b
 *     shift+ ctrl+ alt+ gui+
 *                     optional modifier prefixes, key sources only,
 *                     e.g. ctrl+key:F11
 *     mouse:LEFT|MIDDLE|RIGHT
 *     pad:NAME        gamepad button: SOUTH EAST WEST NORTH BACK GUIDE
 *                     START LEFT_STICK RIGHT_STICK LEFT_SHOULDER
 *                     RIGHT_SHOULDER DPAD_UP DPAD_DOWN DPAD_LEFT
 *                     DPAD_RIGHT MISC1 RIGHT_PADDLE1 LEFT_PADDLE1
 *                     RIGHT_PADDLE2 LEFT_PADDLE2 TOUCHPAD MISC2..MISC6
 *     axis:NAME       gamepad axis: LEFTX LEFTY RIGHTX RIGHTY
 *                     LEFT_TRIGGER RIGHT_TRIGGER
 *
 * Targets:
 *     adb:0xNN        IIgs keycode, 0x00-0x7f
 *     mouse:1         IIgs mouse button 1 (the only IIgs mouse button)
 *     pdl:N           game-port paddle N=0-3, axis or digital source
 *     butn:N          game-port digital button N=0-3
 *     action:NAME     emulator action: mouse_grab config_panel status
 *                     speed debug_go fast_disk warp_pointer screenshot
 *                     fullscreen scanlines crt reset
 *     none            unbound; overrides a built-in binding
 *
 * "invert" after a pdl:N target reverses that source's direction.
 *
 * Matching rules:
 * - A key binding matches when all of its modifiers are held.  The binding
 *   with the most modifiers wins; on a tie, the first one listed wins.
 * - Several sources may share a target.  The target is held while any of
 *   its sources is held, so an ADB key is released only once every source
 *   for it has been released.
 * - Emulator actions run on press only.  Nothing is sent on release.
 * - For pdl, the first axis binding listed for that channel gives the analog
 *   value.  Held digital sources add full deflection while they are held, so
 *   they sum with the axis, and the total is clamped.  A channel with no
 *   binding reads as an unplugged pin.
 */

#include "defc.h"
#include "input_map.h"


#define IMAP_MAX_BINDINGS	512
#define IMAP_NUM_SCANCODES	512
#define IMAP_LINE_MAX		256
#define IMAP_COUNT(a)		((int)(sizeof(a) / sizeof((a)[0])))

enum {
	IMAP_SRC_KEY,
	IMAP_SRC_MOUSE,
	IMAP_SRC_PAD,
	IMAP_SRC_AXIS
};

enum {
	IMAP_TGT_NONE,
	IMAP_TGT_ADB,
	IMAP_TGT_MOUSE1,
	IMAP_TGT_PDL,
	IMAP_TGT_BUTN,
	IMAP_TGT_ACTION
};

struct imap_binding {
	int	src_kind;
	int	src;
	int	mods;
	int	tgt_kind;
	int	tgt;
	int	invert;
};

struct imap_name {
	const char	*name;
	int		value;
};

/* SDL_SCANCODE_* values; see SDL_scancode.h */
static const struct imap_name g_key_names[] = {
	{ "RETURN", 40 }, { "ESCAPE", 41 }, { "BACKSPACE", 42 },
	{ "TAB", 43 }, { "SPACE", 44 }, { "MINUS", 45 }, { "EQUALS", 46 },
	{ "LEFTBRACKET", 47 }, { "RIGHTBRACKET", 48 }, { "BACKSLASH", 49 },
	{ "NONUSHASH", 50 }, { "SEMICOLON", 51 }, { "APOSTROPHE", 52 },
	{ "GRAVE", 53 }, { "COMMA", 54 }, { "PERIOD", 55 }, { "SLASH", 56 },
	{ "CAPSLOCK", 57 }, { "PRINTSCREEN", 70 }, { "SCROLLLOCK", 71 },
	{ "PAUSE", 72 }, { "INSERT", 73 }, { "HOME", 74 }, { "PAGEUP", 75 },
	{ "DELETE", 76 }, { "END", 77 }, { "PAGEDOWN", 78 }, { "RIGHT", 79 },
	{ "LEFT", 80 }, { "DOWN", 81 }, { "UP", 82 }, { "NUMLOCKCLEAR", 83 },
	{ "KP_DIVIDE", 84 }, { "KP_MULTIPLY", 85 }, { "KP_MINUS", 86 },
	{ "KP_PLUS", 87 }, { "KP_ENTER", 88 }, { "KP_1", 89 }, { "KP_2", 90 },
	{ "KP_3", 91 }, { "KP_4", 92 }, { "KP_5", 93 }, { "KP_6", 94 },
	{ "KP_7", 95 }, { "KP_8", 96 }, { "KP_9", 97 }, { "KP_0", 98 },
	{ "KP_PERIOD", 99 }, { "NONUSBACKSLASH", 100 },
	{ "APPLICATION", 101 }, { "POWER", 102 }, { "KP_EQUALS", 103 },
	{ "MENU", 118 }, { "KP_COMMA", 133 }, { "LCTRL", 224 },
	{ "LSHIFT", 225 }, { "LALT", 226 }, { "LGUI", 227 }, { "RCTRL", 228 },
	{ "RSHIFT", 229 }, { "RALT", 230 }, { "RGUI", 231 }
};

static const struct imap_name g_mouse_names[] = {
	{ "LEFT", IMAP_MOUSE_LEFT }, { "MIDDLE", IMAP_MOUSE_MIDDLE },
	{ "RIGHT", IMAP_MOUSE_RIGHT }
};

/* SDL_GAMEPAD_BUTTON_* values; see SDL_gamepad.h */
static const struct imap_name g_pad_names[] = {
	{ "SOUTH", 0 }, { "EAST", 1 }, { "WEST", 2 }, { "NORTH", 3 },
	{ "BACK", 4 }, { "GUIDE", 5 }, { "START", 6 }, { "LEFT_STICK", 7 },
	{ "RIGHT_STICK", 8 }, { "LEFT_SHOULDER", 9 },
	{ "RIGHT_SHOULDER", 10 }, { "DPAD_UP", 11 }, { "DPAD_DOWN", 12 },
	{ "DPAD_LEFT", 13 }, { "DPAD_RIGHT", 14 }, { "MISC1", 15 },
	{ "RIGHT_PADDLE1", 16 }, { "LEFT_PADDLE1", 17 },
	{ "RIGHT_PADDLE2", 18 }, { "LEFT_PADDLE2", 19 }, { "TOUCHPAD", 20 },
	{ "MISC2", 21 }, { "MISC3", 22 }, { "MISC4", 23 }, { "MISC5", 24 },
	{ "MISC6", 25 }
};

/* SDL_GAMEPAD_AXIS_* values; see SDL_gamepad.h */
static const struct imap_name g_axis_names[] = {
	{ "LEFTX", 0 }, { "LEFTY", 1 }, { "RIGHTX", 2 }, { "RIGHTY", 3 },
	{ "LEFT_TRIGGER", 4 }, { "RIGHT_TRIGGER", 5 }
};

static const struct imap_name g_action_names[] = {
	{ "mouse_grab", IMAP_ACT_MOUSE_GRAB },
	{ "config_panel", IMAP_ACT_CONFIG_PANEL },
	{ "status", IMAP_ACT_STATUS },
	{ "speed", IMAP_ACT_SPEED },
	{ "debug_go", IMAP_ACT_DEBUG_GO },
	{ "fast_disk", IMAP_ACT_FAST_DISK },
	{ "warp_pointer", IMAP_ACT_WARP_POINTER },
	{ "screenshot", IMAP_ACT_SCREENSHOT },
	{ "fullscreen", IMAP_ACT_FULLSCREEN },
	{ "scanlines", IMAP_ACT_SCANLINES },
	{ "crt", IMAP_ACT_CRT },
	{ "reset", IMAP_ACT_RESET }
};

/* Built-in profile, same syntax as input.map.  A line in the map file for
 * the same source replaces the entry here.  Example of a user line:
 *     key:F2 = adb:0x35
 */
static const char *const g_default_map[] = {
	"key:A = adb:0x00",
	"key:S = adb:0x01",
	"key:D = adb:0x02",
	"key:F = adb:0x03",
	"key:H = adb:0x04",
	"key:G = adb:0x05",
	"key:Z = adb:0x06",
	"key:X = adb:0x07",
	"key:C = adb:0x08",
	"key:V = adb:0x09",
	"key:B = adb:0x0b",
	"key:Q = adb:0x0c",
	"key:W = adb:0x0d",
	"key:E = adb:0x0e",
	"key:R = adb:0x0f",
	"key:Y = adb:0x10",
	"key:T = adb:0x11",
	"key:O = adb:0x1f",
	"key:U = adb:0x20",
	"key:I = adb:0x22",
	"key:P = adb:0x23",
	"key:L = adb:0x25",
	"key:J = adb:0x26",
	"key:K = adb:0x28",
	"key:N = adb:0x2d",
	"key:M = adb:0x2e",
	"key:1 = adb:0x12",
	"key:2 = adb:0x13",
	"key:3 = adb:0x14",
	"key:4 = adb:0x15",
	"key:5 = adb:0x17",
	"key:6 = adb:0x16",
	"key:7 = adb:0x1a",
	"key:8 = adb:0x1c",
	"key:9 = adb:0x19",
	"key:0 = adb:0x1d",
	"key:MINUS = adb:0x1b",
	"key:EQUALS = adb:0x18",
	"key:LEFTBRACKET = adb:0x21",
	"key:RIGHTBRACKET = adb:0x1e",
	"key:BACKSLASH = adb:0x2a",
	"key:SEMICOLON = adb:0x29",
	"key:APOSTROPHE = adb:0x27",
	"key:GRAVE = adb:0x32",
	"key:COMMA = adb:0x2b",
	"key:PERIOD = adb:0x2f",
	"key:SLASH = adb:0x2c",
	"key:TAB = adb:0x30",
	"key:SPACE = adb:0x31",
	"key:RETURN = adb:0x24",
	"key:ESCAPE = adb:0x35",
	"key:BACKSPACE = adb:0x33",
	"key:DELETE = adb:0x47",
	"key:LSHIFT = adb:0x38",
	"key:RSHIFT = adb:0x38",
	"key:LGUI = adb:0x36",
	"key:LCTRL = adb:0x3a",
	"key:LALT = adb:0x37",
	"key:LEFT = adb:0x3b",
	"key:RIGHT = adb:0x3c",
	"key:DOWN = adb:0x3d",
	"key:UP = adb:0x3e",
	"key:KP_EQUALS = adb:0x51",
	"key:KP_DIVIDE = adb:0x4b",
	"key:KP_MULTIPLY = adb:0x43",
	"key:KP_7 = adb:0x59",
	"key:KP_8 = adb:0x5b",
	"key:KP_9 = adb:0x5c",
	"key:KP_MINUS = adb:0x4e",
	"key:KP_4 = adb:0x56",
	"key:KP_5 = adb:0x57",
	"key:KP_6 = adb:0x58",
	"key:KP_PLUS = adb:0x45",
	"key:KP_1 = adb:0x53",
	"key:KP_2 = adb:0x54",
	"key:KP_3 = adb:0x55",
	"key:KP_0 = adb:0x52",
	"key:KP_PERIOD = adb:0x41",
	"key:KP_ENTER = adb:0x4c",
	"mouse:LEFT = mouse:1",
	"key:F3 = action:mouse_grab",
	"key:F4 = action:config_panel",
	"key:F5 = action:status",
	"key:F6 = action:speed",
	"key:F7 = action:debug_go",
	"shift+key:F7 = action:fast_disk",
	"key:F8 = action:warp_pointer",
	"key:F9 = adb:0x7f",
	"key:F10 = action:screenshot",
	/* ctrl+ is listed before shift+ so Ctrl+Shift+F11 selects CRT */
	"key:F11 = action:fullscreen",
	"ctrl+key:F11 = action:crt",
	"shift+key:F11 = action:scanlines",
	"key:F12 = action:reset"
};

static struct imap_binding	g_bindings[IMAP_MAX_BINDINGS];
static int	g_num_bindings = 0;

static int	g_key_active[IMAP_NUM_SCANCODES];	/* binding index + 1, 0 = none */
static int	g_mouse_active[IMAP_NUM_MOUSE];
static int	g_pad_active[IMAP_NUM_PAD_BUTTONS];
static int	g_axis_val[IMAP_NUM_AXES];

static int	g_adb_level[128];
static int	g_mouse1_level;
static int	g_butn_level[IMAP_NUM_BUTN];
/* Held digital sources per pdl channel: [0] drives up, [1] drives down */
static int	g_pdl_level[IMAP_NUM_PDL][2];
static void	(*g_host_hook)(int action) = NULL;

extern int g_halt_sim;
extern int g_status_enable;
extern int g_fast_disk_emul_en;
extern int g_warp_pointer;
extern int g_hide_pointer;
extern int g_mouse_raw_x;
extern int g_mouse_raw_y;

static int
imap_lookup(const struct imap_name *tab, int count, const char *name)
{
	int	i;

	for(i = 0; i < count; i++) {
		if(!strcasecmp(tab[i].name, name)) {
			return tab[i].value;
		}
	}
	return -1;
}

static int
imap_parse_number(const char *s, int max, int *out)
{
	char	*end;
	long	v;

	v = strtol(s, &end, 0);
	if((end == s) || (*end != '\0') || (v < 0) || (v > max)) {
		return -1;
	}
	*out = (int)v;
	return 0;
}

static int
imap_key_lookup(const char *name)
{
	int	n, val;
	size_t	len;

	if(!strncasecmp(name, "0x", 2)) {
		if(imap_parse_number(name, IMAP_NUM_SCANCODES - 1, &val) < 0) {
			return -1;
		}
		return val;
	}
	len = strlen(name);
	if((len == 1) && isalpha((unsigned char)name[0])) {
		return 4 + (toupper((unsigned char)name[0]) - 'A');
	}
	if((len == 1) && isdigit((unsigned char)name[0])) {
		return (name[0] == '0') ? 39 : (30 + (name[0] - '1'));
	}
	if((toupper((unsigned char)name[0]) == 'F') && (len > 1) &&
				(strspn(name + 1, "0123456789") == len - 1)) {
		n = atoi(name + 1);
		if((n >= 1) && (n <= 12)) {
			return 58 + (n - 1);
		}
		if((n >= 13) && (n <= 24)) {
			return 104 + (n - 13);
		}
		return -1;
	}
	return imap_lookup(g_key_names, IMAP_COUNT(g_key_names), name);
}

static char *
imap_trim(char *s)
{
	char	*end;

	while(isspace((unsigned char)*s)) {
		s++;
	}
	end = s + strlen(s);
	while((end > s) && isspace((unsigned char)end[-1])) {
		end--;
	}
	*end = '\0';
	return s;
}

static const char *
imap_parse_source(char *s, struct imap_binding *b)
{
	static const struct {
		const char	*prefix;
		int		bit;
	} mods[] = {
		{ "shift+", IMAP_MOD_SHIFT },
		{ "ctrl+", IMAP_MOD_CTRL },
		{ "alt+", IMAP_MOD_ALT },
		{ "gui+", IMAP_MOD_GUI }
	};
	char	*name;
	int	i, found;

	b->mods = 0;
	do {
		found = 0;
		for(i = 0; i < IMAP_COUNT(mods); i++) {
			size_t	len = strlen(mods[i].prefix);

			if(!strncasecmp(s, mods[i].prefix, len)) {
				b->mods |= mods[i].bit;
				s += len;
				found = 1;
			}
		}
	} while(found);

	name = strchr(s, ':');
	if(!name) {
		return "source must be kind:NAME";
	}
	*name++ = '\0';
	if(!strcasecmp(s, "key")) {
		b->src_kind = IMAP_SRC_KEY;
		b->src = imap_key_lookup(name);
		if(b->src < 0) {
			return "unknown key name";
		}
	} else if(!strcasecmp(s, "mouse")) {
		b->src_kind = IMAP_SRC_MOUSE;
		b->src = imap_lookup(g_mouse_names, IMAP_COUNT(g_mouse_names),
									name);
		if(b->src < 0) {
			return "unknown mouse button (LEFT, MIDDLE, RIGHT)";
		}
	} else if(!strcasecmp(s, "pad")) {
		b->src_kind = IMAP_SRC_PAD;
		b->src = imap_lookup(g_pad_names, IMAP_COUNT(g_pad_names), name);
		if(b->src < 0) {
			return "unknown gamepad button name";
		}
	} else if(!strcasecmp(s, "axis")) {
		b->src_kind = IMAP_SRC_AXIS;
		b->src = imap_lookup(g_axis_names, IMAP_COUNT(g_axis_names),
									name);
		if(b->src < 0) {
			return "unknown gamepad axis name";
		}
	} else {
		return "unknown source kind (use key, mouse, pad, axis)";
	}
	if(b->mods && (b->src_kind != IMAP_SRC_KEY)) {
		return "modifiers apply to key sources only";
	}
	return NULL;
}

static const char *
imap_parse_target(const char *s, struct imap_binding *b)
{
	char	word[64], opt[64], extra[2], *colon;
	int	n, val;

	n = sscanf(s, "%63s %63s %1s", word, opt, extra);
	if(n < 1) {
		return "missing target";
	}
	if(n == 3) {
		return "unexpected text after target";
	}
	if(!strcasecmp(word, "none")) {
		if(n != 1) {
			return "'none' takes no options";
		}
		b->tgt_kind = IMAP_TGT_NONE;
		return NULL;
	}
	if((n == 2) && strcasecmp(opt, "invert")) {
		return "unknown option, expected 'invert'";
	}
	b->invert = (n == 2);

	colon = strchr(word, ':');
	if(!colon) {
		return "target must be none or kind:value";
	}
	*colon++ = '\0';
	if(!strcasecmp(word, "adb")) {
		if(imap_parse_number(colon, 0x7f, &val) < 0) {
			return "adb target must be a keycode 0x00-0x7f";
		}
		b->tgt_kind = IMAP_TGT_ADB;
		b->tgt = val;
	} else if(!strcasecmp(word, "mouse")) {
		if((imap_parse_number(colon, 1, &val) < 0) || (val != 1)) {
			return "mouse target must be mouse:1";
		}
		b->tgt_kind = IMAP_TGT_MOUSE1;
		b->tgt = 0;
	} else if(!strcasecmp(word, "pdl") || !strcasecmp(word, "butn")) {
		if(imap_parse_number(colon, IMAP_NUM_PDL - 1, &val) < 0) {
			return "pdl and butn take a number 0-3";
		}
		b->tgt_kind = (!strcasecmp(word, "pdl")) ? IMAP_TGT_PDL :
							IMAP_TGT_BUTN;
		b->tgt = val;
	} else if(!strcasecmp(word, "action")) {
		val = imap_lookup(g_action_names, IMAP_COUNT(g_action_names),
								colon);
		if(val < 0) {
			return "unknown action name";
		}
		b->tgt_kind = IMAP_TGT_ACTION;
		b->tgt = val;
	} else {
		return "unknown target kind (use adb, mouse, pdl, butn, action)";
	}
	return NULL;
}

static const char *
imap_check(const struct imap_binding *b)
{
	int	is_axis = (b->src_kind == IMAP_SRC_AXIS);
	int	is_pdl = (b->tgt_kind == IMAP_TGT_PDL);

	if(is_axis && !is_pdl && (b->tgt_kind != IMAP_TGT_NONE)) {
		return "axis sources map only to pdl:N";
	}
	if(b->invert && !is_pdl) {
		return "'invert' applies only to pdl:N targets";
	}
	return NULL;
}

static int
imap_store(const struct imap_binding *b)
{
	int	i;

	for(i = 0; i < g_num_bindings; i++) {
		struct imap_binding *old = &g_bindings[i];

		if((old->src_kind == b->src_kind) && (old->src == b->src) &&
						(old->mods == b->mods)) {
			*old = *b;
			return 0;
		}
	}
	if(g_num_bindings >= IMAP_MAX_BINDINGS) {
		return -1;
	}
	g_bindings[g_num_bindings++] = *b;
	return 0;
}

static void
imap_parse_line(char *line, const char *origin, int lineno)
{
	struct imap_binding	b;
	const char	*err;
	char	*src, *tgt;

	src = strchr(line, '#');
	if(src) {
		*src = '\0';
	}
	src = imap_trim(line);
	if(*src == '\0') {
		return;
	}
	tgt = strchr(src, '=');
	if(!tgt) {
		err = "expected 'source = target'";
	} else {
		*tgt++ = '\0';
		src = imap_trim(src);
		tgt = imap_trim(tgt);
		memset(&b, 0, sizeof(b));
		err = imap_parse_source(src, &b);
		if(!err) {
			err = imap_parse_target(tgt, &b);
		}
		if(!err) {
			err = imap_check(&b);
		}
		if(!err && (imap_store(&b) < 0)) {
			err = "too many bindings";
		}
	}
	if(err) {
		printf("Input map %s line %d: %s\n", origin, lineno, err);
	}
}

static void
imap_reset_state(void)
{
	memset(g_key_active, 0, sizeof(g_key_active));
	memset(g_mouse_active, 0, sizeof(g_mouse_active));
	memset(g_pad_active, 0, sizeof(g_pad_active));
	memset(g_axis_val, 0, sizeof(g_axis_val));
	memset(g_adb_level, 0, sizeof(g_adb_level));
	memset(g_butn_level, 0, sizeof(g_butn_level));
	memset(g_pdl_level, 0, sizeof(g_pdl_level));
	g_mouse1_level = 0;
}

void
imap_init(void)
{
	int	i;

	g_num_bindings = 0;
	imap_reset_state();
	for(i = 0; i < IMAP_COUNT(g_default_map); i++) {
		char	line[IMAP_LINE_MAX];

		snprintf(line, sizeof(line), "%s", g_default_map[i]);
		imap_parse_line(line, "built-in default", i + 1);
	}
}

int
imap_load_file(const char *path)
{
	FILE	*f;
	char	line[IMAP_LINE_MAX];
	int	lineno, c;

	f = fopen(path, "r");
	if(!f) {
		return -1;
	}
	lineno = 0;
	while(fgets(line, sizeof(line), f)) {
		lineno++;
		if(!strchr(line, '\n') && !feof(f)) {
			printf("Input map %s line %d: line too long, skipped\n",
								path, lineno);
			do {
				c = fgetc(f);
			} while((c != '\n') && (c != EOF));
			continue;
		}
		imap_parse_line(line, path, lineno);
	}
	fclose(f);
	return 0;
}

/* Header of the map file written on first run.  The bindings after it are
 * the built-in defaults, commented out, so the file documents them without
 * overriding them.
 */
static const char *const g_map_file_header[] = {
	"# GSplus input map.  One binding per line: source = target [invert]",
	"# Sources: key:NAME mouse:LEFT|MIDDLE|RIGHT pad:NAME axis:NAME",
	"#          (keys take shift+ ctrl+ alt+ gui+ prefixes)",
	"# Targets: adb:0xNN mouse:1 pdl:N [invert] butn:N action:NAME none",
	"# A line replaces the built-in binding for its source.  \"= none\" unbinds it.",
	"# The bindings below are the built-in defaults, commented out.",
	""
};

/* Creates the file only if it does not exist (O_EXCL), so a user's map is
 * never overwritten.  Returns 0 on success.
 */
int
imap_write_default_file(const char *path)
{
	FILE	*f;
	int	fd, i;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if(fd < 0) {
		return -1;
	}
	f = fdopen(fd, "w");
	if(!f) {
		close(fd);
		return -1;
	}
	for(i = 0; i < IMAP_COUNT(g_map_file_header); i++) {
		fprintf(f, "%s\n", g_map_file_header[i]);
	}
	for(i = 0; i < IMAP_COUNT(g_default_map); i++) {
		fprintf(f, "# %s\n", g_default_map[i]);
	}
	return (fclose(f) == 0) ? 0 : -1;
}

void
imap_set_host_hook(void (*hook)(int action))
{
	g_host_hook = hook;
}

static int
imap_action_is_host(int action)
{
	switch(action) {
	case IMAP_ACT_MOUSE_GRAB:
	case IMAP_ACT_SCREENSHOT:
	case IMAP_ACT_FULLSCREEN:
	case IMAP_ACT_SCANLINES:
	case IMAP_ACT_CRT:
		return 1;
	default:
		return 0;
	}
}

static void
imap_run_action(int action)
{
	if(imap_action_is_host(action)) {
		if(g_host_hook) {
			g_host_hook(action);
		}
		return;
	}
	switch(action) {
	case IMAP_ACT_CONFIG_PANEL:
		cfg_toggle_config_panel();
		break;
	case IMAP_ACT_STATUS:
		g_status_enable = !g_status_enable;
		break;
	case IMAP_ACT_SPEED:
		adb_increment_speed();
		break;
	case IMAP_ACT_DEBUG_GO:
		if(g_halt_sim) {
			do_go();
		} else {
			halt2_printf("F7 pressed\n");
		}
		break;
	case IMAP_ACT_FAST_DISK:
		g_fast_disk_emul_en = !g_fast_disk_emul_en;
		iwm_update_fast_disk_emul(g_fast_disk_emul_en);
		printf("g_fast_disk_emul_en is now %d\n", g_fast_disk_emul_en);
		break;
	case IMAP_ACT_WARP_POINTER:
		g_warp_pointer = !g_warp_pointer;
		g_hide_pointer = g_warp_pointer;
		printf("New warp:%d, new hide:%d\n", g_warp_pointer,
							g_hide_pointer);
		break;
	case IMAP_ACT_RESET:
		do_reset();
		break;
	default:
		break;
	}
}

/* Returns 1 when the count crosses zero: 0->1 on press, 1->0 on release */
static int
imap_hold(int *level, int is_up)
{
	if(is_up) {
		return (--(*level) == 0);
	}
	return ((*level)++ == 0);
}

static void
imap_set_mouse1(Kimage *km, int down)
{
	/* Reports the last host position so the click does not move the cursor */
	adb_update_mouse(km, g_mouse_raw_x, g_mouse_raw_y, down ? 1 : 0, 1);
}

static void
imap_binding_event(Kimage *km, const struct imap_binding *b, int is_up)
{
	switch(b->tgt_kind) {
	case IMAP_TGT_ADB:
		if(imap_hold(&g_adb_level[b->tgt], is_up)) {
			adb_physical_key_update(km, b->tgt, is_up);
		}
		break;
	case IMAP_TGT_MOUSE1:
		if(imap_hold(&g_mouse1_level, is_up)) {
			imap_set_mouse1(km, !is_up);
		}
		break;
	case IMAP_TGT_BUTN:
		imap_hold(&g_butn_level[b->tgt], is_up);
		break;
	case IMAP_TGT_PDL:
		g_pdl_level[b->tgt][b->invert ? 1 : 0] += is_up ? -1 : 1;
		break;
	case IMAP_TGT_ACTION:
		if(!is_up) {
			imap_run_action(b->tgt);
		}
		break;
	default:
		break;
	}
}

static int
imap_count_bits(int v)
{
	int	n = 0;

	while(v) {
		n += v & 1;
		v >>= 1;
	}
	return n;
}

static int
imap_find(int kind, int src, int mods)
{
	int	i, best, best_bits, bits;

	best = -1;
	best_bits = -1;
	for(i = 0; i < g_num_bindings; i++) {
		const struct imap_binding *b = &g_bindings[i];

		if((b->src_kind != kind) || (b->src != src) ||
						(b->mods & ~mods)) {
			continue;
		}
		bits = imap_count_bits(b->mods);
		if(bits > best_bits) {
			best = i;
			best_bits = bits;
		}
	}
	return best;
}

/* Presses and releases one source.  *active remembers which binding the
 * press matched, so the release undoes exactly that binding even if the
 * modifiers changed while the key was held. */
static void
imap_source_event(Kimage *km, int *active, int kind, int src, int mods,
								int is_up)
{
	int	idx;

	if(is_up) {
		if(*active) {
			idx = *active - 1;
			*active = 0;
			imap_binding_event(km, &g_bindings[idx], 1);
		}
		return;
	}
	if(*active) {
		return;
	}
	idx = imap_find(kind, src, mods);
	if(idx < 0) {
		return;
	}
	*active = idx + 1;
	imap_binding_event(km, &g_bindings[idx], 0);
}

void
imap_key_event(Kimage *km, int scancode, int mods, int is_up)
{
	if((scancode < 0) || (scancode >= IMAP_NUM_SCANCODES)) {
		return;
	}
	imap_source_event(km, &g_key_active[scancode], IMAP_SRC_KEY, scancode,
								mods, is_up);
}

void
imap_mouse_button(Kimage *km, int button, int is_up)
{
	if((button < 0) || (button >= IMAP_NUM_MOUSE)) {
		return;
	}
	imap_source_event(km, &g_mouse_active[button], IMAP_SRC_MOUSE, button,
								0, is_up);
}

void
imap_pad_button(Kimage *km, int button, int is_up)
{
	if(!km || (button < 0) || (button >= IMAP_NUM_PAD_BUTTONS)) {
		return;
	}
	imap_source_event(km, &g_pad_active[button], IMAP_SRC_PAD, button, 0,
								is_up);
}

void
imap_pad_axis(int axis, int value)
{
	if((axis < 0) || (axis >= IMAP_NUM_AXES)) {
		return;
	}
	g_axis_val[axis] = value;
}

static void
imap_pad_release_buttons(Kimage *km)
{
	int	i;

	for(i = 0; i < IMAP_NUM_PAD_BUTTONS; i++) {
		imap_source_event(km, &g_pad_active[i], IMAP_SRC_PAD, i, 0, 1);
	}
}

/* For a gamepad that was unplugged: buttons released, sticks centered */
void
imap_pad_release_all(Kimage *km)
{
	int	i;

	imap_pad_release_buttons(km);
	for(i = 0; i < IMAP_NUM_AXES; i++) {
		g_axis_val[i] = 0;
	}
}

/* For losing host focus: keys and buttons released, stick positions kept */
void
imap_release_all(Kimage *km)
{
	int	i;

	for(i = 0; i < IMAP_NUM_SCANCODES; i++) {
		imap_source_event(km, &g_key_active[i], IMAP_SRC_KEY, i, 0, 1);
	}
	for(i = 0; i < IMAP_NUM_MOUSE; i++) {
		imap_source_event(km, &g_mouse_active[i], IMAP_SRC_MOUSE, i, 0,
									1);
	}
	imap_pad_release_buttons(km);
}

static const struct imap_binding *
imap_find_pdl_axis(int ch)
{
	int	i;

	for(i = 0; i < g_num_bindings; i++) {
		const struct imap_binding *b = &g_bindings[i];

		if((b->src_kind == IMAP_SRC_AXIS) &&
				(b->tgt_kind == IMAP_TGT_PDL) && (b->tgt == ch)) {
			return b;
		}
	}
	return NULL;
}

int
imap_pdl_value(int ch)
{
	const struct imap_binding *b;
	int	v = 0;

	if((ch < 0) || (ch >= IMAP_NUM_PDL)) {
		return 0;
	}
	b = imap_find_pdl_axis(ch);
	if(b) {
		v = g_axis_val[b->src];
		if(b->invert) {
			v = (v == -32768) ? 32767 : -v;
		}
	}
	v += (g_pdl_level[ch][0] - g_pdl_level[ch][1]) * 32767;
	if(v > 32767) {
		v = 32767;
	}
	if(v < -32768) {
		v = -32768;
	}
	return v;
}

int
imap_pdl_bound(int ch)
{
	int	i;

	if((ch < 0) || (ch >= IMAP_NUM_PDL)) {
		return 0;
	}
	for(i = 0; i < g_num_bindings; i++) {
		if((g_bindings[i].tgt_kind == IMAP_TGT_PDL) &&
						(g_bindings[i].tgt == ch)) {
			return 1;
		}
	}
	return 0;
}

/* True when pdl channels 2p and 2p+1 are the X and Y axes of one stick, so
 * the paddle code can apply the circle-to-square gate to that pair. */
int
imap_pdl_stick_pair(int p)
{
	const struct imap_binding *x, *y;

	if((p < 0) || (p >= IMAP_NUM_PDL / 2)) {
		return 0;
	}
	x = imap_find_pdl_axis(2 * p);
	y = imap_find_pdl_axis(2 * p + 1);
	if(!x || !y) {
		return 0;
	}
	return (x->src < 4) && (y->src < 4) && (x->src != y->src) &&
				((x->src / 2) == (y->src / 2));
}

int
imap_butn_pressed(int n)
{
	if((n < 0) || (n >= IMAP_NUM_BUTN)) {
		return 0;
	}
	return g_butn_level[n] > 0;
}

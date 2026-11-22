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
 *                     DPAD_X DPAD_Y: the D-pad as whole axes, full
 *                     deflection to either side while a direction is held
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
 * "invert" after a pdl:N target reverses that source's direction.  Triggers
 * press past half travel and may drive any target; sticks and the d-pad
 * drive only pdl:N.
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

struct imap_remap_item {
	const char	*name;
	int		group;
	int		tgt_kind;
	int		tgt;
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
	{ "LEFT_TRIGGER", 4 }, { "RIGHT_TRIGGER", 5 },
	{ "DPAD_X", IMAP_AXIS_DPAD_X }, { "DPAD_Y", IMAP_AXIS_DPAD_Y }
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
	"key:DELETE = adb:0x75",
	"key:LSHIFT = adb:0x38",
	"key:RSHIFT = adb:0x38",
	"key:LCTRL = adb:0x36",
	"key:RCTRL = adb:0x36",
	"key:LALT = adb:0x3a",
	"key:RALT = adb:0x3a",
	"key:LGUI = adb:0x37",
	"key:RGUI = adb:0x37",
	"key:LEFT = adb:0x3b",
	"key:RIGHT = adb:0x3c",
	"key:DOWN = adb:0x3d",
	"key:UP = adb:0x3e",
	"key:INSERT = adb:0x72",
	"key:HOME = adb:0x73",
	"key:PAGEUP = adb:0x74",
	"key:END = adb:0x77",
	"key:PAGEDOWN = adb:0x79",
	"key:CAPSLOCK = adb:0x39",
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
	"key:NUMLOCKCLEAR = adb:0x47",
	"mouse:LEFT = mouse:1",
	"mouse:MIDDLE = action:mouse_grab",
	"ctrl+key:F1 = action:config_panel",
	"ctrl+key:F2 = action:mouse_grab",
	"ctrl+key:F12 = action:reset",
	"key:F1 = adb:0x7a",
	"key:F2 = adb:0x78",
	"key:F3 = adb:0x63",
	"key:F4 = adb:0x76",
	"key:F5 = adb:0x60",
	"key:F6 = adb:0x61",
	"key:F7 = adb:0x62",
	"key:F8 = adb:0x64",
	"key:F9 = adb:0x65",
	"key:F10 = adb:0x6d",
	"key:F11 = adb:0x67",
	"key:F12 = adb:0x6f",
	"key:PRINTSCREEN = adb:0x69",
	"key:SCROLLLOCK = adb:0x6b",
	"key:PAUSE = adb:0x71"
};

/* D-pad directions, indexed by g_dpad_held */
enum {
	IMAP_DIR_UP,
	IMAP_DIR_DOWN,
	IMAP_DIR_LEFT,
	IMAP_DIR_RIGHT,
	IMAP_NUM_DIRS
};

#define IMAP_PAD_DPAD_UP	11	/* SDL_GAMEPAD_BUTTON_DPAD_UP */
#define IMAP_PAD_DPAD_DOWN	12
#define IMAP_PAD_DPAD_LEFT	13
#define IMAP_PAD_DPAD_RIGHT	14

static struct imap_binding	g_bindings[IMAP_MAX_BINDINGS];
static int	g_num_bindings = 0;

static int	g_key_active[IMAP_NUM_SCANCODES];	/* binding index + 1, 0 = none */
static int	g_mouse_active[IMAP_NUM_MOUSE];
static int	g_pad_active[IMAP_NUM_PAD_BUTTONS];
static int	g_axis_active[IMAP_NUM_AXES];		/* binding index + 1 */
static int	g_trigger_swallow[IMAP_NUM_AXES];	/* until released */
static int	g_axis_val[IMAP_NUM_AXES];
static int	g_dpad_held[IMAP_NUM_DIRS];

static int	g_adb_level[128];
static int	g_caps_latch = 0;
static int	g_mouse1_level;
static int	g_butn_level[IMAP_NUM_BUTN];
/* Held digital sources per pdl channel: [0] drives up, [1] drives down */
static int	g_pdl_level[IMAP_NUM_PDL][2];
static void	(*g_host_hook)(int action) = NULL;

static struct imap_binding	g_default_bindings[IMAP_MAX_BINDINGS];
static int	g_num_default_bindings = 0;

#define IMAP_SC_ESCAPE		41	/* SDL_SCANCODE_ESCAPE; cancels a capture */
#define IMAP_SC_LCTRL		224	/* SDL_SCANCODE_LCTRL; first modifier */
#define IMAP_SC_RGUI		231	/* SDL_SCANCODE_RGUI */
#define IMAP_REMAP_REST		8000	/* stick must rest inside this to arm */
#define IMAP_REMAP_PUSH		16384	/* deflection that captures an axis */
#define IMAP_AXIS_LEFT_TRIGGER	4	/* SDL_GAMEPAD_AXIS_LEFT_TRIGGER */
#define IMAP_AXIS_RIGHT_TRIGGER	5	/* SDL_GAMEPAD_AXIS_RIGHT_TRIGGER */
#define IMAP_TRIGGER_PRESS	16384	/* trigger presses past half travel */
#define IMAP_PATH_MAX		1024
#define IMAP_ADB_CAPS		0x39	/* ADB Caps Lock; latches */
#define IMAP_REMAP_TEXT		160

static struct imap_remap_item	*g_remap_pending = NULL;
/* Modifier pressed during a capture; it binds when released.  0 is unused. */
static int	g_remap_lone_mod = 0;
static Kimage	*g_remap_km = NULL;
static int	g_remap_axis_armed[IMAP_NUM_AXES];
static char	g_remap_prompt[IMAP_REMAP_TEXT];
static char	g_remap_msg[IMAP_REMAP_TEXT];
static int	g_remap_dirty = 0;
static char	g_map_path[IMAP_PATH_MAX] = "";

static int	imap_remap_capture(int kind, int src, int mods);
static void	imap_remap_axis(int axis, int value);
static void	imap_write_body(FILE *f);

extern int g_halt_sim;
extern int g_config_control_panel;
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

static int
imap_is_trigger(int axis)
{
	return (axis == IMAP_AXIS_LEFT_TRIGGER) ||
				(axis == IMAP_AXIS_RIGHT_TRIGGER);
}

static const char *
imap_check(const struct imap_binding *b)
{
	int	is_axis = (b->src_kind == IMAP_SRC_AXIS);
	int	is_pdl = (b->tgt_kind == IMAP_TGT_PDL);

	if(is_axis && !is_pdl && (b->tgt_kind != IMAP_TGT_NONE) &&
						!imap_is_trigger(b->src)) {
		return "axis sources map only to pdl:N, except triggers";
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
	memset(g_axis_active, 0, sizeof(g_axis_active));
	memset(g_trigger_swallow, 0, sizeof(g_trigger_swallow));
	memset(g_axis_val, 0, sizeof(g_axis_val));
	memset(g_dpad_held, 0, sizeof(g_dpad_held));
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
	memcpy(g_default_bindings, g_bindings, sizeof(g_bindings));
	g_num_default_bindings = g_num_bindings;
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

/* Header of the map file.  The bindings after it are the built-in defaults,
 * commented out, so the file documents them without overriding them.
 */
static const char *const g_map_file_header[] = {
	"# GSplus input map.  One binding per line: source = target [invert]",
	"# Sources: key:NAME mouse:LEFT|MIDDLE|RIGHT pad:NAME axis:NAME",
	"#          (keys take shift+ ctrl+ alt+ gui+ prefixes)",
	"# Targets: adb:0xNN mouse:1 pdl:N [invert] butn:N action:NAME none",
	"# A line replaces the built-in binding for its source.  \"= none\" unbinds it.",
	"# Remap Controls rewrites this file on each change; hand-written comments are dropped.",
	""
};

/* Creates the file only if it does not exist (O_EXCL), so a user's map is
 * never overwritten.  Returns 0 on success.
 */
int
imap_write_default_file(const char *path)
{
	FILE	*f;
	int	fd;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if(fd < 0) {
		return -1;
	}
	f = fdopen(fd, "w");
	if(!f) {
		close(fd);
		return -1;
	}
	imap_write_body(f);
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
	/* The config_panel action stays live so Ctrl+F1 can close the panel */
	if(g_config_control_panel && (action != IMAP_ACT_CONFIG_PANEL)) {
		return;
	}
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
			halt2_printf("Debugger halt requested\n");
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

/* Caps Lock latches like the real key: each press toggles it, and the IIgs
 * only sees ADB 0x39 down and up.  Host Caps Lock state is never read.
 */
static void
imap_caps_press(Kimage *km)
{
	g_caps_latch = !g_caps_latch;
	adb_physical_key_update(km, IMAP_ADB_CAPS, g_caps_latch ? 0 : 1);
}

static int
imap_caps_bound(void)
{
	int	i;

	for(i = 0; i < g_num_bindings; i++) {
		if((g_bindings[i].tgt_kind == IMAP_TGT_ADB) &&
					(g_bindings[i].tgt == IMAP_ADB_CAPS)) {
			return 1;
		}
	}
	return 0;
}

/* Releases the latch once no source drives Caps Lock, so the IIgs caps
 * bit cannot stay stuck on.
 */
static void
imap_caps_sync(Kimage *km)
{
	if(g_caps_latch && !imap_caps_bound()) {
		g_caps_latch = 0;
		adb_physical_key_update(km, IMAP_ADB_CAPS, 1);
	}
}

static void
imap_binding_event(Kimage *km, const struct imap_binding *b, int is_up)
{
	switch(b->tgt_kind) {
	case IMAP_TGT_ADB:
		if(b->tgt == IMAP_ADB_CAPS) {
			if(!is_up) {
				imap_caps_press(km);
			}
			break;
		}
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

/* While the config panel is open, only ADB keys reach the menu.  Mouse,
 * button, and paddle sources are ignored, so the IIgs never sees them.
 */
static void
imap_panel_event(const struct imap_binding *b, int mods, int is_up)
{
	if(b->tgt_kind == IMAP_TGT_ADB) {
		adb_panel_key_event(b->tgt, mods & IMAP_MOD_SHIFT, is_up);
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
		/* pdl:N reads axes as analog values, see imap_pdl_value */
		if((kind == IMAP_SRC_AXIS) && (b->tgt_kind == IMAP_TGT_PDL)) {
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
 * modifiers changed while the key was held.  A positive value means the
 * IIgs saw the press.  A negative value means the press went to the config
 * panel, so its release must not change IIgs state.
 */
static void
imap_source_event(Kimage *km, int *active, int kind, int src, int mods,
								int is_up)
{
	int	idx, to_panel;

	if(is_up) {
		if(*active) {
			to_panel = (*active < 0);
			idx = (to_panel ? -*active : *active) - 1;
			*active = 0;
			if(to_panel) {
				imap_panel_event(&g_bindings[idx], mods, 1);
			} else {
				imap_binding_event(km, &g_bindings[idx], 1);
			}
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
	/* Actions stay live so Ctrl+F1 can close the panel */
	if(g_config_control_panel &&
				(g_bindings[idx].tgt_kind != IMAP_TGT_ACTION)) {
		*active = -(idx + 1);
		imap_panel_event(&g_bindings[idx], mods, 0);
		return;
	}
	*active = idx + 1;
	imap_binding_event(km, &g_bindings[idx], 0);
}

/* Modifier keys bind on release, so a modifier pressed alone binds itself.
 * Any other key binds on press with the modifiers held, so Ctrl+F1 is one
 * capture.  Returns 1 when the event was consumed by the capture.
 */
static int
imap_remap_key_event(int scancode, int mods, int is_up)
{
	int	is_mod = (scancode >= IMAP_SC_LCTRL) &&
					(scancode <= IMAP_SC_RGUI);

	if(!g_remap_pending) {
		return 0;
	}
	if(!is_mod) {
		if(is_up) {
			return 0;
		}
		return imap_remap_capture(IMAP_SRC_KEY, scancode, mods);
	}
	if(!is_up) {
		g_remap_lone_mod = scancode;
		return 1;
	}
	if(scancode != g_remap_lone_mod) {
		return 0;
	}
	g_remap_lone_mod = 0;
	return imap_remap_capture(IMAP_SRC_KEY, scancode, 0);
}

void
imap_key_event(Kimage *km, int scancode, int mods, int is_up)
{
	if((scancode < 0) || (scancode >= IMAP_NUM_SCANCODES)) {
		return;
	}
	if(imap_remap_key_event(scancode, mods, is_up)) {
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
	if(!is_up && imap_remap_capture(IMAP_SRC_MOUSE, button, 0)) {
		return;
	}
	imap_source_event(km, &g_mouse_active[button], IMAP_SRC_MOUSE, button,
								0, is_up);
}

static int
imap_remap_wants_axis(int axis)
{
	if(!g_remap_pending) {
		return 0;
	}
	return (g_remap_pending->tgt_kind == IMAP_TGT_PDL) ||
						imap_is_trigger(axis);
}

static int
imap_dpad_dir(int button)
{
	switch(button) {
	case IMAP_PAD_DPAD_UP:
		return IMAP_DIR_UP;
	case IMAP_PAD_DPAD_DOWN:
		return IMAP_DIR_DOWN;
	case IMAP_PAD_DPAD_LEFT:
		return IMAP_DIR_LEFT;
	case IMAP_PAD_DPAD_RIGHT:
		return IMAP_DIR_RIGHT;
	default:
		return -1;
	}
}

/* Sets DPAD_X and DPAD_Y from the held directions.  Both values are stored
 * before any capture, because a capture releases the held directions.
 */
static void
imap_dpad_update(void)
{
	int	x = g_dpad_held[IMAP_DIR_RIGHT] - g_dpad_held[IMAP_DIR_LEFT];
	int	y = g_dpad_held[IMAP_DIR_DOWN] - g_dpad_held[IMAP_DIR_UP];

	g_axis_val[IMAP_AXIS_DPAD_X] = x * 32767;
	g_axis_val[IMAP_AXIS_DPAD_Y] = y * 32767;
	if(imap_remap_wants_axis(IMAP_AXIS_DPAD_X)) {
		imap_remap_axis(IMAP_AXIS_DPAD_X, g_axis_val[IMAP_AXIS_DPAD_X]);
	}
	if(imap_remap_wants_axis(IMAP_AXIS_DPAD_Y)) {
		imap_remap_axis(IMAP_AXIS_DPAD_Y, g_axis_val[IMAP_AXIS_DPAD_Y]);
	}
}

void
imap_pad_button(Kimage *km, int button, int is_up)
{
	int	dir;

	if(!km || (button < 0) || (button >= IMAP_NUM_PAD_BUTTONS)) {
		return;
	}
	dir = imap_dpad_dir(button);
	if(dir >= 0) {
		int	pdl_capture = !is_up &&
					imap_remap_wants_axis(IMAP_AXIS_DPAD_X);

		g_dpad_held[dir] = !is_up;
		imap_dpad_update();
		if(pdl_capture) {
			return;
		}
	}
	if(!is_up && imap_remap_capture(IMAP_SRC_PAD, button, 0)) {
		return;
	}
	imap_source_event(km, &g_pad_active[button], IMAP_SRC_PAD, button, 0,
								is_up);
}

void
imap_pad_axis(Kimage *km, int axis, int value)
{
	int	pressed;

	if((axis < 0) || (axis >= IMAP_NUM_AXES)) {
		return;
	}
	g_axis_val[axis] = value;
	if(!imap_is_trigger(axis)) {
		if(imap_remap_wants_axis(axis)) {
			imap_remap_axis(axis, value);
		}
		return;
	}
	/* A press that starts during a capture, or that is still held from
	 * one, is swallowed.  Releases always reach the binding so a key or
	 * button it was holding is never left down.
	 */
	pressed = (value > IMAP_TRIGGER_PRESS);
	if(g_remap_pending) {
		imap_remap_axis(axis, value);
		g_trigger_swallow[axis] = pressed;
		if(pressed) {
			return;
		}
	} else if(!pressed) {
		g_trigger_swallow[axis] = 0;
	} else if(g_trigger_swallow[axis]) {
		return;
	}
	imap_source_event(km, &g_axis_active[axis], IMAP_SRC_AXIS, axis, 0,
								!pressed);
}

static void
imap_pad_release_buttons(Kimage *km)
{
	int	i;

	for(i = 0; i < IMAP_NUM_PAD_BUTTONS; i++) {
		imap_source_event(km, &g_pad_active[i], IMAP_SRC_PAD, i, 0, 1);
	}
	for(i = IMAP_AXIS_LEFT_TRIGGER; i <= IMAP_AXIS_RIGHT_TRIGGER; i++) {
		imap_source_event(km, &g_axis_active[i], IMAP_SRC_AXIS, i,
								0, 1);
	}
	memset(g_dpad_held, 0, sizeof(g_dpad_held));
	imap_dpad_update();
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
	/* Triggers are polled every frame, so one still held would press
	 * again at once.  Swallow it until it is released.
	 */
	for(i = IMAP_AXIS_LEFT_TRIGGER; i <= IMAP_AXIS_RIGHT_TRIGGER; i++) {
		g_trigger_swallow[i] = (g_axis_val[i] > IMAP_TRIGGER_PRESS);
	}
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

/* Remap Controls items, in menu order.  Names must not contain commas,
 * because cfg_parse_menu uses a comma as a separator.
 */
static struct imap_remap_item	g_remap_items[] = {
	{ "A", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x00 },
	{ "B", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x0b },
	{ "C", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x08 },
	{ "D", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x02 },
	{ "E", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x0e },
	{ "F", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x03 },
	{ "G", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x05 },
	{ "H", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x04 },
	{ "I", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x22 },
	{ "J", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x26 },
	{ "K", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x28 },
	{ "L", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x25 },
	{ "M", IMAP_RG_LETTERS_AM, IMAP_TGT_ADB, 0x2e },
	{ "N", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x2d },
	{ "O", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x1f },
	{ "P", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x23 },
	{ "Q", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x0c },
	{ "R", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x0f },
	{ "S", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x01 },
	{ "T", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x11 },
	{ "U", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x20 },
	{ "V", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x09 },
	{ "W", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x0d },
	{ "X", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x07 },
	{ "Y", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x10 },
	{ "Z", IMAP_RG_LETTERS_NZ, IMAP_TGT_ADB, 0x06 },
	{ "1", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x12 },
	{ "2", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x13 },
	{ "3", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x14 },
	{ "4", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x15 },
	{ "5", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x17 },
	{ "6", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x16 },
	{ "7", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x1a },
	{ "8", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x1c },
	{ "9", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x19 },
	{ "0", IMAP_RG_NUMBERS, IMAP_TGT_ADB, 0x1d },
	{ "Minus", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x1b },
	{ "Equals", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x18 },
	{ "Left Bracket", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x21 },
	{ "Right Bracket", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x1e },
	{ "Backslash", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x2a },
	{ "Semicolon", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x29 },
	{ "Quote", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x27 },
	{ "Tilde", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x32 },
	{ "Comma", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x2b },
	{ "Period", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x2f },
	{ "Slash", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x2c },
	{ "Space", IMAP_RG_PUNCT, IMAP_TGT_ADB, 0x31 },
	{ "Return", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x24 },
	{ "Tab", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x30 },
	{ "Delete", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x33 },
	{ "Escape", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x35 },
	{ "Help", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x72 },
	{ "Home", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x73 },
	{ "Page Up", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x74 },
	{ "Forward Delete", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x75 },
	{ "End", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x77 },
	{ "Page Down", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x79 },
	{ "Left", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x3b },
	{ "Right", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x3c },
	{ "Down", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x3d },
	{ "Up", IMAP_RG_EDITING, IMAP_TGT_ADB, 0x3e },
	{ "Control", IMAP_RG_MODIFIERS, IMAP_TGT_ADB, 0x36 },
	{ "Command", IMAP_RG_MODIFIERS, IMAP_TGT_ADB, 0x37 },
	{ "Shift", IMAP_RG_MODIFIERS, IMAP_TGT_ADB, 0x38 },
	{ "Caps Lock", IMAP_RG_MODIFIERS, IMAP_TGT_ADB, 0x39 },
	{ "Option", IMAP_RG_MODIFIERS, IMAP_TGT_ADB, 0x3a },
	{ "F1", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x7a },
	{ "F2", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x78 },
	{ "F3", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x63 },
	{ "F4", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x76 },
	{ "F5", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x60 },
	{ "F6", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x61 },
	{ "F7", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x62 },
	{ "F8", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x64 },
	{ "F9", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x65 },
	{ "F10", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x6d },
	{ "F11", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x67 },
	{ "F12", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x6f },
	{ "F13", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x69 },
	{ "F14", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x6b },
	{ "F15", IMAP_RG_EXTENDED, IMAP_TGT_ADB, 0x71 },
	{ "Keypad 0", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x52 },
	{ "Keypad 1", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x53 },
	{ "Keypad 2", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x54 },
	{ "Keypad 3", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x55 },
	{ "Keypad 4", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x56 },
	{ "Keypad 5", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x57 },
	{ "Keypad 6", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x58 },
	{ "Keypad 7", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x59 },
	{ "Keypad 8", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x5b },
	{ "Keypad 9", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x5c },
	{ "Keypad Period", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x41 },
	{ "Keypad Plus", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x45 },
	{ "Keypad Minus", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x4e },
	{ "Keypad Multiply", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x43 },
	{ "Keypad Divide", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x4b },
	{ "Keypad Equals", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x51 },
	{ "Keypad Enter", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x4c },
	{ "Keypad Clear", IMAP_RG_KEYPAD, IMAP_TGT_ADB, 0x47 },
	{ "Mouse button 1", IMAP_RG_MOUSE, IMAP_TGT_MOUSE1, 0 },
	{ "Mouse capture", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_MOUSE_GRAB },
	{ "Config Panel", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_CONFIG_PANEL },
	{ "Status display", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_STATUS },
	{ "Speed", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_SPEED },
	{ "Debugger go", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_DEBUG_GO },
	{ "Fast disk", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_FAST_DISK },
	{ "Warp pointer", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_WARP_POINTER },
	{ "Screenshot", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_SCREENSHOT },
	{ "Fullscreen", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_FULLSCREEN },
	{ "Scanlines", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_SCANLINES },
	{ "CRT effect", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_CRT },
	{ "Reset machine", IMAP_RG_MOUSE, IMAP_TGT_ACTION, IMAP_ACT_RESET },
	{ "Reset key", IMAP_RG_MOUSE, IMAP_TGT_ADB, 0x7f },
	{ "Button 0", IMAP_RG_GAMEPAD, IMAP_TGT_BUTN, 0 },
	{ "Button 1", IMAP_RG_GAMEPAD, IMAP_TGT_BUTN, 1 },
	{ "Button 2", IMAP_RG_GAMEPAD, IMAP_TGT_BUTN, 2 },
	{ "Button 3", IMAP_RG_GAMEPAD, IMAP_TGT_BUTN, 3 },
	{ "Paddle 0 (Joy 1 X)", IMAP_RG_GAMEPAD, IMAP_TGT_PDL, 0 },
	{ "Paddle 1 (Joy 1 Y)", IMAP_RG_GAMEPAD, IMAP_TGT_PDL, 1 },
	{ "Paddle 2 (Joy 2 X)", IMAP_RG_GAMEPAD, IMAP_TGT_PDL, 2 },
	{ "Paddle 3 (Joy 2 Y)", IMAP_RG_GAMEPAD, IMAP_TGT_PDL, 3 }
};

static const char *const g_remap_group_names[IMAP_RG_COUNT] = {
	"Letters A-M", "Letters N-Z", "Numbers", "Punctuation and Space",
	"Editing and Arrows", "Modifiers", "Extended Keyboard", "Keypad",
	"Mouse and Actions", "Gamepad"
};

static const char *
imap_name_of(const struct imap_name *tab, int count, int value)
{
	int	i;

	for(i = 0; i < count; i++) {
		if(tab[i].value == value) {
			return tab[i].name;
		}
	}
	return NULL;
}

static void
imap_addf(char *buf, size_t len, const char *fmt, ...)
{
	size_t	used = strlen(buf);
	va_list	ap;

	if(used >= len) {
		return;
	}
	va_start(ap, fmt);
	vsnprintf(buf + used, len - used, fmt, ap);
	va_end(ap);
}

/* Inverse of imap_key_lookup(), so the name can be written to the map file */
static void
imap_key_name(int sc, char *buf, size_t len)
{
	const char	*name;

	if((sc >= 4) && (sc <= 29)) {
		snprintf(buf, len, "%c", 'A' + (sc - 4));
	} else if((sc >= 30) && (sc <= 38)) {
		snprintf(buf, len, "%c", '1' + (sc - 30));
	} else if(sc == 39) {
		snprintf(buf, len, "0");
	} else if((sc >= 58) && (sc <= 69)) {
		snprintf(buf, len, "F%d", sc - 57);
	} else if((sc >= 104) && (sc <= 115)) {
		snprintf(buf, len, "F%d", sc - 91);
	} else if((name = imap_name_of(g_key_names, IMAP_COUNT(g_key_names),
								sc)) != NULL) {
		snprintf(buf, len, "%s", name);
	} else {
		snprintf(buf, len, "0x%02X", sc);
	}
}

static const char *
imap_src_name(int kind, int src, char *key, size_t len)
{
	const char	*name;

	switch(kind) {
	case IMAP_SRC_KEY:
		imap_key_name(src, key, len);
		return key;
	case IMAP_SRC_MOUSE:
		name = imap_name_of(g_mouse_names, IMAP_COUNT(g_mouse_names),
									src);
		break;
	case IMAP_SRC_PAD:
		name = imap_name_of(g_pad_names, IMAP_COUNT(g_pad_names), src);
		break;
	default:
		name = imap_name_of(g_axis_names, IMAP_COUNT(g_axis_names),
									src);
		break;
	}
	return name ? name : "?";
}

/* File form (display == 0) is read back by imap_parse_source().  Display
 * form is for menus and messages.
 */
static void
imap_source_text(const struct imap_binding *b, int display, char *buf,
								size_t len)
{
	static const char *const	mod_file[] = {
		"shift+", "ctrl+", "alt+", "gui+"
	};
	static const char *const	mod_show[] = {
		"Shift+", "Ctrl+", "Alt+", "GUI+"
	};
	static const char *const	kind_file[] = {
		"key:", "mouse:", "pad:", "axis:"
	};
	static const char *const	kind_show[] = {
		"", "Mouse ", "Pad ", "Axis "
	};
	char	key[16];
	int	i;

	buf[0] = '\0';
	for(i = 0; i < IMAP_COUNT(mod_file); i++) {
		if(b->mods & (1 << i)) {
			imap_addf(buf, len, "%s",
					display ? mod_show[i] : mod_file[i]);
		}
	}
	if(display) {
		imap_addf(buf, len, "%s%s", kind_show[b->src_kind],
				imap_src_name(b->src_kind, b->src, key,
								sizeof(key)));
		if(b->invert) {
			imap_addf(buf, len, " (inverted)");
		}
	} else {
		imap_addf(buf, len, "%s%s", kind_file[b->src_kind],
				imap_src_name(b->src_kind, b->src, key,
								sizeof(key)));
	}
}

static void
imap_target_text(int kind, int tgt, int invert, char *buf, size_t len)
{
	const char	*name;

	switch(kind) {
	case IMAP_TGT_ADB:
		snprintf(buf, len, "adb:0x%02X", tgt);
		break;
	case IMAP_TGT_MOUSE1:
		snprintf(buf, len, "mouse:1");
		break;
	case IMAP_TGT_PDL:
		snprintf(buf, len, "pdl:%d%s", tgt, invert ? " invert" : "");
		break;
	case IMAP_TGT_BUTN:
		snprintf(buf, len, "butn:%d", tgt);
		break;
	case IMAP_TGT_ACTION:
		name = imap_name_of(g_action_names, IMAP_COUNT(g_action_names),
									tgt);
		snprintf(buf, len, "action:%s", name ? name : "?");
		break;
	default:
		snprintf(buf, len, "none");
		break;
	}
}

static const struct imap_remap_item *
imap_item_for(int kind, int tgt)
{
	int	i;

	for(i = 0; i < IMAP_COUNT(g_remap_items); i++) {
		if((g_remap_items[i].tgt_kind == kind) &&
						(g_remap_items[i].tgt == tgt)) {
			return &g_remap_items[i];
		}
	}
	return NULL;
}

static void
imap_target_label(int kind, int tgt, int invert, char *buf, size_t len)
{
	const struct imap_remap_item	*item = imap_item_for(kind, tgt);

	if(item) {
		snprintf(buf, len, "%s", item->name);
	} else {
		imap_target_text(kind, tgt, invert, buf, len);
	}
}

static const struct imap_binding *
imap_default_for(const struct imap_binding *b)
{
	int	i;

	for(i = 0; i < g_num_default_bindings; i++) {
		const struct imap_binding *d = &g_default_bindings[i];

		if((d->src_kind == b->src_kind) && (d->src == b->src) &&
						(d->mods == b->mods)) {
			return d;
		}
	}
	return NULL;
}

/* NONE equals only NONE, so an explicit unbind differs from any target */
static int
imap_same_target(const struct imap_binding *a, const struct imap_binding *b)
{
	if((a->tgt_kind == IMAP_TGT_NONE) || (b->tgt_kind == IMAP_TGT_NONE)) {
		return a->tgt_kind == b->tgt_kind;
	}
	return (a->tgt_kind == b->tgt_kind) && (a->tgt == b->tgt) &&
						(a->invert == b->invert);
}

/* Writes the header, the bindings that differ from the built-in defaults,
 * and the defaults as comments.
 */
static void
imap_write_body(FILE *f)
{
	char	src[64], tgt[64];
	int	i, changed = 0;

	for(i = 0; i < IMAP_COUNT(g_map_file_header); i++) {
		fprintf(f, "%s\n", g_map_file_header[i]);
	}
	for(i = 0; i < g_num_bindings; i++) {
		const struct imap_binding	*b = &g_bindings[i];
		const struct imap_binding	*def = imap_default_for(b);

		/* An unbound combo with no default must still be written, or it
		 * stops shadowing the plain key on the next load.
		 */
		if(def ? imap_same_target(b, def) :
				((b->tgt_kind == IMAP_TGT_NONE) && (b->mods == 0))) {
			continue;
		}
		imap_source_text(b, 0, src, sizeof(src));
		imap_target_text(b->tgt_kind, b->tgt, b->invert, tgt,
								sizeof(tgt));
		fprintf(f, "%s = %s\n", src, tgt);
		changed = 1;
	}
	if(!changed) {
		fprintf(f, "# (none; the built-in defaults apply)\n");
	}
	fprintf(f, "\n# The bindings below are the built-in defaults, "
						"commented out.\n");
	for(i = 0; i < IMAP_COUNT(g_default_map); i++) {
		fprintf(f, "# %s\n", g_default_map[i]);
	}
}

void
imap_set_map_path(const char *path)
{
	if(strlen(path) >= sizeof(g_map_path)) {
		printf("Input map path too long, changes will not be saved: "
							"%s\n", path);
		g_map_path[0] = '\0';
		return;
	}
	snprintf(g_map_path, sizeof(g_map_path), "%s", path);
}

/* Writes through a temp file, so a failed write leaves the old map intact.
 * Appends the outcome to g_remap_msg, which the caller has already set.
 */
static void
imap_remap_save(void)
{
	char	tmp[IMAP_PATH_MAX + 8];
	FILE	*f;
	int	err = 0;

	if(g_map_path[0] == '\0') {
		imap_addf(g_remap_msg, sizeof(g_remap_msg),
				" Changed for this session only; no map file.");
		return;
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", g_map_path);
	f = fopen(tmp, "w");
	if(!f) {
		err = errno;
	} else {
		imap_write_body(f);
		if(ferror(f)) {
			err = EIO;
		}
		if((fclose(f) != 0) && (err == 0)) {
			err = errno;
		}
	}
	if((err == 0) && (rename(tmp, g_map_path) != 0)) {
		err = errno;
	}
	if(err) {
		unlink(tmp);
		imap_addf(g_remap_msg, sizeof(g_remap_msg),
				" Not saved to input map: %s.", strerror(err));
	}
}

/* Binds the pending item to a source and removes that item's other
 * sources.  Every change is saved at once.
 */
static void
imap_remap_bind(int kind, int src, int mods)
{
	struct imap_remap_item	*item = g_remap_pending;
	struct imap_binding	nb, old;
	char	text[64], label[64], list[IMAP_REMAP_TEXT];
	int	i, owner = -1, removed = 0, panels = 0;
	int	is_panel = (item->tgt_kind == IMAP_TGT_ACTION) &&
					(item->tgt == IMAP_ACT_CONFIG_PANEL);

	memset(&old, 0, sizeof(old));
	for(i = 0; i < g_num_bindings; i++) {
		const struct imap_binding *b = &g_bindings[i];

		if((b->src_kind == kind) && (b->src == src) &&
						(b->mods == mods)) {
			owner = i;
			old = *b;
		}
		if((b->tgt_kind == IMAP_TGT_ACTION) &&
					(b->tgt == IMAP_ACT_CONFIG_PANEL)) {
			panels++;
		}
	}
	g_remap_pending = NULL;
	g_remap_dirty = 1;

	if((owner >= 0) && (old.tgt_kind == IMAP_TGT_ACTION) &&
			(old.tgt == IMAP_ACT_CONFIG_PANEL) && (panels == 1) &&
			!is_panel) {
		snprintf(g_remap_msg, sizeof(g_remap_msg),
			"Config Panel needs a key. Bind another key to it first.");
		return;
	}
	if((owner < 0) && (g_num_bindings >= IMAP_MAX_BINDINGS)) {
		snprintf(g_remap_msg, sizeof(g_remap_msg), "Too many bindings.");
		return;
	}

	imap_release_all(g_remap_km);
	list[0] = '\0';
	for(i = 0; i < g_num_bindings; i++) {
		struct imap_binding *b = &g_bindings[i];

		if((i == owner) || (b->tgt_kind == IMAP_TGT_NONE) ||
				(b->tgt_kind != item->tgt_kind) ||
				(b->tgt != item->tgt)) {
			continue;
		}
		imap_source_text(b, 1, text, sizeof(text));
		imap_addf(list, sizeof(list), "%s%s", removed ? ", " : "",
								text);
		removed++;
		b->tgt_kind = IMAP_TGT_NONE;
		b->tgt = 0;
		b->invert = 0;
	}

	nb.src_kind = kind;
	nb.src = src;
	nb.mods = mods;
	nb.tgt_kind = item->tgt_kind;
	nb.tgt = item->tgt;
	nb.invert = 0;
	imap_store(&nb);
	imap_caps_sync(g_remap_km);

	imap_source_text(&nb, 1, text, sizeof(text));
	snprintf(g_remap_msg, sizeof(g_remap_msg), "Bound \"%s\" to %s.",
							item->name, text);
	if(removed) {
		imap_addf(g_remap_msg, sizeof(g_remap_msg),
				" Unbound old key for \"%s\": %s.",
				item->name, list);
	}
	if((owner >= 0) && (old.tgt_kind != IMAP_TGT_NONE) &&
			((old.tgt_kind != item->tgt_kind) ||
			(old.tgt != item->tgt))) {
		imap_target_label(old.tgt_kind, old.tgt, old.invert, label,
								sizeof(label));
		imap_addf(g_remap_msg, sizeof(g_remap_msg),
				" %s no longer triggers %s.", text, label);
	}
	imap_remap_save();
}

static int
imap_remap_capture(int kind, int src, int mods)
{
	if(!g_remap_pending) {
		return 0;
	}
	if((kind == IMAP_SRC_KEY) && (src == IMAP_SC_ESCAPE)) {
		g_remap_pending = NULL;
		snprintf(g_remap_msg, sizeof(g_remap_msg), "Capture cancelled.");
		g_remap_dirty = 1;
		return 1;
	}
	imap_remap_bind(kind, src, mods);
	return 1;
}

/* A stick arms by resting near center, then captures on a firm push.  The
 * capture is never inverted; reversing a stick needs a hand edit.
 */
static void
imap_remap_axis(int axis, int value)
{
	if(abs(value) < IMAP_REMAP_REST) {
		g_remap_axis_armed[axis] = 1;
		return;
	}
	if(g_remap_axis_armed[axis] && (abs(value) > IMAP_REMAP_PUSH)) {
		g_remap_axis_armed[axis] = 0;
		imap_remap_bind(IMAP_SRC_AXIS, axis, 0);
	}
}

const char *
imap_remap_group_name(int group)
{
	if((group < 0) || (group >= IMAP_RG_COUNT)) {
		return "";
	}
	return g_remap_group_names[group];
}

int
imap_remap_group_count(int group)
{
	int	i, n = 0;

	for(i = 0; i < IMAP_COUNT(g_remap_items); i++) {
		if(g_remap_items[i].group == group) {
			n++;
		}
	}
	return n;
}

struct imap_remap_item *
imap_remap_group_item(int group, int idx)
{
	int	i;

	for(i = 0; i < IMAP_COUNT(g_remap_items); i++) {
		if(g_remap_items[i].group != group) {
			continue;
		}
		if(idx == 0) {
			return &g_remap_items[i];
		}
		idx--;
	}
	return NULL;
}

const char *
imap_remap_item_name(const struct imap_remap_item *item)
{
	return item->name;
}

void
imap_remap_sources(const struct imap_remap_item *item, char *buf, size_t len)
{
	char	text[64];
	int	i, n = 0;

	buf[0] = '\0';
	for(i = 0; i < g_num_bindings; i++) {
		const struct imap_binding *b = &g_bindings[i];

		if((b->tgt_kind == IMAP_TGT_NONE) ||
				(b->tgt_kind != item->tgt_kind) ||
				(b->tgt != item->tgt)) {
			continue;
		}
		imap_source_text(b, 1, text, sizeof(text));
		imap_addf(buf, len, "%s%s", n ? ", " : "", text);
		n++;
	}
	if(n == 0) {
		snprintf(buf, len, "(unbound)");
	}
}

void
imap_remap_begin(Kimage *km, struct imap_remap_item *item)
{
	int	i;

	if(!km || !item) {
		return;
	}
	g_remap_km = km;
	g_remap_pending = item;
	g_remap_lone_mod = 0;
	for(i = 0; i < IMAP_NUM_AXES; i++) {
		g_remap_axis_armed[i] = (abs(g_axis_val[i]) < IMAP_REMAP_REST);
	}
	g_remap_msg[0] = '\0';
	snprintf(g_remap_prompt, sizeof(g_remap_prompt),
			"Press a %s for \"%s\" (Esc cancels)",
			(item->tgt_kind == IMAP_TGT_PDL) ?
				"key, button or stick" :
				"key, button or trigger",
			item->name);
	g_remap_dirty = 1;
}

void
imap_remap_unbind(Kimage *km, struct imap_remap_item *item)
{
	int	i, n = 0;

	if(!item) {
		return;
	}
	g_remap_dirty = 1;
	if((item->tgt_kind == IMAP_TGT_ACTION) &&
				(item->tgt == IMAP_ACT_CONFIG_PANEL)) {
		snprintf(g_remap_msg, sizeof(g_remap_msg),
			"Config Panel must keep a key, so it cannot be unbound.");
		return;
	}
	for(i = 0; i < g_num_bindings; i++) {
		if((g_bindings[i].tgt_kind == item->tgt_kind) &&
						(g_bindings[i].tgt == item->tgt)) {
			n++;
		}
	}
	if(n == 0) {
		snprintf(g_remap_msg, sizeof(g_remap_msg), "\"%s\" has no sources.",
							item->name);
		return;
	}
	imap_release_all(km);
	for(i = 0; i < g_num_bindings; i++) {
		struct imap_binding *b = &g_bindings[i];

		if((b->tgt_kind == item->tgt_kind) && (b->tgt == item->tgt)) {
			b->tgt_kind = IMAP_TGT_NONE;
			b->tgt = 0;
			b->invert = 0;
		}
	}
	imap_caps_sync(km);
	snprintf(g_remap_msg, sizeof(g_remap_msg), "Unbound \"%s\".", item->name);
	imap_remap_save();
}

void
imap_remap_reset(Kimage *km)
{
	imap_release_all(km);
	g_remap_pending = NULL;
	memcpy(g_bindings, g_default_bindings, sizeof(g_bindings));
	g_num_bindings = g_num_default_bindings;
	imap_caps_sync(km);
	snprintf(g_remap_msg, sizeof(g_remap_msg),
				"Restored the built-in defaults.");
	imap_remap_save();
	g_remap_dirty = 1;
}

void
imap_remap_cancel(void)
{
	if(g_remap_pending || g_remap_msg[0]) {
		g_remap_dirty = 1;
	}
	g_remap_pending = NULL;
	g_remap_msg[0] = '\0';
}

const char *
imap_remap_status(void)
{
	if(g_remap_pending) {
		return g_remap_prompt;
	}
	if(g_remap_msg[0]) {
		return g_remap_msg;
	}
	return NULL;
}

int
imap_remap_take_dirty(void)
{
	int	d = g_remap_dirty;

	g_remap_dirty = 0;
	return d;
}

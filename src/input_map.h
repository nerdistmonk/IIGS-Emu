/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Copyright 2025-2026 GSplus Contributors         */
/*                                                                    */
/*      This code is covered by the GNU GPL v3                        */
/*      See the file COPYING.txt or https://www.gnu.org/licenses/     */
/**********************************************************************/

#ifndef INPUT_MAP_H
#define INPUT_MAP_H

/* Like the other project headers, this needs defc.h included first (Kimage). */

/* Host modifier bits passed to imap_key_event() */
#define IMAP_MOD_SHIFT		0x01
#define IMAP_MOD_CTRL		0x02
#define IMAP_MOD_ALT		0x04
#define IMAP_MOD_GUI		0x08

/* Host mouse button indices passed to imap_mouse_button() */
#define IMAP_MOUSE_LEFT		0
#define IMAP_MOUSE_MIDDLE	1
#define IMAP_MOUSE_RIGHT	2
#define IMAP_NUM_MOUSE		3

#define IMAP_NUM_PAD_BUTTONS	26	/* SDL_GAMEPAD_BUTTON_COUNT */
#define IMAP_NUM_SDL_AXES	6	/* SDL_GAMEPAD_AXIS_COUNT */
#define IMAP_AXIS_DPAD_X	6	/* D-pad as an axis, see input_map.c */
#define IMAP_AXIS_DPAD_Y	7
#define IMAP_NUM_AXES		8
#define IMAP_NUM_PDL		4
#define IMAP_NUM_BUTN		4

enum imap_action {
	IMAP_ACT_MOUSE_GRAB,
	IMAP_ACT_CONFIG_PANEL,
	IMAP_ACT_STATUS,
	IMAP_ACT_SPEED,
	IMAP_ACT_DEBUG_GO,
	IMAP_ACT_FAST_DISK,
	IMAP_ACT_WARP_POINTER,
	IMAP_ACT_SCREENSHOT,
	IMAP_ACT_FULLSCREEN,
	IMAP_ACT_SCANLINES,
	IMAP_ACT_CRT,
	IMAP_ACT_RESET,
	IMAP_NUM_ACTIONS
};

void imap_init(void);
int imap_load_file(const char *path);
int imap_write_default_file(const char *path);
void imap_set_host_hook(void (*hook)(int action));

void imap_key_event(Kimage *km, int scancode, int mods, int is_up);
void imap_mouse_button(Kimage *km, int button, int is_up);
void imap_pad_button(Kimage *km, int button, int is_up);
void imap_pad_axis(Kimage *km, int axis, int value);
void imap_pad_release_all(Kimage *km);
void imap_release_all(Kimage *km);

int imap_pdl_value(int ch);
int imap_pdl_bound(int ch);
int imap_pdl_stick_pair(int p);
int imap_butn_pressed(int n);

/* Remap Controls.  Items are grouped for the menu.  Each change is written
 * to the map file at once.  Captures take the next key, button or stick.
 */
enum imap_remap_group {
	IMAP_RG_LETTERS_AM,
	IMAP_RG_LETTERS_NZ,
	IMAP_RG_NUMBERS,
	IMAP_RG_PUNCT,
	IMAP_RG_EDITING,
	IMAP_RG_MODIFIERS,
	IMAP_RG_EXTENDED,
	IMAP_RG_KEYPAD,
	IMAP_RG_MOUSE,
	IMAP_RG_GAMEPAD,
	IMAP_RG_COUNT
};

#define IMAP_REMAP_MAX_GROUP	18	/* items in the largest group */

struct imap_remap_item;

void imap_set_map_path(const char *path);
const char *imap_remap_group_name(int group);
int imap_remap_group_count(int group);
struct imap_remap_item *imap_remap_group_item(int group, int idx);
const char *imap_remap_item_name(const struct imap_remap_item *item);
void imap_remap_sources(const struct imap_remap_item *item, char *buf,
								size_t len);
void imap_remap_begin(Kimage *km, struct imap_remap_item *item);
void imap_remap_unbind(Kimage *km, struct imap_remap_item *item);
void imap_remap_reset(Kimage *km);
void imap_remap_cancel(void);
const char *imap_remap_status(void);
int imap_remap_take_dirty(void);

#endif	/* INPUT_MAP_H */

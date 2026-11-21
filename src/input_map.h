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
#define IMAP_NUM_AXES		6	/* SDL_GAMEPAD_AXIS_COUNT */
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
void imap_pad_axis(int axis, int value);
void imap_pad_release_all(Kimage *km);
void imap_release_all(Kimage *km);

int imap_pdl_value(int ch);
int imap_pdl_bound(int ch);
int imap_pdl_stick_pair(int p);
int imap_butn_pressed(int n);

#endif	/* INPUT_MAP_H */

/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*                    Copyright 2002-2023 Kent Dickey                 */
/*                    Copyright 2025-2026 GSplus Contributors         */
/*                                                                    */
/*      This code is covered by the GNU GPL v3                        */
/*      See the file COPYING.txt or https://www.gnu.org/licenses/     */
/**********************************************************************/

#include "defc.h"

/* For the SDL build the joystick backend lives in sdl_driver.c (SDL_Gamepad),
 * so compile out all the native per-platform backends below and mark the
 * routines as defined to skip the fallback stubs. The shared callback helpers
 * at the bottom of this file stay -- they touch no platform/SDL APIs. */
#ifdef SDL_INPUT
# define JOYSTICK_DEFINED
#endif

#if defined(__linux__) && !defined(SDL_INPUT)
# include <linux/joystick.h>
#endif

extern int g_joystick_native_type1;		/* in paddles.c */
extern int g_joystick_native_type2;		/* in paddles.c */
extern int g_joystick_native_type;		/* in paddles.c */
extern int g_paddle_buttons;
extern int g_paddle_val[];


const char *g_joystick_dev = "/dev/js0";	/* default joystick dev file */
#define MAX_JOY_NAME	128

int	g_joystick_native_fd = -1;
int	g_joystick_num_axes = 0;
int	g_joystick_num_buttons = 0;

int	g_joystick_callback_buttons = 0;
int	g_joystick_callback_x = 32767;
int	g_joystick_callback_y = 32767;

#if defined(__linux__) && !defined(SDL_INPUT)
# define JOYSTICK_DEFINED
void
joystick_init()
{
	char	joy_name[MAX_JOY_NAME];
	int	version, fd;
	int	i;

	fd = open(g_joystick_dev, O_RDONLY | O_NONBLOCK);
	if(fd < 0) {
		printf("Unable to open joystick dev file: %s, errno: %d\n",
			g_joystick_dev, errno);
		printf("Defaulting to mouse joystick\n");
		return;
	}

	strcpy(&joy_name[0], "Unknown Joystick");
	version = 0x800;

	ioctl(fd, JSIOCGNAME(MAX_JOY_NAME), &joy_name[0]);
	ioctl(fd, JSIOCGAXES, &g_joystick_num_axes);
	ioctl(fd, JSIOCGBUTTONS, &g_joystick_num_buttons);
	ioctl(fd, JSIOCGVERSION, &version);

	printf("Detected joystick: %s [%d axes, %d buttons vers: %08x]\n",
		joy_name, g_joystick_num_axes, g_joystick_num_buttons,
		version);

	g_joystick_native_type1 = 1;
	g_joystick_native_type2 = -1;
	g_joystick_native_fd = fd;
	for(i = 0; i < 4; i++) {
		g_paddle_val[i] = 32767;
	}
	g_paddle_buttons = 0xc;
}

/* joystick_update_linux() called from paddles.c.  Update g_paddle_val[] */
/*  and g_paddle_buttons with current information */
void
joystick_update(dword64 dfcyc)
{
	struct js_event js;	/* the linux joystick event record */
	int	mask, val, num, type, ret, len;
	int	i;

	/* suck up to 20 events, then give up */
	len = sizeof(struct js_event);
	for(i = 0; i < 20; i++) {
		ret = read(g_joystick_native_fd, &js, len);
		if(ret != len) {
			/* just get out */
			break;
		}
		type = js.type & ~JS_EVENT_INIT;
		val = js.value;
		num = js.number & 3;		/* clamp to 0-3 */
		switch(type) {
		case JS_EVENT_BUTTON:
			mask = 1 << num;
			if(val) {
				val = mask;
			}
			g_paddle_buttons = (g_paddle_buttons & ~mask) | val;
			break;
		case JS_EVENT_AXIS:
			/* val is -32767 to +32767 */
			g_paddle_val[num] = val;
			break;
		}
	}

	if(i > 0) {
		paddle_update_trigger_dcycs(dfcyc);
	}
}

void
joystick_update_buttons()
{
}
#endif /* LINUX */

#ifndef JOYSTICK_DEFINED
/* stubs for the routines */
void
joystick_init()
{
	g_joystick_native_type1 = -1;
	g_joystick_native_type2 = -1;
	g_joystick_native_type = -1;
}

void
joystick_update(dword64 dfcyc)
{
	int	i;

	if(dfcyc) {
		// Avoid unused parameter warnings
	}
	for(i = 0; i < 4; i++) {
		g_paddle_val[i] = 32767;
	}
	g_paddle_buttons = 0xc;
	if(g_joystick_native_type1 >= 0) {
		g_paddle_buttons = 0xc | (g_joystick_callback_buttons & 3);
		g_paddle_val[0] = g_joystick_callback_x;
		g_paddle_val[1] = g_joystick_callback_y;
		paddle_update_trigger_dcycs(dfcyc);
	}
}

void
joystick_update_buttons()
{
	if(g_joystick_native_type1 >= 0) {
		g_paddle_buttons = 0xc | (g_joystick_callback_buttons & 3);
	}
}
#endif

void
joystick_callback_init(int native_type)
{
	g_joystick_native_type1 = native_type;
}

void
joystick_callback_update(word32 buttons, int paddle_x, int paddle_y)
{
	g_joystick_callback_buttons = (g_paddle_buttons & (~3)) | (buttons & 3);
	g_joystick_callback_x = paddle_x;
	g_joystick_callback_y = paddle_y;
}

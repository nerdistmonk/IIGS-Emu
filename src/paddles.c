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
#include "input_map.h"
#include <math.h>

dword64	g_paddle_trig_dfcyc = 0;
int	g_joystick_scale_factor_x = 0x100;
int	g_joystick_scale_factor_y = 0x100;
int	g_joystick_trim_amount_x = 0;
int	g_joystick_trim_amount_y = 0;

int	g_joystick_square_range = 1;	/* remap circular stick gate to square */

extern int g_paddle_buttons;

dword64	g_paddle_dfcyc[4] = { 0, 0, 0, 0 };
		/* g_paddle_dfcyc are the dfcyc the paddle goes to 0 */


#define STICK_DEADZONE	0.08	/* radial, as a fraction of full deflection */

void
paddle_map_circle_to_square(int *xptr, int *yptr)
{
	double	x, y, r, m, scale;

	/* A modern gamepad stick travels in a circle, so a full diagonal
	 *  only reaches ~71% on each axis and games expecting the square
	 *  range of Apple joysticks never see the corners.  Stretch each
	 *  sample along its own direction -- identity on the axes, up to
	 *  sqrt(2) at the diagonals -- so the circular rim lands exactly on
	 *  the square's perimeter. */
	x = *xptr / 32767.0;
	y = *yptr / 32767.0;
	r = sqrt((x * x) + (y * y));
	if(r < STICK_DEADZONE) {
		/* Radial deadzone: sticks do not rest at exactly 0, and the
		 *  stretch below would amplify the drift */
		*xptr = 0;
		*yptr = 0;
		return;
	}
	/* Rescale magnitude so travel is continuous at the deadzone edge */
	scale = (r - STICK_DEADZONE) / (1.0 - STICK_DEADZONE);
	x = (x / r) * scale;
	y = (y / r) * scale;
	m = (fabs(x) > fabs(y)) ? fabs(x) : fabs(y);
	if(m > 0.0) {
		x = x * (scale / m);
		y = y * (scale / m);
	}
	/* Clamp: some pads overshoot the unit circle near the diagonals */
	x = (x > 1.0) ? 1.0 : ((x < -1.0) ? -1.0 : x);
	y = (y > 1.0) ? 1.0 : ((y < -1.0) ? -1.0 : y);
	*xptr = (int)(x * 32767.0);
	*yptr = (int)(y * 32767.0);
}

void
paddle_trigger(dword64 dfcyc)
{
	/* Called by read/write to $c070 */
	g_paddle_trig_dfcyc = dfcyc;

	paddle_update_trigger_dcycs(dfcyc);
}

void
paddle_update_trigger_dcycs(dword64 dfcyc)
{
	dword64	trig_dfcyc;
	int	pdl[IMAP_NUM_PDL];
	int	val, scale, trim;
	int	i;

	for(i = 0; i < IMAP_NUM_PDL; i++) {
		pdl[i] = imap_pdl_value(i);
	}
	if(g_joystick_square_range) {
		for(i = 0; i < IMAP_NUM_PDL / 2; i++) {
			if(imap_pdl_stick_pair(i)) {
				paddle_map_circle_to_square(&pdl[2 * i],
							&pdl[2 * i + 1]);
			}
		}
	}
	for(i = 0; i < IMAP_NUM_PDL; i++) {
		if(!imap_pdl_bound(i)) {
			/* An unplugged paddle pin reads 0 at once, so its
			 *  timer expires immediately */
			g_paddle_dfcyc[i] = dfcyc;
			continue;
		}
		/* convert -32768 to +32767 into 0->2816.0 cycles (the */
		/* paddle delay const) */
		/* First multiply by the scale factor to adjust range */
		if(i & 1) {
			scale = g_joystick_scale_factor_y;
			trim = g_joystick_trim_amount_y;
		} else {
			scale = g_joystick_scale_factor_x;
			trim = g_joystick_trim_amount_x;
		}
		val = (pdl[i] * scale) >> 16;
		/* Val is now from -128 to + 128 since scale is */
		/*  256=1.0, 128 = 0.5 */
		val = val + 128 + trim;
		/* Trim can push val below 0, and converting a negative double
		 *  to unsigned is undefined, so clamp before the conversion */
		if(val < 0) {
			val = 0;
		}
		if(val >= 255) {
			val = 280;	/* increase range */
		}
		trig_dfcyc = dfcyc + (dword64)((val * (2816/255.0)) * 65536);
		g_paddle_dfcyc[i] = trig_dfcyc;
		if(i < 2) {
			dbg_log_info(dfcyc, (scale << 16) | (val & 0xffff),
					(trim << 16) | i, 0x70);
		}
	}
}

int
read_paddles(dword64 dfcyc, int paddle)
{
	dword64	trig_dfcyc;

	trig_dfcyc = g_paddle_dfcyc[paddle & 3];

	if(dfcyc < trig_dfcyc) {
		return 0x80;
	} else {
		return 0x00;
	}
}

void
paddle_update_buttons()
{
	int	n;

	for(n = 0; n < IMAP_NUM_BUTN; n++) {
		if(imap_butn_pressed(n)) {
			g_paddle_buttons |= 1 << n;
		} else {
			g_paddle_buttons &= ~(1 << n);
		}
	}
}

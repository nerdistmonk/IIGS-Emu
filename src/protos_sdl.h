/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*      This code is covered by the GNU GPL v3                        */
/**********************************************************************/

/* Public prototypes for the SDL3 driver (sdl_driver.c / sdl_snd_driver.c).
 * Relies on byte/word32 from defc.h, so include defc.h before this header. */

#ifndef GSPLUS_PROTOS_SDL_H
#define GSPLUS_PROTOS_SDL_H

/* sdl_driver.c */
#define OSD_TEXT_MAX	64	/* longest message osd_show keeps, incl. NUL */
void	osd_show(const char *msg);

/* sdl_snd_driver.c */
void	sdl_snd_init(word32 *shmaddr);
int	sdl_send_audio(byte *ptr, int size);
void	sdl_snd_shutdown(void);

#endif /* GSPLUS_PROTOS_SDL_H */

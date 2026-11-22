/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*      This code is covered by the GNU GPL v3                        */
/**********************************************************************/

/* SDL3 display/input driver and program entry point.
 *
 * KEGS has no platform-independent main(): each display driver owns the entry
* point and the run loop, calling into the core. The loop is: *
 *     parse_argv() -> kegs_init() -> sdl_video_init()
 *     while(running) { run_16ms(); poll input; push framebuffer }
 *
 * The core hands us framebuffers as Kimage objects. Each frame we ask the core
 * to copy its changed rectangles into our pixel buffer (video_out_data) and we
 * upload those to an SDL streaming texture.
 */

#include <SDL3/SDL.h>
#include <time.h>

#include "defc.h"
#include "input_map.h"
#include "protos_sdl.h"
#include "xbrz.h"
#include "gsplus_icon.h"	/* embedded RGBA window icon (see below) */

/* Each KEGS driver defines its own private "window info" wrapper around a core
 * Kimage. Ours holds the SDL window/renderer/texture plus the scratch pixel
 * buffer the core fills. */
typedef struct {
	Kimage		*kimage_ptr;
	SDL_Window	*window;
	SDL_Renderer	*renderer;
	SDL_Texture	*texture;
	SDL_Texture	*overlay;		/* 2-row scanline tile, tiled over the
						 * final output in host pixels */
	int		overlay_for;		/* intensity the overlay was filled for */
	/* CRT effect resources (built lazily by sdl_create_texture when on). */
	SDL_Texture	*crt_target;		/* composited screen (fb+mask) */
	SDL_Texture	*glow_target;		/* downscaled crt_target, used for bloom */
	SDL_Texture	*mask;			/* 1-row R/G/B phosphor-mask tile (MUL),
						 * tiled over the output in host pixels */
	int		mask_for;		/* g_crt_mask the mask was filled for */
	SDL_Vertex	*crt_verts;		/* warped curvature mesh vertices */
	SDL_Vertex	*glow_verts;		/* same mesh, glow-strength colors */
	int		*crt_idx;		/* mesh triangle indices */
	int		crt_nverts;
	int		crt_nidx;
	int		crt_curve_for;		/* g_crt_curve the mesh was built for */
	int		crt_vig_for;		/* g_crt_vignette the mesh was built for */
	int		crt_glow_for;		/* g_crt_glow the mesh was built for */
	int		crt_zoom_for;		/* g_zoom the mesh was built for */
	word32		*data;			/* dest buffer for video_out_data() */
	int		active;
	int		width_req;		/* current logical width  (pixels) */
	int		main_height;		/* current logical height (pixels) */
	int		pixels_per_line;	/* stride of data[]       (pixels) */
	int		tex_scale;		/* texture size / guest size: 2 when the
						 * xBRZ filter backs the texture, else 1 */
	int		xbrz_for;		/* g_xbrz the texture was built for */
	word32		*xbrz_buf;		/* 2x xBRZ output, xBRZ mode only */
} Window_info;

static Window_info g_mainwin_info;

/* Window/display options, defined in config.c and settable via the command line
 * (e.g. "-fullscreen 1") or config.kegs. */
extern int g_fullscreen, g_borderless, g_noaspect, g_highdpi;
extern int g_nohwaccel;
extern int g_scanline_simulator;	/* CRT scanline overlay intensity, 0-100 */
extern int g_crt;			/* curved CRT effect on/off */
extern int g_crt_curve;			/* CRT screen curvature, 0-100 */
extern int g_crt_mask;			/* CRT phosphor-mask strength, 0-100 */
extern int g_crt_glow;			/* CRT glow/bloom strength, 0-100 */
extern int g_crt_vignette;		/* CRT corner darkening, 0-100 */
extern int g_hblur;			/* horizontal linear blur, 0-100 */
extern int g_xbrz;			/* xBRZ 2x upscale filter on/off */
extern int g_vblur;			/* vertical linear blur, 0-100 */
extern int g_zoom;			/* magnification of the final frame, 100-200 */
extern int g_config_control_panel;
extern int g_config_kegs_update_needed;
extern int g_hide_mouse;		/* hide host cursor over window / in fullscreen */
extern int g_mainwin_xpos, g_mainwin_ypos;	/* window position (KEGS config vars) */
extern char *g_cfg_ssdir;		/* screenshot output dir ("" = current dir) */
extern int g_halt_sim;			/* nonzero while the debugger has the CPU halted */
extern int g_osd_enable;		/* on-screen message for hotkey actions */

static int g_is_fullscreen = 0;		/* current fullscreen state (F11 toggles) */
static int g_mouse_over_window = 0;	/* cursor is inside our window (ENTER/LEAVE) */
static int g_cursor_hidden = 0;		/* current SDL cursor state we last set */
static int g_scanline_saved = 50;	/* intensity to restore when toggled back on */
static int g_screenshot_requested = 0;	/* set by F10, serviced at frame end */

/* CRT effect tuning. The curvature amount is a config var (g_crt_curve, 0-100);
 * these bake the rest of the look so the user gets one "CRT Effect" toggle. */
#define CRT_MESH_COLS	32		/* curvature mesh resolution (cells) */
#define CRT_MESH_ROWS	24
#define CRT_MASK_MIN	60		/* off-channel level at full mask strength
					 * (0-255); g_crt_mask scales 255 (off)
					 * down toward this floor */
#define CRT_MASK_STRIPE_PX 2		/* target phosphor stripe width in device
					 * (physical screen) pixels */
#define CRT_GLOW_DIV	3		/* glow downsample factor (blur radius) */

/* Widths of the 2-row scanline tile and the 1-row phosphor-mask tile. Any
 * width works (they're tiled across the output; the mask just needs a multiple
 * of its 3-column R/G/B period); wider means fewer tiles for backends without
 * WRAP support. */
#define SCAN_TILE_W	64
#define MASK_TILE_W	63

/* Up-front buffer size: large enough for every IIgs video mode (incl. borders
 * and scaling headroom). The actual window is sized to the current mode. */
#define GSPLUS_MAX_WIDTH	1280
#define GSPLUS_MAX_HEIGHT	1024

static int g_quit_requested = 0;

static void sdl_create_crt(Window_info *win, int w, int h);
static void sdl_build_crt_mesh(Window_info *win, int w, int h);
static void sdl_render_crt(Window_info *win);
static void sdl_render_framebuffer(Window_info *win, int w, int h, int dst_scale, float zoom);

/* Texture size as a multiple of the guest size: 2 when xBRZ backs the texture. */
static int
sdl_xbrz_scale(void)
{
	return g_xbrz ? 2 : 1;
}

/* (Re)create the streaming texture at the given size. ARGB8888 matches what
 * video_out_data() writes when mdepth == 32. */
static void
sdl_create_texture(Window_info *win, int w, int h)
{
	if(win->texture) {
		SDL_DestroyTexture(win->texture);
		win->texture = NULL;
	}
	win->tex_scale = sdl_xbrz_scale();
	win->xbrz_for = g_xbrz;
	free(win->xbrz_buf);
	win->xbrz_buf = NULL;
	if(win->tex_scale == 2) {
		win->xbrz_buf = calloc((size_t)w * 2 * h * 2, sizeof(word32));
		if(!win->xbrz_buf) {
			printf("cannot allocate the xBRZ frame buffer\n");
			exit(1);
		}
	}
	/* STATIC (not STREAMING) access: we update changed rectangles with
	 * SDL_UpdateTexture and keep the rest of the frame intact. Static textures
	 * are the documented target for SDL_UpdateTexture and preserve their
	 * contents between frames; partial SDL_UpdateTexture on a streaming texture
	 * is unreliable on some SDL render backends. */	win->texture = SDL_CreateTexture(win->renderer, SDL_PIXELFORMAT_ARGB8888,
					SDL_TEXTUREACCESS_STATIC, w * win->tex_scale, h * win->tex_scale);
	if(!win->texture) {
		printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
		return;
	}
	/* Opaque copy (ignore the texture's alpha) and crisp nearest-neighbour. */
	SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(win->texture, SDL_SCALEMODE_NEAREST);

	/* Create the scanline tile once: 2 rows (clear, dark) tiled across the
	 * *output* in host pixels at render time, so the dark lines are always
	 * exactly one screen pixel no matter how far the image is upscaled. It
	 * blends over the scaled picture, so it uses alpha (BLEND). Its size
	 * doesn't depend on the framebuffer, so no rebuild on mode changes. */
	if(!win->overlay) {
		win->overlay = SDL_CreateTexture(win->renderer,
					SDL_PIXELFORMAT_ARGB8888,
					SDL_TEXTUREACCESS_STATIC, SCAN_TILE_W, 2);
		if(win->overlay) {
			SDL_SetTextureBlendMode(win->overlay,
						SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(win->overlay,
						SDL_SCALEMODE_NEAREST);
		}
		win->overlay_for = -1;		/* force a fill */
	}

	/* Same idea for the phosphor mask: one row of R/G/B stripe columns,
	 * tiled across the output in host pixels so the grille pitch stays a
	 * fixed couple of device pixels at any window size. MUL blend so each
	 * column lets one channel through and dims the other two. */
	if(!win->mask) {
		win->mask = SDL_CreateTexture(win->renderer,
					SDL_PIXELFORMAT_ARGB8888,
					SDL_TEXTUREACCESS_STATIC, MASK_TILE_W, 1);
		if(win->mask) {
			SDL_SetTextureBlendMode(win->mask, SDL_BLENDMODE_MUL);
			SDL_SetTextureScaleMode(win->mask,
						SDL_SCALEMODE_NEAREST);
		}
		win->mask_for = -1;		/* force a fill */
	}

	/* (Re)build the CRT effect resources at the new size. Cheap to keep around
	 * even when the effect is off; the render path just skips them. */
	sdl_create_crt(win, w, h);
}

/* --------------------------------------------------------------------------
 * Curved-CRT effect (no shaders, no extra libraries).
 *
 * Built entirely on the 2D renderer:
 *   - screen curvature via SDL_RenderGeometry: a warped vertex mesh that the GPU
 *     samples the framebuffer onto, with vignette baked into the vertex colours
 *   - bloom/glow via a downscaled (= blurred) copy redrawn additively
 *   - an RGB aperture-grille "phosphor" mask (MULTIPLY blend) and the scanline
 *     overlay, both drawn last in host-pixel space (see sdl_render_overlays)
 *
 * Each frame, sdl_render_crt() copies the framebuffer into an offscreen
 * target, downscales it for glow, then warps both onto the curve.
 * -------------------------------------------------------------------------- */

/* Fill the phosphor-mask tile with vertical R/G/B stripe columns, the classic
 * Trinitron aperture-grille look. Alpha 255 so the multiply is exact.
 *
 * The off-channel "dim" level is driven by g_crt_mask (0-100): 0 leaves them at
 * 255 (mask invisible), 100 pulls them down to CRT_MASK_MIN (strongest). The
 * default is deliberately subtle. */
static void
sdl_fill_mask(Window_info *win)
{
	word32	buf[MASK_TILE_W];
	int	x, col, r, g, b, dim, strength;

	if(!win->mask) {
		return;
	}
	strength = g_crt_mask;
	if(strength < 0)   { strength = 0; }
	if(strength > 100) { strength = 100; }
	dim = 255 - (strength * (255 - CRT_MASK_MIN) / 100);
	for(x = 0; x < MASK_TILE_W; x++) {
		col = x % 3;
		r = g = b = dim;
		if(col == 0)      { r = 255; }
		else if(col == 1) { g = 255; }
		else              { b = 255; }
		buf[x] = 0xff000000u | ((word32)r << 16) |
				((word32)g << 8) | (word32)b;
	}
	SDL_UpdateTexture(win->mask, NULL, buf,
				MASK_TILE_W * (int)sizeof(word32));
	win->mask_for = g_crt_mask;
}

/* The config panel is always drawn at 100% so its text stays readable. */
static int
sdl_zoom_percent(void)
{
	return g_config_control_panel ? 100 : g_zoom;
}

/* Zoom percent (100-200) as the scale factor applied about the output centre. */
static float
sdl_zoom_factor(void)
{
	return (float)sdl_zoom_percent() * 0.01f;
}

/* Build the curvature mesh: a CRT_MESH_COLS x CRT_MESH_ROWS grid whose vertices
 * are barrel-distorted (edges bow outward, corners pull in) and tinted darker
 * toward the corners for a vignette. UVs stay uniform so the GPU maps the flat
 * framebuffer onto the curved surface. Positions are in logical pixels; the
 * renderer's logical presentation then letterboxes them into the window. */
static void
sdl_build_crt_mesh(Window_info *win, int w, int h)
{
	int	i, j, nx, ny, n, idx, vig_strength, glow_strength;
	float	curve, vignette, glow, zoom, u, v, px, py, wx, wy, r2, vig;
	float	sx, sy;

	nx = CRT_MESH_COLS + 1;
	ny = CRT_MESH_ROWS + 1;
	free(win->crt_verts);
	free(win->glow_verts);
	free(win->crt_idx);
	win->crt_verts = malloc((size_t)nx * ny * sizeof(SDL_Vertex));
	win->glow_verts = malloc((size_t)nx * ny * sizeof(SDL_Vertex));
	win->crt_idx = malloc((size_t)CRT_MESH_COLS * CRT_MESH_ROWS * 6 *
				sizeof(int));
	if(!win->crt_verts || !win->glow_verts || !win->crt_idx) {
		free(win->crt_verts);  win->crt_verts = NULL;
		free(win->glow_verts); win->glow_verts = NULL;
		free(win->crt_idx);    win->crt_idx = NULL;
		return;
	}

	/* 0-100 curvature knob -> a gentle 0..~0.30 distortion coefficient. */
	curve = (float)g_crt_curve * 0.003f;

	/* 0-100 vignette knob -> 0..1 corner-darkening amount (0 = flat). */
	vig_strength = g_crt_vignette;
	if(vig_strength < 0)   { vig_strength = 0; }
	if(vig_strength > 100) { vig_strength = 100; }
	vignette = (float)vig_strength * 0.01f;

	/* 0-100 glow knob -> 0..1 additive bloom strength (0 = off). */
	glow_strength = g_crt_glow;
	if(glow_strength < 0)   { glow_strength = 0; }
	if(glow_strength > 100) { glow_strength = 100; }
	glow = (float)glow_strength * 0.01f;

	zoom = sdl_zoom_factor();

	n = 0;
	for(j = 0; j < ny; j++) {
		for(i = 0; i < nx; i++) {
			u = (float)i / (float)CRT_MESH_COLS;
			v = (float)j / (float)CRT_MESH_ROWS;
			px = u * 2.0f - 1.0f;		/* [-1,1] */
			py = v * 2.0f - 1.0f;
			/* Barrel: bow each axis out in its middle, pull corners
			 * in by the square of the other axis' distance. */
			wx = px * (1.0f - curve * py * py);
			wy = py * (1.0f - curve * px * px);
			sx = (wx * 0.5f + 0.5f) * w;
			sy = (wy * 0.5f + 0.5f) * h;
			/* Zoom magnifies about the output centre; the overflow is
			 * clipped by the viewport. */
			win->crt_verts[n].position.x = w * 0.5f +
						(sx - w * 0.5f) * zoom;
			win->crt_verts[n].position.y = h * 0.5f +
						(sy - h * 0.5f) * zoom;
			win->crt_verts[n].tex_coord.x = u;
			win->crt_verts[n].tex_coord.y = v;
			r2 = px * px + py * py;		/* 0 centre .. 2 corner */
			vig = 1.0f - vignette * (r2 * 0.5f);
			if(vig < 0.0f) { vig = 0.0f; }
			win->crt_verts[n].color.r = vig;
			win->crt_verts[n].color.g = vig;
			win->crt_verts[n].color.b = vig;
			win->crt_verts[n].color.a = 1.0f;
			/* Glow pass: same mesh, but the bloom strength lives in
			 * the vertex colors. SDL_RenderGeometry ignores
			 * SDL_SetTextureColorMod (only SDL_RenderTexture applies
			 * it), so modulating the glow texture doesn't work --
			 * the strength must be baked in here. */
			win->glow_verts[n] = win->crt_verts[n];
			win->glow_verts[n].color.r = vig * glow;
			win->glow_verts[n].color.g = vig * glow;
			win->glow_verts[n].color.b = vig * glow;
			n++;
		}
	}
	win->crt_nverts = n;

	idx = 0;
	for(j = 0; j < CRT_MESH_ROWS; j++) {
		for(i = 0; i < CRT_MESH_COLS; i++) {
			int n00 = j * nx + i;
			int n10 = n00 + 1;
			int n01 = n00 + nx;
			int n11 = n01 + 1;
			win->crt_idx[idx++] = n00;
			win->crt_idx[idx++] = n10;
			win->crt_idx[idx++] = n11;
			win->crt_idx[idx++] = n00;
			win->crt_idx[idx++] = n11;
			win->crt_idx[idx++] = n01;
		}
	}
	win->crt_nidx = idx;
	win->crt_curve_for = g_crt_curve;
	win->crt_vig_for = g_crt_vignette;
	win->crt_glow_for = g_crt_glow;
	win->crt_zoom_for = sdl_zoom_percent();
}

/* (Re)create the offscreen targets + mesh at the given logical size. */
static void
sdl_create_crt(Window_info *win, int w, int h)
{
	int	gw, gh;

	if(win->crt_target)  { SDL_DestroyTexture(win->crt_target);  }
	if(win->glow_target) { SDL_DestroyTexture(win->glow_target); }
	win->crt_target = win->glow_target = NULL;

	/* Composite target: the (optionally blurred) framebuffer lands here, then
	 * the curve mesh samples it. LINEAR so the warp/upscale to the window
	 * softens like a real tube instead of showing hard pixel edges. */
	win->crt_target = SDL_CreateTexture(win->renderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_TARGET, w * win->tex_scale, h * win->tex_scale);
	if(win->crt_target) {
		SDL_SetTextureScaleMode(win->crt_target, SDL_SCALEMODE_LINEAR);
	}

	/* Glow target: a small copy of crt_target. Downsampling with LINEAR is a
	 * cheap box blur; drawn back additively it becomes the bloom halo. */
	gw = (w * win->tex_scale) / CRT_GLOW_DIV; if(gw < 1) { gw = 1; }
	gh = (h * win->tex_scale) / CRT_GLOW_DIV; if(gh < 1) { gh = 1; }
	win->glow_target = SDL_CreateTexture(win->renderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_TARGET, gw, gh);
	if(win->glow_target) {
		SDL_SetTextureScaleMode(win->glow_target, SDL_SCALEMODE_LINEAR);
	}

	sdl_build_crt_mesh(win, w, h);
}

/* Fill the scanline tile: row 0 transparent, row 1 semi-transparent black.
 * intensity is 0-100 and maps to the alpha of the dark row. */
static void
sdl_fill_overlay(Window_info *win, int intensity)
{
	word32	buf[SCAN_TILE_W * 2];
	word32	argb;
	int	x, alpha;

	if(!win->overlay) {
		return;
	}
	alpha = intensity * 255 / 100;
	if(alpha > 255) { alpha = 255; }
	argb = (word32)alpha << 24;		/* black (RGB 0) with this alpha */
	for(x = 0; x < SCAN_TILE_W; x++) {
		buf[x] = 0;
		buf[SCAN_TILE_W + x] = argb;
	}
	SDL_UpdateTexture(win->overlay, NULL, buf,
				SCAN_TILE_W * (int)sizeof(word32));
	win->overlay_for = intensity;
}

/* Output pixels per mask texel so one stripe covers ~CRT_MASK_STRIPE_PX
 * physical screen pixels regardless of the highdpi setting. Device pixels per
 * output pixel is the display's content scale divided by the window's pixel
 * density (retina, highdpi=1: 2/2 -> output px ARE device px; highdpi=0: 2/1
 * -> the OS doubles each output px). Rounded to an integer so the stripes tile
 * evenly instead of beating against the pixel grid. */
static float
sdl_mask_scale(Window_info *win)
{
	float	density, content, scale;

	density = SDL_GetWindowPixelDensity(win->window);
	content = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(win->window));
	if(density <= 0.0f) { density = 1.0f; }
	if(content <= 0.0f) { content = 1.0f; }
	scale = (float)(int)((float)CRT_MASK_STRIPE_PX * density / content
								+ 0.5f);
	return (scale < 1.0f) ? 1.0f : scale;
}

/* Draw the phosphor mask and scanlines over the finished frame, in *output*
 * (host pixel) space. The picture textures are all at the emulator's logical
 * resolution, so blending these effects in that space and upscaling stretched
 * them along with the picture (~2+ host pixels per scanline/stripe, smeared by
 * the LINEAR upscale). Instead the tiles go on 1:1 after the image has been
 * scaled, so the effects land on actual screen pixels. Logical presentation is
 * turned off for the draw and restored right after (mouse-coordinate mapping
 * needs it). */
static void
sdl_render_overlays(Window_info *win)
{
	SDL_Renderer	*r = win->renderer;
	SDL_FRect	dst;
	int		ow, oh;
	int	want_mask = (g_crt && (g_crt_mask > 0) && win->mask);
	int	want_scan = ((g_scanline_simulator > 0) && win->overlay);

	if(!want_mask && !want_scan) {
		return;
	}
	SDL_SetRenderLogicalPresentation(r, 0, 0,
				SDL_LOGICAL_PRESENTATION_DISABLED);
	if(SDL_GetRenderOutputSize(r, &ow, &oh)) {
		dst.x = 0.0f;
		dst.y = 0.0f;
		dst.w = (float)ow;
		dst.h = (float)oh;
		/* Both cover the letterbox bars too, invisibly: multiplying
		 * black stays black, and blending black over black is black. */
		if(want_mask) {
			if(win->mask_for != g_crt_mask) {
				sdl_fill_mask(win);
			}
			SDL_RenderTextureTiled(r, win->mask, NULL,
						sdl_mask_scale(win), &dst);
		}
		if(want_scan) {
			if(win->overlay_for != g_scanline_simulator) {
				sdl_fill_overlay(win, g_scanline_simulator);
			}
			SDL_RenderTextureTiled(r, win->overlay, NULL, 1.0f, &dst);
		}
	}
	SDL_SetRenderLogicalPresentation(r, win->width_req, win->main_height,
		g_noaspect ? SDL_LOGICAL_PRESENTATION_STRETCH
			   : SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

#define OSD_SHOW_MS	2000
#define OSD_CHAR_PX	(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 2)

static char	g_osd_text[OSD_TEXT_MAX];
static Uint64	g_osd_until_ms = 0;

/* Shows a short message in the top-right corner for OSD_SHOW_MS. */
void
osd_show(const char *msg)
{
	if(!g_osd_enable) {
		return;
	}
	snprintf(g_osd_text, sizeof(g_osd_text), "%s", msg);
	g_osd_until_ms = SDL_GetTicks() + OSD_SHOW_MS;
}

/* Draws the OSD after the picture, in host pixels like sdl_render_overlays, so
 * zoom does not change its size.  Called after the screenshot is taken, so
 * captures never include it. */
static void
sdl_render_osd(Window_info *win)
{
	SDL_Renderer	*r = win->renderer;
	SDL_FRect	box;
	float		pad = 6.0f;
	float		margin = 8.0f;
	int		ow, oh;
	Uint8		dr, dg, db, da;
	SDL_BlendMode	blend;

	if(!g_osd_enable || (g_osd_text[0] == '\0')) {
		return;
	}
	if(SDL_GetTicks() >= g_osd_until_ms) {
		g_osd_text[0] = '\0';
		return;
	}

	SDL_SetRenderLogicalPresentation(r, 0, 0,
				SDL_LOGICAL_PRESENTATION_DISABLED);
	if(SDL_GetRenderOutputSize(r, &ow, &oh)) {
		box.w = (float)(strlen(g_osd_text) * OSD_CHAR_PX) + 2.0f * pad;
		box.h = (float)OSD_CHAR_PX + 2.0f * pad;
		box.x = (float)ow - box.w - margin;
		if(box.x < 0.0f) {
			box.x = 0.0f;
		}
		box.y = margin;

		SDL_GetRenderDrawColor(r, &dr, &dg, &db, &da);
		SDL_GetRenderDrawBlendMode(r, &blend);
		SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
		SDL_RenderFillRect(r, &box);

		/* Debug text is 8x8 per character; the scale doubles it. */
		SDL_SetRenderScale(r, 2.0f, 2.0f);
		SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
		SDL_RenderDebugText(r, (box.x + pad) / 2.0f,
				(box.y + pad) / 2.0f, g_osd_text);
		SDL_SetRenderScale(r, 1.0f, 1.0f);

		SDL_SetRenderDrawColor(r, dr, dg, db, da);
		SDL_SetRenderDrawBlendMode(r, blend);
	}

	SDL_SetRenderLogicalPresentation(r, win->width_req, win->main_height,
		g_noaspect ? SDL_LOGICAL_PRESENTATION_STRETCH
			   : SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

/* Upscale the whole guest frame 2x with xBRZ and upload it to the 2x texture. */
static void
sdl_xbrz_upload(Window_info *win)
{
	xbrz_scale2x(win->data, win->pixels_per_line, win->xbrz_buf,
				win->width_req, win->main_height);
	SDL_UpdateTexture(win->texture, NULL, win->xbrz_buf,
				win->width_req * 2 * (int)sizeof(word32));
}

static void
sdl_video_init(void)
{
	Kimage	*km;
	int	w, h;

	if(!SDL_Init(SDL_INIT_VIDEO)) {
		printf("SDL_Init failed: %s\n", SDL_GetError());
		exit(1);
	}

	km = video_get_kimage(0);		/* 0 = main window, 1 = debugger */
	w = video_get_x_width(km);
	h = video_get_x_height(km);

	/* Tell the core our pixel layout (ARGB8888 -> 0x00RRGGBB) and build the
	 * palettes. The Apple II text/lores/hires palette (g_a2palette_1624) is
	 * populated ONLY by video_set_palette(); without this call it stays all
	 * zeros and every text/HGR pixel renders black, while super-hires (which
	 * uses g_palette_8to1624 directly) still works. The X11 driver does
	 * exactly this in its own init. */
	video_set_red_mask(0xff0000);
	video_set_green_mask(0x00ff00);
	video_set_blue_mask(0x0000ff);
	/* Mark framebuffer pixels opaque (alpha 0xff) so the backbuffer/screenshots
	 * carry a consistent alpha. (Note
	 * the core only applies this in its scaled copy paths; nothing in the
	 * render path may depend on framebuffer alpha -- the glow pass learned
	 * this the hard way and now adds RGB independent of alpha.) */
	video_set_alpha_mask(0xff000000);
	video_set_palette();

	g_mainwin_info.kimage_ptr = km;
	g_mainwin_info.width_req = w;
	g_mainwin_info.main_height = h;
	g_mainwin_info.pixels_per_line = w;
	g_mainwin_info.active = 1;
	g_mainwin_info.data = calloc((size_t)GSPLUS_MAX_WIDTH * GSPLUS_MAX_HEIGHT,
					sizeof(word32));
	if(!g_mainwin_info.data) {
		printf("cannot allocate the %dx%d framebuffer\n",
			GSPLUS_MAX_WIDTH, GSPLUS_MAX_HEIGHT);
		exit(1);
	}

	video_update_scale(km, w, h, 1);

	/* Window flags and size from the configured display options. */
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
	if(g_highdpi)    { flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY; }
	if(g_borderless) { flags |= SDL_WINDOW_BORDERLESS; }
	if(g_fullscreen) { flags |= SDL_WINDOW_FULLSCREEN; g_is_fullscreen = 1; }

	g_mainwin_info.window = SDL_CreateWindow("IIGS-Emu",
				w * sdl_xbrz_scale(), h * sdl_xbrz_scale(), flags);
	if(!g_mainwin_info.window) {
		printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
		exit(1);
	}
	if(!g_fullscreen) {
		SDL_SetWindowPosition(g_mainwin_info.window,
					g_mainwin_xpos, g_mainwin_ypos);
	}

	/* Window/taskbar icon. */
	{
		SDL_Surface *icon = SDL_CreateSurfaceFrom(gsplus_icon_width,
				gsplus_icon_height, SDL_PIXELFORMAT_RGBA32,
				(void *)gsplus_icon_rgba, gsplus_icon_width * 4);
		if(icon) {
			SDL_SetWindowIcon(g_mainwin_info.window, icon);
			SDL_DestroySurface(icon);
		}
	}

	/* "-nohwaccel 1" forces the software renderer; otherwise SDL picks the best. */
	g_mainwin_info.renderer = SDL_CreateRenderer(g_mainwin_info.window,
				g_nohwaccel ? "software" : NULL);
	if(!g_mainwin_info.renderer) {
		printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
		exit(1);
	}
	/* Keep vsync OFF: the core paces every frame to the emulated 60.05Hz via
	 * micro_sleep() in run_16ms(), so a vsync-blocked SDL_RenderPresent() would
	 * add a second ~16ms wait per frame, running the whole emulator (and its
	 * audio production) at half speed and starving the audio queue. Making vsync
	 * coexist with the core's pacer is a backlog item; for now the core is the
	 * sole frame-rate authority. */
	SDL_SetRenderVSync(g_mainwin_info.renderer, 0);
	const char *rname = SDL_GetRendererName(g_mainwin_info.renderer);
	printf("SDL renderer backend: %s\n", rname ? rname : "unknown");

	/* Keep the IIgs aspect ratio (letterbox) unless -noaspect stretches to fill. */
	SDL_SetRenderLogicalPresentation(g_mainwin_info.renderer, w, h,
		g_noaspect ? SDL_LOGICAL_PRESENTATION_STRETCH
			   : SDL_LOGICAL_PRESENTATION_LETTERBOX);

	sdl_create_texture(&g_mainwin_info, w, h);

	/* Mark this window active so the core renders into it. Without this,
	 * kimage->active stays 0 and video_get_active() makes us skip every frame
	 * (black screen). */
	video_set_active(km, 1);

	/* Force the core to output the entire screen on the first frame. Without
	 * this, static content (e.g. the no-ROM config panel) is drawn once during
	 * kegs_init -- before this window/texture existed -- and never produces
	 * dirty rectangles again, leaving the texture black. */
	video_set_x_refresh_needed(km, 1);
}

/* --------------------------------------------------------------------------
 * Keyboard and mouse input.  Raw SDL events go to input_map.c, which decides
 * what each one does.  Keys are identified by SDL physical scancode, so a
 * binding follows the key's position rather than the layout's symbol.
 * ------------------------------------------------------------------------- */
static int
sdl_mod_bits(SDL_Keymod mod)
{
	int	bits = 0;

	if(mod & SDL_KMOD_SHIFT) {
		bits |= IMAP_MOD_SHIFT;
	}
	if(mod & SDL_KMOD_CTRL) {
		bits |= IMAP_MOD_CTRL;
	}
	if(mod & SDL_KMOD_ALT) {
		bits |= IMAP_MOD_ALT;
	}
	if(mod & SDL_KMOD_GUI) {
		bits |= IMAP_MOD_GUI;
	}
	return bits;
}

static void
sdl_handle_key(Window_info *win, const SDL_KeyboardEvent *key, int is_up)
{
	if(key->repeat) {
		return;		/* the IIgs ADB does its own key repeat */
	}
	imap_key_event(win->kimage_ptr, key->scancode,
				sdl_mod_bits(key->mod), is_up);
}

static int
sdl_mouse_index(int sdl_button)
{
	switch(sdl_button) {
	case SDL_BUTTON_LEFT:
		return IMAP_MOUSE_LEFT;
	case SDL_BUTTON_MIDDLE:
		return IMAP_MOUSE_MIDDLE;
	case SDL_BUTTON_RIGHT:
		return IMAP_MOUSE_RIGHT;
	default:
		return -1;
	}
}

/* ----------------------------------------------------------------------- */
/* Game controller input.                                                  */
/*                                                                         */
/* Pads are read through SDL3's Gamepad API, which gives a standard layout */
/* and a mapping database covering XInput, DirectInput, HID and Bluetooth. */
/* Buttons arrive as events.  Axes are sampled once per frame in           */
/* sdl_poll_gamepad_axes(), so a pad's position is right on open and on    */
/* reconnect.  input_map.c decides what each control drives.               */

static void sdl_imap_host_action(int action);

static SDL_Gamepad *g_sdl_gamepad = NULL;	/* the open controller, or NULL */

/* Open the first connected controller SDL has a gamepad mapping for. */
static void
sdl_open_first_gamepad(void)
{
	SDL_JoystickID *ids;
	int	count, i;

	if(g_sdl_gamepad) {
		return;				/* already have one */
	}
	ids = SDL_GetGamepads(&count);
	if(!ids) {
		return;
	}
	for(i = 0; i < count; i++) {
		if(!SDL_IsGamepad(ids[i])) {
			continue;
		}
		g_sdl_gamepad = SDL_OpenGamepad(ids[i]);
		if(g_sdl_gamepad) {
			const char *gname = SDL_GetGamepadName(g_sdl_gamepad);
			printf("SDL gamepad opened: %s\n", gname ? gname : "unknown");
			break;
		}
	}
	SDL_free(ids);
}

/* Forward every stick and trigger position to input_map.c.  Axes are polled
 * rather than read from events so the starting position is correct. */
static void
sdl_poll_gamepad_axes(void)
{
	int	axis;

	if(!g_sdl_gamepad) {
		return;
	}
	for(axis = 0; axis < IMAP_NUM_SDL_AXES; axis++) {
		imap_pad_axis(g_mainwin_info.kimage_ptr, axis,
				SDL_GetGamepadAxis(g_sdl_gamepad, axis));
	}
}

/* Load SDL's controller mapping database. Looks next to the binary first
 *  (copied there at build time), then in ~/.config/iigs-emu/ as a fallback. */
static void
sdl_load_gamepad_mappings(void)
{
	const char *base, *xdg, *home;
	char	path[1024];
	int	count;

	base = SDL_GetBasePath();
	if(base) {
		snprintf(path, sizeof(path), "%sgamecontrollerdb.txt", base);
		count = SDL_AddGamepadMappingsFromFile(path);
		if(count > 0) {
			printf("Loaded %d gamepad mappings: %s\n", count, path);
			return;
		}
	}
	xdg = getenv("XDG_CONFIG_HOME");
	if(xdg && xdg[0]) {
		snprintf(path, sizeof(path), "%s/iigs-emu/gamecontrollerdb.txt",
									xdg);
	} else {
		home = getenv("HOME");
		if(!home) {
			return;
		}
		snprintf(path, sizeof(path),
				"%s/.config/iigs-emu/gamecontrollerdb.txt", home);
	}
	count = SDL_AddGamepadMappingsFromFile(path);
	if(count > 0) {
		printf("Loaded %d gamepad mappings: %s\n", count, path);
	}
}

/* Called once from kegs_init() (before sdl_video_init's SDL_Init).  The host
 * hook is registered first so input_map.c can reach the window from its
 * first key event. */
void
sdl_input_init(void)
{
	imap_set_host_hook(sdl_imap_host_action);
	if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
		printf("SDL_INIT_GAMEPAD failed: %s\n", SDL_GetError());
		return;
	}
	sdl_load_gamepad_mappings();
	sdl_open_first_gamepad();
}

/* Apply the g_fullscreen config var to the live window whenever it changes.
 * Both the fullscreen action and the config menu write g_fullscreen; this
 * keeps the actual window state (g_is_fullscreen) in sync with it, so toggling
 * the option in the config panel takes effect immediately, not just on the
 * next launch. */
static void
sdl_sync_fullscreen(Window_info *win)
{
	int	want = g_fullscreen ? 1 : 0;

	if(want != g_is_fullscreen) {
		g_is_fullscreen = want;
		SDL_SetWindowFullscreen(win->window, want);
	}
}

/* Hide the host OS cursor while it is over our content -- always in fullscreen,
 * and over the window in windowed mode -- unless the user turned it off via the
 * "Hide Mouse Cursor" config option (g_hide_mouse). SDL cursor visibility is
 * app-wide but only manifests over our window, so when the pointer leaves a
 * windowed-mode window we restore it for the rest of the desktop. The early-out
 * keeps this cheap to call every frame, so it also picks up g_hide_mouse and
 * fullscreen changes made from the config menu. */
static void
sdl_update_cursor_visibility(void)
{
	int	want_hidden = g_hide_mouse &&
				(g_is_fullscreen || g_mouse_over_window);

	if(want_hidden == g_cursor_hidden) {
		return;
	}
	g_cursor_hidden = want_hidden;
	if(want_hidden) {
		SDL_HideCursor();
	} else {
		SDL_ShowCursor();
	}
}

/* Toggles pointer capture (bound to Ctrl+F2 and middle-click by default).
 * While captured, the pointer stays confined to the window so the IIgs mouse
 * never loses it. */
static void
sdl_toggle_mouse_grab(Window_info *win)
{
	bool grab = !SDL_GetWindowMouseGrab(win->window);

	SDL_SetWindowMouseGrab(win->window, grab);
	printf("Mouse capture %s\n", grab ? "on" : "off");
	osd_show(grab ? "Mouse capture: on" : "Mouse capture: off");
}

/* Emulator-side actions that input_map.c hands back (see imap_action_is_host).
 * Runs on key press only; key repeat never reaches input_map.c. */
static void
sdl_imap_host_action(int action)
{
	Window_info *win = &g_mainwin_info;

	switch(action) {
	case IMAP_ACT_MOUSE_GRAB:
		sdl_toggle_mouse_grab(win);
		break;
	case IMAP_ACT_SCREENSHOT:
		g_screenshot_requested = 1;
		break;
	case IMAP_ACT_FULLSCREEN:
		/* Toggle the config var and apply it, so the key and the config
		 * menu agree. */
		g_fullscreen = !g_is_fullscreen;
		sdl_sync_fullscreen(win);
		break;
	case IMAP_ACT_SCANLINES:
		if(g_scanline_simulator > 0) {
			g_scanline_saved = g_scanline_simulator;
			g_scanline_simulator = 0;
		} else {
			g_scanline_simulator = g_scanline_saved;
		}
		break;
	case IMAP_ACT_CRT:
		g_crt = !g_crt;
		break;
	case IMAP_ACT_ZOOM_IN:
		/* cfg_int_update clamps to 100-200, as it does for the config menu. */
		cfg_int_update(&g_zoom, g_zoom + 1);
		g_config_kegs_update_needed = 1;
		break;
	case IMAP_ACT_ZOOM_OUT:
		cfg_int_update(&g_zoom, g_zoom - 1);
		g_config_kegs_update_needed = 1;
		break;
	default:
		break;
	}
}

/* Inverse of the zoom in sdl_render_framebuffer and sdl_build_crt_mesh: maps a
 * logical window point back to where it lands on the unzoomed picture. */
static void
sdl_unzoom_point(Window_info *win, float *x, float *y)
{
	float	zoom, cx, cy;

	if(sdl_zoom_percent() == 100) {
		return;
	}
	zoom = sdl_zoom_factor();
	cx = win->width_req * 0.5f;
	cy = win->main_height * 0.5f;
	*x = (*x - cx) / zoom + cx;
	*y = (*y - cy) / zoom + cy;
}

static void
sdl_poll_events(void)
{
	SDL_Event ev;
	Window_info *win = &g_mainwin_info;
	int	idx, mx, my;

	/* Pick up fullscreen / hide-cursor changes made from the config menu. */
	sdl_sync_fullscreen(win);
	sdl_update_cursor_visibility();

	while(SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_EVENT_QUIT:
			g_quit_requested = 1;
			break;
		case SDL_EVENT_WINDOW_MOVED:
			/* Remember the windowed position for the saved config.
			 * Fullscreen moves are not stored. */
			if(!g_is_fullscreen) {
				g_mainwin_xpos = ev.window.data1;
				g_mainwin_ypos = ev.window.data2;
			}
			break;
		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_RESTORED:
			/* Repaint the whole screen when the window (re)appears or resizes. */
			video_set_x_refresh_needed(g_mainwin_info.kimage_ptr, 1);
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			adb_mainwin_focus(1);
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			adb_mainwin_focus(0);
			imap_release_all(win->kimage_ptr);
			adb_kbd_repeat_off();
			break;
		case SDL_EVENT_WINDOW_MOUSE_ENTER:
			g_mouse_over_window = 1;
			sdl_update_cursor_visibility();
			break;
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			g_mouse_over_window = 0;
			sdl_update_cursor_visibility();
			break;
		case SDL_EVENT_DROP_FILE:
			/* Drop a disk image on the window to mount it (slot guessed
			 * from the file size). ev.drop.data is owned by SDL. */
			if(ev.drop.data) {
				cfg_inspect_maybe_insert_file(ev.drop.data);
				video_set_x_refresh_needed(win->kimage_ptr, 1);
			}
			break;
		case SDL_EVENT_KEY_DOWN:
			sdl_handle_key(win, &ev.key, 0);
			break;
		case SDL_EVENT_KEY_UP:
			sdl_handle_key(win, &ev.key, 1);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			/* Map window coords through the renderer's logical
			 * presentation, then into the IIgs framebuffer. */
			SDL_ConvertEventToRenderCoordinates(win->renderer, &ev);
			sdl_unzoom_point(win, &ev.motion.x, &ev.motion.y);
			mx = video_scale_mouse_x(win->kimage_ptr, (int)ev.motion.x, 0);
			my = video_scale_mouse_y(win->kimage_ptr, (int)ev.motion.y, 0);
			adb_update_mouse(win->kimage_ptr, mx, my, 0, 0);
			/* Motion means the pointer is over us; covers the case where
			 * the window opened under the cursor with no ENTER event. */
			if(!g_mouse_over_window) {
				g_mouse_over_window = 1;
				sdl_update_cursor_visibility();
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			idx = sdl_mouse_index(ev.button.button);
			if(idx >= 0) {
				imap_mouse_button(win->kimage_ptr, idx,
					ev.type == SDL_EVENT_MOUSE_BUTTON_UP);
			}
			break;
		case SDL_EVENT_GAMEPAD_ADDED:
			/* A controller was plugged in; adopt it if we have none. */
			sdl_open_first_gamepad();
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			if(g_sdl_gamepad &&
					SDL_GetGamepadID(g_sdl_gamepad) ==
							ev.gbutton.which) {
				imap_pad_button(win->kimage_ptr, ev.gbutton.button,
					ev.type == SDL_EVENT_GAMEPAD_BUTTON_UP);
			}
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			/* If the pad we were using went away, release its inputs and
			 * fall back to any other still-connected controller. */
			if(g_sdl_gamepad &&
					SDL_GetGamepadID(g_sdl_gamepad) ==
							ev.gdevice.which) {
				imap_pad_release_all(win->kimage_ptr);
				SDL_CloseGamepad(g_sdl_gamepad);
				g_sdl_gamepad = NULL;
				sdl_open_first_gamepad();
			}
			break;
		default:
			break;
		}
	}
	sdl_poll_gamepad_axes();
}

/* Build "[<ssdir>/]gsplus_screenshot_YYYYMMDD_HHMMSS.png" into buf. With no
 * ssdir configured the file lands in the current working directory. */
static void
sdl_build_screenshot_path(char *buf, size_t buflen)
{
	char	fname[64];
	time_t	now;
	struct tm *tmv;

	now = time(NULL);
	tmv = localtime(&now);
	if(tmv) {
		strftime(fname, sizeof(fname),
				"iigs-emu_screenshot_%Y%m%d_%H%M%S.png", tmv);
	} else {
		snprintf(fname, sizeof(fname),
				"iigs-emu_screenshot_%lld.png", (long long)now);
	}

	if(g_cfg_ssdir && g_cfg_ssdir[0]) {
		size_t len = strlen(g_cfg_ssdir);
		const char *sep = (g_cfg_ssdir[len - 1] == '/') ? "" : "/";
		snprintf(buf, buflen, "%s%s%s", g_cfg_ssdir, sep, fname);
	} else {
		snprintf(buf, buflen, "%s", fname);
	}
}

/* Grab the currently-rendered frame (post-scaling, including any scanline
 * overlay and letterbox borders) and write it to a PNG. Called after the frame
 * is drawn but before SDL_RenderPresent, so the back buffer is still valid. */
static void
sdl_save_screenshot(Window_info *win)
{
	SDL_Surface *shot, *conv;
	unsigned char *packed;
	char	path[1100];
	size_t	rowbytes;
	int	w, h, y, rc;

	shot = SDL_RenderReadPixels(win->renderer, NULL);
	if(!shot) {
		printf("Screenshot failed: SDL_RenderReadPixels: %s\n",
			SDL_GetError());
		return;
	}
	/* Normalise to byte-order RGBA, which is what write_png_rgba() expects. */
	conv = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(shot);
	if(!conv) {
		printf("Screenshot failed: SDL_ConvertSurface: %s\n",
			SDL_GetError());
		return;
	}

	/* SDL surfaces may pad rows (pitch >= w*4); pack them tightly. */
	w = conv->w;
	h = conv->h;
	rowbytes = (size_t)w * 4;
	packed = malloc(rowbytes * (size_t)h);
	if(!packed) {
		SDL_DestroySurface(conv);
		return;
	}
	for(y = 0; y < h; y++) {
		unsigned char *dst = packed + (size_t)y * rowbytes;
		memcpy(dst, (unsigned char *)conv->pixels
				+ (size_t)y * conv->pitch, rowbytes);
		/* The renderer's back buffer can carry a non-opaque alpha, which
		 * makes viewers composite the PNG over white and wash the colors
		 * out. A screenshot is opaque, so force every alpha byte to 0xff. */
		for(int x = 0; x < w; x++) {
			dst[x * 4 + 3] = 0xff;
		}
	}
	SDL_DestroySurface(conv);

	sdl_build_screenshot_path(path, sizeof(path));
	rc = write_png_rgba(path, packed, w, h);
	free(packed);

	if(rc) {
		printf("Screenshot failed: could not write %s\n", path);
		osd_show("Screenshot failed");
	} else {
		printf("Screenshot saved: %s (%dx%d)\n", path, w, h);
		osd_show("Screenshot saved");
	}
}

/* Linear blur, horizontal (g_hblur) and/or vertical (g_vblur). Each is 0-100 and
 * maps to a blur radius of up to BLUR_MAX_RADIUS source pixels (so 100 spreads
 * each pixel into ~2.5 of its neighbours along that axis, simulating composite-
 * video softness). The kernel is a normalized triangle (tent) of integer-offset
 * taps; a box would look flatter and a gaussian needs more taps for no visible
 * gain at this radius. */
#define BLUR_MAX_RADIUS		2.5f	/* source-pixel blur radius at blur==100 */
#define BLUR_MAX_HALF		3	/* max taps per side (ceil of MAX_RADIUS) */

/* Build a normalized 1D tent kernel for a 0-100 blur amount. Fills
 * weight[0..2*half] over offsets -half..+half so the taps sum to 1, and returns
 * half (0 when blur is off, i.e. a single implied centre tap of weight 1). */
static int
sdl_blur_kernel(int blur, float *weight)
{
	float	radius, base, sum;
	int	k, half;

	if(blur <= 0) {
		weight[0] = 1.0f;
		return 0;
	}
	radius = (float)blur / 100.0f * BLUR_MAX_RADIUS;
	half = (int)radius;
	if(radius > (float)half) { half++; }		/* ceil */
	if(half > BLUR_MAX_HALF) { half = BLUR_MAX_HALF; }
	base = radius + 1.0f;				/* tent half-base; radius 0 => identity */

	sum = 0.0f;
	for(k = -half; k <= half; k++) {
		float wt = 1.0f - (float)(k < 0 ? -k : k) / base;
		if(wt < 0.0f) { wt = 0.0f; }
		weight[k + half] = wt;
		sum += wt;
	}
	for(k = 0; k <= 2 * half; k++) { weight[k] /= sum; }	/* normalize to sum 1 */
	return half;
}

/* Draw win->texture into the current render target, filling the w x h logical
 * area, with the horizontal/vertical blur applied when g_hblur/g_vblur > 0. The
 * taps are summed with additive blending and their weights sum to 1, so the
 * result is a weighted average -- which requires the target to already be cleared
 * to black (both call sites do). With both blurs == 0 this is just the normal
 * crisp 1:1 copy. dst_scale maps the w x h area and the blur shifts into the
 * target's pixels: 1 for logical presentation, tex_scale for an offscreen
 * target sized to the texture. */
static void
sdl_render_framebuffer(Window_info *win, int w, int h, int dst_scale, float zoom)
{
	SDL_Renderer	*r = win->renderer;
	SDL_FRect	dst;
	float		hweight[2 * BLUR_MAX_HALF + 1];
	float		vweight[2 * BLUR_MAX_HALF + 1];
	float		full_w, full_h, zoom_w, zoom_h, zoom_x, zoom_y;
	int		kx, ky, hhalf, vhalf, mod;
	SDL_BlendMode	add;

	hhalf = sdl_blur_kernel(g_hblur, hweight);
	vhalf = sdl_blur_kernel(g_vblur, vweight);

	/* Zoom magnifies the output about its centre. The destination is larger
	 * than the output and the overflow is clipped by the viewport. */
	full_w = (float)(w * dst_scale);
	full_h = (float)(h * dst_scale);
	zoom_w = full_w * zoom;
	zoom_h = full_h * zoom;
	zoom_x = (full_w - zoom_w) * 0.5f;
	zoom_y = (full_h - zoom_h) * 0.5f;

	if((hhalf == 0) && (vhalf == 0)) {
		/* No blur: opaque nearest-neighbour copy. */
		dst.x = zoom_x;
		dst.y = zoom_y;
		dst.w = zoom_w;
		dst.h = zoom_h;
		SDL_RenderTexture(r, win->texture, NULL, &dst);
		return;
	}

	/* Accumulate the weighted, shifted taps additively. We can't use
	 * SDL_BLENDMODE_ADD here: it scales the source by its alpha (dstRGB =
	 * srcRGB*srcA + dstRGB), and the framebuffer texture's alpha reaches the
	 * blender as 0, which zeroes every tap and leaves a black screen. This custom
	 * mode adds the colour-modulated source RGB directly (factor ONE, source alpha
	 * ignored) and leaves the destination alpha untouched, so an offscreen target
	 * stays opaque for any later sampling (e.g. the CRT glow pass).
	 *
	 * Keep the texture at NEAREST: the taps are whole-source-pixel shifts, so the
	 * blur comes entirely from summing the shifted copies. Switching to LINEAR
	 * would also let the upscale-to-window bilinear-filter the image, mushing it
	 * in both axes regardless of which blur is on. The shifts kx/ky are in source
	 * pixels (dst is the same logical space the plain copy fills). */
	add = SDL_ComposeCustomBlendMode(
			SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
			SDL_BLENDOPERATION_ADD,
			SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE,
			SDL_BLENDOPERATION_ADD);
	SDL_SetTextureBlendMode(win->texture, add);
	dst.w = zoom_w;
	dst.h = zoom_h;

	/* Separable 2D tent done in a single accumulation step: each (kx,ky) tap gets
	 * weight hweight[kx]*vweight[ky]. Whichever axis is off has half==0 and one
	 * unit-weight centre tap, so a one-axis blur costs exactly what it did before
	 * and enabling both gives the outer product of the two 1D kernels. */
	for(ky = -vhalf; ky <= vhalf; ky++) {
		float vw = vweight[ky + vhalf];
		dst.y = zoom_y + (float)(ky * dst_scale) * zoom;
		for(kx = -hhalf; kx <= hhalf; kx++) {
			mod = (int)(hweight[kx + hhalf] * vw * 255.0f + 0.5f);
			SDL_SetTextureColorMod(win->texture, (Uint8)mod, (Uint8)mod,
						(Uint8)mod);
			dst.x = zoom_x + (float)(kx * dst_scale) * zoom;
			SDL_RenderTexture(r, win->texture, NULL, &dst);
		}
	}

	/* Restore the texture's normal state for any later/non-blur use. */
	SDL_SetTextureColorMod(win->texture, 255, 255, 255);
	SDL_SetTextureBlendMode(win->texture, SDL_BLENDMODE_NONE);
}

/* Render the framebuffer with the full curved-CRT effect. Assumes the changed
 * rectangles have already been uploaded into win->texture. Three passes:
 *   1. copy the (optionally blurred) framebuffer into crt_target (no curvature),
 *   2. downscale crt_target into glow_target (a cheap blur for bloom),
 *   3. warp crt_target onto the curvature mesh in the window, add the glow,
 *      then draw the mask + scanlines over the result in host-pixel space.
 * Logical presentation is disabled for the offscreen passes (we want a 1:1 fill)
 * and restored for the final on-window geometry pass. */
static void
sdl_render_crt(Window_info *win)
{
	SDL_Renderer *r = win->renderer;
	int	w = win->width_req;
	int	h = win->main_height;

	/* Rebuild the mesh if the curvature, vignette or glow knob changed at
	 * runtime. */
	if((win->crt_curve_for != g_crt_curve) ||
				(win->crt_vig_for != g_crt_vignette) ||
				(win->crt_glow_for != g_crt_glow) ||
				(win->crt_zoom_for != sdl_zoom_percent())) {
		sdl_build_crt_mesh(win, w, h);
		if(!win->crt_verts) {
			return;
		}
	}

	/* --- Pass 1: copy into crt_target (1:1, no logical scaling). The zoom is
	 * not applied here: it is applied to the curve mesh in pass 3. --- */
	SDL_SetRenderLogicalPresentation(r, 0, 0,
				SDL_LOGICAL_PRESENTATION_DISABLED);
	SDL_SetRenderTarget(r, win->crt_target);
	/* Clear to black first: the blur path accumulates additively, so it
	 * needs a cleared target. The alpha value doesn't matter -- the glow
	 * pass adds source RGB directly, independent of alpha (see below). */
	SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
	SDL_RenderClear(r);
	sdl_render_framebuffer(win, w, h, win->tex_scale, 1.0f);

	/* --- Pass 2: downscale crt_target -> glow_target (LINEAR = blur).
	 * Skipped entirely at glow 0 (as is the additive draw below). --- */
	if(g_crt_glow > 0) {
		SDL_SetRenderTarget(r, win->glow_target);
		SDL_RenderTexture(r, win->crt_target, NULL, NULL);
	}

	/* --- Pass 3: warp onto the curve in the window, then add the glow. --- */
	SDL_SetRenderTarget(r, NULL);
	SDL_SetRenderLogicalPresentation(r, w, h,
		g_noaspect ? SDL_LOGICAL_PRESENTATION_STRETCH
			   : SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
	SDL_RenderClear(r);

	SDL_SetTextureBlendMode(win->crt_target, SDL_BLENDMODE_NONE);
	SDL_SetTextureColorMod(win->crt_target, 255, 255, 255);
	SDL_RenderGeometry(r, win->crt_target, win->crt_verts, win->crt_nverts,
				win->crt_idx, win->crt_nidx);

	/* Additive bloom. SDL_BLENDMODE_ADD scales the source by its alpha,
	 * which here is whatever alpha the framebuffer copy happened to carry
	 * (opaque via the scaled video_out_data paths, 0 via the 1:1 path or
	 * after the additive blur) -- making the bloom silently switch on and
	 * off with window size and blur settings. Use a custom blend that adds
	 * the source RGB directly, so the glow never depends on alpha. The
	 * strength is in glow_verts' colors (see sdl_build_crt_mesh). */
	if(g_crt_glow > 0) {
		SDL_SetTextureBlendMode(win->glow_target,
			SDL_ComposeCustomBlendMode(
				SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
				SDL_BLENDOPERATION_ADD,
				SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE,
				SDL_BLENDOPERATION_ADD));
		SDL_RenderGeometry(r, win->glow_target, win->glow_verts,
				win->crt_nverts, win->crt_idx, win->crt_nidx);
	}

	/* Mask and scanlines go on last, in host-pixel space over the warped
	 * image, so they stay at fixed screen-pixel sizes regardless of window
	 * size. (They no longer feed the glow pass; the bloom now comes from
	 * the clean picture, which reads a touch smoother.) */
	sdl_render_overlays(win);
}

static void
sdl_update_display(Window_info *win)
{
	Change_rect rect;
	int	i, w, h;
	word32	*src;
	SDL_Rect r;
	int	xbrz_dirty = 0;

	if(!win->renderer) {
		return;
	}
	if(!video_get_active(win->kimage_ptr)) {
		return;
	}

	/* xBRZ was switched in the config menu: rebuild the texture at the new
	 * scale, resize the window to match, and re-emit the whole screen. */
	if(win->xbrz_for != g_xbrz) {
		sdl_create_texture(win, win->width_req, win->main_height);
		SDL_SetWindowSize(win->window, win->width_req * sdl_xbrz_scale(),
				win->main_height * sdl_xbrz_scale());
		video_set_x_refresh_needed(win->kimage_ptr, 1);
	}

	/* If the IIgs changed video mode, the logical size changes: resize the
	 * window and rebuild the texture to match. */
	if(video_change_aspect_needed(win->kimage_ptr, win->width_req,
						win->main_height)) {
		w = video_get_x_width(win->kimage_ptr);
		h = video_get_x_height(win->kimage_ptr);
		win->width_req = w;
		win->main_height = h;
		win->pixels_per_line = w;
		video_update_scale(win->kimage_ptr, w, h, 1);
		SDL_SetWindowSize(win->window, w * sdl_xbrz_scale(),
				h * sdl_xbrz_scale());
		/* Honor -noaspect here too, matching init and the CRT path; otherwise
		 * a stretched view reverts to letterbox after the first mode change. */
		SDL_SetRenderLogicalPresentation(win->renderer, w, h,
			g_noaspect ? SDL_LOGICAL_PRESENTATION_STRETCH
				   : SDL_LOGICAL_PRESENTATION_LETTERBOX);
		sdl_create_texture(win, w, h);
		/* The new texture starts out blank, so ask the core to re-emit
		 * the whole screen rather than just the next frame's deltas. */
		video_set_x_refresh_needed(win->kimage_ptr, 1);
	}

	/* Ask the core for each changed rectangle (it writes pixels into our
	 * buffer) and upload that rectangle to the texture. */
	for(i = 0; i < MAX_CHANGE_RECTS; i++) {
		if(!video_out_data(win->data, win->kimage_ptr,
					win->pixels_per_line, &rect, i)) {
			break;
		}
		r.x = rect.x;
		r.y = rect.y;
		r.w = rect.width;
		r.h = rect.height;
		src = win->data + (size_t)rect.y * win->pixels_per_line + rect.x;
		if(win->tex_scale == 1) {
			SDL_UpdateTexture(win->texture, &r, src,
				win->pixels_per_line * (int)sizeof(word32));
		} else {
			/* xBRZ needs neighbours, so the frame is re-upscaled whole
			 * below rather than uploaded rect by rect. */
			xbrz_dirty = 1;
		}
	}
	if((win->tex_scale == 2) && xbrz_dirty) {
		sdl_xbrz_upload(win);
	}

	if(g_crt && win->crt_target && win->glow_target && win->crt_verts) {
		/* Full curved-CRT path (curvature + mask + glow + vignette). */
		sdl_render_crt(win);
	} else {
		/* Plain path: framebuffer straight to the window (with optional
		 * blur), scanlines drawn over it in host-pixel space. Clear to
		 * black explicitly since the blur path accumulates additively. */
		SDL_SetRenderDrawColor(win->renderer, 0, 0, 0, 255);
		SDL_RenderClear(win->renderer);
		sdl_render_framebuffer(win, win->width_req, win->main_height, 1,
					sdl_zoom_factor());
		sdl_render_overlays(win);
	}

	/* Service a pending F10 capture now, while the just-drawn frame is
	 * still in the back buffer (RenderPresent may invalidate it). */
	if(g_screenshot_requested) {
		sdl_save_screenshot(win);
		g_screenshot_requested = 0;
	}

	sdl_render_osd(win);
	SDL_RenderPresent(win->renderer);
}

/* --------------------------------------------------------------------------
 * Terminal debugger REPL.
 *
 * The core's built-in 65816 monitor (debugger.c) already writes all its output
 * to stdout. What it lacked under SDL was a way to receive input -- it used to
 * read keystrokes from a dedicated debugger window. Rather than rebuild a
 * terminal inside SDL (scrollback, copy/paste, history are all hard), we drive
 * the monitor straight from the launching terminal, which already has them.
 *
 * A reader thread blocks on stdin so the SDL main loop never stalls; complete
 * lines land in a small queue that sdl_debugger_poll() drains while the CPU is
 * halted (the debug_go action halts or resumes the CPU).  Launched with no
 * terminal (for example from a file manager), stdin hits EOF and the thread
 * simply exits -- the emulator runs normally, just without an interactive
 * debugger.
 * ------------------------------------------------------------------------- */

#define DBG_LINE_MAX	256
#define DBG_QUEUE_LEN	32

static char g_dbg_queue[DBG_QUEUE_LEN][DBG_LINE_MAX];
static int g_dbg_q_head = 0;		/* next slot to read (main thread)   */
static int g_dbg_q_tail = 0;		/* next slot to write (reader thread) */
static SDL_Mutex *g_dbg_mutex = NULL;
static int g_dbg_prompt_shown = 0;

static void
sdl_dbg_push(const char *line)
{
	int	next;

	SDL_LockMutex(g_dbg_mutex);
	next = (g_dbg_q_tail + 1) % DBG_QUEUE_LEN;
	if(next != g_dbg_q_head) {		/* silently drop if full */
		SDL_strlcpy(g_dbg_queue[g_dbg_q_tail], line, DBG_LINE_MAX);
		g_dbg_q_tail = next;
	}
	SDL_UnlockMutex(g_dbg_mutex);
}

/* Reader thread: block on stdin, push each completed line onto the queue.
 * Uses read(), not fgets(): a thread blocked inside stdio holds stdin's
 * FILE lock, and exit() then deadlocks flushing it. */
static int SDLCALL
sdl_stdin_reader(void *data)
{
	char	buf[256];
	char	line[DBG_LINE_MAX];
	int	len = 0, i;
	ssize_t	n;

	(void)data;
	while((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
		for(i = 0; i < n; i++) {
			if(buf[i] == '\n') {
				line[len] = 0;
				sdl_dbg_push(line);
				len = 0;
				continue;
			}
			if(len == DBG_LINE_MAX - 1) {	/* split overlong lines */
				line[len] = 0;
				sdl_dbg_push(line);
				len = 0;
			}
			line[len++] = buf[i];
		}
	}
	if(len > 0) {			/* EOF without a trailing newline */
		line[len] = 0;
		sdl_dbg_push(line);
	}
	return 0;		/* EOF: no terminal attached, or input closed */
}

/* Pop one queued line into out[DBG_LINE_MAX], stripping the trailing newline.
 * Returns 1 if a line was available, 0 otherwise. */
static int
sdl_dbg_dequeue(char *out)
{
	int	got = 0, i;

	SDL_LockMutex(g_dbg_mutex);
	if(g_dbg_q_head != g_dbg_q_tail) {
		SDL_strlcpy(out, g_dbg_queue[g_dbg_q_head], DBG_LINE_MAX);
		g_dbg_q_head = (g_dbg_q_head + 1) % DBG_QUEUE_LEN;
		got = 1;
	}
	SDL_UnlockMutex(g_dbg_mutex);
	if(got) {
		for(i = 0; out[i]; i++) {
			if((out[i] == '\n') || (out[i] == '\r')) {
				out[i] = 0;
				break;
			}
		}
	}
	return got;
}

static void
sdl_dbg_prompt(void)
{
	printf("iigs-emu> ");
	fflush(stdout);
	g_dbg_prompt_shown = 1;
}

/* Create the stdin reader thread. Non-fatal on failure: the emulator still
 * runs, just without terminal debugger input. */
static void
sdl_debugger_init(void)
{
	SDL_Thread *thread;

	g_dbg_mutex = SDL_CreateMutex();
	if(!g_dbg_mutex) {
		printf("Debugger: SDL_CreateMutex failed: %s\n", SDL_GetError());
		return;
	}
	thread = SDL_CreateThread(sdl_stdin_reader, "stdin-reader", NULL);
	if(!thread) {
		printf("Debugger: SDL_CreateThread failed: %s\n", SDL_GetError());
		return;
	}
	SDL_DetachThread(thread);	/* fire-and-forget; dies with the process */
}

/* Drive the terminal monitor: prompt once on halt, then feed queued lines to
 * the core's command parser. Called once per frame from the main loop. */
static void
sdl_debugger_poll(void)
{
	char	line[DBG_LINE_MAX];

	if(!g_dbg_mutex || !g_halt_sim) {
		g_dbg_prompt_shown = 0;		/* running: re-prompt on next halt */
		return;
	}
	if(!g_dbg_prompt_shown) {
		printf("\n[debugger] CPU halted -- type 'h' for help, 'g' to "
							"continue\n");
		sdl_dbg_prompt();
	}
	/* The terminal echoes what the user types; do_debug_cmd() echoes the
	 * command and prints its output. Stop if a command (g/s) resumes the CPU. */
	while(g_halt_sim && sdl_dbg_dequeue(line)) {
		do_debug_cmd(line);
		if(g_halt_sim) {
			sdl_dbg_prompt();
		} else {
			g_dbg_prompt_shown = 0;
		}
	}
}

int
main(int argc, char **argv)
{
	int	mdepth = 32;		/* ARGB8888 -> 32-bit pixels */
	int	ret;

	printf("IIGS-Emu %s (SDL3)\n", GSPLUS_VERSION_STR);

	ret = parse_argv(argc, argv, 1);
	if(ret) {
		printf("parse_argv ret: %d, stopping\n", ret);
		exit(1);
	}

	ret = kegs_init(mdepth, GSPLUS_MAX_WIDTH, GSPLUS_MAX_HEIGHT, 0);
	if(ret) {
		printf("kegs_init ret: %d, stopping\n", ret);
		exit(1);
	}

	sdl_video_init();
	sdl_debugger_init();

	/* Main loop: run_16ms() runs one video frame's worth of CPU + video. */
	while(!g_quit_requested) {
		ret = run_16ms();
		if(ret != 0) {
			printf("run_16ms returned: %d\n", ret);
			break;
		}
		sdl_poll_events();
		sdl_debugger_poll();
		sdl_update_display(&g_mainwin_info);
	}

	sdl_snd_shutdown();
	SDL_Quit();
	return 0;
}

/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

/* The high-level video driver subsystem */

#include "SDL.h"
#include "SDL_sysvideo.h"
#include "SDL_blit.h"
#include "SDL_pixels_c.h"

SDL_VideoDevice *current_video = NULL;

/* Various local functions */
int SDL_VideoInit(const char *driver_name, Uint32 flags);
void SDL_VideoQuit(void);

/*
 * Get the current display surface
 */
SDL_Surface *SDL_GetVideoSurface(void)
{
	SDL_Surface *visible;

	visible = NULL;
	if ( current_video ) {
		visible = current_video->visible;
	}
	return(visible);
}

/*
 * Get the current information about the video hardware
 */
const SDL_VideoInfo *SDL_GetVideoInfo(void)
{
	const SDL_VideoInfo *info;

	info = NULL;
	if ( current_video ) {
		info = &current_video->info;
	}
	return(info);
}

/*
 * Return a pointer to an array of available screen dimensions for the
 * given format, sorted largest to smallest.  Returns NULL if there are
 * no dimensions available for a particular format, or (SDL_Rect **)-1
 * if any dimension is okay for the given format.  If 'format' is NULL,
 * the mode list will be for the format given by SDL_GetVideoInfo()->vfmt
 */
SDL_Rect ** SDL_ListModes (SDL_PixelFormat *format, Uint32 flags)
{
	SDL_VideoDevice *video = current_video;
	SDL_VideoDevice *this  = current_video;
	SDL_Rect **modes;

	modes = NULL;
	if ( SDL_VideoSurface ) {
		if ( format == NULL ) {
			format = SDL_VideoSurface->format;
		}
		modes = video->ListModes(this, format, flags);
	}
	return(modes);
}

/*
 * Check to see if a particular video mode is supported.
 * It returns 0 if the requested mode is not supported under any bit depth,
 * or returns the bits-per-pixel of the closest available mode with the
 * given width and height.  If this bits-per-pixel is different from the
 * one used when setting the video mode, SDL_SetVideoMode() will succeed,
 * but will emulate the requested bits-per-pixel with a shadow surface.
 */
static Uint8 SDL_closest_depths[4][8] = {
	/* 8 bit closest depth ordering */
	{ 0, 8, 16, 15, 32, 24, 0, 0 },
	/* 15,16 bit closest depth ordering */
	{ 0, 16, 15, 32, 24, 8, 0, 0 },
	/* 24 bit closest depth ordering */
	{ 0, 24, 32, 16, 15, 8, 0, 0 },
	/* 32 bit closest depth ordering */
	{ 0, 32, 16, 15, 24, 8, 0, 0 }
};


#ifdef __MACOS__ /* MPW optimization bug? */
#define NEGATIVE_ONE 0xFFFFFFFF
#else
#define NEGATIVE_ONE -1
#endif

int SDL_VideoModeOK (int width, int height, int bpp, Uint32 flags)
{
	int table, b, i;
	int supported;
	SDL_PixelFormat format;
	SDL_Rect **sizes;

	/* Currently 1 and 4 bpp are not supported */
	if ( bpp < 8 || bpp > 32 ) {
		return(0);
	}
	if ( (width <= 0) || (height <= 0) ) {
		return(0);
	}

	/* Search through the list valid of modes */
	SDL_memset(&format, 0, sizeof(format));
	supported = 0;
	table = ((bpp+7)/8)-1;
	SDL_closest_depths[table][0] = bpp;
	SDL_closest_depths[table][7] = 0;
	for ( b = 0; !supported && SDL_closest_depths[table][b]; ++b ) {
		format.BitsPerPixel = SDL_closest_depths[table][b];
		sizes = SDL_ListModes(&format, flags);
		if ( sizes == (SDL_Rect **)0 ) {
			/* No sizes supported at this bit-depth */
			continue;
		} else 
		if (sizes == (SDL_Rect **)NEGATIVE_ONE) {
			/* Any size supported at this bit-depth */
			supported = 1;
			continue;
		} else if (current_video->handles_any_size) {
			/* Driver can center a smaller surface to simulate fullscreen */
			for ( i=0; sizes[i]; ++i ) {
				if ((sizes[i]->w >= width) && (sizes[i]->h >= height)) {
					supported = 1; /* this mode can fit the centered window. */
					break;
				}
			}
		} else
		for ( i=0; sizes[i]; ++i ) {
			if ((sizes[i]->w == width) && (sizes[i]->h == height)) {
				supported = 1;
				break;
			}
		}
	}
	if ( supported ) {
		--b;
		return(SDL_closest_depths[table][b]);
	} else {
		return(0);
	}
}

/*
 * Get the closest non-emulated video mode to the one requested
 */
static int SDL_GetVideoMode (int *w, int *h, int *BitsPerPixel, Uint32 flags)
{
	int table, b, i;
	int supported;
	int native_bpp;
	SDL_PixelFormat format;
	SDL_Rect **sizes;

	/* Check parameters */
	if ( *BitsPerPixel < 8 || *BitsPerPixel > 32 ) {
		SDL_SetError("Invalid bits per pixel (range is {8...32})");
		return(0);
	}
	if ((*w <= 0) || (*h <= 0)) {
		SDL_SetError("Invalid width or height");
		return(0);
	}

	/* Try the original video mode, get the closest depth */
	native_bpp = SDL_VideoModeOK(*w, *h, *BitsPerPixel, flags);
	if ( native_bpp == *BitsPerPixel ) {
		return(1);
	}
	if ( native_bpp > 0 ) {
		*BitsPerPixel = native_bpp;
		return(1);
	}

	/* No exact size match at any depth, look for closest match */
	SDL_memset(&format, 0, sizeof(format));
	supported = 0;
	table = ((*BitsPerPixel+7)/8)-1;
	SDL_closest_depths[table][0] = *BitsPerPixel;
	SDL_closest_depths[table][7] = SDL_VideoSurface->format->BitsPerPixel;
	for ( b = 0; !supported && SDL_closest_depths[table][b]; ++b ) {
		int best;

		format.BitsPerPixel = SDL_closest_depths[table][b];
		sizes = SDL_ListModes(&format, flags);
		if ( sizes == (SDL_Rect **)0 ) {
			/* No sizes supported at this bit-depth */
			continue;
		}
		best=0;
		for ( i=0; sizes[i]; ++i ) {
			/* Mode with both dimensions bigger or equal than asked ? */
			if ((sizes[i]->w >= *w) && (sizes[i]->h >= *h)) {
				/* Mode with any dimension smaller or equal than current best ? */
				if ((sizes[i]->w <= sizes[best]->w) || (sizes[i]->h <= sizes[best]->h)) {
					/* Now choose the mode that has less pixels */
					if ((sizes[i]->w * sizes[i]->h) <= (sizes[best]->w * sizes[best]->h)) {
						best=i;
						supported = 1;
					}
				}
			}
		}
		if (supported) {
			*w=sizes[best]->w;
			*h=sizes[best]->h;
			*BitsPerPixel = SDL_closest_depths[table][b];
		}
	}
	if ( ! supported ) {
		SDL_SetError("No video mode large enough for %dx%d", *w, *h);
	}
	return(supported);
}

/* This should probably go somewhere else -- like SDL_surface.c */
static void SDL_ClearSurface(SDL_Surface *surface)
{
	Uint32 black;

	black = SDL_MapRGB(surface->format, 0, 0, 0);
	SDL_FillRect(surface, NULL, black);
	if (surface->flags&SDL_FULLSCREEN) {
		SDL_Flip(surface);
	}
}

#ifdef __QNXNTO__
    #include <sys/neutrino.h>
#endif /* __QNXNTO__ */

#ifdef WIN32
	extern int sysevents_mouse_pressed;
#endif

/* 
 * Convert a surface into the video pixel format.
 */
SDL_Surface * SDL_DisplayFormat (SDL_Surface *surface)
{
	Uint32 flags;

	if ( ! SDL_PublicSurface ) {
		SDL_SetError("No video mode has been set");
		return(NULL);
	}
	/* Set the flags appropriate for copying to display surface */
	if (((SDL_PublicSurface->flags&SDL_HWSURFACE) == SDL_HWSURFACE) && current_video->info.blit_hw)
		flags = SDL_HWSURFACE;
	else 
		flags = SDL_SWSURFACE;
#ifdef AUTORLE_DISPLAYFORMAT
	flags |= (surface->flags & (SDL_SRCCOLORKEY|SDL_SRCALPHA));
	flags |= SDL_RLEACCELOK;
#else
	flags |= surface->flags & (SDL_SRCCOLORKEY|SDL_SRCALPHA|SDL_RLEACCELOK);
#endif
	return(SDL_ConvertSurface(surface, SDL_PublicSurface->format, flags));
}

/*
 * Convert a surface into a format that's suitable for blitting to
 * the screen, but including an alpha channel.
 */
SDL_Surface *SDL_DisplayFormatAlpha(SDL_Surface *surface)
{
	SDL_PixelFormat *vf;
	SDL_PixelFormat *format;
	SDL_Surface *converted;
	Uint32 flags;
	/* default to ARGB8888 */
	Uint32 amask = 0xff000000;
	Uint32 rmask = 0x00ff0000;
	Uint32 gmask = 0x0000ff00;
	Uint32 bmask = 0x000000ff;

	if ( ! SDL_PublicSurface ) {
		SDL_SetError("No video mode has been set");
		return(NULL);
	}
	vf = SDL_PublicSurface->format;

	switch(vf->BytesPerPixel) {
	    case 2:
		/* For XGY5[56]5, use, AXGY8888, where {X, Y} = {R, B}.
		   For anything else (like ARGB4444) it doesn't matter
		   since we have no special code for it anyway */
		if ( (vf->Rmask == 0x1f) &&
		     (vf->Bmask == 0xf800 || vf->Bmask == 0x7c00)) {
			rmask = 0xff;
			bmask = 0xff0000;
		}
		break;

	    case 3:
	    case 4:
		/* Keep the video format, as long as the high 8 bits are
		   unused or alpha */
		if ( (vf->Rmask == 0xff) && (vf->Bmask == 0xff0000) ) {
			rmask = 0xff;
			bmask = 0xff0000;
		} else if ( vf->Rmask == 0xFF00 && (vf->Bmask == 0xFF000000) ) {
			amask = 0x000000FF;
			rmask = 0x0000FF00;
			gmask = 0x00FF0000;
			bmask = 0xFF000000;
		}
		break;

	    default:
		/* We have no other optimised formats right now. When/if a new
		   optimised alpha format is written, add the converter here */
		break;
	}
	format = SDL_AllocFormat(32, rmask, gmask, bmask, amask);
	flags = SDL_PublicSurface->flags & SDL_HWSURFACE;
	flags |= surface->flags & (SDL_SRCALPHA | SDL_RLEACCELOK);
	converted = SDL_ConvertSurface(surface, format, flags);
	SDL_FreeFormat(format);
	return(converted);
}

/*
 * Update a specific portion of the physical screen
 */
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
	if ( screen ) {
		SDL_Rect rect;

		/* Perform some checking */
		if ( w == 0 )
			w = screen->w;
		if ( h == 0 )
			h = screen->h;
		if ( (int)(x+w) > screen->w )
			return;
		if ( (int)(y+h) > screen->h )
			return;

		/* Fill the rectangle */
		rect.x = (Sint16)x;
		rect.y = (Sint16)y;
		rect.w = (Uint16)w;
		rect.h = (Uint16)h;
		SDL_UpdateRects(screen, 1, &rect);
	}
}

void SDL_UpdateRects (SDL_Surface *screen, int numrects, SDL_Rect *rects)
{
	int i;
	SDL_VideoDevice *video = current_video;
	SDL_VideoDevice *this = current_video;

	if ( screen == SDL_VideoSurface ) {
		/* Update the video surface */
		if ( screen->offset ) {
			for ( i=0; i<numrects; ++i ) {
				rects[i].x += video->offset_x;
				rects[i].y += video->offset_y;
			}
			video->UpdateRects(this, numrects, rects);
			for ( i=0; i<numrects; ++i ) {
				rects[i].x -= video->offset_x;
				rects[i].y -= video->offset_y;
			}
		} else {
			video->UpdateRects(this, numrects, rects);
		}
	}
}

/*
 * Performs hardware double buffering, if possible, or a full update if not.
 */
int SDL_Flip(SDL_Surface *screen)
{
   SDL_UpdateRect(screen, 0, 0, 0, 0);
	return(0);
}

/* Utility function used by SDL_WM_SetIcon();
 * flags & 1 for color key, flags & 2 for alpha channel. */
static void CreateMaskFromColorKeyOrAlpha(SDL_Surface *icon, Uint8 *mask, int flags)
{
	int x, y;
	Uint32 colorkey;
#define SET_MASKBIT(icon, x, y, mask) \
	mask[(y*((icon->w+7)/8))+(x/8)] &= ~(0x01<<(7-(x%8)))

	colorkey = icon->format->colorkey;
	switch (icon->format->BytesPerPixel) {
		case 1: { Uint8 *pixels;
			for ( y=0; y<icon->h; ++y ) {
				pixels = (Uint8 *)icon->pixels + y*icon->pitch;
				for ( x=0; x<icon->w; ++x ) {
					if ( *pixels++ == colorkey ) {
						SET_MASKBIT(icon, x, y, mask);
					}
				}
			}
		}
		break;

		case 2: { Uint16 *pixels;
			for ( y=0; y<icon->h; ++y ) {
				pixels = (Uint16 *)icon->pixels +
				                   y*icon->pitch/2;
				for ( x=0; x<icon->w; ++x ) {
					if ( (flags & 1) && *pixels == colorkey ) {
						SET_MASKBIT(icon, x, y, mask);
					} else if((flags & 2) && (*pixels & icon->format->Amask) == 0) {
						SET_MASKBIT(icon, x, y, mask);
					}
					pixels++;
				}
			}
		}
		break;

		case 4: { Uint32 *pixels;
			for ( y=0; y<icon->h; ++y ) {
				pixels = (Uint32 *)icon->pixels +
				                   y*icon->pitch/4;
				for ( x=0; x<icon->w; ++x ) {
					if ( (flags & 1) && *pixels == colorkey ) {
						SET_MASKBIT(icon, x, y, mask);
					} else if((flags & 2) && (*pixels & icon->format->Amask) == 0) {
						SET_MASKBIT(icon, x, y, mask);
					}
					pixels++;
				}
			}
		}
		break;
	}
}

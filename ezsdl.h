#ifndef EZSDL_H
#define EZSDL_H

#ifdef USE_SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>

#ifndef EZSDL_BITDEPTH
#define EZSDL_BITDEPTH 32
#endif

#if EZSDL_BITDEPTH == 32
#define EZSDL_PIXEL_FMT SDL_PIXELFORMAT_ARGB8888
#else
#define EZSDL_PIXEL_FMT SDL_PIXELFORMAT_RGB565
#endif

#ifdef USE_SDL2
#pragma RcB2 LINK "-lSDL2"
#else
#pragma RcB2 LINK "-lSDL"
#endif

#define SDL_RGB_LSHIFT 8
#define RGB_RED(X) 0 | ((X) << (16+SDL_RGB_LSHIFT))
#define RGB_GREEN(X) 0 | ((X) << (8+SDL_RGB_LSHIFT))
#define RGB_BLUE(X) (X << (0+SDL_RGB_LSHIFT))
#define RGB(R,G,B) (RGB_RED(R) | RGB_GREEN(G) | RGB_BLUE(B))

#define ARGB_RED(X) 0 | ((X) << 16)
#define ARGB_GREEN(X) 0 | ((X) << 8)
#define ARGB_BLUE(X) (X << 0)
#define ARGB(R,G,B) (ARGB_RED(R) | ARGB_GREEN(G) | ARGB_BLUE(B))

#ifndef MAX
#define MAX(A, B) (((A) > (B)) ? (A) : (B))
#define MIN(A, B) (((A) < (B)) ? (A) : (B))
#endif

#define SCALEUP(V, S) (V*S/100)
#define SCALEDOWN(V, S) (V*100/S)
#define SCALECALC(OLDV, NEWV) (NEWV*100/OLDV)
#define NEWSCALE(OLDW, NEWW, OLDH, NEWH) MAX(SCALECALC(OLDW, NEWW), SCALECALC(OLDH, NEWH))

static inline unsigned rgba_to_argb(unsigned col) { return col >> 8; }
static inline unsigned argb_to_rgba(unsigned col) { return col << 8; }

typedef struct bmp4 {
	unsigned width, height;
	unsigned *data;
} bmp4;

static inline bmp4* bmp4_new(unsigned width, unsigned height) {
	size_t wxh = (size_t)width * (size_t)height;
	bmp4* res = malloc((sizeof *res)+(wxh*sizeof(*res->data)));
	if(!res) return 0;
	res->width=width;
	res->height=height;
	res->data=(void*)(res+1);
	return res;
}

static inline bmp4* bmp4_from_filestream(FILE *f) {
	unsigned w, h;
	fread(&w, 4, 1, f);
	fread(&h, 4, 1, f);
	bmp4* b = bmp4_new(w, h);
	if(!b) goto ext;
	size_t i, wxh = (size_t)w * (size_t)h;
	unsigned *p = b->data;
	for(i=0; i<wxh; i++, p++) fread(p, 4, 1, f);
	ext:
	return b;
}
static inline bmp4* bmp4_from_file(const char* fn) {
	FILE*f=fopen(fn, "r");
	if(!f) return 0;
	bmp4 *b = bmp4_from_filestream(f);
	fclose(f);
	return b;
}

static inline void bmp4_fill(bmp4 *b, unsigned color) {
	size_t i = 0;
	size_t max = (size_t)b->width*(size_t)b->height;
	unsigned *d = b->data;
	for(;i<max;i++) *(d++) = color;
}

static inline bmp4* bmp4_new_filled(unsigned width, unsigned height, unsigned color) {
	bmp4 *b = bmp4_new(width, height);
	if(b) bmp4_fill(b, color);
	return b;
}

static inline unsigned* bmp4_scanline(bmp4* b, int y) {
	return &b->data[y * b->width];
}

typedef struct bmp3 {
	unsigned width, height;
	unsigned char *data;
} bmp3;

static inline bmp3* bmp3_new(unsigned width, unsigned height) {
	bmp3* res = malloc((sizeof *res)+(width*height*3));
	res->width=width;
	res->height=height;
	res->data=(void*)(res+1);
	return res;
}

static inline unsigned char* bmp3_scanline(bmp3* b, int y) {
	return &b->data[3*y*b->width];
}

static inline void bmp3_to_bmp4(bmp3* in, bmp4 *out) {
	assert(in->width == out->width && in->height == out->height);
        size_t i = 0;
        size_t max = (size_t)in->width*(size_t)in->height;
	unsigned *d = out->data;
	unsigned char* id = in->data;
	#define ALPHA (0xff << 24)
	for(;i<max;i++,id+=3) *(d++) = ALPHA | (id[0] << 16) | (id[1] << 8) | id[2];
	#undef ALPHA
}

static inline void bmp4_to_bmp3(bmp4* in, bmp3 *out) {
	assert(in->width == out->width && in->height == out->height);
        size_t i = 0;
        size_t max = (size_t)in->width*(size_t)in->height;
	unsigned char *d = out->data;
	unsigned* id = in->data;
	for(;i<max;i++,d+=3,id++) {
		d[0] = (*id >> 16) & 255;
		d[1] = (*id >> 8 ) & 255;
		d[2] = (*id >> 0 ) & 255;
	}
}

typedef struct bmp1 {
	unsigned width, height;
	unsigned char *data;
} bmp1;

static inline bmp1* bmp1_new(unsigned width, unsigned height) {
	bmp1 *res = malloc((sizeof *res)+(width*height*1));
	res->width=width;
	res->height=height;
	res->data=(void*)(res+1);
	return res;
}

static inline unsigned char* bmp1_scanline(bmp1* b, int y) {
	return &b->data[y*b->width];
}

static inline unsigned colget8(unsigned char col, int which) {
	switch (which) {
		case 0: /* red */
			return (col >> 5) * 36;
		case 1: /* green */
			return ((col >> 3) & 3) * 85;
		case 2: /* blue */
			return (col & 7) * 36;
		default:
			return 0;
	}
}

static inline void bmp1_to_bmp4(bmp1* in, bmp4 *out, unsigned palette[256]) {
	assert(in->width == out->width && in->height == out->height);
        size_t i = 0;
        size_t max = (size_t)in->width*(size_t)in->height;
	unsigned *d = out->data;
	unsigned char* id = in->data;
	#define ALPHA (0xff << 24)
	if(!palette)
		for(;i<max;i++,id++) *(d++) = ALPHA | (colget8(*id, 0) << 16) | (colget8(*id, 1) << 8) | colget8(*id, 2);
	else
		for(;i<max;i++,id++) *(d++) = ALPHA | palette[*id];
	#undef ALPHA
}

enum resize_method {
	RM_WINDOW = 0, // hwscale stays fixed, w&h changes
	RM_SCALE = 1,  // hwscale changes, logical w&h stays fixed
};

typedef struct display {
	unsigned width, height;
#ifdef USE_SDL2
	SDL_Window *win;
	SDL_Renderer *ren;
	SDL_Texture *tex;
#else
	SDL_Surface *surface;
#endif
	int fs;
	int flags;
	unsigned hwscale; // hardware scaling in percent
	enum resize_method rm;
} display;

static inline void display_set_resize_method(display *d, enum resize_method rm) {
	d->rm = rm;
}

/* display pointed to is assumed to be zero-filled */
/* hwscale uses SDL2 hardware scaling to render. it's provided as a percentage.
   so 100 means scale 1. the width and height provided need to be the
   UNSCALED dimensions. so if you code for example an emulator for a machine
   with 320x240 pixels, and want 2x scale, set width=320, height=240, hwscale=200
   and the actual window will be 640x480. however, this works transparently,
   so you can do all calculations within the 1x scale boundaries, including
   mouse movements. when you call display_get_width(), it will return 320. */
static inline void display_init(display *d, unsigned width, unsigned height, unsigned hwscale, int flags) {
	static int init_done;
	int sw = SCALEUP(width, hwscale);
	int sh = SCALEUP(height, hwscale);
#ifndef USE_SDL2
	SDL_Surface *old = d->surface;
	if(!flags) flags = SDL_HWPALETTE;
	assert("sorry, SDL1 support for hwscale other than 100 not implemented!" && hwscale == 100);
#endif
	if(!init_done) SDL_Init(SDL_INIT_VIDEO);
	init_done = 1;
#ifdef USE_SDL2
	if(!d->win) {
		d->win = SDL_CreateWindow("ezsdl", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, sw, sh, SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
		SDL_DisplayMode dm = {.format = EZSDL_PIXEL_FMT, .w = sw, .h = sh };
		SDL_SetWindowDisplayMode(d->win, &dm);
		/* SDL2 defaults to inhibit the screensaver, which makes sense
		   for an action game using a joystick, but we use it
		   for an ebook reader that's often minimized or backgrounded
		   and disabling the screensaver for that is confusing at best. */
		SDL_EnableScreenSaver();
	}
	if(d->tex)
		SDL_DestroyTexture(d->tex);
	if(d->ren)
		SDL_DestroyRenderer(d->ren);
	d->ren = SDL_CreateRenderer(d->win, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_TARGETTEXTURE);
	d->tex = SDL_CreateTexture(d->ren, EZSDL_PIXEL_FMT, SDL_TEXTUREACCESS_STREAMING, width, height);
#else
	d->surface = SDL_SetVideoMode(width, height, EZSDL_BITDEPTH, flags);
	if(old && old != d->surface) SDL_FreeSurface(old);
#endif
	d->width = width;
	d->height = height;
	d->fs = 0;
	d->flags = flags;
	d->hwscale = hwscale;
}

static inline void display_toggle_fullscreen_i(display *d, int update);
static inline void display_shutdown(display *d) {
        if(d->fs) display_toggle_fullscreen_i(d, 0);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static inline void display_get_vram_and_pitch(display *d, void** pixels, unsigned *pitch)
{
#ifdef USE_SDL2
	SDL_LockTexture(d->tex, 0, pixels, pitch);
#else
	*pitch = d->surface->pitch;
	*pixels = d->surface->pixels;
#endif
}

static inline void display_release_vram(display *d) {
#ifdef USE_SDL2
	SDL_UnlockTexture(d->tex);
#endif
}

static inline unsigned display_get_width(display *d) {
	return d->width;
}

static inline unsigned display_get_height(display *d) {
	return d->height;
}

static inline bmp4 *display_get_screenshot(display *d) {
	bmp4 *r = bmp4_new(d->width, d->height);
	if(!r) return 0;
	unsigned pitch, *out = r->data, *in;
	void *pixels;
	display_get_vram_and_pitch(d, &pixels, &pitch);
	in = pixels;
	size_t i, l = (size_t)d->width*(size_t)d->height;
	for(i=0;i<l;i++) *(out++)=argb_to_rgba(*(in++));
	display_release_vram(d);
	return r;
}

struct spritesheet {
	unsigned sprite_count, sprites_per_row;
	unsigned sprite_w, sprite_h;
	bmp4 *bitmap;
};

static inline int spritesheet_init(struct spritesheet *result, bmp4 *b, unsigned sprite_w, unsigned sprite_h) {
	result->bitmap = b;
	result->sprite_w = sprite_w;
	result->sprite_h = sprite_h;
	result->sprite_count = (b->height * b->width) / (sprite_h * sprite_w);
	result->sprites_per_row = b->width / sprite_w;
	if((b->height * b->width) % (sprite_h * sprite_w)) return 0; /* picture dimensions are no multiple of the sprite dimensions*/
	return 1;
}

static inline unsigned spritesheet_getspritestart(struct spritesheet *ss, unsigned sprite_nr, unsigned row_nr) {
	unsigned sprite_row = sprite_nr / ss->sprites_per_row;
	unsigned row_off = (row_nr * ss->sprite_w * ss->sprites_per_row);
	unsigned res = (sprite_row * ss->sprite_w * ss->sprite_h * ss->sprites_per_row) + row_off
		       + ((sprite_nr % ss->sprites_per_row) * ss->sprite_w);
	return res;
}

static inline void display_draw_sprite(display *d, struct spritesheet* ss, unsigned sprite_no, unsigned sx, unsigned sy, unsigned scale) {
	if(!scale) scale = 1;
	if(!(d->width >= sx+ss->sprite_w*scale && d->height >= sy+ss->sprite_h*scale))
		return;
	unsigned x,xx,y,yd,ys,pitch;
	unsigned xscale,yscale;
	unsigned transp_col = ss->bitmap->data[0];
	unsigned sprite_pitch = ss->sprites_per_row * ss->sprite_w;
	void *pixels;
	display_get_vram_and_pitch(d, &pixels, &pitch);
	pitch /= sizeof(unsigned);

	for(y=0,yd=sy*pitch, ys=spritesheet_getspritestart(ss, sprite_no, y); y < ss->sprite_h; y++,ys+=sprite_pitch)
		for(yscale=0; yscale<scale; yscale++,yd+=pitch)
			for(x=0, xx=sx; x < ss->sprite_w; x++)
				for(xscale = 0; xscale < scale; xscale++,xx++)
					if(ss->bitmap->data[ys+x] != transp_col)
					((unsigned*)pixels)[yd + xx] = rgba_to_argb(ss->bitmap->data[ys+x]);
	display_release_vram(d);
}

static inline void display_draw(display *d, bmp4* b, unsigned sx, unsigned sy, unsigned scale) {
	if(!scale) scale = 1;
	assert(d->width >= sx+b->width*scale && d->height >= sy+b->height*scale);
	unsigned x,xx,y,yd,ys,pitch;
	unsigned xscale,yscale;
	void* pixels;
	display_get_vram_and_pitch(d, &pixels, &pitch);
	pitch /= sizeof(unsigned);
	for(y=0,yd=sy*pitch, ys=0; y < b->height; y++,ys+=b->width)
		for(yscale=0; yscale<scale; yscale++,yd+=pitch)
			for(x=0, xx=sx; x < b->width; x++)
				for(xscale = 0; xscale < scale; xscale++,xx++)
					((unsigned*)pixels)[yd + xx] = rgba_to_argb(b->data[ys+x]);
	display_release_vram(d);
	//SDL_UpdateRect(d->surface, sx, sy, b->width*scale, b->height*scale);
}

static inline void display_draw_vline(display *d, unsigned sx, unsigned sy, unsigned height, unsigned color, unsigned scale) {
	void *pixels;
	if(!scale) scale = 1;
	assert(d->width >= sx && d->height >= sy+height*scale);
	unsigned y,yd,yscale,xscale,pitch;
	display_get_vram_and_pitch(d, &pixels, &pitch);
	pitch /= sizeof(unsigned);
	for(y = sy, yd=sy*pitch; y < sy+height; y++) for(yscale=0;yscale<scale;yscale++,yd+=pitch)
	for(xscale=0; xscale<scale; xscale++)
		((unsigned*)pixels)[yd + sx + xscale] = rgba_to_argb(color);
	display_release_vram(d);
}

static inline void display_draw_hline(display *d, unsigned sx, unsigned sy, unsigned width, unsigned color, unsigned scale) {
	void *pixels;
	if(!scale) scale = 1;
	assert(d->width >= sx+width*scale && d->height >= sy);
	unsigned x,yd,yscale,pitch;
	display_get_vram_and_pitch(d, &pixels, &pitch);
	pitch /= sizeof(unsigned);
	for(yscale = 0, yd=sy*pitch; yscale < scale; yscale++,yd+=pitch)
	for(x=sx;x<sx+width*scale;x++)
		((unsigned*)pixels)[yd + x] = rgba_to_argb(color);
	display_release_vram(d);
}

static inline void display_fill_rect(display *d, unsigned sx, unsigned sy, unsigned width, unsigned height, unsigned color, unsigned scale) {
	void *pixels;
	if(!scale) scale = 1;
	assert(d->width >= sx+width*scale && d->height >= sy+height*scale);
	unsigned x,xx,y,yd,pitch;
	unsigned xscale,yscale;
	display_get_vram_and_pitch(d, &pixels, &pitch);
	pitch /= sizeof(unsigned);
	for(y=0,yd=sy*pitch; y < height; y++)
		for(yscale=0; yscale<scale; yscale++,yd+=pitch)
			for(x=0, xx=sx; x < width; x++)
				for(xscale = 0; xscale < scale; xscale++,xx++)
					((unsigned*)pixels)[yd + xx] = rgba_to_argb(color);
	display_release_vram(d);
}

static inline void display_update_region(display *d, unsigned x, unsigned y, unsigned w, unsigned h)
{
#ifdef USE_SDL2
	SDL_Rect sarea = {.x = x, .y = y, .w = w, .h = h};
	SDL_Rect darea = {
		.x = SCALEUP(x, d->hwscale),
		.y = SCALEUP(y, d->hwscale),
		.w = SCALEUP(w, d->hwscale),
		.h = SCALEUP(h, d->hwscale)};
	SDL_RenderCopy(d->ren, d->tex, &sarea, &darea);
	SDL_RenderPresent(d->ren);
#else
	SDL_UpdateRect(d->surface, x, y, w, h);
#endif
}

static inline void display_refresh(display *d) {
	display_update_region(d, 0, 0, d->width, d->height);
}

static inline void display_clear(display *d) {
	void *pixels;
	unsigned *ptr;
	unsigned pitch;
	display_get_vram_and_pitch(d, &pixels, &pitch);
	ptr = pixels;
	pitch /= sizeof(unsigned);
	unsigned x, y;
	for(y = 0; y < d->height; y++) for (x = 0; x < d->width; x++)
		ptr[y*pitch + x] = 0;
	display_release_vram(d);
}

static inline void display_toggle_fullscreen_i(display *d, int update) {
#ifdef USE_SDL2
	SDL_SetWindowFullscreen(d->win, d->fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
	d->fs = !d->fs;
#else
	d->fs = !d->fs;
	SDL_WM_ToggleFullScreen(d->surface);
	if(!update) return;
	SDL_Delay(1);
	//display_clear(d);
	SDL_UpdateRect(d->surface,0,0,d->width,d->height);
	SDL_Delay(1);
#endif
}

static inline void display_toggle_fullscreen(display *d) {
	display_toggle_fullscreen_i(d, 1);
}


#if 0
static inline void display_hline(display *d, unsigned color, unsigned x, unsigned y, unsigned len, unsigned scale) {
	unsigned y, x;
}
#endif

enum eventtypes {
	EV_NONE = 0,
	EV_KEYDOWN,
	EV_KEYUP,
	EV_MOUSEMOVE,
	EV_MOUSEDOWN,
	EV_MOUSEUP,
	EV_MOUSEWHEEL,
	EV_JOYMOVE,
	EV_JOYDOWN,
	EV_JOYUP,
	EV_QUIT,
	EV_RESIZE,
	EV_NEEDREDRAW,
	EV_HANDLED,
	EV_MAX
};

enum cbtypes {
	CB_GAMETICK = 0,
	CB_KEYDOWN,
	CB_KEYUP,
	CB_MOUSEMOVE,
	CB_MOUSEDOWN,
	CB_MOUSEUP,
	CB_MOUSEWHEEL,
	CB_JOYMOVE,
	CB_JOYDOWN,
	CB_JOYUP,
	CB_TIMER,
	CB_RESIZE,
	CB_MAX
};

typedef struct event {
	int which, mod;
	int xval, yval;
} ezevent;

typedef int(*eventcallbackfunc)(void*data, struct event* ev);
struct callback {
	void* data;
	eventcallbackfunc cb;
};

typedef struct inp {
	struct callback callbacks[CB_MAX];
	struct { int x, y; } mouse;
} inp;

static inline void inp_setcb(struct inp *in, enum cbtypes type, eventcallbackfunc cb, void* data) {
	in->callbacks[type].data = data;
	in->callbacks[type].cb = cb;
}

typedef void (*event_process_func)(struct inp* inp, SDL_Event* sdl_event, struct event *myevent);
static void event_process_mousemove(struct inp* inp, SDL_Event* sdl_event, struct event *myevent) {
	myevent->xval = sdl_event->motion.x;
	myevent->yval = sdl_event->motion.y;
	inp->mouse.x = sdl_event->motion.x;
	inp->mouse.y = sdl_event->motion.y;
}

static void event_process_mousedown(struct inp* inp, SDL_Event* sdl_event, struct event *myevent) {
	myevent->xval = sdl_event->button.x;
	myevent->yval = sdl_event->button.y;
	myevent->which = sdl_event->button.button;
	inp->mouse.x = sdl_event->motion.x;
	inp->mouse.y = sdl_event->motion.y;
}

static void event_process_mousewheel(struct inp* inp, SDL_Event* sdl_event, struct event *myevent) {
#ifdef USE_SDL2
	myevent->yval = sdl_event->wheel.y * -1;
#else
	if(sdl_event->button.button == SDL_BUTTON_WHEELDOWN)
		myevent->yval = 1;
	else
		myevent->yval = -1;
#endif
}

static void event_process_keydown(struct inp* inp, SDL_Event* sdl_event, struct event *myevent) {
	myevent->which = sdl_event->key.keysym.sym;
	myevent->mod = sdl_event->key.keysym.mod;
}

static void event_process_resize(struct inp* inp, SDL_Event* sdl_event, struct event *myevent) {
#ifdef USE_SDL2
	myevent->xval = sdl_event->window.data1;
	myevent->yval = sdl_event->window.data2;
#else
	myevent->xval = sdl_event->resize.w;
	myevent->yval = sdl_event->resize.h;
#endif
}

static long long ezsdl_getutime64(void) {
        struct timeval t;
        gettimeofday(&t, NULL);
        return (t.tv_sec * 1000LL * 1000LL) + t.tv_usec;
}

static struct ezsdl {
	struct display disp;
	struct inp inp;
} ezsdl;

static inline void ezsdl_init(unsigned width, unsigned height, unsigned hwscale, int flags) {
	display_init(&ezsdl.disp, width, height, hwscale, flags);
}

static inline void ezsdl_shutdown(void) {
	display_shutdown(&ezsdl.disp);
}

static inline void ezsdl_set_resize_method(enum resize_method rm) {
	display_set_resize_method(&ezsdl.disp, rm);
}

static inline void ezsdl_set_title(const char* text) {
#ifdef USE_SDL2
	SDL_SetWindowTitle(ezsdl.disp.win, text);
#else
	SDL_WM_SetCaption(text, 0);
#endif
}

static inline void ezsdl_draw(bmp4*b, unsigned x, unsigned y, int scale) {
	display_draw(&ezsdl.disp, b, x, y, scale);
}

static inline void ezsdl_draw_vline(unsigned sx, unsigned sy, unsigned height, unsigned color, unsigned scale) {
	display_draw_vline(&ezsdl.disp, sx, sy, height, color, scale);
}

static inline void ezsdl_draw_hline(unsigned sx, unsigned sy, unsigned width, unsigned color, unsigned scale) {
	display_draw_hline(&ezsdl.disp, sx, sy, width, color, scale);
}

static inline void ezsdl_fill_rect(unsigned sx, unsigned sy, unsigned width, unsigned height, unsigned color, unsigned scale) {
	display_fill_rect(&ezsdl.disp, sx, sy, width, height, color, scale);
}

static inline void ezsdl_draw_sprite(struct spritesheet* ss, unsigned sprite_no, unsigned x, unsigned y, unsigned scale) {
	display_draw_sprite(&ezsdl.disp, ss, sprite_no, x, y, scale);
}

static inline bmp4* ezsdl_get_screenshot(void) {
	return display_get_screenshot(&ezsdl.disp);
}

static inline void ezsdl_clear(void) {
	display_clear(&ezsdl.disp);
}

static inline void ezsdl_toggle_fullscreen(void) {
	display_toggle_fullscreen(&ezsdl.disp);
}

static inline void ezsdl_refresh(void) {
	display_refresh(&ezsdl.disp);
}

static inline void ezsdl_update_region(unsigned x, unsigned y, unsigned w, unsigned h) {
	display_update_region(&ezsdl.disp, x, y, w, h);
}

static inline void ezsdl_setcb(enum cbtypes type, eventcallbackfunc cb, void* data) {
	inp_setcb(&ezsdl.inp, type, cb, data);
}

static inline void ezsdl_sleep(unsigned ms) {
	SDL_Delay(ms);
}

static inline void ezsdl_get_vram_and_pitch(void **pixels, unsigned *pitch) {
        display_get_vram_and_pitch(&ezsdl.disp, pixels, pitch);
}

static inline void ezsdl_release_vram(void) {
	display_release_vram(&ezsdl.disp);
}

static inline unsigned ezsdl_get_width(void) {
	return display_get_width(&ezsdl.disp);
}

static inline unsigned ezsdl_get_height(void) {
	return display_get_height(&ezsdl.disp);
}


/* return event without blocking, call callbacks if so defined. */
static inline enum eventtypes ezsdl_getevent(struct event *myevent) {
	static const event_process_func event_processors[CB_MAX] = {
		[CB_MOUSEMOVE] = event_process_mousemove,
		[CB_MOUSEDOWN] = event_process_mousedown,
		[CB_MOUSEWHEEL] = event_process_mousewheel,
		[CB_MOUSEUP] = event_process_mousedown,
		[CB_KEYDOWN] = event_process_keydown,
		[CB_KEYUP] = event_process_keydown,
		[CB_RESIZE] = event_process_resize,
	};
	struct inp *in = &ezsdl.inp;
	SDL_Event sdl_event;
	enum eventtypes e = EV_NONE;
	if (SDL_PollEvent(&sdl_event)) {
		enum cbtypes t = CB_MAX;
		switch (sdl_event.type) {
			case SDL_MOUSEMOTION:
				if(sdl_event.motion.x < 0) sdl_event.motion.x = 0;
				if(sdl_event.motion.y < 0) sdl_event.motion.y = 0;
				sdl_event.motion.x = SCALEDOWN(sdl_event.motion.x, ezsdl.disp.hwscale);
				sdl_event.motion.y = SCALEDOWN(sdl_event.motion.y, ezsdl.disp.hwscale);
				e = EV_MOUSEMOVE;
				t = CB_MOUSEMOVE;
				break;
#ifdef USE_SDL2
			case SDL_MOUSEWHEEL:
				e = EV_MOUSEWHEEL;
				t = CB_MOUSEWHEEL;
				break;
#endif
			case SDL_MOUSEBUTTONDOWN:
#ifndef USE_SDL2
				if(sdl_event.button.button == SDL_BUTTON_WHEELDOWN ||
				   sdl_event.button.button == SDL_BUTTON_WHEELUP) {
					e = EV_MOUSEWHEEL;
					t = CB_MOUSEWHEEL;
				} else
#endif
				{
					sdl_event.button.x = SCALEDOWN(sdl_event.button.x, ezsdl.disp.hwscale);
					sdl_event.button.y = SCALEDOWN(sdl_event.button.y, ezsdl.disp.hwscale);
					e = EV_MOUSEDOWN;
					t = CB_MOUSEDOWN;
				}
				break;
			case SDL_MOUSEBUTTONUP:
#ifndef USE_SDL2
				if(sdl_event.button.button == SDL_BUTTON_WHEELDOWN ||
				   sdl_event.button.button == SDL_BUTTON_WHEELUP) {
					e = EV_MOUSEWHEEL;
					t = CB_MOUSEWHEEL;
				} else
#endif
				{
					sdl_event.button.x = SCALEDOWN(sdl_event.button.x, ezsdl.disp.hwscale);
					sdl_event.button.y = SCALEDOWN(sdl_event.button.y, ezsdl.disp.hwscale);
					e = EV_MOUSEUP;
					t = CB_MOUSEUP;
				}
				break;
			case SDL_QUIT:
				return EV_QUIT;
			case SDL_KEYDOWN:
				if (sdl_event.key.keysym.sym == SDLK_RETURN &&
				   (sdl_event.key.keysym.mod & KMOD_LALT) ||
				   (sdl_event.key.keysym.mod & KMOD_RALT)) {
					ezsdl_toggle_fullscreen();
					SDL_Delay(1);
					e = EV_NEEDREDRAW;
				} else {
					e = EV_KEYDOWN;
					t = CB_KEYDOWN;
				}
				break;
			case SDL_KEYUP:
				e = EV_KEYUP;
				t = CB_KEYUP;
				break;
#ifndef USE_SDL2
			case SDL_VIDEORESIZE:
				// TODO : SDL1 hw scaling
				display_init(&ezsdl.disp, sdl_event.resize.w, sdl_event.resize.w, 100, ezsdl.disp.flags);
				e = EV_RESIZE;
				t = CB_RESIZE;
				break;
#else
			case SDL_WINDOWEVENT:
				if(sdl_event.window.event == SDL_WINDOWEVENT_RESIZED) {
					int s;
					if(ezsdl.disp.rm == RM_WINDOW) {
						s = ezsdl.disp.hwscale;
						sdl_event.window.data1 = SCALEDOWN(sdl_event.window.data1, s);
						sdl_event.window.data2 = SCALEDOWN(sdl_event.window.data2, s);
					} else {
						s = NEWSCALE(ezsdl.disp.width, sdl_event.window.data1, ezsdl.disp.height, sdl_event.window.data2);
						sdl_event.window.data1 = ezsdl.disp.width;
						sdl_event.window.data2 = ezsdl.disp.height;
					}
					display_init(&ezsdl.disp, sdl_event.window.data1, sdl_event.window.data2, s, ezsdl.disp.flags);
					e = EV_RESIZE;
					t = CB_RESIZE;
				} else if (sdl_event.window.event == SDL_WINDOWEVENT_EXPOSED) {
					e = EV_NEEDREDRAW;
				}
				break;
#endif
		}
		if(t != CB_MAX) {
			if(event_processors[t]) event_processors[t](in, &sdl_event, myevent);
			if(in->callbacks[t].cb)
				if(in->callbacks[t].cb(in->callbacks[t].data, myevent))
					e = EV_NEEDREDRAW;
				else
					e = EV_HANDLED;
		}
	}
	return e;
}

static inline void ezsdl_start(void) {
	struct event myevent;
	while(1) {
		unsigned need_redraw = 0;
		unsigned ms_passed = 0;
		do {
			enum eventtypes e;
			while((e = ezsdl_getevent(&myevent)) != EV_NONE)
				if(e == EV_QUIT) return;
				else if(e == EV_NEEDREDRAW) need_redraw = 1;
			SDL_Delay(1);
		} while (++ms_passed < 20);

		long long tm = ezsdl_getutime64();
		myevent.which = need_redraw;
		struct inp* in = &ezsdl.inp;
		if(in->callbacks[CB_GAMETICK].cb)
			(void) in->callbacks[CB_GAMETICK].cb(in->callbacks[CB_GAMETICK].data, &myevent);

		ms_passed = (ezsdl_getutime64() - tm)/1000;
	}
}

#endif

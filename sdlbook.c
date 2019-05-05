/*
 * Copyright (C) 2019 rofl0r. see COPYING for LICENSE details.
 */

#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/time.h>
#include <libdjvu/ddjvuapi.h>
#include "ezsdl.h"
#include "topaz.h"

#pragma RcB2 LINK "-ldjvulibre" "-lSSL"

static struct config_data {
	int w, h;
	int scale;
} config_data;

static ddjvu_context_t *ctx;
static ddjvu_document_t *doc;
static const char* filename;
static int page_count, curr_page;
static bmp4* bmp_font;
static struct spritesheet ss_font;
static unsigned long tickcounter;
static int scroll_line;
static const int fps = 64;
static ddjvu_rect_t page_dims;
static unsigned *image_data;

static void update_title(void) {
	char buf[64];
	snprintf(buf, sizeof buf, "SDLBook [%d/%d] %s", curr_page, page_count, filename);
	ezsdl_set_title(buf);
}


FILE* cfg_open(const char *fn, const char* mode) { return fopen(fn, mode ? mode : "r"); }

#define cfg_close(F) do { fclose(F); F = 0; } while(0)

char* cfg_getstr(FILE *f, const char *key, char*  buf, size_t bufsize) {
	fseek(f, 0, SEEK_SET);
	size_t l = strlen(key);
	while(fgets(buf, bufsize, f)) {
		if(!strncmp(buf, key, l) && buf[l] == '=') {
			size_t x = l;
			while(buf[++x] && buf[x] != '\n');
			buf[x] = 0;
			memmove(buf, buf + l + 1, x - l);
			return buf;
		}
	}
	*buf = 0;
	return 0;
}

int cfg_getint(FILE *f, const char *key) {
	char buf[64];
	char *res = cfg_getstr(f, key, buf, sizeof buf);
	if(res) return atoi(res);
	return 0;
}

static void read_write_config(int doread) {
	FILE *config;
	char fbuf[512];
	snprintf(fbuf, sizeof fbuf, "%s/.sdlbook.cfg", getenv("HOME"));

	if(doread)
		config = cfg_open(fbuf, "r");
	else
		config = cfg_open(fbuf, "w");

	if(config) {
		if(doread) {
			config_data.w = cfg_getint(config, "w");
			config_data.h = cfg_getint(config, "h");
			config_data.scale = cfg_getint(config, "scale");
		} else {
			fprintf(config, "w=%d\nh=%d\nscale=%d\n",
				ezsdl_get_width(),
				ezsdl_get_height(),
				config_data.scale);
		}
		cfg_close(config);
	}
	if(doread) {
		if(!config_data.w) config_data.w = 640;
		if(!config_data.h) config_data.h = 480;
		if(!config_data.scale) config_data.scale = 100;
	}
}

static void handle(int);
static int cleanup(void);

static void die(const char *fmt, ...)
{
	handle(FALSE);
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr,"\n");
	exit(cleanup());
}

#define FONT_W 8
#define FONT_H 8
static void init_gfx() {
	bmp_font       = bmp4_new(128, 128);
	memcpy(bmp_font->data, topaz_font+8, 128*128*4);
	if(!spritesheet_init(&ss_font, bmp_font, FONT_W, FONT_H)) dprintf(2, "oops\n");
}

static int get_font_width(char letter) {
	return ss_font.sprite_w;
}

static unsigned get_font_render_length(const char* text) {
	unsigned x=0;
	for(;*text && *text != '\n';x+=get_font_width(*(text++)));
	return x;
}

static void draw_font(const char* text, struct spritesheet *font, unsigned x, unsigned y, unsigned scale) {
	for(;*text && *text != '\n';x+=scale*get_font_width(*(text++)))
		ezsdl_draw_sprite(font, *text, x, y, scale);
}

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

static inline unsigned get_image_pixel(int x, int y) {
	return image_data[y * page_dims.w + x];
}

static void draw() {
	int x, y;
	unsigned *ptr = (void *) ezsdl_get_vram();
	unsigned pitch = ezsdl_get_pitch()/4;
	int ymax = MIN(ezsdl_get_height(), page_dims.h*2-scroll_line),
	    xmax = MIN(ezsdl_get_width(), page_dims.w);
	for(y = 0; y < ymax; y++)
		for (x = 0; x < xmax; x++)
			ptr[y*pitch + x] = get_image_pixel(x, y+scroll_line); //SRGB_BLACK;
}

static int game_tick(int need_redraw) {
	long long ms_used = 0;
	if(need_redraw) {
		long long tstamp = ezsdl_getutime64();
		draw();
		ezsdl_refresh();
		ms_used = ezsdl_getutime64() - tstamp;
	}
	long sleepms = 1000/fps - ms_used;
	if(sleepms >= 0) ezsdl_sleep(sleepms);

	tickcounter++;
	return 0;
}

static unsigned* convert_rgb24_to_rgba(char* image, int w, int h, unsigned* new) {
	bmp3 in = {
		.width = w,
		.height = h,
		.data = image,
	};
	bmp4 out = {
		.width = w,
		.height = h,
		.data = new,
	};
	bmp3_to_bmp4(&in, &out);
	return new;
}

static char* render_page(ddjvu_page_t *page, int pageno, ddjvu_rect_t *res_rect, ddjvu_rect_t *desired_rect)
{
	ddjvu_rect_t prect; // pixels of image
	ddjvu_rect_t rrect; // pixels of segment (info_segment)
	ddjvu_format_style_t style;
	ddjvu_render_mode_t mode;
	ddjvu_format_t *fmt;
	int iw = ddjvu_page_get_width(page);
	int ih = ddjvu_page_get_height(page);
	int dpi = ddjvu_page_get_resolution(page);
	ddjvu_page_type_t type = ddjvu_page_get_type(page);
	char *image = 0;
	char white = 0xFF;
	int rowsize;
	int enforce_aspect_ratio = 1;

	prect.x = 0;
	prect.y = 0;
	if (desired_rect) {
		prect.w = desired_rect->w;
		prect.h = desired_rect->h;
		enforce_aspect_ratio = 0;
	} else if (config_data.scale > 0) {
		prect.w = (unsigned int) (iw * (double)config_data.scale) / dpi;
		prect.h = (unsigned int) (ih * (double)config_data.scale) / dpi;
	} else {
		prect.w = (iw * 100) / dpi;
		prect.h = (ih * 100) / dpi;
	}
	if (enforce_aspect_ratio) {
		double dw = (double)iw / prect.w;
		double dh = (double)ih / prect.h;
		if (dw > dh)
			prect.h = (int)(ih / dw);
		else
			prect.w = (int)(iw / dh);
	}

	rrect = prect;
#if 0
	// show only section/segment of rendered image
	if (segment_given > 0) {
		rrect = segment_rect;
		if (rrect.x < 0)
			rrect.x = prect.w - rrect.w + rrect.x;
		if (rrect.y < 0)
			rrect.y = prect.h - rrect.h + rrect.y;
	}
#endif

	mode = DDJVU_RENDER_COLOR;
	style = DDJVU_FORMAT_RGB24;

	if (!(fmt = ddjvu_format_create(style, 0, 0)))
		die("Cannot determine pixel style for page %d", pageno);

	ddjvu_format_set_row_order(fmt, 1);

	if (style == DDJVU_FORMAT_MSBTOLSB) {
		rowsize = (rrect.w + 7) / 8;
		white = 0;
	} else if (style == DDJVU_FORMAT_GREY8)
		rowsize = rrect.w;
	else
		rowsize = rrect.w * 3;
	if(!(image = malloc(rowsize * rrect.h)))
		die("Cannot allocate image buffer for page %d", pageno);

	/* fill image with white in case rendering fails */
	if(!ddjvu_page_render(page, mode, &prect, &rrect, fmt, rowsize, image))
		memset(image, white, rowsize * rrect.h);

	ddjvu_format_release(fmt);
	*res_rect = rrect;
	return image;
}

static void* prep_page(int pageno, ddjvu_rect_t *res_rect, ddjvu_rect_t *desired_rect)
{
	if(pageno >= page_count) return 0;
	ddjvu_page_t *page;
	if (!(page = ddjvu_page_create_by_pageno(doc, pageno)))
		die("Can't access page %d.", pageno);
	while (! ddjvu_page_decoding_done(page))
		handle(TRUE);
	if (ddjvu_page_decoding_error(page)) {
		handle(FALSE);
		die("Can't decode page %d", pageno);
	}
	void *image = render_page(page, pageno, res_rect, desired_rect);
	ddjvu_page_release(page);
	return image;
}

static void* prep_pages() {
	ddjvu_rect_t p1rect, p2rect;
	char *p1data = prep_page(curr_page, &p1rect, 0);
	char *p2data = prep_page(curr_page+1, &p2rect, 0);
	if(!p2data) {
		/* probably last page hit */
		p2data = calloc(3,p1rect.w*p1rect.h);
		p2rect = p1rect;
	}
	if(p1rect.w != p2rect.w || p1rect.h != p2rect.h) {
		/* sometimes the start page of a book has a different format */
		free(p1data);
		p1data = prep_page(curr_page, &p1rect, &p2rect);
	}
	if(!(p1data && p2data)) return NULL;
	assert(p1rect.w == p2rect.w && p1rect.h == p2rect.h);

	size_t one_pic = p1rect.w*p1rect.h;
	unsigned* imgbuf = malloc(4 * one_pic * 2);

	convert_rgb24_to_rgba(p1data, p1rect.w, p1rect.h, imgbuf);
	convert_rgb24_to_rgba(p2data, p2rect.w, p2rect.h, imgbuf+one_pic);
	page_dims = p1rect;
	free(p1data);
	free(p2data);
	return imgbuf;
}

static void handle(int wait) {
	const ddjvu_message_t *msg;
	if (!ctx)
		return;
	if (wait)
		msg = ddjvu_message_wait(ctx);
	while ((msg = ddjvu_message_peek(ctx)))	{
		switch(msg->m_any.tag) {
		case DDJVU_ERROR:
			fprintf(stderr,"ddjvu: %s\n", msg->m_error.message);
			if (msg->m_error.filename)
				fprintf(stderr,"ddjvu: '%s:%d'\n",
				        msg->m_error.filename, msg->m_error.lineno);
		default:
			break;
		}
		ddjvu_message_pop(ctx);
	}
}

static void swap_image(void *new) {
	void *old = image_data;
	image_data = new;
	free(old);
}

static int set_page(int no) {
	if(no >= page_count) no = page_count-1;
	if(no < 0) curr_page = 0;
	else curr_page = no;
	swap_image(prep_pages());
	update_title();
	return 1;
}

static int change_page(int incr) {
	if(curr_page + incr >= 0 && curr_page + incr < page_count)
		curr_page += incr;
	swap_image(prep_pages());
	update_title();
	return 1;
}

static int change_scale(int incr) {
	if (config_data.scale + incr <= 999 && config_data.scale + incr >= 0)
		config_data.scale += incr;
	swap_image(prep_pages());
	return 1;
}

static int change_scroll(int incr) {
	if(scroll_line + incr < 0) {
		if(curr_page == 0) scroll_line = 0;
		else {
			change_page(-1);
			scroll_line = scroll_line + incr + (int)page_dims.h;
		}
	} else if(scroll_line + incr > page_dims.h) {
		scroll_line = scroll_line + incr - (int)page_dims.h;
		change_page(+1);
	} else
		scroll_line += incr;
	return 1;
}

#define HELP_TEXT \
	"HELP SCREEN - TYPE ENTER TO EXIT\n" \
	"UP, DOWN ARROW - SCROLL 32 PIX\n" \
	"CTRL + UP, DOWN ARROW - SCROLL 96 PIX\n" \
	"PAGE_UP/DOWN - SCROLL ONE PAGE\n" \
	"KEYPAD +/- OR CTRL-WHEEL - ZOOM\n" \
	"G - ENTER PAGE NUMBER\n" \
	"Q - QUIT\n" \
	"F1 - SHOW HELP SCREEN\n"

static int get_return_count(const char* text) {
	int count = 0;
	while(*text) if(*(text++) == '\n') count++;
	return count;
}

static void draw_font_lines(const char* text, struct spritesheet *font,
				unsigned x, unsigned y, unsigned scale)
{
	const char *p = text;
	unsigned yy = y;
	do {
		draw_font(p, font, x, yy, scale);
		yy += 10*2;
	} while((p = strchr(p, '\n')), p++);
}

static void input_loop(const char* title, char *result) {
	int ret_count = get_return_count(title);
	if(!ret_count) ret_count = 1;
	int desired_height = (ret_count+2) * 10 * 2;
	ezsdl_fill_rect(0,0, ezsdl_get_width(), MIN(desired_height, ezsdl_get_height()), RGB(0xff,0x00,0x00), 1);
	draw_font_lines(title, &ss_font, 8, 8, 2);
	ezsdl_update_region(0, 0, ezsdl_get_width(), MIN(desired_height, ezsdl_get_height()));
	char* p = result;
	*p = 0;
	struct event event;
	while(1) {
		enum eventtypes e;
		while((e = ezsdl_getevent(&event)) == EV_NONE) ezsdl_sleep(1);
		switch(e) {
		case EV_QUIT:
		case EV_KEYUP:
			switch(event.which) {
			case SDLK_BACKSPACE:
				if(p > result) p--;
				*p = 0;
				goto drawit;
			case SDLK_RETURN: case SDLK_ESCAPE:
				*p = 0;
				ezsdl_clear();
				return;
			default:
				*(p++) = event.which;
				*p = 0;
			drawit:
				ezsdl_fill_rect(8, desired_height - 10*2, ezsdl_get_width() -8, MIN(desired_height, ezsdl_get_height()), RGB(0xff,0x00,0x00), 1);
				draw_font(result, &ss_font, 8, desired_height - 10*2, 2);
				ezsdl_update_region(0, 0, ezsdl_get_width(), MIN(desired_height, ezsdl_get_height()));
				break;
			}
			break;
		default: break;
		}
	}
}

static int cleanup(void) {
	if(doc)
		ddjvu_document_release(doc);
	if(ctx)
		ddjvu_context_release(ctx);

	read_write_config(0);

	ezsdl_shutdown();

	return 1;
}

int main(int argc, char **argv) {
	if(argc != 2)
		die("need djvu filename as argv[1]");

	filename = argv[1];

	if(!(ctx = ddjvu_context_create(argv[0])))
		die("can't create djvu context");

	if(!(doc = ddjvu_document_create_by_filename(ctx, filename, TRUE)))
		die("can't open djvu document '%s'", filename);

	if(strrchr(filename, '/')) filename = strrchr(filename, '/')+1;

	while (! ddjvu_document_decoding_done(doc))
		handle(TRUE);

	if (ddjvu_document_decoding_error(doc))
		die("can't decode document");

	page_count = ddjvu_document_get_pagenum(doc);
	curr_page = 0;

	config_data.scale = 100;

	read_write_config(1);

	ezsdl_init(config_data.w, config_data.h, SDL_HWPALETTE | SDL_RESIZABLE);

	image_data = prep_pages();

	init_gfx();

	SDL_ShowCursor(1);
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
	struct event event;

	update_title();

	game_tick(1);

	unsigned left_ctrl_pressed = 0;
	unsigned right_ctrl_pressed = 0;

	while(1) {
		unsigned need_redraw = 0;
		enum eventtypes e;
		while((e = ezsdl_getevent(&event)) != EV_NONE) {
			need_redraw = 0;
			switch (e) {
				case EV_MOUSEWHEEL:
					if(left_ctrl_pressed || right_ctrl_pressed)
						need_redraw = change_scale(event.yval*10);
					else
						need_redraw = change_scroll(event.yval*64);
					break;
				case EV_NEEDREDRAW: case EV_RESIZE:
					need_redraw = 1;
					break;
				case EV_QUIT:
					goto dun_goofed;
				case EV_KEYDOWN:
					switch(event.which) {
						case SDLK_LCTRL:
							left_ctrl_pressed = 1;
							break;
						case SDLK_RCTRL:
							right_ctrl_pressed = 1;
							break;
						case SDLK_q:
							goto dun_goofed;
						case SDLK_KP_PLUS:
							need_redraw = change_scale(+10);
							break;
						case SDLK_KP_MINUS:
							need_redraw = change_scale(-10);
							break;
						case SDLK_PAGEDOWN:
							need_redraw = change_scroll(+page_dims.h);
							break;
						case SDLK_PAGEUP:
							need_redraw = change_scroll(-page_dims.h);
							break;
						case SDLK_UP:
							if((event.mod & KMOD_LCTRL) || (event.mod & KMOD_RCTRL))
								need_redraw = change_scroll(-96);
							else
								need_redraw = change_scroll(-32);
							break;
						case SDLK_DOWN:
							if((event.mod & KMOD_LCTRL) || (event.mod & KMOD_RCTRL))
								need_redraw = change_scroll(+96);
							else
								need_redraw = change_scroll(+32);
							break;
						case SDLK_RETURN:
							if((event.mod & KMOD_LALT) ||
							   (event.mod & KMOD_RALT)) {
								ezsdl_toggle_fullscreen();
								ezsdl_sleep(1);
								game_tick(1);
								need_redraw = 1;
							}
							break;
						default:
							break;
					}
					break;
				case EV_KEYUP:
					switch(event.which) {
						case SDLK_LCTRL:
							left_ctrl_pressed = 0;
							break;
						case SDLK_RCTRL:
							right_ctrl_pressed = 0;
							break;
						case SDLK_F1:
							{
								char buf[32];
								buf[0] = 0;
								input_loop(HELP_TEXT, buf);
								need_redraw = 1;
							}
							break;

						case SDLK_g:
							{
								char buf[32];
								buf[0] = 0;
								input_loop("enter page no", buf);
								set_page(atoi(buf));
								need_redraw = 1;
							}
							break;
						case SDLK_c:
							ezsdl_clear();
							ezsdl_refresh();
							need_redraw = 1;
							break;
						case SDLK_ESCAPE:
							goto dun_goofed;
						default:
							break;
					}
				default:
					break;
			}
		}
		if(game_tick(need_redraw)) {
		}
	}

dun_goofed:

	cleanup();
	return 0;
}

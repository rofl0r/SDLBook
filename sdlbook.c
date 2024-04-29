/*
 * Copyright (C) 2019-202 rofl0r. see COPYING for LICENSE details.
 */

#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <assert.h>
#include <sys/time.h>
#include <libdjvu/ddjvuapi.h>
#include <mupdf/fitz.h>
#include "ezsdl.h"
#include "topaz.h"

#pragma RcB2 LINK "-ldjvulibre" "-lSSL" "-lmupdf"

static struct config_data {
	int w, h;
	int scale;
} config_data;

enum be_type {
	BE_DJVU = 0,
	BE_MUPDF,
};
static struct doc {
	enum be_type be;
	union {
		struct djvu_doc {
			ddjvu_context_t *ctx;
			ddjvu_document_t *doc;
		} ddoc;
		struct pdf_doc {
			fz_context *ctx;
			fz_document *doc;
		} pdoc;
	} u;
} doc;

#define DDOC doc.u.ddoc
#define PDOC doc.u.pdoc
#define IS_DJVU (doc.be == BE_DJVU)

static const char* filename;
static int page_count, curr_page;
static bmp4* bmp_font;
static struct spritesheet ss_font;
static unsigned long tickcounter;
static int scroll_line_v;
static int scroll_line_h;
static const int fps = 64;
static ddjvu_rect_t page_dims;
static unsigned *image_data;

static void update_title(void) {
	char buf[64];
	snprintf(buf, sizeof buf, "SDLBook [%d/%d] (%d%%) %s",
			curr_page, page_count, config_data.scale, filename);
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

static int get_page_bottom() {
#ifndef OFF_SMOOTH
	return page_dims.h;
#else
	return page_dims.h/2;
#endif
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
	int x, y, yline;
	void *pixels;
	unsigned *ptr;
	unsigned pitch;
	int xoff = MAX((int)(ezsdl_get_width() - page_dims.w)/2, 0);
	int xmax = page_dims.w;
	int ymax = get_page_bottom()*2;
	if(scroll_line_v > ymax) return;
	ymax = MIN(ezsdl_get_height(), ymax-scroll_line_v),
	xmax = MIN(ezsdl_get_width(), xmax-scroll_line_h);
	ezsdl_get_vram_and_pitch(&pixels, &pitch);
	ptr = pixels;
	pitch/=4;
	for(y = 0; y < ymax; y++) {
		yline = y*pitch + xoff;
		for (x = 0; x < xmax; x++)
			ptr[yline + x] = get_image_pixel(x+scroll_line_h, y+scroll_line_v);
	}
	ezsdl_release_vram();
}

static void draw_borders() {
	int x, y, yline, xoff = MAX((int)(ezsdl_get_width() - page_dims.w)/2, 0);
	int ymax = ezsdl_get_height();
	if (!xoff) return;
	void* pixels;
	unsigned *vram, *ptr;
	unsigned pitch;
	ezsdl_get_vram_and_pitch(&pixels, &pitch);
	vram = pixels;
	pitch /= 4;
	for(y = 0, yline = 0; y < ymax; y++, yline+=pitch)
		for(x = 0, ptr=vram+yline; x < xoff; x++, ptr++)
			*ptr = ARGB(0,0,0);
	for(y = 0, yline = 0; y < ymax; y++, yline+=pitch)
		for(x = 0, ptr=vram+yline+xoff+page_dims.w; x < xoff; x++, ptr++)
			*ptr = ARGB(0,0,0);
	ezsdl_release_vram();
}

static void draw_bottom() {
	int x, y, yline;
	int xmax, ymax, ymin;
	ymin = get_page_bottom()*2-scroll_line_v;
	if(ymin < 0) return;
	void *pixels;
	unsigned *ptr;
	unsigned pitch;
	ezsdl_get_vram_and_pitch(&pixels, &pitch);
	ptr = pixels;
	pitch /= 4;
	int xoff = MAX((int)(ezsdl_get_width() - page_dims.w)/2, 0);
	ymax = ezsdl_get_height();
	xmax = MIN(ezsdl_get_width(), page_dims.w);
	for(y = ymin; y < ymax; y++) {
		yline = y*pitch + xoff;
		for (x = 0; x < xmax; x++)
			ptr[yline + x] = ARGB(0,0,0);
	}
	ezsdl_release_vram();
}


static int game_tick(int need_redraw) {
	long long ms_used = 0;
	if(need_redraw) {
		long long tstamp = ezsdl_getutime64();
		draw();
		if(need_redraw & 2) draw_borders();
		draw_bottom();
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

static void prepare_rect(ddjvu_rect_t *prect, ddjvu_rect_t* desired_rect,
			double iw, double ih, int dpi)
{
	int enforce_aspect_ratio = 1;

	prect->x = 0;
	prect->y = 0;
	if (desired_rect) {
		prect->w = desired_rect->w;
		prect->h = desired_rect->h;
		enforce_aspect_ratio = 0;
	} else if (config_data.scale > 0) {
		prect->w = (unsigned int) (iw * (double)config_data.scale) / dpi;
		prect->h = (unsigned int) (ih * (double)config_data.scale) / dpi;
	} else {
		prect->w = (iw * 100) / dpi;
		prect->h = (ih * 100) / dpi;
	}
	if (enforce_aspect_ratio) {
		double dw = iw / prect->w;
		double dh = ih / prect->h;
		if (dw > dh)
			prect->h = (int)(ih / dw);
		else
			prect->w = (int)(iw / dh);
	}
}

static void* render_pdf_page(int pageno, ddjvu_rect_t *res_rect, ddjvu_rect_t *desired_rect)
{
	ddjvu_rect_t prect;
	/* mupdf platform/x11/pdfapp.c */
	fz_page *page;
	fz_rect bounds;
	fz_try(PDOC.ctx) {
		page = fz_load_page(PDOC.ctx, PDOC.doc, pageno);
		bounds = fz_bound_page(PDOC.ctx, page);
	}
	fz_catch(PDOC.ctx) {
		die("failed to load page %d\n", pageno);
	}

	double iw = bounds.x1 - bounds.x0;
	double ih = bounds.y1 - bounds.y0;
	int dpi = 72;

	prepare_rect(&prect, desired_rect, iw, ih, dpi);

	int rowsize = prect.w * 3;
	void *image;
	if(!(image = malloc(rowsize * prect.h)))
		die("Cannot allocate image buffer for page %d", pageno);

	fz_matrix ctm;
	fz_pixmap *pix;

	ctm = fz_scale((double) prect.w / iw, (double) prect.h / ih);
	pix = fz_new_pixmap_from_page(PDOC.ctx, page, ctm, fz_device_rgb(PDOC.ctx), 0);

	fz_drop_page(PDOC.ctx, page);

	if (!pix) {
		free(image);
		return NULL;
	}
	assert(pix->w >= prect.w && pix->h >= prect.h);

	unsigned char* out, *s;
	unsigned x,y,xn;
	for (y = 0, out = image; y < prect.h; y++) {
		s = &pix->samples[y * pix->stride];
		xn = 0;
		for (x = 0; x < prect.w; x++, xn += pix->n) {
			*(out++) = s[xn + 0];
			*(out++) = s[xn + 1];
			*(out++) = s[xn + 2];
		}
	}
	fz_drop_pixmap(PDOC.ctx, pix);

	*res_rect = prect;
	return image;
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

	prepare_rect(&prect, desired_rect, iw, ih, dpi);

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
	if(!IS_DJVU)
		return render_pdf_page(pageno, res_rect, desired_rect);

	ddjvu_page_t *page;
	if (!(page = ddjvu_page_create_by_pageno(DDOC.doc, pageno)))
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

static void* prep_pages(int *need_redraw) {
	ddjvu_rect_t p1rect;
#ifndef OFF_SMOOTH
	ddjvu_rect_t p2rect;
#endif
	static int last_page = -1, last_scale = -1;
	if(curr_page == last_page && last_scale == config_data.scale)
		return image_data;
	last_page = curr_page;
	last_scale = config_data.scale;
	if(need_redraw) *need_redraw = 1;
	char *p1data = prep_page(curr_page, &p1rect, 0);
#ifndef OFF_SMOOTH
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
		if(need_redraw) *need_redraw = 2;
	}
	if(!(p1data && p2data)) return NULL;
	assert(p1rect.w == p2rect.w && p1rect.h == p2rect.h);
	int stored_images = 2;
#else	
	if(!(p1data)) return NULL;
	int stored_images = 1;
#endif

	size_t one_pic = p1rect.w*p1rect.h;
	unsigned* imgbuf = malloc(4 * one_pic * stored_images);

	convert_rgb24_to_rgba(p1data, p1rect.w, p1rect.h, imgbuf);
	page_dims = p1rect;
	free(p1data);
#ifndef OFF_SMOOTH
	convert_rgb24_to_rgba(p2data, p2rect.w, p2rect.h, imgbuf+one_pic);
	free(p2data);
#endif
	return imgbuf;
}

static void handle(int wait) {
	const ddjvu_message_t *msg;
	if (!IS_DJVU || !DDOC.ctx)
		return;
	if (wait)
		msg = ddjvu_message_wait(DDOC.ctx);
	while ((msg = ddjvu_message_peek(DDOC.ctx)))	{
		switch(msg->m_any.tag) {
		case DDJVU_ERROR:
			fprintf(stderr,"ddjvu: %s\n", msg->m_error.message);
			if (msg->m_error.filename)
				fprintf(stderr,"ddjvu: '%s:%d'\n",
				        msg->m_error.filename, msg->m_error.lineno);
		default:
			break;
		}
		ddjvu_message_pop(DDOC.ctx);
	}
}

static void swap_image(void *new) {
	void *old = image_data;
	if(old == new) return;
	image_data = new;
	free(old);
}

static int set_page(int no) {
	int need_redraw;
	if(no >= page_count) no = page_count-1;
	if(no < 0) curr_page = 0;
	else curr_page = no;
	swap_image(prep_pages(&need_redraw));
	update_title();
	return need_redraw;
}

static int change_page(int incr) {
	int need_redraw;
	if(curr_page + incr >= 0 && curr_page + incr < page_count)
		curr_page += incr;
	swap_image(prep_pages(&need_redraw));
	update_title();
	return need_redraw;
}

static int change_scale(int incr) {
	int need_redraw;
	if (config_data.scale + incr <= 999 && config_data.scale + incr > 0)
		config_data.scale += incr;
	else return 0;
	swap_image(prep_pages(&need_redraw));
	update_title();
	if(scroll_line_h + ezsdl_get_width() > page_dims.w) {
		scroll_line_h = MAX((int)(page_dims.w - ezsdl_get_width()), 0);
		return 2;
	}
	return incr < 0 ? 2 : need_redraw;
}

static int change_scroll_v(int incr) {
	int need_redraw = 1;
#ifndef OFF_SMOOTH
	int page_bottom_prv = scroll_line_v + incr + get_page_bottom();
#else
	int page_bottom_prv = get_page_bottom();
	
	if(ezsdl_get_height() >= page_dims.h || abs(incr) == get_page_bottom()) {
		scroll_line_v = 0;
		change_page(incr > 0 ? +1 : -1);
		return need_redraw;
	}
#endif
	if(scroll_line_v + incr < 0) {
		if(curr_page == 0) scroll_line_v = 0;
		else {
			need_redraw = change_page(-1);
			scroll_line_v = MAX(page_bottom_prv, 0);
		}
	} else if(curr_page >= page_count-1) {
	adjust_last_page:
		scroll_line_v = MIN(scroll_line_v + incr, abs((int)page_dims.h - ezsdl_get_height()));
	} else if(scroll_line_v + incr > get_page_bottom()) {
		scroll_line_v = scroll_line_v + incr - get_page_bottom();
		need_redraw = change_page(+1);
		if(curr_page >= page_count-1) {
			incr = 0;
			goto adjust_last_page;
		}
	} else
		scroll_line_v += incr;
	return need_redraw;
}

static int change_scroll_h(int incr) {
	int sw = ezsdl_get_width(), pw = page_dims.w;
	int old_scroll = scroll_line_h;
	if (scroll_line_h + incr <= 0)
		scroll_line_h = 0;
	else if(incr < 0 || (sw <= pw && scroll_line_h + incr <= pw - sw))
		scroll_line_h += incr;
	return old_scroll != scroll_line_h;
}

#define HELP_TEXT \
	"HELP SCREEN - HIT ANY KEY TO EXIT\n" \
	"UP, DOWN ARROW - SCROLL 32 PIX\n" \
	"CTRL + UP, DOWN ARROW - SCROLL 96 PIX\n" \
	"PAGE_UP/DOWN - SCROLL ONE PAGE\n" \
	"KEYPAD +/- OR CTRL-WHEEL - ZOOM\n" \
	"G - ENTER PAGE NUMBER\n" \
	"Q/ESC - QUIT\n" \
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
enum input_flags {
	INPUT_LOOP_RET,
	INPUT_LOOP_NUMERIC
};

static void input_loop(const char* title, char *result, enum input_flags flags)
{
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
		out:;
				*p = 0;
				ezsdl_clear();
				return;
			default:
				if(flags == INPUT_LOOP_RET)
					goto out;
				else if(flags == INPUT_LOOP_NUMERIC && isdigit(event.which) && (p - result < 20)) {
					*(p++) = event.which;
					*p = 0;
				}
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

static void djvu_cleanup(void) {
	if(!IS_DJVU) return;
	if(DDOC.doc)
		ddjvu_document_release(DDOC.doc);
	if(DDOC.ctx)
		ddjvu_context_release(DDOC.ctx);
}

static void pdf_cleanup(void) {
	if(IS_DJVU) return;
	if(PDOC.doc)
		fz_drop_document(PDOC.ctx, PDOC.doc);
	if(PDOC.ctx)
		fz_drop_context(PDOC.ctx);
}

static int cleanup(void) {
	djvu_cleanup();
	pdf_cleanup();

	read_write_config(0);

	ezsdl_shutdown();

	return 1;
}

static int open_djvu(char* app, const char *fn) {
	doc.be = BE_DJVU;
	if(!(DDOC.ctx = ddjvu_context_create(app)))
		return 0;
	if(!(DDOC.doc = ddjvu_document_create_by_filename(DDOC.ctx, fn, TRUE))) {
		djvu_cleanup();
		return 0;
	}
	return 1;
}

static void decode_doc(void) {
	if(IS_DJVU) {
		while (! ddjvu_document_decoding_done(DDOC.doc))
			handle(TRUE);

		if (ddjvu_document_decoding_error(DDOC.doc))
			die("can't decode document");
	}
}

static int open_pdf(const char *fn) {
	doc.be = BE_MUPDF;
	PDOC.ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	fz_register_document_handlers(PDOC.ctx);
	fz_try (PDOC.ctx) {
		PDOC.doc = fz_open_document(PDOC.ctx, fn);
	} fz_catch (PDOC.ctx) {
		fz_drop_context(PDOC.ctx);
		PDOC.ctx = 0;
		return 0;
	}
	return 1;
}

int main(int argc, char **argv) {
	if(argc != 2)
		die("need djvu filename as argv[1]");

	filename = argv[1];

	char *p = strrchr(filename, '.');
	if(p && !strcasecmp(p, ".djvu")) {
		if(!open_djvu(argv[0], filename))
			die("can't open djvu document '%s'", filename);
	} else if(!open_pdf(filename))
		die("can't open mupdf document '%s'", filename);

	decode_doc();

	if(strrchr(filename, '/')) filename = strrchr(filename, '/')+1;


	page_count = IS_DJVU ?
		ddjvu_document_get_pagenum(DDOC.doc) :
		fz_count_pages(PDOC.ctx, PDOC.doc);

	curr_page = 0;

	config_data.scale = 100;

	read_write_config(1);

	ezsdl_init(config_data.w, config_data.h,
#ifndef USE_SDL2
		SDL_HWPALETTE | SDL_RESIZABLE
#else
		0
#endif
	);

	image_data = prep_pages(NULL);

	init_gfx();

	SDL_ShowCursor(1);
#ifndef USE_SDL2
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
#endif
	struct event event;

	update_title();

	game_tick(1);

	unsigned left_ctrl_pressed = 0;
	unsigned right_ctrl_pressed = 0;
	unsigned mb_left_down = 0;
	unsigned mouse_y = 0;
	unsigned mouse_x = 0;

	while(1) {
		unsigned need_redraw = 0;
		int scroll_dist_v = 0;
		int scroll_dist_h = 0;
		int scale_dist = 0;
		enum eventtypes e;
		while((e = ezsdl_getevent(&event)) != EV_NONE) {
			need_redraw = 0;
			switch (e) {
				case EV_MOUSEDOWN:
					if(event.which == SDL_BUTTON_LEFT) mb_left_down = 1;
					break;
				case EV_MOUSEUP:
					if(event.which == SDL_BUTTON_LEFT) mb_left_down = 0;
					break;
				case EV_MOUSEMOVE:
					if(mb_left_down && mouse_y != event.yval)
						scroll_dist_v += mouse_y-event.yval;
					if(mb_left_down && mouse_x != event.xval)
						scroll_dist_h += mouse_x-event.xval;
					mouse_y = event.yval;
					mouse_x = event.xval;
					break;
				case EV_MOUSEWHEEL:
					if(left_ctrl_pressed || right_ctrl_pressed)
						scale_dist += event.yval*-10;
					else
						scroll_dist_v += event.yval*64;
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
							scroll_dist_v += get_page_bottom();
							break;
						case SDLK_PAGEUP:
							scroll_dist_v -= get_page_bottom();
							break;
						case SDLK_UP:
							if((event.mod & KMOD_LCTRL) || (event.mod & KMOD_RCTRL))
								scroll_dist_v += -96;
							else
								scroll_dist_v += -32;
							break;
						case SDLK_DOWN:
							if((event.mod & KMOD_LCTRL) || (event.mod & KMOD_RCTRL))
								scroll_dist_v += +96;
							else
								scroll_dist_v += +32;
							break;
						case SDLK_LEFT:
							if((event.mod & KMOD_LCTRL) || (event.mod & KMOD_RCTRL))
								need_redraw = change_scroll_h(-96);
							else
								need_redraw = change_scroll_h(-32);
							break;
						case SDLK_RIGHT:
							if((event.mod & KMOD_LCTRL) || (event.mod & KMOD_RCTRL))
								need_redraw = change_scroll_h(+96);
							else
								need_redraw = change_scroll_h(+32);
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
						case SDLK_UP:
						case SDLK_DOWN:
						case SDLK_LEFT:
						case SDLK_RIGHT:
						case SDLK_PAGEUP:
						case SDLK_PAGEDOWN:
							need_redraw = 1;
							break;
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
								input_loop(HELP_TEXT, buf, INPUT_LOOP_RET);
								need_redraw = 1;
							}
							break;

						case SDLK_g:
							{
								char buf[32];
								buf[0] = 0;
								input_loop("enter page no", buf, INPUT_LOOP_NUMERIC);
								if(*buf) need_redraw = set_page(atoi(buf));
								else need_redraw = 1;
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
			if(need_redraw) game_tick(need_redraw);
		}
		if(scroll_dist_v) need_redraw |= change_scroll_v(scroll_dist_v);
		if(scroll_dist_h) need_redraw |= change_scroll_h(scroll_dist_h);
		if(scale_dist) need_redraw |= change_scale(scale_dist);

		if(game_tick(need_redraw)) {
		}
	}

dun_goofed:

	cleanup();
	return 0;
}

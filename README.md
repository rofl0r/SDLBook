SDLBook - a tiny djvu/pdf eBook reader
======================================

## What?

Tiny eBook reader using only `SDL`, `djvulibre`, and `libmupdf`.

## Why?

Since there are no djvu readers with reasonable dependency requirements
(i.e. GTK+2, X11, SDL1 or 2, framebuffer) available, I decided to write
my own, by taking a firm glance at what djvulibre's `ddjvu` utility does
and rendering to a video buffer instead of a file. I'm not interested in
installing heavy-weight GUI toolkits such as QT4,5,GTK+3,4 etc just for
a simple eBook reader. Nor in installing python3 just to run someone's
meson build recipe.

As I dislike the interruptive page jumping of `mupdf-x11`, I've added
mupdf support too so it can take advantage of the smooth page border
scrolling mechanism of SDLBook. Therefore it now also supports all
the formats supported by mupdf, namely `pdf`, `epub`, `fb2`, `xps`,
`openxps`, `cbz`, `cbr`.

## How?

- Install `SDL` and `djvulibre` libraries including development headers.
- Compile using `gnu make` by running `make CFLAGS=-O2`.
- Install as root user by doing `make install prefix=/usr`.
- Run like `sdlbook /path/to/file.djvu`
- Press F1 to see available keyboard shortcuts
- Have fun reading.

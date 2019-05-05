SDLBook - a tiny djvu eBook reader
==================================

## What?

Tiny djvu eBook reader using only `SDL` and `djvulibre`.

## Why?

Since there are no djvu readers with reasonable dependency requirements
(i.e. GTK+2, X11, SDL1 or 2, framebuffer) available, I decided to write
my own, by taking a firm glance at what djvulibre's `ddjvu` utility does
and rendering to a video buffer instead of a file. I'm not interested in
installing heavy-weight GUI toolkits such as QT4,5,GTK+3,4 etc just for
a simple eBook reader. Nor in installing python3 just to run someone's
meson build recipe.

## How?

- Install `SDL` and `djvulibre` libraries including development headers.
- Compile using `gnu make` by running `make CFLAGS=-O2`.
- Install as root user by doing `make install prefix=/usr`.
- Run like `sdlbook /path/to/file.djvu`
- Press F1 to see available keyboard shortcuts
- Have fun reading.

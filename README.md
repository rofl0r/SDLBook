SDLBook - a tiny djvu/pdf eBook reader
======================================

## What?

Tiny eBook reader using only `SDL` or `SDL2`, `djvulibre`, and `libmupdf`.

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
  if you want to use SDL2, run `make SDL2=1 CFLAGS=-O2` instead.
- Install as root user by doing `make install prefix=/usr`.
- Run like `sdlbook /path/to/file.djvu`
- Press F1 to see available keyboard shortcuts
- Have fun reading.

## Screenshot
<img alt="sdlbook-screenshot" src="https://private-user-images.githubusercontent.com/341425/636529842-0ae83cfe-5449-49ba-9157-63e00996ea66.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3ODY3OTk1NjgsIm5iZiI6MTc4Njc5OTI2OCwicGF0aCI6Ii8zNDE0MjUvNjM2NTI5ODQyLTBhZTgzY2ZlLTU0NDktNDliYS05MTU3LTYzZTAwOTk2ZWE2Ni5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjYwODE1JTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI2MDgxNVQxMzA3NDhaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT04OWFmZDNkZDIxZGZiMTYxOTRkMDM2ZjgyNTE4NGM4Njg0NmUwMTUwYTIyZDczMjg2YTIzZDAwOGE4MzM5OThhJlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCZyZXNwb25zZS1jb250ZW50LXR5cGU9aW1hZ2UlMkZwbmcifQ.vmyIDNphOwPWs-8uC9qIxDPHVWgqEPbixLcXrsBE0bs" />

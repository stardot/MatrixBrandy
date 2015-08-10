/*
** This file is part of the Brandy Basic V Interpreter.
** Copyright (C) 2000, 2001, 2002, 2003, 2004 David Daniels
**
**
** Brandy is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2, or (at your option)
** any later version.
**
** Brandy is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with Brandy; see the file COPYING.  If not, write to
** the Free Software Foundation, 59 Temple Place - Suite 330,
** Boston, MA 02111-1307, USA.
**
**
** This file contains graphics routines to draw shapes. It was derived
** from code in the GPL'd jlib graphics library which was written by
** and Copyright (C) 1995-97 Jonathan Paul Griffiths.
**
** Copyright (C) 2006 by Colin Tuckley
**
*/

#include <stdlib.h>
#include <SDL.h>
#include "target.h"

#define MAX_YRES 1280
#define MAX_XRES 16384
static int32 left[MAX_YRES], right[MAX_YRES];

#define FAST_2_MUL(x) ((x)<<1)
#define FAST_3_MUL(x) (((x)<<1)+x)
#define FAST_4_DIV(x) ((x)>>2)

/* Use Bresenham's line algorithm to trace an edge of the polygon. */
void trace_edge(int32 x1, int32 y1, int32 x2, int32 y2)
{
  int32 dx, dy, xf, yf, a, b, t, i;

  if (x1 == x2 && y1 == y2) return;

  if (x2 > x1) {
    dx = x2 - x1;
    xf = 1;
  }
  else {
    dx = x1 - x2;
    xf = -1;
  }

  if (y2 > y1) {
    dy = y2 - y1;
    yf = 1;
  }
  else {
    dy = y1 - y2;
    yf = -1;
  }

  if (dx > dy) {
    a = dy + dy;
    t = a - dx;
    b = t - dx;
    for (i = 0; i <= dx; i++) {
      if (x1 < left[y1]) left[y1] = x1;
      if (x1 > right[y1]) right[y1] = x1;
      x1 += xf;
      if (t < 0)
        t += a;
      else {
        t += b;
        y1 += yf;
      }
    }
  }
  else {
    a = dx + dx;
    t = a - dy;
    b = t - dy;
    for (i = 0; i <= dy; i++) {
      if (x1 < left[y1]) left[y1] = x1;
      if (x1 > right[y1]) right[y1] = x1;
      y1 += yf;
      if (t < 0)
        t += a;
      else {
        t += b;
        x1 += xf;
      }
    }
  }
}

/*
** Draw a horizontal line
*/
void draw_h_line(SDL_Surface *sr, int32 sw, int32 sh, int32 x1, int32 y, int32 x2, Uint32 col) {
  int32 tt, i;
  if (x1 > x2) {
    tt = x1; x1 = x2; x2 = tt;
  }
  if ( y >= 0 && y < sh ) {
    if (x1 < 0) x1 = 0;
    if (x1 >= sw) x1 = sw-1;
    if (x2 < 0) x2 = 0;
    if (x2 >= sw) x2 = sw-1;
    for (i = x1; i <= x2; i++)
      *((Uint32*)sr->pixels + i + y*sw) = col;
  }
}

/*
** Draw a filled polygon of n vertices
*/
void buff_convex_poly(SDL_Surface *sr, int32 sw, int32 sh, int32 n, int32 *x, int32 *y, Uint32 col) {
  int32 i, iy;
  int32 low = MAX_YRES, high = 0;

  /* set highest and lowest points to visit */
  for (i = 0; i < n; i++) {
    if (y[i] > high)
      high = y[i];
    if (y[i] < low)
      low = y[i];
  }

  /* reset the minumum amount of the edge tables */
  for (iy = low; iy <= high; iy++) {
    left[iy] = MAX_XRES + 1;
    right[iy] = - 1;
  }

  /* define edges */
  trace_edge(x[n - 1], y[n - 1], x[0], y[0]);

  for (i = 0; i < n - 1; i++)
    trace_edge(x[i], y[i], x[i + 1], y[i + 1]);

  /* fill horizontal spans of pixels from left[] to right[] */
  for (iy = low; iy <= high; iy++)
    draw_h_line(sr, sw, sh, left[iy], iy, right[iy], col);
}

/*
** 'draw_line' draws an arbitary line in the graphics buffer 'sr'.
** clipping for x & y is implemented
*/
void draw_line(SDL_Surface *sr, int32 sw, int32 sh, int32 x1, int32 y1, int32 x2, int32 y2, Uint32 col) {
  int d, x, y, ax, ay, sx, sy, dx, dy, tt;
  if (x1 > x2) {
    tt = x1; x1 = x2; x2 = tt;
    tt = y1; y1 = y2; y2 = tt;
  }
  dx = x2 - x1;
  ax = abs(dx) << 1;
  sx = ((dx < 0) ? -1 : 1);
  dy = y2 - y1;
  ay = abs(dy) << 1;
  sy = ((dy < 0) ? -1 : 1);

  x = x1;
  y = y1;

  if (ax > ay) {
    d = ay - (ax >> 1);
    while (x != x2) {
      if ((x >= 0) && (x < sw) && (y >= 0) && (y < sh)) 
        *((Uint32*)sr->pixels + x + y*sw) = col;
      if (d >= 0) {
        y += sy;
        d -= ax;
      }
      x += sx;
      d += ay;
    }
  } else {
    d = ax - (ay >> 1);
    while (y != y2) {
      if ((x >= 0) && (x < sw) && (y >= 0) && (y < sh)) 
        *((Uint32*)sr->pixels + x + y*sw) = col;
      if (d >= 0) {
        x += sx;
        d -= ay;
      }
      y += sy;
      d += ax;
    }
  }
  if ((x >= 0) && (x < sw) && (y >= 0) && (y < sh)) 
    *((Uint32*)sr->pixels + x + y*sw) = col;
}

/*
** 'filled_triangle' draws a filled triangle in the graphics buffer 'sr'.
*/
void filled_triangle(SDL_Surface *sr, int32 sw, int32 sh, int32 x1, int32 y1, int32 x2, int32 y2,
                     int32 x3, int32 y3, Uint32 col)
{
  int x[3], y[3];

  x[0]=x1;
  x[1]=x2;
  x[2]=x3;

  y[0]=y1;
  y[1]=y2;
  y[2]=y3;

  buff_convex_poly(sr, sw, sh, 3, x, y, col);
}

/*
** Draw an ellipse into a buffer
*/
void draw_ellipse(SDL_Surface *sr, int32 sw, int32 sh, int32 x0, int32 y0, int32 a, int32 b, Uint32 c) {
  int32 x, y, y1, aa, bb, d, g, h;
  Uint32 *dest;

  aa = a * a;
  bb = b * b;

  h = (FAST_4_DIV(aa)) - b * aa + bb;
  g = (FAST_4_DIV(9 * aa)) - (FAST_3_MUL(b * aa)) + bb;
  x = 0;
  y = b;

  while (g < 0) {
    if (((y0 - y) >= 0) && ((y0 - y) < sh)) {
      dest = ((Uint32*)sr->pixels + x0 + (y0 - y)*sw);
      if (((x0 - x) >= 0) && ((x0 - x) < sw)) *(dest - x) = c;
      if (((x0 + x) >= 0) && ((x0 + x) < sw)) *(dest + x) = c;
    }
    if (((y0 + y) >= 0) && ((y0 + y) < sh)) {
      dest = ((Uint32*)sr->pixels + x0 + (y0 + y)*sw);
      if (((x0 - x) >= 0) && ((x0 - x) < sw)) *(dest - x) = c;
      if (((x0 + x) >= 0) && ((x0 + x) < sw)) *(dest + x) = c;
    }

    if (h < 0) {
      d = ((FAST_2_MUL(x)) + 3) * bb;
      g += d;
    }
    else {
      d = ((FAST_2_MUL(x)) + 3) * bb - FAST_2_MUL((y - 1) * aa);
      g += (d + (FAST_2_MUL(aa)));
      --y;
    }

    h += d;
    ++x;
  }

  y1 = y;
  h = (FAST_4_DIV(bb)) - a * bb + aa;
  x = a;
  y = 0;

  while (y <= y1) {
    if (((y0 - y) >= 0) && ((y0 - y) < sh)) {
      dest = ((Uint32*)sr->pixels + x0 + (y0 - y)*sw);
      if (((x0 - x) >= 0) && ((x0 - x) < sw)) *(dest - x) = c;
      if (((x0 + x) >= 0) && ((x0 + x) < sw)) *(dest + x) = c;
    } 
    if (((y0 + y) >= 0) && ((y0 + y) < sh)) {
      dest = ((Uint32*)sr->pixels + x0 + (y0 + y)*sw);
      if (((x0 - x) >= 0) && ((x0 - x) < sw)) *(dest - x) = c;
      if (((x0 + x) >= 0) && ((x0 + x) < sw)) *(dest + x) = c;
    }

    if (h < 0)
      h += ((FAST_2_MUL(y)) + 3) * aa;
    else {
      h += (((FAST_2_MUL(y) + 3) * aa) - (FAST_2_MUL(x - 1) * bb));
      --x;
    }
    ++y;
  }
}

/*
** Draw a filled ellipse into a buffer
*/
void filled_ellipse(SDL_Surface *sr, int32 sw, int32 sh, int32 x0, int32 y0, int32 a, int32 b, Uint32 c) {
  int32 x, y, y1, aa, bb, d, g, h;

  aa = a * a;
  bb = b * b;

  h = (FAST_4_DIV(aa)) - b * aa + bb;
  g = (FAST_4_DIV(9 * aa)) - (FAST_3_MUL(b * aa)) + bb;
  x = 0;
  y = b;

  while (g < 0) {
    draw_h_line(sr, sw, sh, x0 - x, y0 + y, x0 + x, c);
    draw_h_line(sr, sw, sh, x0 - x, y0 - y, x0 + x, c);

    if (h < 0) {
      d = ((FAST_2_MUL(x)) + 3) * bb;
      g += d;
    }
    else {
      d = ((FAST_2_MUL(x)) + 3) * bb - FAST_2_MUL((y - 1) * aa);
      g += (d + (FAST_2_MUL(aa)));
      --y;
    }

    h += d;
    ++x;
  }

  y1 = y;
  h = (FAST_4_DIV(bb)) - a * bb + aa;
  x = a;
  y = 0;

  while (y <= y1) {
    draw_h_line(sr, sw, sh, x0 - x, y0 + y, x0 + x, c);
    draw_h_line(sr, sw, sh, x0 - x, y0 - y, x0 + x, c);

    if (h < 0)
      h += ((FAST_2_MUL(y)) + 3) * aa;
    else {
      h += (((FAST_2_MUL(y) + 3) * aa) - (FAST_2_MUL(x - 1) * bb));
      --x;
    }
    ++y;
  }
}

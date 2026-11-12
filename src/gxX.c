/* Copyright (C) 1988-2022 by George Mason University. See file COPYRIGHT for more information. */

/* X interface for Cairo -- buttons, rubber bands, drop menus, dialog boxes
   and screen save/show are supported; no X-based pattern fill. */

/* Include ./configure's header file */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <sys/time.h>
#include <sys/select.h>

#include <cairo.h>
#include <cairo-xlib.h>

#include "gatypes.h"
#include "gx.h"
#include "gxC.h"
#include "bitmaps2.h"

/* function prototypes */
void gxdXflush (void);
void *galloc (size_t, char *);

/* Interface for X11 Release 3  */

/* Device interface level.  Following routines need to be
   interfaced to the hardware:

   gxdbgn - Initialize graphics output.  Set up any hardware
            scaling needed, clear the graphics display area,
            etc.
   gxdcol - Set hardware color.  The colors should be set up
            as follows:
            0 - black;   1 - white    2 - red     3 - green
            4 - blue     5 - cyan     6 - magenta 7 - yellow
            8 - orange   9 - purple  10 - yell-grn  11 - lt.blue
           12 - ora.yell 3 - blu-grn 14 - blu-purp  15 - grey
            If colors are not available then grey scales can
            be used, or the call can be a no op.
   gxdwid - Set hardware line weight.
   gxdfrm - New frame.  If in single buffer mode, clear the active
            display.  If in double buffer mode, clear the background
            buffer.
   gxdsgl - Initiate single buffer mode.  Normal mode of operation.
   gxddbl - Initiate double buffer mode, if available.  Both the
            foreground and background displays should be cleared.
   gxdswp - Swap buffers when in double buffer mode.  Should take
            no action if in single buffer mode.
   gxdrec - Draw a color filled rectangle.
   gxddrw - Draw a line using current attributes
   gxdmov - Move to a new point
   gxdend - Terminate graphics output.
                                                                  */

static gaint batch=0;                       /* Batch mode? */
static gadouble xscl,yscl;                  /* Window Scaling */
static gadouble xsize, ysize;               /* User specified size */
static gaint dblmode;                       /* single or double buffering */
static gaint width,height,depth;            /* Window dimensions */
static struct gevent *evbase;               /* Anchor for GrADS event queue */
static cairo_surface_t *surface=NULL,*surface2=NULL; /* Cairo surfaces */

/* All stuff passed to Xlib routines as args are put here in
   static areas since we are not invoking Xlib routines from main*/

static Visual *xvis;
static Screen *sptr;
Display *display=(Display *)NULL;
static gaint snum;
static XEvent report;
Window win=(Window) NULL;   /* used via extern in gagui */
static Drawable drwbl,drwbl2;
static char *window_name = "GrADS " GRADS_VERSION "";
static char *display_name = NULL; 
static char *icon_name = "GrADS";
static Pixmap icon_pixmap;
static XSizeHints size_hints;
static gaint argc;
static char **argv;
static char *args[4];
static char *name = "grads";
static char *dgeom = "500x400+10+10";
static char *ugeom = NULL;
static gaint dblmode;                     /* single or double buffering */
static XSetWindowAttributes xsetw;
static gaint rstate = 1;              /* Redraw state -- when zero,
                                      acceptance of X Events is
                                      blocked. */
static gaint bsflg;                   /* Backing store enabled or not */
static gaint excnt;                   /* Count of exposes to skip */

/* ------------------------------------------------------------------
   Widgets for the Cairo display interface: buttons and rubber bands.
   Ported from the X11 display interface (gxX11.c). Widgets are drawn
   with Cairo on the X surfaces, so text is anti-aliased like the rest
   of the plot. Also dialog boxes and screen save/show.
   ------------------------------------------------------------------ */

static struct gobj obj[512];    /* Displayed objects (foreground) */
static struct gobj obj2[512];   /* Background objects (double buffer mode) */
static gaint obnum=0,obnum2=0;  /* Current number of objects */
static struct gbtn btn[256];    /* Current buttons */
static struct gbtn btn2[256];   /* Background buttons */
static struct grbb rbb[32];     /* Current rubber-band regions */
static struct grbb rbb2[32];    /* Background rbb regions */
static struct gdmu dmu[200];    /* Current dropmenus */
static struct gdmu dmu2[200];   /* Background dropmenus */

void gxrswd (gaint);

/* Initialize all widget tables */

static void gxwinit (void) {
gaint i;
  for (i=0; i<256; i++) { btn[i].num = -1; btn2[i].num = -1; btn[i].ch = NULL; btn2[i].ch = NULL; }
  for (i=0; i<200; i++) { dmu[i].num = -1; dmu2[i].num = -1; dmu[i].ch = NULL; dmu2[i].ch = NULL; }
  for (i=0; i<32; i++)  { rbb[i].num = -1; rbb2[i].num = -1; }
  for (i=0; i<512; i++) { obj[i].type = -1; obj2[i].type = -1; }
  obnum = 0; obnum2 = 0;
}

/* Clear the list of displayed objects. bg=0 foreground, bg=1 background */

static void gxwclr (gaint bg) {
gaint i;
  if (bg) { for (i=0; i<512; i++) obj2[i].type = -1; obnum2 = 0; }
  else    { for (i=0; i<512; i++) obj[i].type = -1;  obnum = 0;  }
}

/* Get the slot in the list of displayed objects for a widget (pwd points
   to its btn/rbb/dmu struct). A widget drawn again (same number) reuses
   its slot, and slots of cleared widgets are reused: otherwise the list
   fills up when scripts redraw the same widgets many times. */

static struct gobj *gxwobj (gaint bg, void *pwd) {
struct gobj *ob;
gaint *pn, i, free;
  ob = bg ? obj2 : obj;
  pn = bg ? &obnum2 : &obnum;
  free = -1;
  for (i=0; i<*pn; i++) {
    if (ob[i].type>0 && (void *)ob[i].iob.dmu==pwd) return (&(ob[i]));
    if (ob[i].type==0 && free<0) free = i;
  }
  if (free>-1) return (&(ob[free]));
  if (*pn>511) { printf ("Error: Too many widgets on screen\n"); return (NULL); }
  return (&(ob[(*pn)++]));
}

/* Surface where widgets of a given layer are drawn */

static cairo_surface_t *gxwsfc (gaint bg) {
  if (bg && dblmode && surface2) return (surface2);
  return (surface);
}

/* Set a GrADS color as the cairo source (same rules as gxCcol) */

static void gxwcol (cairo_t *c, gaint clr) {
struct gxdbquery dbq;
gaint bcol;
  bcol = gxdbkq();
  if (bcol==1) {
    if (clr==0)      clr = 1;
    else if (clr==1) clr = 0;
  }
  gxdbqcol(clr, &dbq);
  if (dbq.alpha<0 || dbq.alpha>255) dbq.alpha = 255;
  cairo_set_source_rgba(c, (double)(dbq.red)/255.0, (double)(dbq.green)/255.0,
                           (double)(dbq.blue)/255.0, (double)(dbq.alpha)/255.0);
}

/* Outline width in pixels, as in the X11 interface */

static gaint gxwlw (gaint thk) {
  if (thk>11) return (3);
  if (thk>5) return (2);
  return (1);
}

/* Draw a pixel-aligned line */

static void gxwline (cairo_t *c, gaint i1, gaint j1, gaint i2, gaint j2, gaint lw) {
gadouble off;
  off = (lw%2) ? 0.5 : 0.0;
  cairo_set_line_width(c, (double)lw);
  cairo_move_to(c, i1+off, j1+off);
  cairo_line_to(c, i2+off, j2+off);
  cairo_stroke(c);
}

/* Font size (pixels) for widget labels, chosen from the window size */

static gadouble gxwfsz (void) {
  if (width<601 || height<421) return (11.0);
  if (width<1001 || height<651) return (13.0);
  return (16.0);
}

/* Draw the two halves of a button outline */

static void gxwbout (cairo_t *c, struct gbtn *gbb, gaint c1, gaint c2) {
gaint lw;
  lw = gxwlw(gbb->thk);
  cairo_set_line_cap(c, CAIRO_LINE_CAP_SQUARE);
  if (c1>-1) {
    gxwcol(c, c1);
    gxwline(c, gbb->ilo, gbb->jhi, gbb->ihi, gbb->jhi, lw);
    gxwline(c, gbb->ihi, gbb->jhi, gbb->ihi, gbb->jlo, lw);
  }
  if (c2>-1) {
    gxwcol(c, c2);
    gxwline(c, gbb->ihi, gbb->jlo, gbb->ilo, gbb->jlo, lw);
    gxwline(c, gbb->ilo, gbb->jlo, gbb->ilo, gbb->jhi, lw);
  }
}

/* Compute the pixel extent of a button and draw it on a surface */

static void gxwbtn (struct gbtn *gbb, cairo_surface_t *sfc) {
cairo_t *c;
cairo_text_extents_t te;
cairo_font_extents_t fe;
gaint i, j, w, h, ccc;

  i = (gaint)(gbb->x*xscl+0.5);
  j = height - (gaint)(gbb->y*yscl+0.5);
  w = (gaint)(gbb->w*xscl+0.5) - 2;
  h = (gaint)(gbb->h*yscl+0.5) - 2;
  gbb->ilo = 1 + i - w/2;
  gbb->jlo = 1 + j - h/2;
  gbb->ihi = gbb->ilo + w;
  gbb->jhi = gbb->jlo + h;
  if (sfc==NULL) return;

  c = cairo_create(sfc);
  cairo_set_antialias(c, CAIRO_ANTIALIAS_NONE);

  /* Background */
  ccc = gbb->state ? gbb->btc : gbb->bc;
  if (ccc>-1 && w>0 && h>0) {
    gxwcol(c, ccc);
    cairo_rectangle(c, gbb->ilo, gbb->jlo, w, h);
    cairo_fill(c);
  }

  /* Outline */
  if (gbb->state) gxwbout(c, gbb, gbb->otc1, gbb->otc2);
  else gxwbout(c, gbb, gbb->oc1, gbb->oc2);

  /* Label, centered in the button */
  ccc = gbb->state ? gbb->ftc : gbb->fc;
  if (ccc>-1 && gbb->ch && *(gbb->ch)) {
    cairo_set_antialias(c, CAIRO_ANTIALIAS_DEFAULT);
    cairo_select_font_face(c, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(c, gxwfsz());
    cairo_text_extents(c, gbb->ch, &te);
    cairo_font_extents(c, &fe);
    gxwcol(c, ccc);
    cairo_move_to(c, (double)i - te.width/2.0 - te.x_bearing,
                     (double)j + (fe.ascent - fe.descent)/2.0);
    cairo_show_text(c, gbb->ch);
  }
  cairo_destroy(c);
  cairo_surface_flush(sfc);
}

/* Register a button in the list of displayed objects */

static void gxwregb (struct gbtn *gbb, gaint bg) {
struct gobj *pob;
  if ((pob = gxwobj(bg, gbb)) == NULL) return;
  pob->type = 1;
  pob->mb = -1;
  pob->i1 = gbb->ilo;  pob->i2 = gbb->ihi;
  pob->j1 = gbb->jlo;  pob->j2 = gbb->jhi;
  pob->iob.btn = gbb;
}

/* Register a rubber-band region in the list of displayed objects */

static void gxwregr (struct grbb *prb, gaint bg) {
struct gobj *pob;
  if ((pob = gxwobj(bg, prb)) == NULL) return;
  pob->type = 2;
  pob->mb = prb->mb;
  pob->i1 = (gaint)(prb->xlo*xscl+0.5);
  pob->i2 = (gaint)(prb->xhi*xscl+0.5);
  pob->j1 = height - (gaint)(prb->yhi*yscl+0.5);
  pob->j2 = height - (gaint)(prb->ylo*yscl+0.5);
  pob->iob.rbb = prb;
}

/* Routine to display a button widget.
      redraw -- the button is being redrawn (pbn is ignored, unless
                redraw>1: 2 = new label, 3 = new label and colors,
                used by the 'redraw button' command)
      btnrel -- the button is being redrawn in a new state
                due to a buttonpress/buttonrelease event.
      nstat  -- forces the state to go to this new setting. */

void gxdpbn (gaint bnum, struct gbtn *pbn, gaint redraw, gaint btnrel, gaint nstat) {
struct gbtn *gbb;
gaint bg;

  if (batch || surface==NULL) return;
  if (bnum<0 || bnum>255) return;
  bg = (dblmode && !btnrel);
  gbb = bg ? &(btn2[bnum]) : &(btn[bnum]);
  if (!redraw) {
    if (gbb->num>-1 && gbb->ch!=NULL && gbb->ch!=pbn->ch) gree(gbb->ch,"f500");
    *gbb = *pbn;
    gbb->num = bnum;
  }
  if (gbb->num<0) return;
  if (nstat>-1) gbb->state = nstat;
  if (redraw>1 && pbn) {
    if (pbn->ch) {
      if (gbb->ch) gree(gbb->ch,"f500");
      gbb->ch = pbn->ch;
      gbb->len = pbn->len;
    }
    if (redraw==3) {
      gbb->fc = pbn->fc; gbb->bc = pbn->bc;
      gbb->oc1 = pbn->oc1; gbb->oc2 = pbn->oc2;
      gbb->ftc = pbn->ftc; gbb->btc = pbn->btc;
      gbb->otc1 = pbn->otc1; gbb->otc2 = pbn->otc2;
    }
  }
  gxwbtn(gbb, gxwsfc(bg));
  if (!redraw) gxwregb(gbb, bg);
  XFlush(display);
}

/* Set up a rubber-band region. */

void gxdrbb (gaint num, gaint type, gadouble xlo, gadouble ylo, gadouble xhi, gadouble yhi, gaint mbc) {
struct grbb *prb;
gaint bg;

  if (batch || surface==NULL) return;
  if (num<0 || num>31) return;
  if (xlo>=xhi || ylo>=yhi) return;
  bg = dblmode;
  prb = bg ? &(rbb2[num]) : &(rbb[num]);
  prb->num = num;
  prb->xlo = xlo; prb->xhi = xhi;
  prb->ylo = ylo; prb->yhi = yhi;
  prb->type = type;
  prb->mb = mbc;
  gxwregr(prb, bg);
}

/* ---------------------------- Drop menus ---------------------------- */

/* Select the (bold oblique) font used by drop menus */

static void gxwmfont (cairo_t *c) {
  cairo_select_font_face(c, "Sans", CAIRO_FONT_SLANT_OBLIQUE, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(c, gxwfsz());
}

/* Draw the header of a drop menu. tog=1 uses the 'selected' colors. */

static void gxwdmhdr (struct gdmu *gmu, cairo_surface_t *sfc, gaint tog) {
cairo_t *c;
cairo_font_extents_t fe;
gaint w, h, fc, bc, oc1, oc2, lw;

  if (sfc==NULL) return;
  w = gmu->ihi - gmu->ilo;
  h = gmu->jhi - gmu->jlo;
  fc = gmu->fc; bc = gmu->bc; oc1 = gmu->oc1; oc2 = gmu->oc2;
  if (tog) {
    if (gmu->tbc>-1 && gmu->tfc>-1) { fc = gmu->tfc; bc = gmu->tbc; }
    else { fc = -1; bc = -1; }
    oc1 = gmu->toc1; oc2 = gmu->toc2;
  }
  c = cairo_create(sfc);
  cairo_set_antialias(c, CAIRO_ANTIALIAS_NONE);
  if (bc>-1) {
    gxwcol(c, bc);
    cairo_rectangle(c, gmu->ilo, gmu->jlo, w+1, h+1);
    cairo_fill(c);
  }
  lw = gxwlw(gmu->thk);
  cairo_set_line_cap(c, CAIRO_LINE_CAP_SQUARE);
  if (oc1>-1) {
    gxwcol(c, oc1);
    gxwline(c, gmu->ilo, gmu->jhi, gmu->ihi, gmu->jhi, lw);
    gxwline(c, gmu->ihi, gmu->jhi, gmu->ihi, gmu->jlo, lw);
  }
  if (oc2>-1) {
    gxwcol(c, oc2);
    gxwline(c, gmu->ihi, gmu->jlo, gmu->ilo, gmu->jlo, lw);
    gxwline(c, gmu->ilo, gmu->jlo, gmu->ilo, gmu->jhi, lw);
  }
  if (fc>-1 && gmu->ch && *(gmu->ch)) {
    cairo_set_antialias(c, CAIRO_ANTIALIAS_DEFAULT);
    gxwmfont(c);
    cairo_font_extents(c, &fe);
    gxwcol(c, fc);
    cairo_move_to(c, (double)gmu->ilo + fe.ascent/2.0,
                     (double)(gmu->jlo + h/2) + (fe.ascent - fe.descent)/2.0);
    cairo_show_text(c, gmu->ch);   /* header is the first '\0'-terminated entry */
  }
  cairo_destroy(c);
  cairo_surface_flush(sfc);
}

/* Compute the pixel extent of a drop menu header and draw it */

static void gxwdmu (struct gdmu *gmu, cairo_surface_t *sfc) {
gaint i, j, w, h;
  i = (gaint)(gmu->x*xscl+0.5);
  j = height - (gaint)(gmu->y*yscl+0.5);
  w = (gaint)(gmu->w*xscl+0.5) - 2;
  h = (gaint)(gmu->h*yscl+0.5) - 2;
  gmu->ilo = 1 + i - w/2;
  gmu->jlo = 1 + j - h/2;
  gmu->ihi = gmu->ilo + w;
  gmu->jhi = gmu->jlo + h;
  gxwdmhdr(gmu, sfc, 0);
}

/* Register a drop menu header in the list of displayed objects */

static void gxwregm (struct gdmu *gmu, gaint bg) {
struct gobj *pob;
  if ((pob = gxwobj(bg, gmu)) == NULL) return;
  pob->type = 3;
  pob->mb = -1;
  pob->i1 = gmu->ilo;  pob->i2 = gmu->ihi;
  pob->j1 = gmu->jlo;  pob->j2 = gmu->jhi;
  pob->iob.dmu = gmu;
}

/* Routine to display a drop menu widget.
      redraw -- the menu is being redrawn; pmu is ignored.
   Cascade menus are only stored, they are shown when needed. */

void gxdrmu (gaint mnum, struct gdmu *pmu, gaint redraw, gaint nstat) {
struct gdmu *gmu;
gaint bg;

  if (batch || surface==NULL) return;
  if (mnum<0 || mnum>199) return;
  bg = dblmode;
  gmu = bg ? &(dmu2[mnum]) : &(dmu[mnum]);
  if (!redraw) {
    if (gmu->num>-1 && gmu->ch!=NULL && gmu->ch!=pmu->ch) gree(gmu->ch,"f507");
    *gmu = *pmu;
    gmu->num = mnum;
  }
  if (gmu->num<0) return;
  if (gmu->casc) return;
  gxwdmu(gmu, gxwsfc(bg));
  if (!redraw) gxwregm(gmu, bg);
  XFlush(display);
}

/* State of the open (possibly cascading) menus. Up to 4 levels. */

#define GXWMXIT 200
struct gxwpop {
  struct gdmu *gmu;
  gaint cnt;                     /* number of items */
  gaint h, lw;                   /* item height, outline width */
  gaint isiz, jsiz;              /* menu size */
  gaint imin, imax, jmin, jmax;  /* menu position */
  gaint hi;                      /* highlighted item (-1 none) */
  char *txt[GXWMXIT];            /* item labels */
  gaint tlen[GXWMXIT];           /* label lengths (without cascade marker) */
  gaint ptrs[GXWMXIT];           /* cascade menu number, or -1 */
  cairo_surface_t *save;         /* what the menu covers */
};
static struct gxwpop gxwpops[4];
static gaint dmlev, dmnum[4], dmsel[4], dmcur[4];
static gaint dmpi, dmpj;          /* where the click opening the menu occurred */

/* Parse the items of a menu. Items ending with >NN> or >NNN> open
   cascade menu number NN/NNN. The header (first entry) is skipped
   for top level menus. */

static void gxwpparse (struct gxwpop *p, gaint skiphdr) {
struct gdmu *gmu;
char *ch;
gaint i, len, k, n;

  gmu = p->gmu;
  p->cnt = 0;
  ch = gmu->ch;
  i = 0;
  if (skiphdr) {
    len = strlen(ch);
    ch += len+1;
    i = len+1;
  }
  while (i<gmu->len && p->cnt<GXWMXIT) {
    len = strlen(ch);
    p->txt[p->cnt] = ch;
    p->tlen[p->cnt] = len;
    p->ptrs[p->cnt] = -1;
    for (k=0; k<len; k++) {
      if (ch[k]!='>') continue;
      n = 0;
      while (k+1+n<len && ch[k+1+n]>='0' && ch[k+1+n]<='9') n++;
      if ((n==2 || (n==3 && ch[k+1]<='1')) && k+1+n<len && ch[k+1+n]=='>') {
        p->ptrs[p->cnt] = atoi(ch+k+1);
        p->tlen[p->cnt] = k;
        break;
      }
    }
    while (p->tlen[p->cnt]>0 && ch[p->tlen[p->cnt]-1]==' ') p->tlen[p->cnt]--;
    ch += len+1;
    i += len+1;
    p->cnt++;
  }
}

/* Cascade arrow for an item. hi=1 swaps the colors (selected look). */

static void gxwparrow (cairo_t *c, struct gxwpop *p, gaint item, gaint hi) {
gaint j1, j2, ca, cb, x1, x2;
  ca = hi ? p->gmu->soc1 : p->gmu->soc2;
  cb = hi ? p->gmu->soc2 : p->gmu->soc1;
  j1 = p->h/5;
  j2 = (item+1)*p->h - p->h/2;
  x1 = p->isiz - j1*3;
  x2 = p->isiz - j1;
  if (ca>-1) {
    gxwcol(c, ca);
    gxwline(c, x1, j2+j1, x1, j2-j1, p->lw);
    gxwline(c, x1, j2-j1, x2, j2, p->lw);
  }
  if (cb>-1) {
    gxwcol(c, cb);
    gxwline(c, x2, j2, x1, j2+j1, p->lw);
  }
}

/* Draw a menu, with the highlighted item framed */

static void gxwpdraw (struct gxwpop *p) {
struct gdmu *gmu;
cairo_t *c;
cairo_surface_t *img;
char buf[512];
gaint k, lw, j1, j2, w, h;

  gmu = p->gmu;
  lw = p->lw; w = p->isiz; h = p->jsiz;
  /* Draw off-screen, then copy in one go: no flicker */
  img = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
  c = cairo_create(img);
  if (p->save) {
    cairo_set_source_surface(c, p->save, 0, 0);
    cairo_paint(c);
  }
  cairo_set_antialias(c, CAIRO_ANTIALIAS_NONE);
  cairo_set_line_cap(c, CAIRO_LINE_CAP_SQUARE);
  if (gmu->bbc>-1) {
    gxwcol(c, gmu->bbc);
    cairo_paint(c);
  }
  if (gmu->boc1>-1) {
    gxwcol(c, gmu->boc1);
    gxwline(c, 0, h-1, w-1, h-1, 1);
    gxwline(c, w-1, h-1, w-1, 0, 1);
  }
  if (gmu->boc2>-1) {
    gxwcol(c, gmu->boc2);
    gxwline(c, w-1, 0, 0, 0, 1);
    gxwline(c, 0, 0, 0, h-1, 1);
  }
  for (k=0; k<p->cnt; k++) {
    if (gmu->bfc>-1) {
      snprintf(buf, sizeof(buf), "%.*s", p->tlen[k], p->txt[k]);
      cairo_set_antialias(c, CAIRO_ANTIALIAS_DEFAULT);
      gxwmfont(c);
      gxwcol(c, gmu->bfc);
      cairo_move_to(c, p->h/4, (k+1)*p->h - p->h/3);
      cairo_show_text(c, buf);
      cairo_set_antialias(c, CAIRO_ANTIALIAS_NONE);
    }
    if (p->ptrs[k]>-1) gxwparrow(c, p, k, k==p->hi);
  }
  if (p->hi>-1) {
    j1 = p->hi*p->h + lw; j2 = (p->hi+1)*p->h - lw - 1;
    if (gmu->soc2>-1) {
      gxwcol(c, gmu->soc2);
      gxwline(c, lw, j1, lw, j2, lw);
      gxwline(c, lw, j1, w-lw-1, j1, lw);
    }
    if (gmu->soc1>-1) {
      gxwcol(c, gmu->soc1);
      gxwline(c, w-lw-1, j1, w-lw-1, j2, lw);
      gxwline(c, lw, j2, w-lw-1, j2, lw);
    }
  }
  cairo_destroy(c);
  cairo_surface_flush(img);
  c = cairo_create(surface);
  cairo_set_source_surface(c, img, p->imin, p->jmin);
  cairo_rectangle(c, p->imin, p->jmin, w, h);
  cairo_fill(c);
  cairo_destroy(c);
  cairo_surface_destroy(img);
  cairo_surface_flush(surface);
  XFlush(display);
}

/* Menu item under pixel i,j; -1 if outside the menu */

static gaint gxwpitem (struct gxwpop *p, gaint i, gaint j) {
gaint item;
  if (i<p->imin || i>=p->imax || j<p->jmin || j>=p->jmax) return (-1);
  item = (j - p->jmin)/p->h;
  if (item>=p->cnt) item = p->cnt-1;
  return (item);
}

/* Forget selections at levels deeper than lvl */

static void gxwpclr (gaint lvl) {
gaint k;
  for (k=lvl+1; k<4; k++) { dmnum[k] = -1; dmsel[k] = -1; }
}

/* Is a cascade menu available for an item? */

static gaint gxwpcasc (struct gxwpop *p, gaint item) {
gaint n;
  if (item<0 || dmlev>2) return (0);
  n = p->ptrs[item];
  if (n<0 || n>199) return (0);
  return (dmu[n].num==n);
}

/* Delay (ms) before a cascade menu opens, or closes when the pointer
   rests on another item of a parent menu. Avoids menus that open and
   close continuously while the pointer moves across them. */

#define GXWPDLY 300

static long gxwpms (void) {
struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec*1000L + tv.tv_usec/1000L);
}

/* Next pointer event into report, waiting at most until the time
   deadline (ms; -1 means forever). Returns 0 on timeout. */

static gaint gxwpnext (long deadline) {
long mask, msk2, ms;
fd_set fds;
struct timeval tv;
gaint fd;

  mask = ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | PointerMotionMask;
  msk2 = ButtonMotionMask | PointerMotionMask;
  fd = ConnectionNumber(display);
  while (1) {
    if (deadline<0) XMaskEvent(display, mask, &report);
    else if (!XCheckMaskEvent(display, mask, &report)) {
      ms = deadline - gxwpms();
      if (ms<=0) return (0);
      XFlush(display);
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      tv.tv_sec = ms/1000;
      tv.tv_usec = (ms%1000)*1000;
      select(fd+1, &fds, NULL, NULL, &tv);
      continue;
    }
    if (report.type==MotionNotify) {
      while (XCheckMaskEvent(display, msk2, &report));
    }
    return (1);
  }
}

/* Show a menu and follow the pointer until a selection is made.
   porig, iorig, jorig: preferred position. Returns -999 when done,
   or -99 if the pointer rested on another item of a parent menu. */

static gaint gxwpopdm (struct gdmu *gmu, gaint iobj, gaint porig, gaint iorig, gaint jorig) {
struct gxwpop *p;
cairo_t *c;
cairo_text_extents_t te;
cairo_font_extents_t fe;
char buf[512];
gaint lvl, k, w, maxw, cascf, i, j, item, itemold, dmflag, reuse, r, ii, moved;
gaint fast, pend, clos, got;
long pendt, clost, deadline;

  lvl = ++dmlev;
  p = &(gxwpops[lvl]);
  p->gmu = gmu;
  gxwpparse(p, lvl==0);
  dmnum[lvl] = gmu->num;
  dmsel[lvl] = -1;
  gxwpclr(lvl);
  if (p->cnt<1) { dmsel[lvl] = 0; dmlev--; return (-999); }

  /* Menu size */
  c = cairo_create(surface);
  gxwmfont(c);
  cairo_font_extents(c, &fe);
  p->h = 2*(gaint)(fe.ascent+0.5);
  maxw = 0;
  cascf = 0;
  for (k=0; k<p->cnt; k++) {
    snprintf(buf, sizeof(buf), "%.*s", p->tlen[k], p->txt[k]);
    cairo_text_extents(c, buf, &te);
    w = (gaint)(te.x_advance+0.5) + p->h/2;
    if (w>maxw) maxw = w;
    if (p->ptrs[k]>-1) cascf = 1;
  }
  if (cascf) {
    cairo_text_extents(c, "xxx", &te);
    maxw += (gaint)(te.x_advance+0.5);
  }
  cairo_destroy(c);
  p->lw = gxwlw(gmu->thk);
  p->isiz = maxw;
  p->jsiz = p->h * p->cnt;

  /* Position: below the header, or to the right of the parent item;
     moved if it does not fit in the window */
  p->imin = iorig; p->imax = p->imin + p->isiz;
  p->jmin = jorig; p->jmax = p->jmin + p->jsiz;
  if (p->imax>width) {
    if (lvl==0) p->imax = width;
    else p->imax = porig;
    p->imin = p->imax - p->isiz;
  }
  if (p->imin<0) { p->imin = 0; p->imax = p->isiz; }
  if (p->jmax>height && p->jmin<height) {
    /* Too tall: first reduce the item height (down to the text height) */
    k = (height - p->jmin)/p->cnt;
    if (k<(gaint)(fe.ascent+fe.descent+0.5)+4) k = (gaint)(fe.ascent+fe.descent+0.5)+4;
    if (k<p->h) {
      p->h = k;
      p->jsiz = p->h * p->cnt;
      p->jmax = p->jmin + p->jsiz;
    }
  }
  if (p->jmax>height) {
    p->jmax = height;
    p->jmin = p->jmax - p->jsiz;
  }
  if (p->jmin<0) { p->jmin = 0; p->jmax = p->jsiz; }

  /* Save what the menu covers, then draw it */
  cairo_surface_flush(surface);
  p->save = cairo_image_surface_create(CAIRO_FORMAT_RGB24, p->isiz, p->jsiz);
  c = cairo_create(p->save);
  cairo_set_source_surface(c, surface, -p->imin, -p->jmin);
  cairo_paint(c);
  cairo_destroy(c);
  p->hi = -1;
  gxwpdraw(p);

  /* Follow the pointer */
  itemold = -1;
  dmflag = -999;
  reuse = 0;
  fast = 0;
  pend = -1; pendt = 0;    /* cascade waiting to be opened, and when */
  clos = 0;  clost = 0;    /* this menu waiting to be closed, and when */
  moved = (lvl>0);   /* the release of the click opening the menu does not select */
  while (1) {
    if (reuse) got = 1;
    else {
      deadline = -1;
      if (pend>-1) deadline = pendt;
      if (clos && (deadline<0 || clost<deadline)) deadline = clost;
      got = gxwpnext(deadline);
    }
    reuse = 0;

    if (got) {
      if (report.type==MotionNotify) { i = report.xmotion.x; j = report.xmotion.y;
        if (abs(i-dmpi)>3 || abs(j-dmpj)>3) moved = 1; }
      else { i = report.xbutton.x; j = report.xbutton.y; }
      item = gxwpitem(p, i, j);
      if (item!=p->hi) { p->hi = item; gxwpdraw(p); }

      if (report.type==ButtonPress) {
        /* A click makes the selection (or closes the menu if outside) */
        pend = -1; clos = 0;
        if (gxwpcasc(p, item)) {
          dmcur[lvl] = item;
          r = gxwpopdm(&(dmu[p->ptrs[item]]), iobj, p->imin, p->imax, p->jmin + item*p->h);
          if (r==-99) { gxwpclr(lvl); itemold = item; reuse = 1; fast = 1; continue; }
          dmflag = r;
          if (dmsel[lvl+1]<1) item = -1;
        }
        itemold = item;
        break;
      }

      if (report.type==ButtonRelease) {
        if (item>-1 && moved) { itemold = item; break; }
        continue;   /* released outside, or without dragging: the menu stays open */
      }

      /* Pointer motion */
      if (item==-1) {
        /* Outside this menu: nothing happens, unless the pointer rests
           on another item of a parent menu (then this menu closes) */
        pend = -1;
        k = 0;
        for (ii=0; ii<lvl; ii++) {
          r = gxwpitem(&(gxwpops[ii]), i, j);
          if (r>-1 && r!=dmcur[ii]) k = 1;
        }
        if (!k) clos = 0;
        else if (!clos) { clos = 1; clost = gxwpms() + (fast ? 0 : GXWPDLY); }
      } else {
        clos = 0;
        if (!gxwpcasc(p, item)) pend = -1;
        else if (item!=itemold) { pend = item; pendt = gxwpms() + (fast ? 0 : GXWPDLY); }
      }
      itemold = item;
      fast = 0;
    }

    /* Timers */
    if (clos && gxwpms()>=clost) { dmflag = -99; itemold = -1; break; }
    if (pend>-1 && gxwpms()>=pendt) {
      /* The pointer rested on an item with a cascade menu: open it */
      item = pend;
      pend = -1;
      dmcur[lvl] = item;
      r = gxwpopdm(&(dmu[p->ptrs[item]]), iobj, p->imin, p->imax, p->jmin + item*p->h);
      if (r==-99) { gxwpclr(lvl); itemold = item; reuse = 1; fast = 1; continue; }
      dmflag = r;
      if (dmsel[lvl+1]<1) item = -1;
      itemold = item;
      break;
    }
  }

  /* Selection: 1-based item number; -1 for a cascade item without
     a selection in its sub-menu */
  dmsel[lvl] = itemold + 1;
  /* (at the deepest level there is no sub-menu, and no dmsel[lvl+1]) */
  if (itemold>-1 && p->ptrs[itemold]>-1 && (lvl>=3 || dmsel[lvl+1]<1)) dmsel[lvl] = -1;

  /* Remove the menu */
  c = cairo_create(surface);
  cairo_set_source_surface(c, p->save, p->imin, p->jmin);
  cairo_rectangle(c, p->imin, p->jmin, p->isiz, p->jsiz);
  cairo_fill(c);
  cairo_destroy(c);
  cairo_surface_destroy(p->save);
  p->save = NULL;
  cairo_surface_flush(surface);
  XFlush(display);
  dmlev--;
  return (dmflag);
}

/* Click occurred on a drop menu header. Returns -999 when done.
   Moving the pointer onto another drop menu header does nothing. */

gaint gxevdm (struct gevent *geve, int iobj, int ipos, int jpos) {
struct gdmu *gmu;
gaint i, ival;

  geve->type = 3;
  gmu = obj[iobj].iob.dmu;

  /* Header in 'selected' colors while the menu is open */
  gxwdmhdr(gmu, surface, 1);

  dmlev = -1;
  for (i=0; i<4; i++) { dmnum[i] = -1; dmsel[i] = -1; dmcur[i] = -1; }
  dmpi = ipos; dmpj = jpos;

  /* Follow the pointer also when no mouse button is held */
  XSelectInput(display, win, ButtonReleaseMask | ButtonPressMask | ButtonMotionMask |
               PointerMotionMask | ExposureMask | StructureNotifyMask);
  ival = gxwpopdm(gmu, iobj, obj[iobj].i1, obj[iobj].i1, obj[iobj].j2);
  XSelectInput(display, win, ButtonReleaseMask | ButtonPressMask | ButtonMotionMask |
               ExposureMask | StructureNotifyMask);
  while (XCheckMaskEvent(display, PointerMotionMask | ButtonMotionMask, &report));

  /* Header back in standard colors */
  gxwdmhdr(gmu, surface, 0);
  XFlush(display);

  for (i=0; i<4; i++) if (dmsel[i]<0) dmnum[i] = -1;
  for (i=0; i<4; i++) {
    geve->info[2*i]   = dmnum[i];
    geve->info[2*i+1] = dmsel[i];
  }
  return (ival);
}

/* ---------------------------- Dialog box ---------------------------- */

#define GXWDLEN 500

/* State of the dialog box being edited */
static struct {
  gaint imin, jmin, isiz, jsiz;  /* box position and size */
  gaint layout;                  /* 0: one line; 1: default box (prompt above);
                                    2: two lines (prompt / text) */
  gaint asc;                     /* font ascent */
  gaint j1, j2;                  /* edit field: top, height */
  gaint base;                    /* edit field baseline */
  gaint xtxt;                    /* x of the first displayed char of the field */
  gaint pc, fc, bc, oc, th;      /* colors, thickness */
  char prompt[GXWDLEN+2];
  char txt[GXWDLEN+2];
  gaint c1, c2;                  /* selection [c1,c2); caret at c1 when c1==c2 */
  gaint err;                     /* selection marks an error (numeric input) */
} gd;

static void gxwdfont (cairo_t *c, gaint oblique) {
  cairo_select_font_face(c, "Sans", oblique ? CAIRO_FONT_SLANT_OBLIQUE : CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(c, gxwfsz());
}

/* Width in pixels of the first n chars of a string */

static gadouble gxwdtw (cairo_t *c, const char *s, gaint n) {
char buf[GXWDLEN+GXWDLEN+4];
cairo_text_extents_t te;
  if (n<=0) return (0.0);
  snprintf(buf, sizeof(buf), "%.*s", n, s);
  cairo_text_extents(c, buf, &te);
  return (te.x_advance);
}

/* The string shown in the edit field: in the one-line layout the
   prompt precedes the editable text. *off gets the prompt length. */

static void gxwddisp (char *disp, gaint *off) {
  if (gd.layout==0 && gd.prompt[0]) {
    snprintf(disp, GXWDLEN+GXWDLEN+4, "%s%s", gd.prompt, gd.txt);
    *off = strlen(gd.prompt);
  } else {
    snprintf(disp, GXWDLEN+GXWDLEN+4, "%s", gd.txt);
    *off = 0;
  }
}

/* Draw the dialog box */

static void gxwddraw (void) {
cairo_t *c;
cairo_font_extents_t fe;
char disp[GXWDLEN+GXWDLEN+4];
gadouble tw, x1, x2, wp;
gaint off, x;

  c = cairo_create(surface);
  cairo_rectangle(c, gd.imin, gd.jmin, gd.isiz, gd.jsiz);
  cairo_clip(c);
  cairo_translate(c, gd.imin, gd.jmin);
  cairo_set_antialias(c, CAIRO_ANTIALIAS_NONE);

  /* Box */
  gxwcol(c, gd.bc);
  cairo_paint(c);
  gxwcol(c, gd.oc);
  cairo_set_line_width(c, 1.0);
  cairo_rectangle(c, 0.5, 0.5, gd.isiz-1, gd.jsiz-1);
  if (gd.th>5) cairo_rectangle(c, 1.5, 1.5, gd.isiz-3, gd.jsiz-3);
  cairo_stroke(c);
  cairo_set_antialias(c, CAIRO_ANTIALIAS_DEFAULT);

  /* Prompt above the field, with a shadow */
  if (gd.layout && gd.prompt[0]) {
    gxwdfont(c, 1);
    wp = gxwdtw(c, gd.prompt, strlen(gd.prompt));
    x = (gaint)((gd.isiz - wp)/2.0);
    gxwcol(c, 0);
    cairo_move_to(c, x, gd.layout==1 ? gd.asc*2 : gd.jsiz*3/7);
    cairo_show_text(c, gd.prompt);
    gxwcol(c, gd.pc);
    cairo_move_to(c, x-1, (gd.layout==1 ? gd.asc*2 : gd.jsiz*3/7) - 1);
    cairo_show_text(c, gd.prompt);
  }

  /* Edit field: text left-aligned after a prompt, centered otherwise;
     scrolled to keep the caret visible when it does not fit */
  gxwdfont(c, 0);
  cairo_font_extents(c, &fe);
  gxwddisp(disp, &off);
  tw = gxwdtw(c, disp, strlen(disp));
  if (tw + gd.asc < gd.isiz) {
    if (gd.layout==0 && gd.prompt[0]) gd.xtxt = gd.asc/2;
    else gd.xtxt = (gaint)((gd.isiz - tw)/2.0);
  } else {
    gd.xtxt = (gaint)(gd.isiz - tw - gd.asc/2);
    x1 = gxwdtw(c, disp, off+gd.c1);
    if (gd.xtxt + x1 < gd.asc/2) gd.xtxt = (gaint)(gd.asc/2 - x1);
  }
  cairo_save(c);
  cairo_rectangle(c, 2, gd.j1+1, gd.isiz-4, gd.j2-2);
  cairo_clip(c);
  gxwcol(c, gd.fc);
  cairo_move_to(c, gd.xtxt, gd.base);
  cairo_show_text(c, disp);
  x1 = gd.xtxt + gxwdtw(c, disp, off+gd.c1);
  if (gd.c1<gd.c2) {
    /* Selection: reversed colors (red background for an error) */
    x2 = gd.xtxt + gxwdtw(c, disp, off+gd.c2);
    cairo_rectangle(c, x1, gd.base - fe.ascent, x2-x1, fe.ascent + fe.descent);
    gxwcol(c, gd.err ? 2 : gd.fc);
    cairo_fill_preserve(c);
    cairo_clip(c);
    gxwcol(c, gd.bc);
    cairo_move_to(c, gd.xtxt, gd.base);
    cairo_show_text(c, disp);
  } else {
    /* Caret */
    cairo_set_antialias(c, CAIRO_ANTIALIAS_NONE);
    cairo_set_line_width(c, 1.0);
    cairo_move_to(c, (gaint)x1 + 0.5, gd.base - fe.ascent);
    cairo_line_to(c, (gaint)x1 + 0.5, gd.base + fe.descent);
    cairo_stroke(c);
  }
  cairo_restore(c);

  cairo_destroy(c);
  cairo_surface_flush(surface);
  XFlush(display);
}

/* Character boundary nearest to window pixel column i */

static gaint gxwdpos (gaint i) {
cairo_t *c;
char disp[GXWDLEN+GXWDLEN+4];
gaint off, k, n, best;
gadouble x, d, dbest;

  c = cairo_create(surface);
  gxwdfont(c, 0);
  gxwddisp(disp, &off);
  n = strlen(gd.txt);
  best = 0; dbest = 1.0e30;
  for (k=0; k<=n; k++) {
    x = gd.imin + gd.xtxt + gxwdtw(c, disp, off+k);
    d = x>i ? x-i : i-x;
    if (d<dbest) { dbest = d; best = k; }
  }
  cairo_destroy(c);
  return (best);
}

/* Replace the selection [c1,c2) with string s */

static void gxwdins (const char *s) {
gaint n, ls;
  n = strlen(gd.txt);
  ls = strlen(s);
  if (n - (gd.c2-gd.c1) + ls > GXWDLEN) return;
  memmove(gd.txt + gd.c1 + ls, gd.txt + gd.c2, n - gd.c2 + 1);   /* tail, with the '\0' */
  memcpy(gd.txt + gd.c1, s, ls);
  gd.c1 += ls;
  gd.c2 = gd.c1;
}

/* Check numeric input. Returns 1 if valid (fixing it up), 0 if not
   (the offending part is selected, or a '#' placeholder is inserted
   and selected where a value is missing). */

static gaint gxwdnum (void) {
char *t, *end;
gaint n, k, digit, dot;

  t = gd.txt;
  n = strlen(t);
  while (n>0 && t[n-1]==' ') t[--n] = '\0';
  for (k=0; k<n; k++) {
    if (!strchr("0123456789+-.eE", t[k])) {
      gd.c1 = k; gd.c2 = k+1;
      return (0);
    }
  }
  digit = 0;
  for (k=0; k<n && t[k]!='e' && t[k]!='E'; k++) if (t[k]>='0' && t[k]<='9') digit = 1;
  if (!digit) {
    /* no value: placeholder at the start */
    gd.c1 = 0; gd.c2 = 0;
    gxwdins("#");
    gd.c1 = 0;
    return (0);
  }
  strtod(t, &end);
  if (*end != '\0') {
    k = end - t;
    if ((t[k]=='e' || t[k]=='E') &&
        (t[k+1]=='\0' || ((t[k+1]=='+' || t[k+1]=='-') && t[k+2]=='\0'))) {
      /* missing exponent: placeholder at the end */
      gd.c1 = n; gd.c2 = n;
      gxwdins("#");
      gd.c1 = n;
      return (0);
    }
    gd.c1 = k; gd.c2 = k+1;
    return (0);
  }
  /* Insert a zero before a leading decimal point */
  k = (t[0]=='+' || t[0]=='-') ? 1 : 0;
  dot = (t[k]=='.');
  if (dot) { gd.c1 = k; gd.c2 = k; gxwdins("0"); }
  return (1);
}

static gaint gxwderr (Display *d, XErrorEvent *e) { return (0); }

/* Dialog box: show a prompt and let the user edit a string.
   The edited string, terminated by a newline, is returned (allocated
   with galloc). See 'query dialog' and 'set dialog'. */

char *gxdlg (struct gdlg *qry) {
cairo_t *c;
cairo_surface_t *save;
cairo_font_extents_t fe;
XErrorHandler oldh;
KeySym keysym;
Time lastdown;
char buff[16], *src, *rch;
gaint i, k, len, sep, twoline, cnt, done, anchor, drag, inbox;
gadouble w1, w2, w, hb;

  if (batch || surface==NULL) {
    printf("Error: dialog boxes do not work in batch mode\n");
    return (NULL);
  }

  /* Split the string into a prompt and an initial text. The first '|'
     or '/' separates them; a '/' puts the text on a second line. */
  src = qry->ch ? qry->ch : "";
  len = strlen(src);
  sep = -1;
  twoline = 0;
  for (i=0; i<len; i++) {
    if ((src[i]=='|' || src[i]=='/') && sep<0) sep = i;
    if (src[i]=='/' && i>0) twoline = 1;
  }
  gd.prompt[0] = '\0';
  if (sep>0) snprintf(gd.prompt, GXWDLEN, "%.*s ", sep, src);
  i = sep+1;
  while (i<len && (src[i]=='/' || src[i]==' ' || src[i]=='|')) i++;
  snprintf(gd.txt, GXWDLEN, "%s", src+i);

  if (qry->x<0 && qry->y<0 && qry->w<0 && qry->h<0) gd.layout = 1;
  else if (twoline) gd.layout = 2;
  else gd.layout = 0;
  if (gd.layout==1 && gd.prompt[0]=='\0') {
    strcpy(gd.prompt, gd.txt);   /* same size buffers */
    gd.txt[0] = '\0';
  }
  if (gd.layout) {   /* the prompt is shown on its own: trim it */
    k = strlen(gd.prompt);
    while (k>0 && gd.prompt[k-1]==' ') gd.prompt[--k] = '\0';
  }
  gd.pc = qry->pc>-1 ? qry->pc : 1;
  gd.fc = qry->fc>-1 ? qry->fc : 0;
  gd.bc = qry->bc>-1 ? qry->bc : 15;
  gd.oc = qry->oc>-1 ? qry->oc : 1;
  gd.th = qry->th;

  /* Box size and position (pixels) */
  c = cairo_create(surface);
  gxwdfont(c, 1);
  w1 = gxwdtw(c, gd.prompt, strlen(gd.prompt));
  gxwdfont(c, 0);
  w2 = gxwdtw(c, gd.txt, strlen(gd.txt));
  cairo_font_extents(c, &fe);
  cairo_destroy(c);
  gd.asc = (gaint)(fe.ascent+0.5);
  w = twoline ? (w1>w2 ? w1 : w2) : w1+w2;
  hb = twoline ? gd.asc*1.8 : gd.asc;

  if (qry->h==-1) {
    gd.jsiz = (gaint)(hb*5);
    if (gd.jsiz>height) gd.jsiz = height*3/4;
  } else if (qry->h==0) gd.jsiz = (gaint)(hb*1.8);
  else gd.jsiz = (gaint)(qry->h*yscl+0.5);
  if (qry->w==-1) gd.isiz = width*2/3;
  else if (qry->w==0) gd.isiz = (gaint)(w + hb*1.5);
  else gd.isiz = (gaint)(qry->w*xscl+0.5);
  if (gd.isiz<20) gd.isiz = 20;
  if (gd.jsiz<gd.asc+6) gd.jsiz = gd.asc+6;
  if (qry->x==-1) gd.imin = (width-gd.isiz)/2;
  else gd.imin = (gaint)(qry->x*xscl+0.5) - gd.isiz/2;
  if (qry->y==-1) gd.jmin = (height-gd.jsiz)/2;
  else gd.jmin = height - ((gaint)(qry->y*yscl+0.5) + gd.jsiz/2);

  /* Edit field */
  if (gd.layout==1) {
    gd.j1 = gd.asc*2 + 1;
    gd.j2 = gd.jsiz - gd.j1 - 1;
    gd.base = gd.asc*4;
  } else if (gd.layout==2) {
    gd.j1 = gd.jsiz/2;
    gd.j2 = gd.jsiz - gd.j1 - 1;
    gd.base = gd.jsiz*6/7;
  } else {
    gd.j1 = 1;
    gd.j2 = gd.jsiz - 2;
    gd.base = (gd.jsiz + gd.asc)/2 - 1;
  }
  gd.c1 = gd.c2 = strlen(gd.txt);
  gd.err = 0;

  /* Save what the box covers, then draw it */
  cairo_surface_flush(surface);
  save = cairo_image_surface_create(CAIRO_FORMAT_RGB24, gd.isiz, gd.jsiz);
  c = cairo_create(save);
  cairo_set_source_surface(c, surface, -gd.imin, -gd.jmin);
  cairo_paint(c);
  cairo_destroy(c);
  gxwddraw();

  /* Get the keyboard. Errors (e.g. window not viewable) are ignored. */
  XSelectInput(display, win, ButtonReleaseMask | ButtonPressMask | ButtonMotionMask |
               KeyPressMask | ExposureMask | StructureNotifyMask);
  XSync(display, False);
  oldh = XSetErrorHandler(gxwderr);
  XRaiseWindow(display, win);
  XSetInputFocus(display, win, RevertToParent, CurrentTime);
  XSync(display, False);
  XSetErrorHandler(oldh);

  /* Edit until Enter */
  done = 0;
  drag = 0;
  anchor = gd.c1;
  lastdown = 0;
  while (!done) {
    XMaskEvent(display, ButtonReleaseMask | ButtonPressMask | ButtonMotionMask |
               KeyPressMask | ExposureMask | StructureNotifyMask, &report);
    if (report.type==KeyPress) {
      cnt = XLookupString(&(report.xkey), buff, sizeof(buff)-1, &keysym, NULL);
      len = strlen(gd.txt);
      if (keysym==XK_Return || keysym==XK_KP_Enter || keysym==XK_Linefeed) {
        if (qry->nu==1) {
          gd.err = 0;
          if (gxwdnum()) done = 1;
          else gd.err = 1;
        } else done = 1;
      } else if (keysym==XK_BackSpace) {
        if (gd.c1==gd.c2 && gd.c1>0) gd.c1--;
        gxwdins("");
        gd.err = 0;
      } else if (keysym==XK_Delete || keysym==XK_KP_Delete) {
        if (gd.c1==gd.c2 && gd.c2<len) gd.c2++;
        gxwdins("");
        gd.err = 0;
      } else if (keysym==XK_Left || keysym==XK_KP_Left) {
        if (gd.c1==gd.c2 && gd.c1>0) gd.c1--;
        gd.c2 = gd.c1;
        gd.err = 0;
      } else if (keysym==XK_Right || keysym==XK_KP_Right) {
        if (gd.c1==gd.c2 && gd.c2<len) gd.c2++;
        gd.c1 = gd.c2;
        gd.err = 0;
      } else if (keysym==XK_Home || keysym==XK_KP_Home) {
        gd.c1 = gd.c2 = 0;
        gd.err = 0;
      } else if (keysym==XK_End || keysym==XK_KP_End) {
        gd.c1 = gd.c2 = len;
        gd.err = 0;
      } else if (keysym==XK_Up || keysym==XK_KP_Up) {          /* extend selection */
        if (gd.c2<len) gd.c2++;
      } else if (keysym==XK_Down || keysym==XK_KP_Down) {      /* shrink selection */
        if (gd.c2>gd.c1) gd.c2--;
      } else if (cnt==1 && keysym>=XK_space && keysym<=XK_asciitilde) {
        buff[1] = '\0';
        if (qry->nu==1 && !strchr("0123456789+-.eE", buff[0])) continue;
        gxwdins(buff);
        gd.err = 0;
      } else continue;
      anchor = gd.c1;
    } else if (report.type==ButtonPress) {
      i = report.xbutton.x; k = report.xbutton.y;
      inbox = (i>=gd.imin && i<gd.imin+gd.isiz && k>=gd.jmin && k<gd.jmin+gd.jsiz);
      if (!inbox) continue;
      gd.err = 0;
      if (lastdown && report.xbutton.time >= lastdown &&
          report.xbutton.time - lastdown < 300) {     /* double click: select all */
        gd.c1 = 0;
        gd.c2 = strlen(gd.txt);
        drag = 0;
      } else {
        gd.c1 = gd.c2 = anchor = gxwdpos(i);
        drag = 1;
      }
      lastdown = report.xbutton.time;
    } else if (report.type==MotionNotify) {
      if (!drag) continue;
      while (XCheckMaskEvent(display, ButtonMotionMask, &report));
      k = gxwdpos(report.xmotion.x);
      gd.c1 = k<anchor ? k : anchor;
      gd.c2 = k<anchor ? anchor : k;
    } else if (report.type==ButtonRelease) {
      drag = 0;
      continue;
    } else if (report.type!=Expose) continue;
    gxwddraw();
  }

  /* Restore the screen */
  XSelectInput(display, win, ButtonReleaseMask | ButtonPressMask | ButtonMotionMask |
               ExposureMask | StructureNotifyMask);
  c = cairo_create(surface);
  cairo_set_source_surface(c, save, gd.imin, gd.jmin);
  cairo_rectangle(c, gd.imin, gd.jmin, gd.isiz, gd.jsiz);
  cairo_fill(c);
  cairo_destroy(c);
  cairo_surface_destroy(save);
  cairo_surface_flush(surface);
  XFlush(display);

  /* Result: numeric input loses its '+' signs */
  len = strlen(gd.txt);
  rch = (char *)galloc(len+2, "dlgres");
  if (rch==NULL) {
    printf ("Memory Allocation Error: Dialog Box\n");
    return (NULL);
  }
  k = 0;
  for (i=0; i<len; i++) {
    if (qry->nu==1 && gd.txt[i]=='+') continue;
    rch[k++] = gd.txt[i];
  }
  rch[k++] = '\n';
  rch[k] = '\0';
  return (rch);
}


/* Redraw all widgets of a layer and rebuild its list of displayed objects.
   flag=0 foreground, flag=1 background. */

void gxrdrw (int flag) {
struct gbtn *pb;
struct grbb *pr;
struct gdmu *pm;
cairo_surface_t *sfc;
gaint i;

  if (flag && !dblmode) return;
  pb = flag ? btn2 : btn;
  pr = flag ? rbb2 : rbb;
  pm = flag ? dmu2 : dmu;
  sfc = gxwsfc(flag);
  gxwclr(flag);
  for (i=0; i<256; i++) {
    if (pb[i].num>-1) {
      gxwbtn(&(pb[i]), sfc);
      gxwregb(&(pb[i]), flag);
    }
  }
  for (i=0; i<32; i++) {
    if (pr[i].num>-1) gxwregr(&(pr[i]), flag);
  }
  for (i=0; i<200; i++) {
    if (pm[i].num>-1 && !pm[i].casc) {
      gxwdmu(&(pm[i]), sfc);
      gxwregm(&(pm[i]), flag);
    }
  }
}

/* Reset all widgets; release memory as appropriate. */
/* flag = 0 resets foreground, flag = 1 resets both,
   flag = 2 resets background only; for after swapping */

void gxrswd (gaint flag) {
gaint i;

  if (flag!=2) {
    for (i=0; i<256; i++) {
      if (btn[i].num>-1 && btn[i].ch!=NULL) gree(btn[i].ch,"f501");
      btn[i].num = -1;
      btn[i].ch = NULL;
    }
    for (i=0; i<200; i++) {
      if (dmu[i].num>-1 && dmu[i].ch!=NULL) gree(dmu[i].ch,"f502");
      dmu[i].num = -1;
      dmu[i].ch = NULL;
    }
    for (i=0; i<32; i++) rbb[i].num = -1;
  }

  if (flag) {
    for (i=0; i<256; i++) {
      if (flag!=2) {
        if (btn2[i].num>-1 && btn2[i].ch!=NULL) gree(btn2[i].ch,"f503");
      }
      btn2[i].num = -1;
      btn2[i].ch = NULL;
    }
    for (i=0; i<200; i++) {
      if (flag!=2) {
        if (dmu2[i].num>-1 && dmu2[i].ch!=NULL) gree(dmu2[i].ch,"f504");
      }
      dmu2[i].num = -1;
      dmu2[i].ch = NULL;
    }
    for (i=0; i<32; i++) rbb2[i].num = -1;
  }
}

/* Copy all widgets from background to foreground during swap in double buffer mode */

void gxcpwd (void) {
struct grbb *grb;
struct gbtn *gbn;
struct gdmu *gmu;
gaint i;

  for (i=0; i<256; i++) if (btn2[i].num>-1) btn[i] = btn2[i];
  for (i=0; i<200; i++) if (dmu2[i].num>-1) dmu[i] = dmu2[i];
  for (i=0; i<32; i++)  if (rbb2[i].num>-1) rbb[i] = rbb2[i];

  /* Rebuild list of currently displayed items */
  gxwclr(0);
  obnum = obnum2;
  for (i=0; i<obnum; i++) {
    obj[i] = obj2[i];
    if (obj[i].type==1) {
      gbn = obj[i].iob.btn;
      obj[i].iob.btn = &(btn[gbn->num]);
    } else if (obj[i].type==2) {
      grb = obj[i].iob.rbb;
      obj[i].iob.rbb = &(rbb[grb->num]);
    } else if (obj[i].type==3) {
      gmu = obj[i].iob.dmu;
      obj[i].iob.dmu = &(dmu[gmu->num]);
    }
  }
  gxwclr(1);
}

/* Reset a particular widget, given widget type and number */

void gxrs1wd (int wdtyp, int wdnum) {
struct grbb *grb;
struct gbtn *gbn;
struct gdmu *gmu;
gaint ii,jj=0;

  if (wdtyp<1 || wdtyp>3) return;
  if (wdtyp==1 && (wdnum<0 || wdnum>255)) return;
  if (wdtyp==2 && (wdnum<0 || wdnum>31)) return;
  if (wdtyp==3 && (wdnum<0 || wdnum>199)) return;

  /* Remove this widget from the list of displayed items */
  ii = 0;
  while (ii<512 && obj[ii].type>-1) {
    if (obj[ii].type!=0 && obj[ii].type==wdtyp) {
      if (obj[ii].type==1) {
        gbn = obj[ii].iob.btn;
        jj = gbn->num;
      } else if (obj[ii].type==2) {
        grb = obj[ii].iob.rbb;
        jj = grb->num;
      } else if (obj[ii].type==3) {
        gmu = obj[ii].iob.dmu;
        jj = gmu->num;
      }
      if (jj==wdnum) {
        obj[ii].type = 0;   /* widget is now ignored */
        break;
      }
    }
    ii++;
  }

  /* Remove this widget from the widget array */
  if (wdtyp==1) {
    if (btn[wdnum].num>-1 && btn[wdnum].ch!=NULL) gree(btn[wdnum].ch,"f505");
    btn[wdnum].num = -1;
    btn[wdnum].ch = NULL;
  } else if (wdtyp==2) {
    rbb[wdnum].num = -1;
  } else if (wdtyp==3) {
    if (dmu[wdnum].num>-1 && dmu[wdnum].ch!=NULL) gree(dmu[wdnum].ch,"f506");
    dmu[wdnum].num = -1;
    dmu[wdnum].ch = NULL;
  }
}

/* Click occurred over a button object. */

void gxevbn (struct gevent *geve, int iobj) {
struct gbtn *gbb;
cairo_t *c;
gaint jj,c1,c2;

  geve->type = 1;
  gbb = obj[iobj].iob.btn;
  jj = gbb->num;
  geve->info[0] = jj;
  gbb = &(btn[jj]);
  if (gbb->state) {
    c1 = gbb->otc1; c2 = gbb->otc2;
    geve->info[1] = 0;
  } else {
    c1 = gbb->oc1; c2 = gbb->oc2;
    geve->info[1] = 1;
  }

  /* Redraw button outline as pressed, if appropriate
     (ie, if the outline colors are different) */
  if ( !(gbb->state && gbb->oc1==gbb->otc2 && gbb->oc2==gbb->otc1)) {
    if (c2>-1 && c1>-1) {
      c = cairo_create(surface);
      cairo_set_antialias(c, CAIRO_ANTIALIAS_NONE);
      gxwbout(c, gbb, c2, c1);
      cairo_destroy(c);
      cairo_surface_flush(surface);
      XFlush(display);
    }
  }

  /* Wait for button release, then redraw the button with its new state */
  while (1) {
    XMaskEvent(display, ButtonReleaseMask | ButtonMotionMask, &report);
    if (report.type == ButtonRelease) break;
  }
  gbb->state = gbb->state ? 0 : 1;
  gxdpbn (jj, NULL, 1, 1, -1);
}

/* Restore a rectangular area of the window from a snapshot */

static void gxwrest (cairo_t *c, cairo_surface_t *snap, gaint i1, gaint j1, gaint i2, gaint j2) {
gaint t;
  if (i1>i2) { t=i1; i1=i2; i2=t; }
  if (j1>j2) { t=j1; j1=j2; j2=t; }
  cairo_save(c);
  cairo_rectangle(c, i1-2, j1-2, i2-i1+5, j2-j1+5);
  cairo_clip(c);
  cairo_set_source_surface(c, snap, 0, 0);
  cairo_paint(c);
  cairo_restore(c);
}

/* Draw the rubber band: a white line overlaid by a black dashed line,
   visible on any background */

static void gxwband (cairo_t *c, gaint typ, gaint i1, gaint j1, gaint i2, gaint j2) {
static const double dash[2] = {4.0, 4.0};
gaint k;
  for (k=0; k<2; k++) {
    cairo_save(c);
    if (k==0) cairo_set_source_rgb(c, 1.0, 1.0, 1.0);
    else {
      cairo_set_source_rgb(c, 0.0, 0.0, 0.0);
      cairo_set_dash(c, dash, 2, 0.0);
    }
    cairo_set_line_width(c, 1.0);
    if (typ==1) {
      cairo_rectangle(c, (i1<i2?i1:i2)+0.5, (j1<j2?j1:j2)+0.5, abs(i2-i1), abs(j2-j1));
    } else {
      cairo_move_to(c, i1+0.5, j1+0.5);
      cairo_line_to(c, i2+0.5, j2+0.5);
    }
    cairo_stroke(c);
    cairo_restore(c);
  }
}

/* Click occurred in a rubber-banded region. */

void gxevrb (struct gevent *geve, int iobj, int i, int j) {
struct grbb *grb;
cairo_surface_t *snap;
cairo_t *c, *sc;
gaint i2,j2,i2o,j2o,xoflg,typ;
gaint ilo,ihi,jlo,jhi;

  geve->type = 2;
  grb = obj[iobj].iob.rbb;
  geve->info[0] = grb->num;
  typ = grb->type;
  ilo = obj[iobj].i1;  ihi = obj[iobj].i2;
  jlo = obj[iobj].j1;  jhi = obj[iobj].j2;

  /* Take a snapshot of the window, used to erase the band while it moves */
  cairo_surface_flush(surface);
  snap = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
  sc = cairo_create(snap);
  cairo_set_source_surface(sc, surface, 0, 0);
  cairo_paint(sc);
  cairo_destroy(sc);
  c = cairo_create(surface);
  cairo_set_antialias(c, CAIRO_ANTIALIAS_NONE);

  /* Loop on button motion, waiting for button release */
  xoflg = 0;
  i2o = j2o = 0;
  while (1) {
    XMaskEvent(display, ButtonReleaseMask|ButtonMotionMask, &report);
    if (report.type!=MotionNotify) break;
    /* skip to the most recent motion event */
    while (XCheckMaskEvent(display, ButtonMotionMask, &report));
    i2 = report.xmotion.x;
    j2 = report.xmotion.y;
    if (i2<ilo) i2 = ilo;
    if (i2>ihi) i2 = ihi;
    if (j2<jlo) j2 = jlo;
    if (j2>jhi) j2 = jhi;
    if (xoflg) gxwrest(c, snap, i, j, i2o, j2o);
    gxwband(c, typ, i, j, i2, j2);
    cairo_surface_flush(surface);
    XFlush(display);
    i2o = i2; j2o = j2; xoflg = 1;
  }
  if (xoflg) gxwrest(c, snap, i, j, i2o, j2o);
  cairo_destroy(c);
  cairo_surface_flush(surface);
  cairo_surface_destroy(snap);
  XFlush(display);

  /* Position of the button release, limited to the region */
  i2 = report.xbutton.x; j2 = report.xbutton.y;
  if (i2<ilo) i2 = ilo;
  if (i2>ihi) i2 = ihi;
  if (j2<jlo) j2 = jlo;
  if (j2>jhi) j2 = jhi;
  geve->rinfo[0] = xsize*((gadouble)i2)/width;
  geve->rinfo[1] = ysize - ysize*((gadouble)j2)/height;
}



/* tell x interface that we are in batch mode */

void gxdbat (void) {
  batch = 1;
}

/* Routine to specify user-defined geometry string for X display window. 
   Must be called before gxdbgn to have any affect */

void gxdgeo (char *arg) {
  ugeom = arg;
}

void gxdbgn (gadouble xsz, gadouble ysz) {
gaint dw,dh,flag,ipos,jpos,border;

  evbase = NULL;
  excnt = 0;
  gxwinit();
  args[0] = name;
  args[1] = NULL;
  argv = args;
  argc = 1;
  xsize = xsz;
  ysize = ysz;
  border = 4;

  /* Connect to X server */
  if ((display=XOpenDisplay(display_name)) == NULL) {
    printf("Error in GXSTRT: Unable to connect to X server\n");
    exit(-1);
  }

  /* Get screen size from display structure macro, then figure out proper window size */
  snum = DefaultScreen(display);
  sptr = DefaultScreenOfDisplay(display);
  xvis = DefaultVisual(display,snum);
  bsflg = 0;
  if (DoesBackingStore(sptr)) bsflg = 1;
  dw = DisplayWidth(display, snum);
  dh = DisplayHeight(display, snum);
  depth = DefaultDepth(display, snum);
  ipos = 0;
  jpos = 0;

  /* window sizes are scaled according to the height of display */
  if ( xsize >= ysize ) {           /* landscape */
    if (xsize==11.0 && ysize==8.5) {    /* preserve old default */
      dh = (gaint)((gadouble)dh*0.6);        
      dw = (gaint)((gadouble)(dh)*xsz/ysz);  
    } else {
      dw = (gaint)((gadouble)(dw)*0.6);      
      dh = (gaint)((gadouble)dw*ysz/xsz);
    }
  } else {                          /* portrait */
    dh = (gaint)((gadouble)dh*0.9);
    dw = (gaint)((gadouble)(dh)*xsz/ysz);
  } 

  xscl = (gadouble)(dw)/xsize;
  yscl = (gadouble)(dh)/ysize;

  size_hints.flags = PPosition | PSize ;
  if (ugeom) {
    XGeometry (display,snum,ugeom,dgeom,4,1,1,0,0,&ipos,&jpos,&dw,&dh);
    size_hints.flags = USPosition | USSize ;
  }
  size_hints.x = ipos;
  size_hints.y = jpos;
  size_hints.width = dw;
  size_hints.height = dh;
  width = dw ; 
  height = dh ;
  xscl = (gadouble) (dw) / xsize ;
  yscl = (gadouble) (dh) / ysize ;

  /* Create window */
  win = XCreateSimpleWindow(display,RootWindow(display,snum),ipos,jpos,dw,dh,border,
			    WhitePixel(display,snum),BlackPixel(display,snum));

  /* Set up icon pixmap */
  icon_pixmap = XCreateBitmapFromData(display,win,(char*)icon_bitmap_bits,
				      icon_bitmap_width,icon_bitmap_height);

  /* Set standard properties */
  XSetStandardProperties(display,win,window_name,icon_name,icon_pixmap,argv,argc,&size_hints);

  /* Select event types */
  XSelectInput(display, win, ButtonReleaseMask | ButtonPressMask | ButtonMotionMask | ExposureMask | StructureNotifyMask);

  /* Display Window */
  XMapWindow(display,win);

  /* We now have to wait for the expose event that indicates our
     window is up.  Also handle any resizes so we get the scaling
     right in case it gets changed right away.  We will check again
     for resizes during frame operations */
  flag = 1;
  while (flag)  {
    XNextEvent(display,&report);
    switch  (report.type) {
    case Expose:
      if (report.xexpose.count != 0) break;
      else flag = 0;
      break;
    case ConfigureNotify:
      width = report.xconfigure.width;
      height = report.xconfigure.height;
      xscl = (gadouble)(width)/xsize;
      yscl = (gadouble)(height)/ysize;    
      break;
    }
  }

  /* Now ready for drawing */
  drwbl = win;   /* Initial drawable is the visible window */
  dblmode = 0;   /* Initially no double buffering mode     */

  xsetw.backing_store = Always;
  XChangeWindowAttributes (display,win,CWBackingStore,&xsetw);

  /* Set up the cairo Xlib surface and initialize gxC */
  surface = cairo_xlib_surface_create (display,drwbl,xvis,dw,dh);
  gxCbgn(surface,xsz,ysz,dw,dh);

}

void gxdend (void) {
  gxCend();
  cairo_surface_finish (surface);
  cairo_surface_destroy (surface);
  XCloseDisplay(display);
}

/* Frame action.  Values for action are:
      0 -- new frame (clear display), wait before clearing.
      1 -- new frame, no wait.
      2 -- New frame in double buffer mode.
      7 -- new frame, but just clear graphics.  Do not clear
           event queue; redraw widgets.
      8 -- clear only the event queue.
      9 -- flush the X request buffer 
*/

void gxdfrm (gaint iact) {
struct gevent *geve,*geve2;
  if (iact==9) {
    gxdeve(0);
    gxdXflush();
    return;
  }
  if (iact==0 || iact==1 || iact==7) {
    gxCfrm();
    gxwclr(0);
  }

  /* Flush X event queue. If iact is 7, keep the event info, otherwise discard it. */
  if (iact==7) gxdeve(0);
  else {
    while (XCheckMaskEvent(display, ButtonReleaseMask | ButtonPressMask |
             ButtonMotionMask | KeyPressMask |
             ExposureMask | StructureNotifyMask, &report)) {
      if (report.type==ConfigureNotify) {
        if(width!=report.xconfigure.width||height!=report.xconfigure.height){
          width = report.xconfigure.width;
          height = report.xconfigure.height;
          xscl = (gadouble)(width)/xsize;
          yscl = (gadouble)(height)/ysize;
          if (iact==8) gxdrdw();
        }
      }
    }

    /* Flush GrADS event queue */
    geve = evbase;
    while (geve) {
      geve2 = geve->forw;
      free (geve);
      geve = geve2;
    }
    evbase = NULL;
  }

  /* Reset all widgets if appropriate. */
  if (iact<7 && iact!=2) gxrswd(0);

  /* Redraw all widgets if appropriate. */
  if (iact==7) {
    gxrdrw(0);
    gxdXflush();
  }
}

/* Examine X event queue.  Flag tells us if we should wait for an
   event.  Any GrADS events (mouse-button presses) are queued.
   If flag is 2, wait for any event, not just a mouse event.  */

void gxdeve (gaint flag) {
struct gevent *geve, *geve2;
gaint i,j,ii,rc,wflg,button,eflg,rdrflg,idm;

  if (flag && evbase) flag = 0;   /* Don't wait if an event stacked */
  wflg = flag;
  eflg = 0;
  rdrflg = 0;
  while (1) {
    if (wflg && !rdrflg) {
      XMaskEvent(display, ButtonReleaseMask | ButtonPressMask |
          ButtonMotionMask | ExposureMask | StructureNotifyMask, &report);
      rc = 1;
    } else {
      rc = XCheckMaskEvent(display, ButtonReleaseMask | ButtonPressMask |
          ButtonMotionMask | ExposureMask | StructureNotifyMask, &report);
    }
    if (!rc && rdrflg) {
      gxdrdw(); 
      rdrflg = 0; 
      continue;
    }
    if (!rc) break;

    switch  (report.type) {
    case ButtonRelease:
      break;
    case Expose:
      if (excnt>0) excnt--;
      else if (!bsflg) rdrflg = 1;
      break;
    case ButtonPress:
      geve = (struct gevent *)malloc(sizeof(struct gevent));
      if (geve==NULL) {
        printf ("Memory allocation error in event queue!!!!!!\n");
        eflg = 1;
        break;
      }
      if (evbase==NULL) evbase = geve;
      else {
        geve2 = evbase;
        while (geve2->forw) geve2 = geve2->forw;
        geve2->forw = geve;
      }
      geve->forw = NULL;
      i = report.xbutton.x;
      j = report.xbutton.y;
      button = report.xbutton.button;
      if (button==Button1) button=1;
      else if (button==Button2) button=2;
      else if (button==Button3) button=3;
      else if (button==Button4) button=4;
      else if (button==Button5) button=5;
      geve->mbtn = button;
      geve->x = xsize*((gadouble)i)/width;
      geve->y = ysize - ysize*((gadouble)j)/height;
      geve->type = 0;
      /* Scan to see if point-click event was on one of our
         widgets.  Handling depends on what is clicked on. */
      ii = 0;
      while (ii<512 && obj[ii].type>-1) {
        if (obj[ii].type!=0 && i>obj[ii].i1 && i<obj[ii].i2 &&
                               j>obj[ii].j1 && j<obj[ii].j2) {
          if (obj[ii].mb < 0 || obj[ii].mb == button) {
            if (obj[ii].type==1) gxevbn(geve,ii);
            else if (obj[ii].type==2) gxevrb(geve,ii,i,j);
            else if (obj[ii].type==3) {
              idm = ii;
              while (idm != -999) idm = gxevdm(geve,idm,i,j);
            }
            break;
          }
        }
        ii++;
      }
      wflg = 0;                  /* Check for more events, but don't
                                    wait if there aren't any. */
      break;
    case ConfigureNotify:
      if(width!=report.xconfigure.width||height!=report.xconfigure.height){
        width = report.xconfigure.width;
        height = report.xconfigure.height;
        xscl = (gadouble)(width)/xsize;
        yscl = (gadouble)(height)/ysize;
        rdrflg = 1;
        if (flag==2) wflg = 0;
      }
      break;
    }
    if (eflg) break;
  }
  if (rdrflg) {
    gxdrdw();
  }
}

/* Return info on mouse button press event.  Wait if requested. */

void gxdbtn (gaint flag, gadouble *xpos, gadouble *ypos,
	     gaint *mbtn, gaint *type, gaint *info, gadouble *rinfo) {
struct gevent *geve;
gaint i;

  if (batch) {
    *xpos = -999.9;
    *ypos = -999.9;
    return;
  }
  if (flag==3) {
    /* Current pointer position, no button press needed.
       mbtn is the button being held down (0 if none), type is 0.
       Pending events are handled first (so that the window size is current);
       any button press stays in the queue for a later request. */
    Window qroot,qchild,qtop,qpar,qr2,qover,*qkids;
    int qrx,qry,qwx,qwy,qi,qj;
    unsigned int qmask,qm2,qnk;
    gxdeve(0);
    if (!XQueryPointer(display,win,&qroot,&qchild,&qrx,&qry,&qwx,&qwy,&qmask)) {
      *xpos = -999.9;        /* pointer is on another screen */
      *ypos = -999.9;
      *mbtn = -1;
      *type = -1;
      return;
    }
    /* The pointer must also be over our own top-level window: over another
       window, or (under XWayland) over anything that is not an X window, the
       server still reports the last position seen inside the window. */
    qtop = win;
    while (XQueryTree(display,qtop,&qr2,&qpar,&qkids,&qnk)) {
      if (qkids) XFree(qkids);
      if (qpar==qr2 || qpar==None) break;
      qtop = qpar;
    }
    qover = None;
    XQueryPointer(display,qroot,&qr2,&qover,&qrx,&qry,&qi,&qj,&qm2);
    if (qover!=qtop || qwx<0 || qwx>=width || qwy<0 || qwy>=height) {
      *xpos = -999.9;        /* pointer is outside the window */
      *ypos = -999.9;
      *mbtn = -1;
      *type = -1;
      return;
    }
    *xpos = xsize*((gadouble)qwx)/width;
    *ypos = ysize - ysize*((gadouble)qwy)/height;
    if      (qmask & Button1Mask) *mbtn = 1;
    else if (qmask & Button2Mask) *mbtn = 2;
    else if (qmask & Button3Mask) *mbtn = 3;
    else *mbtn = 0;
    *type = 0;
    return;
  }
  gxdeve(flag);
  if (evbase==NULL) {
    *xpos = -999.9;
    *ypos = -999.9;
    *mbtn = -1;
    *type = -1;
  } else {
    geve = evbase;
    *xpos = geve->x;
    *ypos = geve->y;
    *mbtn = geve->mbtn;
    *type = geve->type;
    for (i=0; i<10; i++) *(info+i) = geve->info[i];
    for (i=0; i<4; i++) *(rinfo+i) = geve->rinfo[i];
    evbase = geve->forw;       /* Take event off queue */
    free(geve);
  }

}

gaint gxdacol (gaint clr, gaint red, gaint green, gaint blue, gaint alpha) { 
  /* define a new color -- this is a no-op for cairo */
  return(0);
}

void gxdcol (gaint clr) {                    /* set color to new value */
  gxCcol(clr);
}

void gxdwid (gaint wid){                     /* Set line width */
  gxCwid(wid);
}

void gxdmov (gadouble x, gadouble y){        /* Move to x,y   */
  gxCmov(x,y);
}

void gxddrw (gadouble x, gadouble y) {       /* Draw to x,y */
  gxCdrw (x,y);
  if (QLength(display)&&rstate) gxdeve(0);
}

void gxdrec (gadouble x1, gadouble x2, gadouble y1, gadouble y2) {   /* draw a filled rectangle */
  gxCrec(x1,x2,y1,y2);
  if (QLength(display)&&rstate) gxdeve(0);
}
 
void gxdcirc (gadouble x, gadouble y, gadouble r, gaint flg) {
  gxCcirc(x,y,r,flg);
  if (QLength(display)&&rstate) gxdeve(0);
}

void gxddbl (void) {                         /* turn on double buffer mode */
  XSync(display, 0);
  gxCfrm();  /* clear the foreground */
  /* create a new background surface, set it to be the current drawable */
  surface2 = cairo_surface_create_similar (surface, CAIRO_CONTENT_COLOR_ALPHA, width, height);
  cairo_xlib_surface_set_drawable(surface2,drwbl,width,height);
  gxCsfc(surface2);   /* set the cairo graphics context to the new background surface */
  drwbl2 = cairo_xlib_surface_get_drawable(surface2); 
  drwbl = drwbl2;
  gxCfrm();  /* clear the new background surface */
  dblmode = 1; 
  gxrswd(1);  /* Reset all widgets */
  gxwclr(0); gxwclr(1);
  return;
}

void gxdswp (void) {                        /* swap command -- copy backgroud to the foreground */
  if (dblmode) {
     /* this will draw the background (surface2) onto the foreground (surface) and
	reset the cairo context to the background surface  */
    gxCswp(surface,surface2);
    gxCfrm();     /* clear the background */
   }
  gxrswd(0);
  gxcpwd();
  gxrswd(2);
  return;
}

void gxdsgl (void) {                         /* turn off double buffer mode */
  if (dblmode) {
    gxrswd(1);
    gxwclr(0); gxwclr(1);
    /* destroy the background surface */
     cairo_surface_destroy(surface2);
     /* set the window as the current drawable */
     drwbl = win;
     cairo_xlib_surface_set_drawable(surface,drwbl,width,height);
     gxCsfc(surface);   /* this will set the cairo graphics context to the foreground surface */
  }
  dblmode = 0;
  return;
}

void gxdfil (gadouble *xy, gaint n) {        /* draw a filled polygon */
  gxCfil (xy,n);
  if (QLength(display)&&rstate) gxdeve(0);
}

void gxdxsz (gaint xx, gaint yy) {           /* resize the X window */
  if (batch) return;
  if (xx==width && yy==height) return;
  XResizeWindow (display, win, xx, yy);
  gxdeve(2);
}

void gxdrdw (void) {                        /* Redraw when user resizes window */
  rstate = 0;
  cairo_xlib_surface_set_size(surface, width, height);
  gxCrsiz (width, height);
  gxhdrw(0,0);
  gxrdrw(0);
  if (dblmode) gxrdrw(1);
  rstate = 1;
}

/* Screen save and show operations. 'screen save N' copies the displayed
   window; 'screen show N' paints it on the current drawing surface
   (the background buffer in double buffer mode), scaled if the window
   has been resized in the meantime. */

static cairo_surface_t *scrsav[200];
static gaint scrinit = 0;

static gaint gxwscrok (int n) {
gaint i;
  if (batch) {
    printf("Error: the screen command does not work in batch mode\n");
    return (0);
  }
  if (!scrinit) { for (i=0; i<200; i++) scrsav[i] = NULL; scrinit = 1; }
  return (n>=0 && n<200 && surface!=NULL);
}

void gxdssv (int frame) {
cairo_t *c;
  if (!gxwscrok(frame)) return;
  gxCflush(1);                   /* complete pending drawing */
  cairo_surface_flush(surface);
  if (scrsav[frame]) cairo_surface_destroy(scrsav[frame]);
  scrsav[frame] = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
  if (cairo_surface_status(scrsav[frame]) != CAIRO_STATUS_SUCCESS) {
    printf ("Error allocating memory for screen save operation\n");
    cairo_surface_destroy(scrsav[frame]);
    scrsav[frame] = NULL;
    return;
  }
  c = cairo_create(scrsav[frame]);
  cairo_set_source_surface(c, surface, 0, 0);
  cairo_paint(c);
  cairo_destroy(c);
}

void gxdssh (int cnt) {
cairo_surface_t *sfc;
cairo_t *c;
gaint sw, sh;
  if (!gxwscrok(cnt) || scrsav[cnt]==NULL) return;
  gxCflush(1);
  sfc = gxwsfc(1);
  sw = cairo_image_surface_get_width(scrsav[cnt]);
  sh = cairo_image_surface_get_height(scrsav[cnt]);
  c = cairo_create(sfc);
  if (sw!=width || sh!=height) cairo_scale(c, (double)width/sw, (double)height/sh);
  cairo_set_source_surface(c, scrsav[cnt], 0, 0);
  cairo_paint(c);
  cairo_destroy(c);
  cairo_surface_flush(sfc);
  XFlush(display);
}

void gxdsfr (int frame) {
  if (!gxwscrok(frame)) return;
  if (scrsav[frame]) {
    cairo_surface_destroy(scrsav[frame]);
    scrsav[frame] = NULL;
  }
}

/* Routine to install stipple pixmaps, a no-op */

void gxdptn (int typ, int den, int ang) {
}

/* Gives current window information */

gaint win_data (struct xinfo *xinf) {
  int absx, absy;
  Window dummy;
  XWindowAttributes result;

  if (batch) return (0);
  if (display == (Display *) NULL || win == (Window) NULL) return 0;
  if (!XGetWindowAttributes (display, win, &result) ) return 0;
  if (!XTranslateCoordinates (display, win, RootWindow (display, snum), 0, 0,
			      &absx, &absy, &dummy) ) return 0;
  xinf->winid=(int)win;
  xinf->winw=result.width;  
  xinf->winh=result.height;  
  xinf->winb=result.border_width;
  xinf->winx=absx;  
  xinf->winy=absy;  
  return 1;
}
 
/* Given x,y page location, return screen pixel location */

void gxdgcoord (gadouble x, gadouble y, gaint *i, gaint *j) {
  if (batch) {
    *i = -999;
    *j = -999;
    return;
  }
  *i = (gaint)(x*xscl+0.5);
  *j = height - (gaint)(y*yscl+0.5);
}

void gxdimg(gaint *im, gaint imin, gaint jmin, gaint isiz, gaint jsiz) {
  printf("Warning: The Cairo graphics display interface does not support 'gxout imap'\n");
}

/* Get the x bearing (width) used to plot a character */

gadouble gxdqchl (char ch, gaint fn, gadouble w) {
  return (gxCqchl(ch,fn,w)); 
}

/* Plot a character */
gadouble gxdch (char ch, gaint fn, gadouble x, gadouble y, gadouble w, gadouble h, gadouble rot) {
gadouble xadv;

  xadv = gxCch(ch,fn,x,y,w,h,rot);   /* draw character with Cairo, width of character returned */
  if (QLength(display)&&rstate) gxdeve(0);
  return (xadv);
}

/* Flush pending graphics; called from gxC and gxX */
void gxdXflush (void) {
  XFlush(display);
}

/* Allows higher levels to indicate various options, conditions, etc. to the
   hardware rendering layer.  */
/* opt = 4:  major finish point; complete any ongoing graphics rendering */

void gxdopt (gaint opt) {
  if (opt==4) gxCflush(1);
}

void gxsetpatt (gaint pnum) {
  gxCpattrset(pnum);   
}

void gxdsignal (gaint sig) {
  if (sig==1) gxCflush(1);   /* finish rendering */
  if (sig==2) gxCaa(0);      /* disable anti-aliasing */
  if (sig==3) gxCaa(1);      /* enable anti-aliasing */
  if (sig==4) gxCpush();     /* push */
  if (sig==5) gxCpop();      /* pop */
}

void gxdclip (gadouble xlo, gadouble xhi, gadouble ylo, gadouble yhi) {
  gxCclip (xlo,xhi,ylo,yhi);
}

void gxdcfg (void) {
  printf("X%d.%d ",X_PROTOCOL,X_PROTOCOL_REVISION);
  gxCcfg();
}

/* Check to see if this display backend supports fonts */
gaint gxdckfont (void) {
  return (1);
}

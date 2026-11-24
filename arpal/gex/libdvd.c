#ifndef POD
#define LW
#undef lDiag
//#define lDiag

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <float.h>
//#include <tgmath.h>
#include "grads.h"

#define float  double

#ifndef min
#define min(a,b) ((a) <= (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) ((a) >= (b) ? (a) : (b))
#endif
#ifndef SIGN
#define SIGN(a,b) ((b) >= 0.0 ? fabs(a) : -fabs(a))
#endif
/*mf 971022 --- expose Mike Fiorino's global struct to these routines for warning level setting mf*/
extern struct gamfcmn mfcmn;
/*mf 971022 --- expose Mike Fiorino's global struct to these routines for warning level setting mf*/

static struct gaufb *ufba;  /* Anchor for user function defs */
char *gxgnam(char *);  /* This is also in gx.h */
static char pout[256];   /* Build error msgs here */
static char *mon[12] = {"jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec"};

static int verbose = 1;   /*ams by default only print error messages and help ams*/

/* Function routine names.  Add a new function by putting the prototype here and adding to the if tests below */
gaint ffDVDbterp (struct gafunc *pfc, struct gastat *pst);
gaint ffDVDraigrid (struct gafunc *pfc, struct gastat *pst);
gaint ffDVDshift (struct gafunc *pfc, struct gastat *pst);
gaint ffDVDtimeShift (struct gafunc *pfc, struct gastat *pst);
gaint ffDVDbilin (struct gafunc *pfc, struct gastat *pst);
gaint ffDVDbicub (struct gafunc *pfc, struct gastat *pst);

/* prototyping */
int DVDfindOffset(gadouble, gadouble, gadouble);
void DVDfindRange(gadouble, gadouble, gadouble, gadouble, int, int, gadouble, gadouble, gadouble, gadouble, int *);
gadouble DVDintercept(gadouble, gadouble, gadouble, gadouble);
gadouble DVDfindDelta(gadouble *, int);
gadouble DVDgetValue(gadouble *, char *, gadouble *, gadouble *, gadouble, gadouble, int, int, gadouble, gadouble, gadouble, gadouble, gadouble);
void DVDboxAverage(gadouble *, char *, gadouble *, gadouble *, int, int, gadouble *, gadouble, gadouble *, gadouble *, int, int);

/* GrADS v1.x compatibility functions */
int gex_expr1(char *expr, struct gastat *pst);
int gex_setUndef (struct gastat *pst);
#define gaexpr gex_expr1
#define valprs getdbl

struct gafile *getfile(char *ch, struct gastat *pst) {
  char name[20], vnam[20], cc, *pos;
  int i,fnum,ii,jj,dval,rc,dotflg,idim,jdim,dim;
  int id[4];
  float dmin[4],dmax[4],d1,d2;
  int gmin[4],gmax[4];
  struct gafile *pfi;
  float (*conv) (float *, float);
  float *cvals,*r,*r2,lonr,rlon,dir,spd,wrot,a,b;
  int size,j;

/* Get the variable or function name.  It must start with a
   letter, and consist of letters or numbers or underscore.  */
  i=0;
  while ( (*ch>='a' && *ch<='z') || (*ch>='0' && *ch<='9' ) || (*ch == '_') ) {
    name[i] = *ch;
    vnam[i] = *ch;
    ch++; i++;
    if (i>16) break;
  }
  name[i] = '\0';
  vnam[i] = '\0';  /* Save 'i' for next loop */

/* Check for the data set number in the variable name.  If there, then this has to be a variable name */
  fnum = pst->fnum;
  dotflg=0;
  if (*ch == '.') {
    dotflg=1;
    ch++;
    pos = intprs(ch,&fnum);
    if (pos==NULL || fnum<1) {
      sprintf (pout,"Syntax error: Bad file number for variable %s \n", name);
      gaprnt (0,pout);
      return (NULL);
    }
    vnam[i] = '.';
    i++;
    while (ch<pos) {
      vnam[i] = *ch;
      ch++; i++;
    }
    vnam[i] = '\0';
  }

/* Check for a predefined data object. */
  pfi = NULL;
  if (!dotflg) pfi = getdfn(name,pst);

/* If not a defined grid, get a pointer to a file structure    */
  if (pfi==NULL) {
    if (!dotflg) pfi = pst->pfid;
    else {
      pfi = pst->pfi1;
      for (i=1; i<fnum && pfi!=NULL; i++) pfi = pfi->pforw;
      if (pfi==NULL) {
	gaprnt (0,"Data Request Error:  File number out of range \n");
	sprintf (pout,"  Variable = %s \n",vnam);
	gaprnt (0,pout);
	return (NULL);
      }
    }
  }
  return (pfi);
}

void gaprnt_ ( int level, char *msg ) {
  if ( level <= verbose ) gaprnt(level,msg);
}

char *nam1var[3] = {"BTERP","RAIGRID","SHIFT"};
char *usg1var[3] = {"bterp\nusage: d bterp(expr,reference_grid)\n",
	            "raigrid \nusage: d raigrid(expr,radius)\n",
	            "shift \nusage: d shift(expr,Xshift,Yshift)\n"};

/* --------------------------------------------------------
  gastat pst;
  pst->type : tipo (0=station,1=grid)
  pst->idim : varying dimension for X axis (-1=number,0=LON,1=LAT,2=LEV,3=TIM,4=EPS)
  pst->jdim : varying dimension for Y axis (-1=number,0=LON,1=LAT,2=LEV,3=TIM,4=EPS)
  pst->dimmin[5] Dimension start for each dimension (X,Y,Z,T,E) in grid units.
  pst->dimmax[5] Dimension end for each dimension (X,Y,Z,T,E) in grid units.
  pst->fnum : file number
  pst.result.pgr : pgr
  gagrid pgr;
  pst->idim : dimension for rows (-1=number,0=LON,1=LAT,2=LEV,3=TIM,4=EPS)
  pst->jdim : dimension for columns (-1=number,0=LON,1=LAT,2=LEV,3=TIM,4=EPS)
  pgr->isiz : number of elements per row (number of columns)
  pgr->jsiz : number of rows
  pgr->grid : indirizzo della griglia (isiz*jsiz array contenente i valori dell'expr)
  pgr->undef : undef value
  pgr->umask : mask for undef value in the grid
  pgr->rmin : min grid value
  pgr->rmax : max grid value
  pgr->ivals : tira fuori le coords, caricate su x1 e y1
  pgr->iavals : tira fuori le coords in grid units
-------------------------------------------------------- */

/* -------------------------------------------------------- */
/*
  dvd - grid shift: shift(expr,dx,dy) moves the values of expr by dx grid
  points along the 1st varying dimension and dy along the 2nd (the points
  left uncovered are undefined).

  ARPAL 2.2.5-19: the undefined points (umask) are moved too: before only
  the values were moved, so an undefined point (whose value in the grid can
  be anything) became defined; the result is written in the grid itself
  (the source grid was leaked, replaced by a malloc'ed one).
*/
gaint ffDVDshift (struct gafunc *pfc, struct gastat *pst) {
  gaint rc, isiz, jsiz, nn, i, j, k, is, js, ioff, joff;
  gadouble xshift, yshift, *gr, *tg=NULL;
  struct gagrid *pgr;
  char *gru, *tu=NULL;

  if (pfc->argnum!=3) {
    gaprnt (0,"Error from shift: Three arguments expected: shift(expr,dx,dy)\n");
    return (1);
  }
  if (valprs(pfc->argpnt[1],&xshift)==NULL || valprs(pfc->argpnt[2],&yshift)==NULL) {
    gaprnt (0,"Error from shift: the 2nd and 3rd arguments must be numbers of grid points\n");
    return (1);
  }
  ioff = (gaint)xshift;
  joff = (gaint)yshift;

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type!=1) {
    gaprnt (0,"Error from shift: The 1st argument is not a grid expression \n");
    gafree (pst);
    return (1);
  }
  pgr = pst->result.pgr;
  if (ioff==0 && joff==0) return (0);

  isiz = pgr->isiz;
  jsiz = pgr->jsiz;
  nn = isiz*jsiz;
  gr  = pgr->grid;
  gru = pgr->umask;
  tg = (gadouble *)malloc(sizeof(gadouble)*nn);
  tu = (char *)malloc(nn);
  if (tg==NULL || tu==NULL) {
    gaprnt (0,"Error from shift: memory allocation error\n");
    free(tg); free(tu); gafree(pst);
    return (1);
  }
  memcpy(tg, gr, sizeof(gadouble)*nn);
  memcpy(tu, gru, nn);
  for (k=0; k<nn; k++) { gr[k] = pgr->undef; gru[k] = 0; }
  for (j=0; j<jsiz; j++) {
    js = j + joff;
    if (js<0 || js>=jsiz) continue;
    for (i=0; i<isiz; i++) {
      is = i + ioff;
      if (is<0 || is>=isiz) continue;
      if (!tu[isiz*j+i]) continue;
      gr[isiz*js+is]  = tg[isiz*j+i];
      gru[isiz*js+is] = 1;
    }
  }
  free(tg);
  free(tu);
  return (0);
}

/* -------------------------------------------------------- */
/*
  dvd - time shift: tshift(expr,minutes) returns expr with its time axis
  moved by the given number of minutes (negative = back in time).

  ARPAL 2.2.5-19: the shift is applied to a copy of the time values of the
  result grid. Before, they were changed in place, but they are shared with
  the source (pfi->grvals[3] of the file or of the defined variable):
  'd tshift(var,-90)' moved for good the time axis of a defined variable
  'var'. The copies are kept in a ring of static buffers, never released:
  after a display GrADS keeps pointers to the scaling of the plot (q xy2w,
  q w2xy...), that must stay valid after the grid is released. If the grid
  has its own scaling (alocf) it is changed in place. Negative shifts are
  normalized (the minutes stayed negative, e.g. 6:-90), and nothing is
  printed at every call.
*/
#define TSNBUF 16
static gadouble tsbuf[TSNBUF][8];
static gaint tsnext = 0;

gaint ffDVDtimeShift (struct gafunc *pfc, struct gastat *pst) {
  gaint rc, toff, i;
  gadouble tshift;
  struct gagrid *pgr;
  struct dt Delta, Time, Res;
  gadouble *tv;

  /* Evaluate args number */
  if (pfc->argnum!=2) {
    gaprnt (0,"Error from tshift: Two arguments expected: tshift(expr,minutes)\n");
    return (1);
  }

  /* 2nd argument: the time shift in minutes */
  if (valprs(pfc->argpnt[1],&tshift)==NULL) {
    gaprnt (0,"Error from tshift: the 2nd argument must be a number of minutes\n");
    return (1);
  }
  toff = (gaint)tshift;

  /* Evaluate the 1st expression, type = grid */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type!=1) {
    gaprnt (0,"Error from tshift: The 1st argument is not a grid expression \n");
    gafree (pst);
    return (1);
  }
  pgr = pst->result.pgr;

  /* nothing to do without shift or if time does not vary */
  if (toff==0 || (pgr->idim!=3 && pgr->jdim!=3)) return (0);

  /* time values of the grid (8 values: yr mo dy hr mn, increments) */
  if (pgr->jdim==3) tv = pgr->jvals;
  else              tv = pgr->ivals;
  if (tv==NULL) return (0);
  /* shared with the source: work on a copy */
  if (!pgr->alocf) {
    for (i=0; i<8; i++) tsbuf[tsnext][i] = tv[i];
    tv = tsbuf[tsnext];
    tsnext = (tsnext+1) % TSNBUF;
    if (pgr->jdim==3) pgr->jvals = tv;
    else              pgr->ivals = tv;
  }

  /* initial time of the time axis */
  Time.yr = (gaint)tv[0];
  Time.mo = (gaint)tv[1];
  Time.dy = (gaint)tv[2];
  Time.hr = (gaint)tv[3];
  Time.mn = (gaint)tv[4];
  Delta.yr = 0; Delta.mo = 0; Delta.dy = 0; Delta.hr = 0;
  if (toff>0) {
    Delta.mn = toff;
    Res = Time;
    timadd(&Delta, &Res);           /* Res = Time + Delta */
  } else {
    Delta.mn = -toff;
    Res = Delta;
    timsub(&Time, &Res);            /* Res = Time - Delta */
  }
  tv[0] = Res.yr;
  tv[1] = Res.mo;
  tv[2] = Res.dy;
  tv[3] = Res.hr;
  tv[4] = Res.mn;
  /* for time the abs <-> grid conversion uses the same values */
  if (pgr->jdim==3) pgr->javals = tv;
  else              pgr->iavals = tv;
  return (0);
}

/* -------------------------------------------------------- */
/*
  dvd - raigrid(expr,radius): spreads the defined values of a sparse grid
  (e.g. binned rain gauges) to the grid points within radius (grid units),
  with weights 1/d^2 (the point itself has weight 4); the defined points
  keep their value, the points out of reach of all the data stay undefined.
  Lon and lat must be the varying dimensions.

  ARPAL 2.2.5-19: the defined points are those of the mask (umask), not
  those whose value differs from undef (an undefined point can have any
  value); the result is written in the grid itself and the work arrays are
  released (the grid of the expression and three arrays were leaked at
  every call); the unused coordinates are no more computed.
*/
gaint ffDVDraigrid (struct gafunc *pfc, struct gastat *pst) {
  gaint rc, i, j, k, isiz, jsiz, nn, krad, ksiz, is, js, ig, jg;
  gadouble radius, *gr, *sum=NULL, *wsum=NULL, *weight=NULL, w, v;
  struct gagrid *pgr;
  char *gru, *wok=NULL;

  if (pfc->argnum<2 || pfc->argnum>3) {
    gaprnt (0,"Error from raigrid: Two arguments expected: raigrid(expr,radius)\n");
    return (1);
  }
  if (valprs(pfc->argpnt[1],&radius)==NULL) {
    sprintf (pout,"Error from raigrid: Expecting arg2: %s to be the radius in grid points\n",pfc->argpnt[1]);
    gaprnt (0,pout);
    return (1);
  }
  krad = (gaint)(radius+0.5);
  ksiz = 2*krad+1;

  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) return (rc);
  if (pst->type!=1) {
    gaprnt (0,"Error from raigrid: The 1st argument is not a grid expression \n");
    gafree (pst);
    return (1);
  }
  pgr = pst->result.pgr;
  if (pgr->idim!=0 || pgr->jdim!=1) {
    gaprnt (0,"Error from raigrid: The varying dimensions must be X and Y \n");
    gafree (pst);
    return (1);
  }
  /* radius below 1 grid point: nothing to spread */
  if (krad<1) return (0);
  isiz = pgr->isiz;
  jsiz = pgr->jsiz;
  nn = isiz*jsiz;
  gr  = pgr->grid;
  gru = pgr->umask;

  sum    = (gadouble *)calloc(nn, sizeof(gadouble));
  wsum   = (gadouble *)calloc(nn, sizeof(gadouble));
  weight = (gadouble *)malloc(sizeof(gadouble)*ksiz*ksiz);
  wok    = (char *)malloc(ksiz*ksiz);
  if (sum==NULL || wsum==NULL || weight==NULL || wok==NULL) {
    gaprnt (0,"Error from raigrid: memory allocation error\n");
    free(sum); free(wsum); free(weight); free(wok); gafree(pst);
    return (1);
  }

  /* weights 1/d^2 inside the circle of radius krad (1/d >= 1/krad) */
  for (j=-krad; j<=krad; j++) {
    for (i=-krad; i<=krad; i++) {
      k = ksiz*(j+krad) + (i+krad);
      w = (i==0 && j==0) ? 0.5 : sqrt((gadouble)(i*i+j*j));
      w = 1.0/w;
      wok[k] = (w >= 1.0/krad);
      weight[k] = w*w;
    }
  }

  /* every defined point spreads its value to the points around */
  for (js=0; js<jsiz; js++) {
    for (is=0; is<isiz; is++) {
      if (!gru[isiz*js+is]) continue;
      v = gr[isiz*js+is];
      for (j=-krad; j<=krad; j++) {
        jg = js+j;
        if (jg<0 || jg>=jsiz) continue;
        for (i=-krad; i<=krad; i++) {
          ig = is+i;
          if (ig<0 || ig>=isiz) continue;
          k = ksiz*(j+krad) + (i+krad);
          if (!wok[k]) continue;
          sum[isiz*jg+ig]  += v*weight[k];
          wsum[isiz*jg+ig] += weight[k];
        }
      }
    }
  }

  /* weighted mean where reached, the defined points keep their value */
  for (k=0; k<nn; k++) {
    if (gru[k]) continue;
    if (wsum[k]>0.0) { gr[k] = sum[k]/wsum[k]; gru[k] = 1; }
    else gr[k] = pgr->undef;
  }

  free(sum); free(wsum); free(weight); free(wok);
  return (0);
}

/* -------------------------------------------------------- */
/*
  dvd - bterp(expr,grid): box average of expr on the cells of grid (like
  lterp, but each cell of the destination grid gets the area weighted mean
  of the source cells it covers; undefined source cells do not count).
  Both expressions must vary in lon and lat.

  ARPAL 2.2.5-19: the check of the varying dimensions was wrong (it failed
  only when the 1st dimension was lat); the undefined source cells are
  those of the mask (umask), not those equal to undef; the source grid is
  released with gagfre (only its data were released, and with a single
  value the data are inside the structure); no release of uninitialized
  pointers on errors; the unused directions idir2/jdir2 (which read x2[1]
  also for one point) are removed.
*/
static gaint DVDcoords (struct gagrid *pgr, gaint ij, gadouble **c) {
  gadouble (*conv) (gadouble *, gadouble);
  gadouble *vals, *x;
  gaint i, n, d;
  d = ij ? pgr->jdim : pgr->idim;
  n = ij ? pgr->jsiz : pgr->isiz;
  conv = ij ? pgr->jgrab : pgr->igrab;
  vals = ij ? pgr->jvals : pgr->ivals;
  x = (gadouble *)galloc(sizeof(gadouble)*n,"dvdcoord");
  if (x==NULL) return (1);
  for (i=0; i<n; i++) x[i] = conv(vals,(gadouble)(pgr->dimmin[d]+i));
  *c = x;
  return (0);
}

gaint ffDVDbterp (struct gafunc *pfc, struct gastat *pst) {
  struct gastat pst2;
  struct gagrid *pgr1=NULL, *pgr2;
  gadouble *x1=NULL, *x2=NULL, *y1=NULL, *y2=NULL;
  gaint rc, error=0;

  if (pfc->argnum!=2) {
    gaprnt (0,"Error from bterp: Two arguments expected: bterp(expr,grid)\n");
    return (1);
  }

  /* 2nd expression: the destination grid (it becomes the result) */
  pst2 = *pst;
  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) return (rc);
  if (pst->type!=1) {
    gaprnt (0,"Error from bterp: The 2nd argument is not a grid expression \n");
    gafree (pst);
    return (1);
  }
  pgr2 = pst->result.pgr;
  if (pgr2->idim!=0 || pgr2->jdim!=1) {
    gaprnt (0,"Error from bterp: The grids must vary in LON and LAT \n");
    gafree (pst);
    return (1);
  }

  /* 1st expression: the data to be averaged */
  rc = gaexpr(pfc->argpnt[0],&pst2);
  if (rc) { gafree (pst); return (rc); }
  if (pst2.type!=1) {
    gaprnt (0,"Error from bterp: The 1st argument is not a grid expression \n");
    gafree (&pst2);
    gafree (pst);
    return (1);
  }
  pgr1 = pst2.result.pgr;
  if (pgr1->idim!=0 || pgr1->jdim!=1) {
    gaprnt (0,"Error from bterp: The grids must vary in LON and LAT \n");
    error=1; goto err;
  }

  if (DVDcoords(pgr1,0,&x1) || DVDcoords(pgr1,1,&y1) ||
      DVDcoords(pgr2,0,&x2) || DVDcoords(pgr2,1,&y2)) {
    gaprnt (0,"Error from bterp: memory allocation error\n");
    error=1; goto err;
  }

  DVDboxAverage(pgr1->grid, pgr1->umask, x1, y1, pgr1->isiz, pgr1->jsiz,
                pgr2->grid, pgr2->undef, x2, y2, pgr2->isiz, pgr2->jsiz);

err:
  if (x1!=NULL) gree(x1,"f400");
  if (x2!=NULL) gree(x2,"f401");
  if (y1!=NULL) gree(y1,"f402");
  if (y2!=NULL) gree(y2,"f403");
  if (pgr1!=NULL) gagfre(pgr1);
  if (error) { gafree (pst); return (1); }
  return (0);
}

/* -------------------------------------------------------- */
/*
  dvd - Interpolazione in un punto lon,lat

  bilin(expr,lon,lat)   bilineare, dai 4 punti griglia attorno
  bicub(expr,lon,lat)   bicubica, dai 16 punti griglia attorno

  lon e lat devono essere fissi nell'ambiente; le altre dimensioni
  (livello, tempo, ensemble) possono variare e il risultato varia come loro,
  ad esempio un profilo verticale se varia z.
  expr viene valutata nei punti griglia che circondano lon,lat, con lon e
  lat fissati ogni volta su un punto griglia, e i risultati sono combinati
  con pesi separabili wx(i)*wy(j):
    x = posizione di lon in unita' griglia, i0 = floor(x), fx = x-i0
    y = posizione di lat in unita' griglia, j0 = floor(y), fy = y-j0
  bilineare, punti i0..i0+1:  w(i0) = 1-fx, w(i0+1) = fx
  bicubica, punti i0-1..i0+2: kernel cubico di Keys (a=-0.5, Catmull-Rom):
    passa per i punti griglia e riproduce esattamente i campi lineari
  I punti con peso nullo non vengono valutati (lon o lat esattamente su un
  punto griglia, anche sul bordo del dominio).
  bilin: se uno dei punti con peso non nullo e' undef, il risultato e' undef.
  bicub: se manca uno dei 16 punti (undef o fuori dal dominio, ad esempio
  vicino al bordo) in quell'elemento si usa la bilineare.
  La griglia di riferimento e' quella del file della prima variabile di expr
  (es. t.2 -> file 2, una variabile definita con define -> la sua griglia),
  altrimenti quella del file di default.
*/

/* pesi lungo un asse: w[0..3] per i punti i0-1, i0, i0+1, i0+2 */
static void DVDinterpWeights(gadouble f, gaint cubic, gadouble *w) {
  if (cubic) {
    w[0] = ((-0.5*f + 1.0)*f - 0.5)*f;
    w[1] = (1.5*f - 2.5)*f*f + 1.0;
    w[2] = ((-1.5*f + 2.0)*f + 0.5)*f;
    w[3] = (0.5*f - 0.5)*f*f;
  } else {
    w[0] = 0.0;
    w[1] = 1.0 - f;
    w[2] = f;
    w[3] = 0.0;
  }
}

gaint ffDVDinterp (struct gafunc *pfc, struct gastat *pst, gaint cubic) {
  struct gafile *pfi;
  struct gagrid *res=NULL, *pgr;
  gadouble lon, lat, xg, yg, fx, fy, w, wl;
  gadouble dmin0, dmax0, dmin1, dmax1;
  gadouble wx[4], wy[4], lx[4], ly[4];
  gadouble *lsum=NULL;
  char *lok=NULL, *cok=NULL;
  gadouble (*conv) (gadouble *, gadouble);
  gaint i0, j0, a, b, i, j, n, siz=0, rc, first=1, error=0, inner;
  char *fname = cubic ? "BICUB" : "BILIN";

  if (pfc->argnum!=3) {
    sprintf (pout,"Error from %s: Three arguments expected\n  usage: %s(expr,lon,lat)\n",
             fname, cubic ? "bicub" : "bilin");
    gaprnt (0,pout);
    return (1);
  }
  if (valprs(pfc->argpnt[1],&lon)==NULL) {
    sprintf (pout,"Error from %s: Expecting arg2: %s to be a longitude\n",fname,pfc->argpnt[1]);
    gaprnt (0,pout);
    return (1);
  }
  if (valprs(pfc->argpnt[2],&lat)==NULL) {
    sprintf (pout,"Error from %s: Expecting arg3: %s to be a latitude\n",fname,pfc->argpnt[2]);
    gaprnt (0,pout);
    return (1);
  }

  /* lon e lat fissi: il risultato varia solo nelle altre dimensioni */
  if (pst->idim==0 || pst->idim==1 || pst->jdim==0 || pst->jdim==1) {
    sprintf (pout,"Error from %s: lon and lat must be fixed (e.g. set x 1, set y 1)\n",fname);
    gaprnt (0,pout);
    return (1);
  }

  /* griglia di riferimento */
  pfi = getfile(pfc->argpnt[0],pst);
  if (pfi==NULL) return (1);
  if (pfi->type==2 || pfi->type==3) {
    sprintf (pout,"Error from %s: The 1st argument must be a grid expression\n",fname);
    gaprnt (0,pout);
    return (1);
  }

  /* posizione in unita' griglia e pesi */
  conv = pfi->ab2gr[0];
  xg = conv(pfi->abvals[0],lon);
  conv = pfi->ab2gr[1];
  yg = conv(pfi->abvals[1],lat);
  i0 = (gaint) floor(xg);  fx = xg - i0;
  j0 = (gaint) floor(yg);  fy = yg - j0;
  if (fx < FUZZ_SCALE) fx = 0.0;
  if (fx > 1.0-FUZZ_SCALE) { i0++; fx = 0.0; }
  if (fy < FUZZ_SCALE) fy = 0.0;
  if (fy > 1.0-FUZZ_SCALE) { j0++; fy = 0.0; }
  DVDinterpWeights(fx, cubic, wx);
  DVDinterpWeights(fy, cubic, wy);
  DVDinterpWeights(fx, 0, lx);
  DVDinterpWeights(fy, 0, ly);

  if (mfcmn.warnflg > 0) {
    sprintf (pout,"%s: lon %g lat %g -> x %g y %g (i0 %d fx %g, j0 %d fy %g)\n",
             fname,lon,lat,xg,yg,i0,fx,j0,fy);
    gaprnt (2,pout);
  }

  /* l'ambiente lon/lat viene ripristinato alla fine */
  dmin0 = pst->dmin[0]; dmax0 = pst->dmax[0];
  dmin1 = pst->dmin[1]; dmax1 = pst->dmax[1];

  for (b=0; b<4; b++) {
    for (a=0; a<4; a++) {
      w = wx[a]*wy[b];
      if (w == 0.0) continue;
      i = i0 + a - 1;
      j = j0 + b - 1;
      inner = (a==1 || a==2) && (b==1 || b==2);
      wl = inner ? lx[a]*ly[b] : 0.0;

      /* bicubica: un punto esterno fuori dal dominio non si valuta,
         gli elementi ripiegano sulla bilineare */
      if (!inner && ((!pfi->wrap && (i<1 || i>pfi->dnum[0])) || j<1 || j>pfi->dnum[1])) {
        if (!first) for (n=0; n<siz; n++) cok[n] = 0;
        else cubic = -1;   /* segnala: cubica gia' invalida ovunque */
        continue;
      }

      conv = pfi->gr2ab[0];
      pst->dmin[0] = pst->dmax[0] = conv(pfi->grvals[0],(gadouble)i);
      conv = pfi->gr2ab[1];
      pst->dmin[1] = pst->dmax[1] = conv(pfi->grvals[1],(gadouble)j);

      rc = gaexpr(pfc->argpnt[0],pst);
      if (rc) { error=1; goto err; }
      if (pst->type!=1) {
        sprintf (pout,"Error from %s: The 1st argument is not a grid expression\n",fname);
        gaprnt (0,pout);
        gafree (pst);
        error=1; goto err;
      }
      pgr = pst->result.pgr;
      pst->result.pgr = NULL;

      if (first) {
        /* il primo punto valutato fa da contenitore del risultato */
        res = pgr;
        siz = res->isiz * res->jsiz;
        if (cubic) {
          lsum = (gadouble *) malloc(sizeof(gadouble)*siz);
          lok  = (char *) malloc(siz);
          cok  = (char *) malloc(siz);
          if (lsum==NULL || lok==NULL || cok==NULL) {
            gaprnt (0,"Error from BICUB: memory allocation error\n");
            error=1; goto err;
          }
          for (n=0; n<siz; n++) {
            lok[n] = inner ? res->umask[n] : 1;   /* lok conta solo i 4 punti interni */
            cok[n] = (cubic>0) ? res->umask[n] : 0;
            lsum[n] = res->umask[n] ? wl*res->grid[n] : 0.0;
          }
        }
        for (n=0; n<siz; n++)
          if (res->umask[n]) res->grid[n] *= w;
        first = 0;
        continue;
      }

      if (pgr->isiz!=res->isiz || pgr->jsiz!=res->jsiz) {
        sprintf (pout,"Error from %s: grids at the surrounding points differ in size\n",fname);
        gaprnt (0,pout);
        gagfre (pgr);
        error=1; goto err;
      }
      if (cubic) {
        for (n=0; n<siz; n++) {
          if (pgr->umask[n]) {
            if (cok[n]) res->grid[n] += w * pgr->grid[n];
            if (inner && lok[n]) lsum[n] += wl * pgr->grid[n];
          } else {
            cok[n] = 0;
            if (inner) lok[n] = 0;
          }
        }
      } else {
        for (n=0; n<siz; n++) {
          if (res->umask[n] && pgr->umask[n]) res->grid[n] += w * pgr->grid[n];
          else res->umask[n] = 0;
        }
      }
      gagfre (pgr);
    }
  }

  if (res==NULL) {   /* non dovrebbe succedere: il punto i0,j0 ha sempre peso */
    sprintf (pout,"Error from %s: no grid point evaluated\n",fname);
    gaprnt (0,pout);
    error=1; goto err;
  }

  /* bicubica dove ci sono tutti i punti, altrimenti bilineare, altrimenti undef */
  if (cubic) {
    for (n=0; n<siz; n++) {
      if (cok[n]) res->umask[n] = 1;
      else if (lok[n]) { res->grid[n] = lsum[n]; res->umask[n] = 1; }
      else res->umask[n] = 0;
    }
  }
  for (n=0; n<siz; n++)
    if (!res->umask[n]) res->grid[n] = res->undef;

err:
  pst->dmin[0] = dmin0; pst->dmax[0] = dmax0;
  pst->dmin[1] = dmin1; pst->dmax[1] = dmax1;
  if (lsum!=NULL) free(lsum);
  if (lok!=NULL) free(lok);
  if (cok!=NULL) free(cok);
  if (error) {
    if (res!=NULL) gagfre (res);
    return (1);
  }
  pst->result.pgr = res;
  pst->type = 1;
  return (0);
}

gaint ffDVDbilin (struct gafunc *pfc, struct gastat *pst) {
  return ffDVDinterp (pfc, pst, 0);
}

gaint ffDVDbicub (struct gafunc *pfc, struct gastat *pst) {
  return ffDVDinterp (pfc, pst, 1);
}

/* -------------------------------------------------------- */
/*
  dvd - funzione di collegamento tra grads e questa libreria
*/
int f_dvd (struct gafunc *pfc, struct gastat *pst) {

  int rc;
  char *name = pfc->argpnt[pfc->argnum];

     if ( strcmp ( "bterp" , name) == 0 ) rc = ffDVDbterp ( pfc, pst );
     else if ( strcmp ( "raigrid" , name) == 0 ) rc = ffDVDraigrid ( pfc, pst );
     else if ( strcmp ( "shift" , name) == 0 ) rc = ffDVDshift ( pfc, pst );
     else if ( strcmp ( "tshift" , name) == 0 ) rc = ffDVDtimeShift ( pfc, pst );
     else if ( strcmp ( "bilin" , name) == 0 ) rc = ffDVDbilin ( pfc, pst );
     else if ( strcmp ( "bicub" , name) == 0 ) rc = ffDVDbicub ( pfc, pst );
     else {
       sprintf(pout,"Function '%s' not found in libdvd!\n" , name);
       gaprnt(0,pout);
       return 1;
     }

    if (rc) return rc;

    /* Make sure undef mask is properly set */
    rc = gex_setUndef (pst);
    return rc; 

}

/* -------------------------------------------------------- */
/*
  stampa la griglia con coords e values
*/
void DVDlogGrid(char label[], double *gr, double u, double *x, double *y, int isiz, int jsiz) {
  int i, j;
  double dx = DVDfindDelta(x, isiz);
  double dy = DVDfindDelta(y, jsiz);
  double value;
  printf("\n-- %10s  %d x %d   res %.3f x %.3f --------------------------\n", label, isiz, jsiz, dx, dy);
  printf("         lon |");
  for (i = 0; i < isiz; i++) printf("%7.2f", x[i]);
  printf("\n");
  for (i = 0; i < isiz+2; i++) printf("%s", "-------");
  printf("\n");
  printf("   lat   i/j |");
  for (i = 0; i < isiz; i++) printf("%7d", i);
  printf("\n");
  for (j = jsiz-1; j > -1; j--) {
    printf("%7.2f%5d |", y[j], j);
    for (i = 0; i < isiz; i++) {
      value = gr[isiz*j + i];
      printf("%7.2f", (value == u ? -99.99 : value));
    }
    printf("\n");
  }
}

/* -------------------------------------------------------- */
/*
  fa la box average della griglia 1 sulla 2
*/
void DVDboxAverage(gadouble *gr1, char *m1, gadouble *x1, gadouble *y1, int isiz1, int jsiz1, gadouble *gr2, gadouble u2, gadouble *x2, gadouble *y2, int isiz2, int jsiz2) {

/* definitions */
  gadouble dx1 = DVDfindDelta(x1, isiz1);
  gadouble dy1 = DVDfindDelta(y1, jsiz1);
  gadouble dx2 = DVDfindDelta(x2, isiz2);
  gadouble dy2 = DVDfindDelta(y2, jsiz2);
  int i2, j2;
  gadouble lon2, lat2;

/* box averaging */
  for (j2 = 0; j2 < jsiz2; j2++) {
    lat2 = y2[j2];
    for (i2 = 0; i2 < isiz2; i2++) {
      lon2 = x2[i2];
#ifdef DVDlog
      printf("----------------------\nBOX AVG grid2 point = (%.2f,%.2f) grid = (%d,%d)\n", lon2, lat2, i2, j2);
#endif
      //gr2[isiz2*j2 + i2] = DVDgetValue(gr1, u1, x1, y1, dx1, dy1, isiz1, jsiz1, u2, lon2, lat2, dx2, dy2);
      *(gr2 + (isiz2*j2) + i2) = DVDgetValue(gr1, m1, x1, y1, dx1, dy1, isiz1, jsiz1, u2, lon2, lat2, dx2, dy2);
    }
  }

/* log */
#ifdef DVDlog
  DVDlogGrid("GRID1", gr1, u2, x1, y1, isiz1, jsiz1);
  DVDlogGrid("GRID2", gr2, u2, x2, y2, isiz2, jsiz2);
#endif

}

/* -------------------------------------------------------- */
/*
  torna il valore pesato di gr1 sull'area definita intorno a lon2, lat2 con passo dx2, dy2
  i valori mancanti di gr1 (maschera m1 a 0) non contribuiscono
*/
gadouble DVDgetValue(gadouble *gr1, char *m1, gadouble *x1, gadouble *y1, gadouble dx1, gadouble dy1, int isiz1, int jsiz1, gadouble u2, gadouble lon2, gadouble lat2, gadouble dx2, gadouble dy2) {
  gadouble lon10 = x1[0];
  gadouble lat10 = y1[0];
  int i1, j1;
  gadouble lenX, lenY, area, value;
  gadouble areaTot = 0;
  gadouble valueTot = 0;
  int range[4] = {-1,-1,-1,-1};
  DVDfindRange(lon10, lat10, dx1, dy1, isiz1, jsiz1, lon2, lat2, dx2, dy2, range);
  if (range[0] != -1) {
    for (j1 = range[2]; j1 <= range[3]; j1++) {
      lenY = DVDintercept(y1[j1], lat2, dy1, dy2);
      for (i1 = range[0]; i1 <= range[1]; i1++) {
	lenX = DVDintercept(x1[i1], lon2, dx1, dx2);
	area = lenX * lenY;
	if (area > 10*FLT_EPSILON) {
	  //value = gr1[isiz1*j1 + i1];
	  value = *(gr1 + (isiz1*j1) + i1);
//printf("i1:%d j1:%d value: %g  u1: %g  diff: %g\n", i1, j1, value, u1, value - u1);
	  if (!m1[isiz1*j1 + i1]) continue;   /* undefined source cell */
	  areaTot += area;
	  valueTot += (value * area);
	  #ifdef DVDlog
	    printf("DVDgetValue grid2 point has contribution = %.1f  with area = %f  for grid1 grid(%d,%d)\n", value, area, i1, j1);
	  #endif
	}
      }
    }
  }
  if (areaTot > 10*FLT_EPSILON) {
    value = valueTot / areaTot;
  } else {
    value = u2;
  }
  #ifdef DVDlog
    printf("DVDgetValue grid2 point has value = %f  areatot = %f\n", value, areaTot);
  #endif
  return value;
}

/* -------------------------------------------------------- */
/*
  lon10, lat10 punto di partenza griglia 1 rispetto alla selezione lat, lon effettuata
  trova le i,j min e max della griglia 1 coinvolti dalla cella con coord lon2,lat2 della griglia 2
  il risultato sta nel vettore range (0=imin, 1=imax, 2=jmin, 3=jmax)
  se il range va fuori dai limiti della griglia 1 viene clippato ai limiti
  se il range e' completamente fuori dai limiti della griglia 1 i valori di range vengono posti a -1
*/
void DVDfindRange(gadouble lon10, gadouble lat10, gadouble dx1, gadouble dy1, int isiz1, int jsiz1, gadouble lon2, gadouble lat2, gadouble dx2, gadouble dy2, int *range) {
  gadouble lonm2 = lon2 - dx2/2;
  gadouble lonp2 = lon2 + dx2/2;
  gadouble latm2 = lat2 - dy2/2;
  gadouble latp2 = lat2 + dy2/2;
#ifdef DVDlog
  printf("DVDfindRange grid2 point = (%.2f,%.2f) dx = %.2f -> cell area = (%.2f,%.2f) (%.2f,%.2f)\n", lon2, lat2, dx2, lonm2, latm2, lonp2, latp2);
#endif
  range[0] = DVDfindOffset(lon10, dx1, lonm2);
  range[1] = DVDfindOffset(lon10, dx1, lonp2);
  range[2] = DVDfindOffset(lat10, dy1, latm2);
  range[3] = DVDfindOffset(lat10, dy1, latp2);
#ifdef DVDlog
  printf("DVDfindRange grid1 -> range before i1: %d:%d j1: %d:%d\n", range[0], range[1], range[2], range[3]);
#endif
  if (
      (range[0] > isiz1-1 || range[1] < 0 || range[2] > jsiz1-1 || range[3] < 0)
      //|| (range[0] == isiz1-1 && range[1] > range[0])
      //|| (range[1] == 0 && range[0] < range[1])
      //|| (range[2] == jsiz1-1 && range[3] > range[2])
      //|| (range[3] == 0 && range[2] < range[3])
     ) {
    range[0] = -1;
    range[1] = -1;
    range[2] = -1;
    range[3] = -1;
  } else {
    range[0] = max(range[0],0);
    range[1] = min(range[1],isiz1-1);
    range[2] = max(range[2],0);
    range[3] = min(range[3],jsiz1-1);
  }
#ifdef DVDlog
  printf("DVDfindRange grid1 -> range after  i1: %d:%d j1: %d:%d\n\n", range[0], range[1], range[2], range[3]);
#endif
}

/* -------------------------------------------------------- */
/*
  torna il delta medio all'interno di un vettore di coordinate
*/
gadouble DVDfindDelta(gadouble *coords, int siz) {
  gadouble delta = (coords[siz-1]-coords[0]) / (siz-1);
  return delta;
}

/* -------------------------------------------------------- */
/*
  torna l'offset (intero) in numero di celle di coord rispetto a coord0
  in sostanza torna il numero della cella di pertinenza a partire da 0 anziche' 1
*/
int DVDfindOffset(gadouble coord0, gadouble delta, gadouble coord) {

  int icoord = (int) floor((coord-(coord0-delta/2))/delta);
  //printf("DVDfindOffset coord0:%.2f delta:%.2f coord:%.2f -> offset:%f  icoord:%d\n", coord0, delta, coord, (coord-(coord0-delta/2))/delta, icoord);
#ifdef DVDlog
  printf("DVDfindOffset coord0:%.2f delta:%.2f coord:%.2f -> icoord:%d\n", coord0, delta, coord, icoord);
#endif
  return icoord;
}


/* -------------------------------------------------------- */
/*
  torna la lunghezza dell'intersezione tra il segmento di coordinata l1
  e ampiezza d1 rispetto al segmento di coordinata l2 e ampiezza d2
*/
gadouble DVDintercept(gadouble l1, gadouble l2, gadouble d1, gadouble d2) {
  gadouble lm1 = l1 - d1/2;
  gadouble lp1 = l1 + d1/2;
  gadouble lm2 = l2 - d2/2;
  gadouble lp2 = l2 + d2/2;
  gadouble len = max(min(lp1, lp2) - max(lm1, lm2), 0.0);
  return len;
}
#else

=pod

=head1 NAME

libdvd.gex - Davide Sacchetti Collection of GrADS v2.0 Extensions 

=head1 SYNOPSIS

=item

display B<bterp>(I<EXPR>,I<EXPR>) -  Box averaging: same as lterp but with box averaging

=item

display B<raigrid>(I<EXPR>,I<radius>) -  expands binned data using 1/r2 weights

=item

display B<shift>(I<EXPR>,I<Xshift>,I<Yshift>) -  shift EXPR by Xshift in X and Yshift in Y

=item

display B<bilin>(I<EXPR>,I<lon>,I<lat>) -  bilinear interpolation of EXPR at lon,lat from the 4 surrounding grid points; lon and lat must be fixed, the result varies like the other dimensions (e.g. a vertical profile)

=item

display B<bicub>(I<EXPR>,I<lon>,I<lat>) -  bicubic (Keys/Catmull-Rom) interpolation of EXPR at lon,lat from the 16 surrounding grid points; falls back to bilinear where one of them is missing (e.g. near the domain border)

=back

=head1 AUTHORS

davide.sacchetti@arpal.gov.it

This is free software released under the GNU General Public License;
see the source for copying conditions. There is NO warranty; not even
for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

=cut

#endif

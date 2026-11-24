#ifndef POD
/*
  libperc.gex - percentile of an expression over a range of a dimension
  (normally the ensemble members), by M. Sardo.

    percentile(expr, e=e1, e=e2, p)

  For each grid point, the value at position ceil(p/100*n) (nearest rank)
  among the n defined values of expr for e=e1..e2; undefined where all the
  values are undefined.

  ARPAL 2.2.5-19:
  - selection (quickselect, O(n) per point) of the k-th value instead of
    qsort of an array of pointers (O(n log n) per point, every comparison a
    function call with two indirections); the grid points are visited in
    memory order (the loop ran along the columns, with stride isiz);
  - undefined values (umask) are skipped: before they were sorted as
    ordinary numbers;
  - the result is written in the grid of the first member and all the
    other grids are released (the grid of the last member was leaked, and
    the result was allocated with malloc and the undef value truncated to
    a char);
  - the expression is evaluated once per member (it was evaluated one more
    time at the start only to check its type);
  - no uninitialized name in the error messages, no "idx" print at every
    call, grids released on errors.
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "grads.h"

/* GrADS v1.x compatibility functions */
int gex_expr1(char *expr, struct gastat *pst);
int gex_setUndef (struct gastat *pst);
#define gaexpr gex_expr1

static char pout[256];   /* Build error msgs here */
static const char *fnam = "percentile";

/* k-th smallest of a[0..n-1] (k from 0); a is reordered (Hoare partition
   with median of three pivot, handles equal values) */
static gadouble percSelect (gadouble *a, gaint n, gaint k) {
  gaint l, r, m, i, j;
  gadouble piv, t;
  l = 0; r = n-1;
  while (r>l) {
    m = l + (r-l)/2;
    if (a[m]<a[l]) { t=a[m]; a[m]=a[l]; a[l]=t; }
    if (a[r]<a[l]) { t=a[r]; a[r]=a[l]; a[l]=t; }
    if (a[r]<a[m]) { t=a[r]; a[r]=a[m]; a[m]=t; }
    piv = a[m];
    i = l; j = r;
    while (i<=j) {
      while (a[i]<piv) i++;
      while (a[j]>piv) j--;
      if (i<=j) { t=a[i]; a[i]=a[j]; a[j]=t; i++; j--; }
    }
    if (k<=j) r = j;
    else if (k>=i) l = i;
    else return (a[k]);
  }
  return (a[k]);
}

/* release the grids of the members read so far */
static void percFree (struct gagrid **tab, gaint n) {
  gaint i;
  for (i=0; i<n; i++) if (tab[i]) gagfre(tab[i]);
}

gaint ffDVDpercentile (struct gafunc *pfc, struct gastat *pst) {
  struct gafile *pfi;
  struct gagrid *pgr, **tab=NULL;
  gadouble (*conv) (gadouble *, gadouble);
  gadouble gr1, gr2, percentile, v, *vals=NULL;
  gaint rc, dim, dim2, d1, d2, ngrid, nread, wflag=0, isiz, jsiz, nn;
  gaint i, z, nv, idx;
  char *ch, *um;

  if (pfc->argnum!=4) {
    gaprnt (0,"Error from percentile: 4 arguments expected\n");
    gaprnt (0,"  usage: percentile(expr,e=e1,e=e2,perc)\n");
    return (1);
  }

  pfi = pst->pfid;
  ch = dimprs (pfc->argpnt[1], pst, pfi, &dim, &gr1, 1, &wflag);
  if (ch==NULL || wflag==2 || dim!=4) {
    snprintf(pout,255,"Error from %s: 1st dimension expression invalid (e=...)\n",fnam);
    gaprnt(0,pout);
    return (1);
  }
  ch = dimprs (pfc->argpnt[2], pst, pfi, &dim2, &gr2, 1, &wflag);
  if (ch==NULL || dim2!=dim || gr2<gr1 || wflag==2) {
    snprintf(pout,255,"Error from %s: 2nd dimension expression invalid (e=...)\n",fnam);
    gaprnt(0,pout);
    return (1);
  }
  if (getdbl(pfc->argpnt[3],&percentile)==NULL || percentile<=0 || percentile>100) {
    snprintf(pout,255,"Error from %s: 4th argument %s must be in ]0,100]\n",fnam,pfc->argpnt[3]);
    gaprnt(0,pout);
    return (1);
  }
  d1 = (gaint)gr1;
  d2 = (gaint)gr2;
  ngrid = d2-d1+1;

  tab  = (struct gagrid **)calloc(ngrid, sizeof(struct gagrid *));
  vals = (gadouble *)malloc(sizeof(gadouble)*ngrid);
  if (tab==NULL || vals==NULL) {
    gaprnt (0,"Error from percentile: memory allocation failed\n");
    free(tab); free(vals);
    return (1);
  }

  /* the dimension of the members does not vary in the result */
  if (pst->idim==dim) { pst->idim = pst->jdim; pst->jdim = -1; }
  if (pst->jdim==dim) pst->jdim = -1;

  /* one grid per member */
  conv = pfi->gr2ab[dim];
  nread = 0;
  isiz = jsiz = 0;
  for (z=0; z<ngrid; z++) {
    v = conv(pfi->grvals[dim], (gadouble)(d1+z));
    pst->dmin[dim] = v;
    pst->dmax[dim] = v;
    rc = gaexpr(pfc->argpnt[0],pst);
    if (rc) { percFree(tab,nread); free(tab); free(vals); return (rc); }
    if (pst->type!=1) {
      gaprnt (0,"Error from percentile: the 1st argument is not a grid expression\n");
      gafree (pst);
      percFree(tab,nread); free(tab); free(vals);
      return (1);
    }
    pgr = pst->result.pgr;
    if (z==0) { isiz = pgr->isiz; jsiz = pgr->jsiz; }
    else if (pgr->isiz!=isiz || pgr->jsiz!=jsiz) {
      gaprnt (0,"Error from percentile: the members have different grids\n");
      gagfre(pgr);
      percFree(tab,nread); free(tab); free(vals);
      return (1);
    }
    tab[z] = pgr;
    nread++;
  }

  /* percentile point by point, in memory order; the result goes in the
     grid of the first member (its value at the point has already been
     copied in vals when it is overwritten) */
  pgr = tab[0];
  nn = isiz*jsiz;
  for (i=0; i<nn; i++) {
    nv = 0;
    for (z=0; z<ngrid; z++) {
      um = tab[z]->umask;
      if (um==NULL || um[i]) vals[nv++] = tab[z]->grid[i];
    }
    if (nv==0) {
      pgr->grid[i] = pgr->undef;
      if (pgr->umask) pgr->umask[i] = 0;
      continue;
    }
    idx = (gaint)ceil(percentile/100.0*nv) - 1;
    if (idx<0) idx = 0;
    if (idx>nv-1) idx = nv-1;
    v = percSelect(vals, nv, idx);
    pgr->grid[i] = v;
    if (pgr->umask) pgr->umask[i] = 1;
  }
  /* rmin/rmax are not set here: with a single point *grid is rmin and
     *umask is umin (the old code overwrote the result at a point) */

  percFree(tab+1, ngrid-1);
  free(tab);
  free(vals);
  pst->type = 1;
  pst->result.pgr = pgr;
  return (0);
}

/* -------------------------------------------------------- */
/*
  dvd - funzione di collegamento tra grads e questa libreria
*/
int f_dvd (struct gafunc *pfc, struct gastat *pst) {
  int rc;
  char *name = pfc->argpnt[pfc->argnum];

  if ( strcmp ( "percentile" , name) == 0 ) rc = ffDVDpercentile ( pfc, pst );
  else {
    snprintf(pout,255,"Function '%s' not found in libperc!\n" , name);
    gaprnt(0,pout);
    return 1;
  }
  if (rc) return rc;

  /* Make sure undef mask is properly set */
  rc = gex_setUndef (pst);
  return rc;
}

#else

=pod

=head1 NAME

libperc.gex - percentile over the ensemble members

=head1 SYNOPSIS

display B<percentile>(I<EXPR>,I<e=e1>,I<e=e2>,I<perc>) - for each grid point
the value of rank ceil(perc/100*n) among the n defined values of EXPR for
e=e1..e2 (nearest rank)

=head1 AUTHORS

massimo.sardo@arpal.gov.it, davide.sacchetti@arpal.gov.it

This is free software released under the GNU General Public License;
see the source for copying conditions. There is NO warranty; not even
for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

=cut

#endif

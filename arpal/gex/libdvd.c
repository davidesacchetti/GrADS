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
gadouble DVDgetValue(gadouble *, gadouble, gadouble *, gadouble *, gadouble, gadouble, int, int, gadouble, gadouble, gadouble, gadouble, gadouble);
void DVDboxAverage(gadouble *, gadouble, gadouble *, gadouble *, int, int, gadouble *, gadouble, gadouble *, gadouble *, int, int);

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
  dvd - Performs grid shift
*/
gaint ffDVDshift (struct gafunc *pfc, struct gastat *pst) {
  gaint rc,isiz,jsiz,nn,error=0,i,j;
  gadouble *gr;
  gadouble *xx=NULL,*yy=NULL,undef;
  gadouble (*iconv) (gadouble *, gadouble);
  struct gagrid *pgr;
  char *gru;
  size_t sz;
  float xshift,yshift;
  gadouble *xgrid_out;

  /* Evaluate args number */
  if (pfc->argnum!=3) {
    gaprnt (0,"Error from SHIFT: Three arguments expected\n");
    return (1);
  }

  /* Evaluate the 1st expression, type = grid */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) { error=1; goto err; }
  if (pst->type!=1) {
    gaprnt (0,"Error from SHIFT: The 1st argument is not a grid expression \n");
    error=1; goto err;
  }

  /* Evaluate the 2st expression, type = number, the x-shift */
  if (valprs(pfc->argpnt[1],&xshift)==NULL) {
    sprintf (pout,"Error from %s: Invalid Argument\n","xshift");
    gaprnt (0,pout);
    sprintf (pout,"  Expecting arg%d: %s to be a value\n",2,pfc->argpnt[1]);
    gaprnt (0,pout);
    return (1);
  }
  /* nearbyint from tgmath.h */
  //int ioff=nearbyint(xshift);
  int ioff=xshift;

  /* Evaluate the 3st expression, type = number, the y-shift */
  if (valprs(pfc->argpnt[2],&yshift)==NULL) {
    sprintf (pout,"Error from %s: Invalid Argument\n","yshift");
    gaprnt (0,pout);
    sprintf (pout,"  Expecting arg%d: %s to be a value\n",3,pfc->argpnt[2]);
    gaprnt (0,pout);
    return (1);
  }
  //int joff=nearbyint(yshift);
  int joff=yshift;

  /* Get the grid */
  pgr = pst->result.pgr;     /* grid */
  gr  = pgr->grid;
  gru = pgr->umask;

  isiz=pgr->isiz;
  jsiz=pgr->jsiz;
  undef=pgr->undef;
  nn=isiz*jsiz;

  /* init vecs */
  xgrid_out=(float *)malloc(sizeof(float)*(nn));
  for(i=0;i<isiz*jsiz;i++){
   *(xgrid_out + i)=undef;
  }

  /* defining grid */
  int ishift,jshift;
  for(i=0;i<isiz;i++){
    ishift=i+ioff;
    if(ishift<0 || ishift>=isiz) continue;
    for(j=0;j<jsiz;j++){
      jshift=j+joff;
      if(jshift<0 || jshift>=jsiz) continue;
      xgrid_out[isiz*jshift+ishift]=*(gr + (isiz*j) + i);
    }
  }

  /* saving output grid */
  pgr->grid = xgrid_out;

err:
  /* release memory */
  if (error) if (pst) gafree (pst); 
  if (error) 
    return 1;
  else 
    return 0;

}

/* -------------------------------------------------------- */
/*
  dvd - Performs PERMANENT TIME shift
*/
gaint ffDVDtimeShift (struct gafunc *pfc, struct gastat *pst) {
  gaint rc,isiz,jsiz,nn,error=0,i,j;
  gadouble *gr;
  gadouble *xx=NULL,*yy=NULL,undef;
  gadouble (*iconv) (gadouble *, gadouble);
  struct gagrid *pgr;
  char *gru;
  size_t sz;
  float tshift;

  /* Evaluate args number */
  if (pfc->argnum!=2) {
    gaprnt (0,"Error from SHIFT: Two arguments expected\n");
    return (1);
  }

  /* Evaluate the 1st expression, type = grid */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) { error=1; goto err; }
  if (pst->type!=1) {
    gaprnt (0,"Error from SHIFT: The 1st argument is not a grid expression \n");
    error=1; goto err;
  }

  /* Evaluate the 2st expression, type = number, the time-shift [mn] */
  if (valprs(pfc->argpnt[1],&tshift)==NULL) {
    tshift = 0;
  }
  //int toff=nearbyint(tshift);
  int toff=tshift;

  /* Get the grid */
  pgr = pst->result.pgr;     /* grid */

  /* time shift */
  if (toff && (pgr->idim == 3 || pgr->jdim == 3)) {
    struct dt Delta;
//    int tnegative = 0;
//    if (toff<0) {
//      toff *= -1;
//      tnegative = 1;
//    }
    Delta.yr = 0;
    Delta.mo = 0;
    Delta.dy = 0;
    Delta.hr = 0;
    Delta.mn = toff;
    struct dt Time;
    if (pgr->jdim == 3) {
      Time.yr = (int) pgr->jvals[0];
      Time.mo = (int) pgr->jvals[1];
      Time.dy = (int) pgr->jvals[2];
      Time.hr = (int) pgr->jvals[3];
      Time.mn = (int) pgr->jvals[4];
      printf("\n>>> tshift in Jdim: %d.%d.%d %d:%d -> ", Time.yr, Time.mo, Time.dy, Time.hr, Time.mn);
//      if (tnegative) {
//	timsub(&Delta, &Time);
//      } else {
	timadd(&Delta, &Time);
//      }
      printf("%d.%d.%d %d:%d PERMANENT\n", Time.yr, Time.mo, Time.dy, Time.hr, Time.mn);
      pgr->jvals[0] = (float) Time.yr;
      pgr->jvals[1] = (float) Time.mo;
      pgr->jvals[2] = (float) Time.dy;
      pgr->jvals[3] = (float) Time.hr;
      pgr->jvals[4] = (float) Time.mn;
    } else {
      Time.yr = (int) pgr->ivals[0];
      Time.mo = (int) pgr->ivals[1];
      Time.dy = (int) pgr->ivals[2];
      Time.hr = (int) pgr->ivals[3];
      Time.mn = (int) pgr->ivals[4];
      printf("\n>>> tshift on Idim: %d.%d.%d %d:%d -> ", Time.yr, Time.mo, Time.dy, Time.hr, Time.mn);
//      if (tnegative) {
//	timsub(&Delta, &Time);
//      } else {
	timadd(&Delta, &Time);
//      }
      printf("%d.%d.%d %d:%d PERMANENT\n\n", Time.yr, Time.mo, Time.dy, Time.hr, Time.mn);
      pgr->ivals[0] = (float) Time.yr;
      pgr->ivals[1] = (float) Time.mo;
      pgr->ivals[2] = (float) Time.dy;
      pgr->ivals[3] = (float) Time.hr;
      pgr->ivals[4] = (float) Time.mn;
    }
  }

err:
  /* release memory */
  if (error) if (pst) gafree (pst); 
  if (error) 
    return 1;
  else 
    return 0;

}

/* -------------------------------------------------------- */
/*
  dvd - Performs 1/(d^2) averaging
*/
gaint ffDVDraigrid (struct gafunc *pfc, struct gastat *pst) {
  gaint rc,i,j,error=0,isiz,jsiz,nn,krad,k;
  gadouble *gr;
  gadouble *xx=NULL,*yy=NULL,undef;
  gadouble (*iconv) (gadouble *, gadouble);
  struct gagrid *pgr;
  char *gru;
  size_t sz;
  float radius;
  char method[]="1/d2";
  float *weight_out, *weight;
  gadouble *xgrid_out;
  int LOG=0;
  int istaz,jstaz,igrid,jgrid;
  float wght,xgrid_staz;

  /* Evaluate args number */
  if (pfc->argnum<2 || pfc->argnum>3) {
    gaprnt (0,"Error from RAIGRID: Two or Three arguments expected\n");
    return (1);
  }

  /* Evaluate the 1st expression, type = grid, the binned data values to be filtered */
  rc = gaexpr(pfc->argpnt[0],pst);
  if (rc) { error=1; goto err; }
  if (pst->type!=1) {
    gaprnt (0,"Error from RAIGRID: The 1st argument is not a grid expression \n");
    error=1; goto err;
  }

  /* Evaluate the 2st expression, type = number, the radius of the filter */
  if (valprs(pfc->argpnt[1],&radius)==NULL) {
    sprintf (pout,"Error from %s: Invalid Argument\n","radius");
    gaprnt (0,pout);
    sprintf (pout,"  Expecting arg%d: %s to be a value\n",2,pfc->argpnt[1]);
    gaprnt (0,pout);
    return (1);
  }
  krad=radius+0.5;
  int ksiz=2*krad+1;

  /* Evaluate the 3st expression, type = string, the method of the filter */
  if (pfc->argnum>2) {
    getwrd(method,pfc->argpnt[2],4);
    lowcas(method);
  }

  #ifdef DVDlog
  printf ("radius:%f method:<%s>\n",radius,method);
  #endif

  /* Check environment */
  if (pst->idim!=0) {
    gaprnt (0,"Error from RAIGRID: The first varying dimension must be X \n");
    error=1; goto err;
  }
  if (pst->jdim!=1) {
    gaprnt (0,"Error from RAIGRID: The second varying dimension must be Y \n");
    error=1; goto err;
  }

  /* Get the grid */
  pgr = pst->result.pgr;     /* grid */
  gr  = pgr->grid;
  gru = pgr->umask;

  /* define output grid */
//  gr2  = pgr2->grid;

  /* get i dim coordinates (generic) */
  if (pgr->idim>-1) {
    xx = NULL;
    sz = sizeof(gadouble)*pgr->isiz;
    xx = (gadouble *)galloc(sz,"xx");
    if (xx==NULL) { error=1; goto err; }
    if (pgr->idim==3) {
      j=0;
      for (i=pgr->dimmin[pgr->idim];i<=pgr->dimmax[pgr->idim];i++) {
	*(xx+j) = (gadouble)i;
	j++;
      }
    } else {
      iconv = pgr->igrab;
      j=0;
      for (i=pgr->dimmin[pgr->idim];i<=pgr->dimmax[pgr->idim];i++) {
	*(xx+j) = iconv(pgr->ivals,(gadouble)i);
	j++;
      }
    }
  }
  /* get j dim coordinates (generic) */
  if (pgr->jdim>-1) {
    yy = NULL;
    sz = sizeof(gadouble)*pgr->jsiz;
    yy = (gadouble *)galloc(sz,"yy");
    if (yy==NULL) { error=1; goto err; }
    if (pgr->jdim==3) {
      j=0;
      for (i=pgr->dimmin[pgr->jdim];i<=pgr->dimmax[pgr->jdim];i++) {
	*(yy+j) = (gadouble)i;
	j++;
      }
    } else {
      iconv = pgr->jgrab;
      j=0;
      for (i=pgr->dimmin[pgr->jdim];i<=pgr->dimmax[pgr->jdim];i++) {
	*(yy+j) = iconv(pgr->jvals,(gadouble)i);
	j++;
      }
    }
  }
  isiz=pgr->isiz;
  jsiz=pgr->jsiz;
  undef=pgr->undef;
  nn=isiz*jsiz;

  /* init vecs */
  xgrid_out=(float *)malloc(sizeof(float)*(nn));
  weight_out=(float *)malloc(sizeof(float)*(nn));
  weight=(gadouble *)malloc(sizeof(gadouble)*((2*krad+1)*(2*krad+1)));
  for(k=0;k<nn;k++){
    xgrid_out[k]=undef;
    weight_out[k]=undef;
  }
  for(k=0;k<((2*krad+1)*(2*krad+1));k++){
    weight[k]=undef;
  }

  /* defining weights: define distance matrix */
  for(i=-krad;i<=krad;i++){
    for(j=-krad;j<=krad;j++){
      k=(ksiz*(j+krad) + (i+krad));
      weight[k]=sqrt(i*i+j*j);
      if (i==0 && j==0) weight[k]=0.5;
      weight[k]=1./weight[k];					//pesi reciproci
      if (weight[k]<1./krad) weight[k]=undef;			//metto a undef fuori dal cerchio
      if (weight[k]!=undef) weight[k]=weight[k]*weight[k];	//quadrato dei pesi
    }
  }

  /* applying weights */
  /* istaz: punto grigliato di gr, i: indice del peso centrato su istaz, igrid: punto griglia dato da istaz+i */
  for(istaz=0;istaz<isiz;istaz++){
    for(jstaz=0;jstaz<jsiz;jstaz++){
      xgrid_staz=*(gr + (isiz*jstaz) + istaz);
      /* caso dato non valido: non ho niente da propagare */
      if (xgrid_staz==undef) continue;
      for(i=-krad;i<=krad;i++){
	igrid=istaz+i;
	/* spostadomi nel peso in X sono fuori dalla matrice: esco */
	if(igrid<0 || igrid>=isiz) continue;
	for(j=-krad;j<=krad;j++){
	  jgrid=jstaz+j;
	  /* spostadomi nel peso in Y sono fuori dalla matrice: esco */
	  if(jgrid<0 || jgrid>=jsiz) continue;
	  wght=*(weight + ksiz*(j+krad) + (i+krad));
	  /* caso peso non valido: lascio stare la matrice */
	  if (wght==undef) continue;
	  k=(isiz*jgrid) + igrid;
	  /* caso peso valido e punto vergine nella matrice: lo inserisco */
#ifdef DVDlog
printf("PRE  staz:(%2d,%2d) grid:(%2d,%2d) shift:(%2d,%2d) xstaz:%7.2f wght:%7.2f w_out:%7.2f x_out:%7.2f\n",
  istaz,jstaz,igrid,jgrid,i,j,
  (xgrid_staz!=undef ? xgrid_staz : -999.9),
  (wght!=undef ? wght : -999.9),
  (weight_out[k]!=undef ? weight_out[k] : -999.9),
  (xgrid_out[k]!=undef ? xgrid_out[k] : -999.9));
#endif

	  if (xgrid_out[k]==undef) {
	    xgrid_out[k]=xgrid_staz*wght;
	    weight_out[k]=wght;
	  /* caso peso valido e punto non vergine nella matrice: lo sommo */
	  } else {
	    xgrid_out[k]=xgrid_out[k]+xgrid_staz*wght;
	    weight_out[k]=weight_out[k]+wght;
	  }
#ifdef DVDlog
printf("SUM                                          xstaz:%7.2f wght:%7.2f w_out:%7.2f x_out:%7.2f\n",
  (xgrid_staz!=undef ? xgrid_staz : -999.9),
  (wght!=undef ? wght : -999.9),
  (weight_out[k]!=undef ? weight_out[k] : -999.9),
  (xgrid_out[k]!=undef ? xgrid_out[k] : -999.9));
#endif
	}
      }
    }
  }

  /* normalizing by weight sum */
  for(k=0;k<nn;k++){
    if(weight_out[k]!=undef) xgrid_out[k]/=weight_out[k];
  }

  /* restore original exact values where defiend */
  for(k=0;k<nn;k++){
    if(*(gr + k)!=undef) xgrid_out[k]=*(gr + k);
  }

  /* log weights and matrix */
  if (LOG) {
    printf("WEIGHTS\n\t");
    for(i=-krad;i<=krad;i++){
      printf(" i=%d\t",i);
    }
    printf("\n");
    for(j=-krad;j<=krad;j++){
      printf("j=%d\t",j);
      for(i=-krad;i<=krad;i++){
	wght=*(weight + ksiz*(j+krad) + (i+krad));
	printf("%+.2f\t",(wght!=undef ? wght: -9.99));
	if (i==krad)  printf("\n");
      }
    }
    printf("MATRIX NEW (OLD)\n\t");
    for(i=0;i<isiz;i++){
      printf(" i=%d\t",i);
    }
    printf("\n");
    float new,old,wgh;
    for(j=0;j<jsiz;j++){
      for(i=0;i<isiz;i++){
	if (i==0)  printf("NEW j=%d\t",j);
	new=*(xgrid_out + isiz*j + i);
	printf("%d\t",(new!=undef ? (int)new : -999));
	if (i==isiz-1)  printf("\n");
      }
      for(i=0;i<isiz;i++){
	if (i==0)  printf("OLD j=%d\t",j);
	old=*(gr + isiz*j + i);
	printf("%d\t",(old!=undef ? (int)old : -999));
	if (i==isiz-1)  printf("\n");
      }
      for(i=0;i<isiz;i++){
	if (i==0)  printf("WGH j=%d\t",j);
	wgh=*(weight_out + isiz*j + i);
	printf("%.2f\t",(wgh!=undef ? wgh : -9.99));
	if (i==isiz-1)  printf("\n--------------------------------------------------\n");
      }
    }
  }

  /* saving output grid */
  pgr->grid = xgrid_out;

err:
  /* release memory */
  if (error) if (pst) gafree (pst); 
  if (xx!=NULL) gree(xx,"f400");
  if (yy!=NULL) gree(yy,"f401");
  if (error) 
    return 1;
  else 
    return 0;
}

/* -------------------------------------------------------- */
/*
  dvd - Performs box averaging between two grids
*/
gaint ffDVDbterp (struct gafunc *pfc, struct gastat *pst) {
  struct gastat pst2;
  struct gagrid *pgr1,*pgr2;
  gadouble (*iconv) (gadouble *, gadouble);
  gadouble *x1=NULL,*x2=NULL,*y1=NULL,*y2=NULL;
  gadouble *gr1,*gr2;
  gadouble xd,yd,rd,t1,t2,tstrt,tscl;
  gaint rc,i,j,i1,i2,ij,ij2,idir2,jdir2,j1,j2,error=0;
  char *gr1u,*gr2u;
  size_t sz;
  int isiz1,jsiz1,isiz2,jsiz2;
  gadouble u1,u2;
  
   /* Evaluate args number */
  if (pfc->argnum!=2) {
    gaprnt (0,"Error from BTERP: Two arguments expected \n");
    return (1);
  }

   /* Evaluate the 2nd expression, the destination grid */
  pst2=*pst;
  rc = gaexpr(pfc->argpnt[1],pst);
  if (rc) { error=1; goto err; }
  if (pst->type!=1) {
    gaprnt (0,"Error from BTERP: The 2nd argument is not a grid expression \n");
    error=1; goto err;
  }

  /* Check environment */
  if (pst->idim!=0 && pst->idim==1) {
    gaprnt (0,"Error from BTERP: The grids must vary on LON and LAT \n");
    error=1; goto err;
  }

  /* Evaluate the 1st expression, the data values to be interpolated */
  rc = gaexpr(pfc->argpnt[0],&pst2);
  if (rc) { error=1; goto err; }
  if (pst2.type!=1) {
    gaprnt (0,"Error from BTERP: The 1st argument is not a grid expression \n");
    error=1; goto err;
  }

  /* Verify that the varying dimensions are equivalent */
  if (pst->idim!=pst2.idim || pst->jdim!=pst2.jdim) {
    printf ("Error from BTERP: Grids have different varying dimensions \n");
    error=1; goto err;
  }

  /* Get the grids */
  pgr1 = pst2.result.pgr;     /* data source grid (input)  */
  pgr2 = pst->result.pgr;     /* destination grid (result) */
  gr1  = pgr1->grid;
  gr1u = pgr1->umask;
  gr2  = pgr2->grid;
  gr2u = pgr2->umask;

  /* get i dim coordinates for both grids */
  x1 = NULL;
  sz = sizeof(gadouble)*pgr1->isiz;
  x1 = (gadouble *)galloc(sz,"x1");
  if (x1==NULL) { error=1; goto err; }
  iconv = pgr1->igrab;
  j=0;
  for (i=pgr1->dimmin[pgr1->idim];i<=pgr1->dimmax[pgr1->idim];i++) {
    *(x1+j) = iconv(pgr1->ivals,(gadouble)i);
    j++;
  }
  x2 = NULL;
  sz = sizeof(gadouble)*pgr2->isiz;
  x2 = (gadouble *)galloc(sz,"x2");
  if (x2==NULL) { error=1; goto err; }
  iconv = pgr2->igrab;
  j=0;
  for (i=pgr2->dimmin[pgr2->idim];i<=pgr2->dimmax[pgr2->idim];i++) {
    *(x2+j) = iconv(pgr2->ivals,(gadouble)i);
    j++;
  }

  /* get j dim coordinates for both grids */
  y1 = NULL;
  sz = sizeof(gadouble)*pgr1->jsiz;
  y1 = (gadouble *)galloc(sz,"y1");
  if (y1==NULL) { error=1; goto err; }
  iconv = pgr1->jgrab;
  j=0;
  for (i=pgr1->dimmin[pgr1->jdim];i<=pgr1->dimmax[pgr1->jdim];i++) {
    *(y1+j) = iconv(pgr1->jvals,(gadouble)i);
    j++;
  }
  y2 = NULL;
  sz = sizeof(gadouble)*pgr2->jsiz;
  y2 = (gadouble *)galloc(sz,"y2");
  if (y2==NULL) { error=1; goto err; }
  iconv = pgr2->jgrab;
  j=0;
  for (i=pgr2->dimmin[pgr2->jdim];i<=pgr2->dimmax[pgr2->jdim];i++) {
    *(y2+j) = iconv(pgr2->jvals,(gadouble)i);
    j++;
  }

  /* see which way x y vary with i : -1 decresce, +1 cresce */
  if (pgr2->ilinr == 0) {
    idir2 = -1;
    if (*(x2+1) > *x2) idir2 = 1;
  } else {
    idir2 = 1;
  }
  if (pgr2->jlinr == 0) {
    jdir2 = -1;
    if (*(y2+1) > *y2) jdir2 = 1;
  } else {
    jdir2 = 1;
  }

/* box average */
  isiz1=pgr1->isiz;
  jsiz1=pgr1->jsiz;
  isiz2=pgr2->isiz;
  jsiz2=pgr2->jsiz;
  u1=pgr1->undef;
  u2=pgr2->undef;
  DVDboxAverage(gr1, u1, x1, y1, isiz1, jsiz1, gr2, u2, x2, y2, isiz2, jsiz2);

err:
  /* release memory */
  if (error) if (pst) gafree (pst); 
  if (x1!=NULL) gree(x1,"f400");
  if (x2!=NULL) gree(x2,"f401");
  if (y1!=NULL) gree(y1,"f402");
  if (y2!=NULL) gree(y2,"f403");
  if (gr1!=NULL) gree (gr1, NULL);
  if (gr1u!=NULL) gree (gr1u, NULL);
  if (error) 
    return 1;
  else 
    return 0;
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
void DVDboxAverage(gadouble *gr1, gadouble u1, gadouble *x1, gadouble *y1, int isiz1, int jsiz1, gadouble *gr2, gadouble u2, gadouble *x2, gadouble *y2, int isiz2, int jsiz2) {

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
      *(gr2 + (isiz2*j2) + i2) = DVDgetValue(gr1, u1, x1, y1, dx1, dy1, isiz1, jsiz1, u2, lon2, lat2, dx2, dy2);
    }
  }

/* log */
#ifdef DVDlog
  DVDlogGrid("GRID1", gr1, u1, x1, y1, isiz1, jsiz1);
  DVDlogGrid("GRID2", gr2, u2, x2, y2, isiz2, jsiz2);
#endif

}

/* -------------------------------------------------------- */
/*
  torna il valore pesato di gr1 sull'area definita intorno a lon2, lat2 con passo dx2, dy2
  i valori mancanti di gr1 non contribuiscono
*/
gadouble DVDgetValue(gadouble *gr1, gadouble u1, gadouble *x1, gadouble *y1, gadouble dx1, gadouble dy1, int isiz1, int jsiz1, gadouble u2, gadouble lon2, gadouble lat2, gadouble dx2, gadouble dy2) {
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
	  if (fabs(value - u1) < 10*FLT_EPSILON) continue;
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

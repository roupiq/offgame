/* lrslib.c     library code for lrs                     */

/* This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */

/*2022.1.22  lrs now has 3 modes controlled by these routines: 
 lrs_run      default, V->H or H->V conversion               
 redund_run   redundancy removal, redund or redund_list options 
 fel_run      Fourier-Motzkin elimination, project or elimiminate options 
*/

/* modified by Gary Roumanis and Skip Jordan for multithread compatability */
/* truncate needs mod to supress last pivot */
/* need to add a test for non-degenerate pivot step in reverse I guess?? */
/* Copyright: David Avis 2005,2022 avis@cs.mcgill.ca         */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <limits.h>
#include <libgen.h>
#include <sys/time.h>
#include "lrsrestart.h"
#include "lrslib.h"

#ifdef PLRS
#include <omp.h>
//#include <stdatomic.h>
//static _Atomic long overflow_detected=0;
static long overflow_detected=0;     /* =0 no overflow =1 overflowed */
#else
static long overflow_detected=0;     /* =0 no overflow =1 overflowed */
#endif

static long overflow=0; /* =0 no overflow =1 restart  */
static unsigned long dict_limit=50;
static unsigned long dict_count=1, cache_tries=0, cache_misses=0;

/* Variables and functions global to this file only */

static long lrs_checkpoint_seconds = 0;

static long lrs_Q_count = 0;	/* Track how many lrs_dat records are 
					   allocated */
static size_t infileLen;                /* length of cache of input file       */
static char *infile = NULL;             /* cache of input for restart          */
static char infilename[PATH_MAX];
static char outfilename[PATH_MAX];
static char tmpfilename[PATH_MAX];

static lrs_dat *lrs_Q_list[MAX_LRS_Q + 1];

static lrs_dic *new_lrs_dic (long m, long d, long m_A);


static void cache_dict (lrs_dic ** D_p, lrs_dat * Q, long i, long j);
static long check_cache (lrs_dic ** D_p, lrs_dat * Q, long *i_p, long *j_p);
static void save_basis (lrs_dic * D, lrs_dat * Q);

static void pushQ (lrs_dat * Q, long m, long d, long m_A);

static int tmpfd;

#ifndef TIMES
static void ptimes (void);
static double get_time(void);
#endif

char *lrs_basename(char *path);

/*******************************/
/* signals handling            */
/*******************************/
#ifndef SIGNALS
static void checkpoint ();
static void die_gracefully ();
static void setup_signals (void);
static void timecheck ();
#endif

/*******************************/
/* functions  for external use */
/*******************************/

/******************************************************************/
/* lrs_run is the main reverse search part of lrs                 */
/* should be called by lrsv2_main which does setup and close also */
/******************************************************************/
long
lrs_run ( lrs_dic *P, lrs_dat * Q)

{
	
  lrs_dic *Pin;
  lrs_mp_matrix Lin;		/* holds input linearities if any are found             */
  long col;			/* output column index for dictionary                   */
  long startcol = 0;
  long prune = FALSE;		/* if TRUE, getnextbasis will prune tree and backtrack  */

  Pin=P;


/*********************************************************************************/
/* Step 1: Find a starting cobasis from default of specified order               */
/*         P is created to hold  active dictionary data and may be cached        */
/*         Lin is created if necessary to hold linearity space                   */
/*         Print linearity space if any, and retrieve output from first dict.    */
/*********************************************************************************/

  if (lrs_getfirstbasis (&P, Q, &Lin, FALSE)==0 || overflow_detected)
     {
      if(Q->verbose && !Q->mplrs && overflow_detected)
        lrs_warning(Q,"warning","*overflow beginning of step 1");
      lrs_free_dic(P,Q);            /* note Q is not free here and can be reused     */
      return 1;
    }

  /* Pivot to a starting dictionary                      */
  /* There may have been column redundancy               */
  /* If so the linearity space is obtained and redundant */
  /* columns are removed. User can access linearity space */
  /* from lrs_mp_matrix Lin dimensions nredundcol x d+1  */



  if (Q->homogeneous && Q->hull)
    startcol++;			/* col zero not treated as redundant   */

  if(!Q->lponly && !Q->restart && Q->child == 0)  /* printed once in multithreading */
      for (col = startcol; col < Q->nredundcol; col++)	/* print linearity space */
          lrs_printoutput (Q, Lin[col]);	  /* Array Lin[][] holds the coeffs.     */

  if (Q->child >1)
     Q->giveoutput=FALSE;

  if(Q->nredundcol > 0)
     lrs_clear_mp_matrix(Lin,Q->nredundcol,Q->n);

  if(Q->plrs && Q->child==0)   /* we just wanted initial output */
    {
      lrs_free_dic(P,Q);
      return 0;
    }

/*********************************************************************************/
/* Step 3: Terminate if lponly option set, otherwise initiate a reverse          */
/*         search from the starting dictionary. Get output for each new dict.    */
/*********************************************************************************/



  /* We initiate reverse search from this dictionary       */
  /* getting new dictionaries until the search is complete */
  /* User can access each output line from output which is */
  /* vertex/ray/facet from the lrs_mp_vector output         */
  /* prune is TRUE if tree should be pruned at current node */
  do
    {
//2015.6.5   after maxcobases reached, generate subtrees that have not been enumerated
//2018.1.19  fix printcobasis bug when maxcobases set
//2019.5.8   new givoutput flag to avoid printing restart cobases

     prune=lrs_checkbound(P,Q);

     if (!prune && Q->giveoutput)
       {
          lrs_open_outputblock();               /* keeps output together when using mplrs */

          for (col = 0; col <= P->d; col++)          /* print output if any */
           if (!Q->testlin && lrs_getsolution (P, Q, Q->output, col))
	       lrs_printoutput (Q, Q->output);
          lrs_close_outputblock();
       }
     else
         Q->giveoutput=TRUE;           /* first output supressed for restart */

/*2020.3.9  bounds on objective function check corrected */
/*2023.6.30 do not proceed from high incidence bases     */
     if(Q->nincidence >= Q->maxincidence && P->depth >= Q->minprunedepth)
      {
       if(Q->verbose && !Q->mplrs)
         fprintf(lrs_ofp,"\n*pruning: incidence=%ld depth=%ld",Q->nincidence,P->depth);
       prune=TRUE;
      }

     if ((Q->maxcobases > 0) &&  (Q->count[2] >=Q->maxcobases))
        {
          prune=TRUE;
          if( !lrs_leaf(P,Q))                /* do not return cobases of a leaf */     
               lrs_return_unexplored(P,Q);
         }

     save_basis(P,Q);

/*2023.1.9*/
     if(overflow_detected)
        {
         Q->m=Pin->m;
         lrs_free_dic(Pin,Q);
         if(Q->debug)
            fprintf(lrs_ofp,"\n*Overflow detected");
         fflush(lrs_ofp);
         return 0;
        }

    }while ( !Q->lponly && lrs_getnextbasis (&P, Q, prune));  // do ...

  
  if (Q->lponly)
    lrs_lpoutput(P,Q,Q->output);
  else
    if(!Q->plrs)
       lrs_printtotals (P, Q);	/* print final totals, including estimates       */
  Q->m=P->m;
  lrs_free_dic(P,Q);            /* note Q is not free here and can be reused     */

  return 0;
}
/*********************************************/
/* end lrs_run                               */
/*********************************************/


/*******************************************************/
/* redund_run is main loop for redundancy removal      */
/*******************************************************/
long
redund_run  ( lrs_dic *P, lrs_dat * Q)

{
  lrs_mp_matrix Ain;            /* holds a copy of the input matrix to output at the end */

  long ineq;			/* input inequality number of current index             */

  lrs_mp_matrix Lin;		/* holds input linearities if any are found             */

  long i, j, d, m;
  long nlinearity;		/* number of linearities in input file                  */
  long lastdv;
  long index;			/* basic index for redundancy test */
  long min,nin;
  const char * redlab[] = { "*sr", "*nr","*re","*li"};
  long debug = Q->debug;
  long *redineq  = Q->redineq;

/*********************************************************************************/

/* if non-negative flag is set, non-negative constraints are not input */
/* explicitly, and are not checked for redundancy                      */


  m = P->m_A;              /* number of rows of A matrix */   
  d = P->d;
  min=Q->m;
  nin=Q->n;
  Q->Ain = lrs_alloc_mp_matrix (Q->m, Q->n);     /* make a copy of A matrix for output later            */
  Ain=Q->Ain;

  Q->printcobasis=0;

  for (i = 1; i <= m; i++)
    {
      for (j = 0; j <= d; j++)
        copy (Ain[i][j], P->A[i][j]);
    }


/*********************************************************************************/
/* Step 1: Find a starting cobasis from default of specified order               */
/*         Lin is created if necessary to hold linearity space                   */
/*********************************************************************************/

  if ( lrs_getfirstbasis (&P, Q, &Lin, TRUE)==0 || overflow_detected) 
         return 1;

  if(Q->mplrs && Q->redundphase==1 && Q->tid == 1)  
    {
      for (i = 0; i < Q->nlinearity; i++)
          redineq[Q->linearity[i]] = 2;
      goto done;        /* consumer rank=1 print only */
    }
  /* Pivot to a starting dictionary                      */
  /* There may have been column redundancy               */
  /* If so the linearity space is obtained and redundant */
  /* columns are removed. User can access linearity space */
  /* from lrs_mp_matrix Lin dimensions nredundcol x d+1  */

  d=P->d;
  lastdv=Q->lastdv;
  if(Q->nredundcol > 0)
     lrs_clear_mp_matrix(Lin,Q->nredundcol,Q->n);
  if (Q->testlin)      /* we added extra var and column to look for interior point */
   {

       lrs_getsolution(P,Q,Q->output,0);
       lrs_lpoutput(P,Q,Q->output);
       if( negative (P->objnum) )
         return 1;
       remove_artificial(P,Q);
       if(Q->debug)
         fprintf(lrs_ofp,"\n*Q->fel=%ld Q->tid=%ld Q->hiddenlin=%ld",Q->fel,Q->tid, Q->hiddenlin);
   }

   if(Q->mplrs)  /* used for workers at which did not do the lp step */
     cleanupA(P,Q);

  if(overflow_detected)
    return 1;


/*********************************************************************************/
/* Step 2: Test rows i where redineq[i]=1 for redundancy                         */
/*********************************************************************************/

/* note some of these may have been changed in getting initial dictionary        */
  m = P->m_A;
  d = P->d;
  nlinearity = Q->nlinearity;
  lastdv = Q->lastdv;
/* linearities are not considered for redundancy */

  for (i = 0; i < nlinearity; i++)
    redineq[Q->linearity[i]] = 2L;

  if (Q->debug)
    {
      fprintf (lrs_ofp, "\n*Step 2: redundphase=%ld testlin=%ld redineq:\n",Q->redundphase, Q->testlin);
      for (i = 1; i <= m; i++)
	fprintf (lrs_ofp, " %ld", redineq[i]);
    }


/* Q->redundphase=1 indicates no hidden linearities                      */
/* in this case only one lp per row is performed, the redundancy check     */
/* otherwise two lps are performed to test for linearity                   */


/* rows 0..lastdv are cost, decision variables, or linearities  */
/* other rows need to be tested                                */

  if(redineq[0] == 0)     /* 2020.9.11 patch, this was set to 1 in readredund but got reset somewhere */
      redineq[0] = 1;

  if (debug)
     fprintf (lrs_ofp, "\nlastdv=%ld, redineq[0]=%ld", lastdv, redineq[0]);

/* main  loop for checking redundancy of +1 rows in  redineq */

/*2023.11.15 lrs can skip looking for linearities but mplrs can't */
  if( !Q->testlin && Q->redund && !Q->mplrs) 
     Q->redundphase=1;

  if(Q->debug)
    fprintf(lrs_ofp,"\n*testlin=%ld redund=%ld redundphase=%ld",Q->testlin, Q->redund,Q->redundphase);

  if(!Q->mplrs)
   {
    if(Q->fel)
       fprintf(lrs_ofp,"\n*removing redundant rows");
    else if (!Q->testlin)
               fprintf(lrs_ofp,"\n*checking for redundant rows only");
         else
            if(Q->fullredund)
               fprintf(lrs_ofp,"\n*finding minimum representation");
   }

/* redundphase=0 lin+ine tested      =1 ine only after hidden linearities removed*/

  for (index = lastdv + redineq[0]; index <= m + d; index++)
    {
      ineq = Q->inequality[index - lastdv];	/* the input inequality number corr. to this index */
      redineq[0] = ineq;                        /* used for restarting after arithmetic overflow   */
     
      if( redineq[ineq]==1 )
        {
         redineq[ineq] = checkindex (P, Q, index,Q->redundphase);
         if(overflow_detected)
           return 1;
         if(Q->debug)
	      fprintf (lrs_ofp, "\ncheck index=%ld, inequality=%ld, redineq=%ld", index, ineq, redineq[ineq]);
         if(!Q->fel && Q->verbose )
            lrs_printrow (redlab[redineq[ineq]+1], Q, Ain[ineq], Q->inputd);
         fflush(lrs_ofp);
        }

    }				/* end for index ..... */
 
done:
if(Q->debug)
  {
   fprintf(lrs_ofp,"\n*done: rank=%ld redundphase=%ld",Q->tid,Q->redundphase);
   fprintf (lrs_ofp, "\n*redineq:");
     for (i = 1; i <= m; i++)
	fprintf (lrs_ofp, " %ld", redineq[i]);
  }

/* 2023.6.3 all except boss or consumer go home */
  
 if(Q->mplrs && Q->tid > 1 )
    {    
     lrs_clear_mp_matrix(Q->Ain,min,nin); 
     Q->m=P->m;
     lrs_free_dic(P,Q);   
     return 0;
    }
  

 if(Q->fel && Q->hull)
   lrs_clear_mp_matrix(Q->Ain,min,nin);
 else
   redund_print(P,Q);


 if(!Q->fel)
   lrs_clear_mp_matrix(Q->Ain,min,nin); 
 lrs_free_dic(P,Q);
 return 0;
}

/***********redund_run************************/
void
cleanupA(lrs_dic *P,lrs_dat *Q)
{
/* remove duplicate rows from the raw A matrix including those */
/* which appear as an identity row x_i=x_j for slacks i and j  */
/* no indices updated except to mark linearities in redineq    */
/* duplicate and identity rows are simply zeroed out           */

  lrs_mp_matrix A1;
  lrs_mp_matrix A = P->A;
  long i, j, d, m;
  long k,oneindex,nonzero;
  long debug = Q->debug;
//long verbose = Q->verbose;
  long lastdv=Q->lastdv;
  long *redineq  = Q->redineq;
  long *Row=P->Row;
  long *Col=P->Col;
  m = P->m_A;              /* number of rows of A matrix */
  d = P->d;
  A1=lrs_alloc_mp_matrix(m,d);
  for(i=1;i<=m;i++)
     for (j=0;j<=d;j++)
       copy(A1[i][j],A[i][j]);

/* linearities are not considered for duplicates */

  for (i = 0; i < Q->nlinearity; i++)
     redineq[Q->linearity[i]] = 2L;

  if(debug)
    {
     fprintf (lrs_ofp, "\n*cleanupA_start *redineq:");
     for (i = 1; i <= m; i++)
        fprintf (lrs_ofp, " %ld", redineq[i]);
    }

/* clean up A reducing by Gcd and zeroing out duplicate rows */
 for(i=lastdv+1;i<=m;i++)
  if(redineq[Row[i]] != 2)
    reducearray(A[Row[i]],d+1);

/* check non-decision variable rows of the dictionary */
 for(i=lastdv+1;i<=m;i++)
  if(redineq[Row[i]] != 2)
   {

/* first we check for identity rows */

    oneindex=-1;
    nonzero=0;
    j=0;
    while(j <= d && nonzero <= 1)
      {
        if(!zero(A[Row[i]][Col[j]]))
         {
          nonzero++;
          if(one(A[Row[i]][Col[j]]))
             oneindex=j;
         }
        j++;
      }
     if(!nonzero || (nonzero==1 && oneindex >= 0))     /* zero or identity row found*/
       {
          if(Q->debug)
             fprintf(lrs_ofp,"\n* nonzero=%ld i=%ld m=%ld d=%ld j=%ld oneindex=%ld",nonzero,i,m,d,j,oneindex);
          if(nonzero)
            itomp(ZERO, A[Row[i]][Col[oneindex]]);
          redineq[Row[i]]=-1;    
          for (k=0;k<=d;k++)
           itomp(ZERO, A[Row[i]][k]);
       }
     else   /* now we check for duplicate pairs rows */
       {
        for (j=lastdv+1;j<i;j++)      
         {
           k=0;

           while(k<=d)
              if( ! mp_equal( A[Row[i]][k], A[Row[j]][k] ) )
                break;
              else
                k++;
           if( k > d )    /* zero out an equal row */
             {
               if(Q->debug)
                  fprintf(lrs_ofp,"\n*i=%ld j=%ld Row[%ld]=Row[%ld]",i,j,Row[i],Row[j]);
               redineq[Row[i]]=-1;
               for (k=0;k<=d;k++)
                   itomp(ZERO, A[Row[i]][k]);
               break;
             }
          }   /* for j */
        }    /* else now we check */       
   }        /* for i */



/* restore non duplicate rows that were reduced */
  for(i=1;i<=m;i++)
     for (j=0;j<=d;j++)
        if(!zero(A[i][j]))
          {
           for (k=0;k<=d;k++)
               copy(A[i][k],A1[i][k]);
           break;
          }

  lrs_clear_mp_matrix (A1,m,d);
  if(debug)
    {
     fprintf (lrs_ofp, "\n*cleanupA_end *redineq:");
     for (i = 1; i <= m; i++)
        fprintf (lrs_ofp, " %ld", redineq[i]);
    }
  if(debug)
    {
     printA(P,Q);
     prawA(P,Q);
    }
  debug=FALSE;
}     /* end of cleanupA() */

void  remove_artificial(lrs_dic *P,lrs_dat *Q)
{
  long k,cob;
  long m=P->m_A;
  long lastdv=Q->lastdv;
  long *B = P->B;

  long debug = Q->debug;

       if(debug)
             printA(P,Q);
       if(P->C[0] != lastdv)   /* pivot artificial var to cobasis */
         {
          k=0;
          while (k <= m && B[k]!=lastdv)
               k++;
          if(k>m)
           {
             fprintf(lrs_ofp,"\n*artificial variable missing : bye bye\n");
             exit(1);
           }
          cob=0;
          while (cob < lastdv-1)
            {
              if ( !zero(P->A[P->Row[k]][P->Col[cob]]))
                      break;
              cob++;
             }
          if(cob == lastdv-1)
           {
             fprintf(lrs_ofp,"\n*artificial variable cannot leave basis : bye bye\n");
             exit(1);
           }

           pivot(P,Q,k,cob);
           update(P,Q,&k,&cob);

            if(debug)
              printA(P,Q);
         }
       removecobasicindex(P,Q,0); /* lying: we remove the artificial basic variable */
       Q->lastdv--;
       Q->inputd--;
       if(debug)
          printA(P,Q);
}             /* remove_artificial */

void  redund_print(lrs_dic *P,lrs_dat *Q)
{
  long i,j,k,cob,index;
  long nredund;                 /* number of redundant rows in input file               */
  long *redineq=Q->redineq;
  long *inequality = Q->inequality;
  long m = P->m_A;              /* number of rows of A matrix */
  long nlinearity = Q->nlinearity;
  long lastdv=Q->lastdv;
  long d;
  lrs_mp_matrix Ain=Q->Ain;
  long *B = P->B;
  long *C = P->C;
  long verbose=Q->verbose;
  long debug=Q->debug;

  if (debug)
    {
      fprintf (lrs_ofp, "\n*redund_print redineq:");
      for (i = 1; i <= m; i++)
        fprintf (lrs_ofp, " %ld", redineq[i]);
      fprintf (lrs_ofp, "\n*inequality:");
      for (i = 1; i <= m; i++)
        fprintf (lrs_ofp, " %ld", inequality[i]);

    }

 d=P->d;
 lastdv=Q->lastdv;

/* update linearity with newly found linearities if any */
/* 2022.4.21    remove linearly dependent linearities */

   nlinearity=0;
   for (i = 1; i <= m; i++)
       if(redineq[i]==2) 
        {
          if(debug)
            printA(P,Q);
          j=1;
          while (j<=m && inequality[j] != i ) j++;
          k=1;
          cob=0;
          if ( j<=m )  /* hidden linearity, look in cobasis */
            {
              index=j+lastdv;       /* we are looking for variable index */
              while (k<=m && B[k] != index ) k++;
              while(cob<lastdv && C[cob] != index ) cob++;
             }
          if(debug)
                 fprintf(lrs_ofp,"\n# i=%ld j=%ld k=%ld lastdv=%ld index=%ld cob=%ld",i,j,k,lastdv,j+lastdv,cob);
          if( cob < lastdv || j>m )     /* index removed or in cobasis */
            {
              if(debug)
                fprintf(lrs_ofp,"\n*linearity in row=%ld removed or in cobasis, independent",i);
              Q->linearity[nlinearity++]=i;
            }
          else   /*index in basis */
            {
              cob=0;
              while (cob < d)
                 {
                   if(debug)
                      pmp("   A",P->A[P->Row[k]][P->Col[cob]]);
                   if ( !zero(P->A[P->Row[k]][P->Col[cob]]))
                      {
                        if(debug)
                         {
                          fprintf(lrs_ofp,"\n* i=%ld cob=%ld lastdv=%ld ",i,cob,lastdv);
                          fprintf(lrs_ofp,"C[cob]=%ld  inequality=%ld ",C[cob],inequality[C[cob]-lastdv]);
                         }
/* 2023.11.2*/
                        if (redineq[inequality[C[cob]-lastdv]] != 2)
                          break;
                      }
                   cob++;
                 }
              if(cob==d)
               {
                 if(verbose)
                   fprintf(lrs_ofp,"\n*linearity in row=%ld dependent, made redundant",i);
                 redineq[i]= -1;   /* could not remove linearity from basis */
               }
              else                   /* linearity in cobasis  now */
               {
                 if(verbose)
                    fprintf(lrs_ofp,"\n*linearity in row=%ld pivoted to cobasis",i);
                 pivot(P,Q,k,cob); 
                 update(P,Q,&k,&cob);
                 Q->linearity[nlinearity++]=i;
               }
            }      /* else index in basis */
        }
  if(debug)
      {
       fprintf(lrs_ofp,"\n*redineq:");
        for(j=1;j<=m;j++)
           fprintf(lrs_ofp," %ld",redineq[j]);
      }

  if(Q->nonnegative)
    fprintf (lrs_ofp, "\nnonnegative");

  if (!Q->hull)
    fprintf (lrs_ofp, "\nH-representation");
  else
    fprintf (lrs_ofp, "\nV-representation");

/* linearities will be printed first in output */

  if (nlinearity > 0)
    {
      fprintf (lrs_ofp, "\nlinearity %ld", nlinearity);
      for (i = 1; i <= nlinearity; i++)
	fprintf (lrs_ofp, " %ld", i);

    }

  nredund = 0;		/* count number of non-redundant inequalities */

  for (i = 1; i <= m; i++)
    if (redineq[i] == 0)
      nredund++;

  fprintf (lrs_ofp, "\nbegin");
  fprintf (lrs_ofp, "\n%ld %ld rational", nlinearity+nredund, Q->inputd+1-Q->hull);

/* print the linearities first */
  
  for (i = 0; i < nlinearity; i++)
    lrs_printrow ("", Q, Ain[Q->linearity[i]], Q->inputd);

  for (i = 1; i <= m; i++)
   {
    if (redineq[i] == 0)
      lrs_printrow ("", Q, Ain[i], Q->inputd);

/* remove redundant rows */
/*
    if( redineq[i] != 1 && redineq[i] != -1)    
     {
      row++;
      for(j=0; j<= Q->inputd;j++)
         copy(P->A[row][j],Ain[i][j]);
     }
*/
   }
  
  fprintf (lrs_ofp, "\nend");

  if(sizeof(Q->projmess) > 0 )
     fprintf (lrs_ofp, "\n%s",Q->projmess);
  if(verbose)
     fprintf (lrs_ofp, "\nverbose");
  if(debug   || Q->redund)
   {
    fprintf (lrs_ofp, "\n*input had %ld rows and %ld columns", m, Q->inputd+1);
   }

  redineq[0]=m - nredund-nlinearity; /* number of redundant rows */

  if( m==nredund || redineq[0]==0)
   {
    if(debug || Q->redund)
     {
      fprintf (lrs_ofp, "\n*no redundant rows found");
      if(!Q->fullredund)
         fprintf (lrs_ofp, " among those tested");
      fprintf (lrs_ofp, "\n");
     }
   }
  else
     {
      j=0;
      if(debug || Q->redund)
       {
        fprintf (lrs_ofp, "\n* %ld redundant row(s) found", redineq[0]);
        if(!Q->fullredund)
          fprintf (lrs_ofp, " among those tested");
        fprintf (lrs_ofp, "\n");
        for (i=1; i<=m; i++)
         if(redineq[i]==1 || redineq[i]==-1)
           {
            j++;
            if(j>20)
              {
               j=1;
               fprintf (lrs_ofp, "\n %ld",i);
              }
            else
               fprintf (lrs_ofp, " %ld",i);
           }
          if(Q->nonnegative)
             fprintf (lrs_ofp, "\n*implicit nonnegative constraints not tested");
         }
     }
  if(nlinearity > Q->nlinearity && Q->redund)
    {
     if(nlinearity - Q->nlinearity==1)
          fprintf (lrs_ofp, "\n* %ld hidden linearity found",nlinearity - Q->nlinearity);
     else
          fprintf (lrs_ofp, "\n* %ld hidden linearities found",nlinearity - Q->nlinearity);
     if(!Q->fullredund)
          fprintf (lrs_ofp, " among those tested");
     fprintf (lrs_ofp, "\n");
    }
  if(Q->mplrs)              /* always checks for hidden linearities */
     Q->testlin=1;

  if( Q->redund && !Q->hull)
   {
    if(!Q->testlin && Q->hiddenlin)
       fprintf (lrs_ofp, "\n*no check for hidden linearities");
    else
       if(Q->fullredund)  
         fprintf (lrs_ofp, "\n*minimum representation : dimension=%ld", Q->inputd-nlinearity);
   }
  

  Q->nlinearity=nlinearity;

  fprintf (lrs_ofp, "\n");
  return;
}           /* end of redund_print */


/*******************/
/* lrs_printoutput */
/*  one line only   */
/*******************/
void 
lrs_printoutput (lrs_dat * Q, lrs_mp_vector output)
{
  char *sss;
  char **ss;

  long i;
  long len=0;

  if (Q->countonly)
     return;

  ss = (char **)malloc((1+Q->n) * sizeof(char*));

  if (Q->hull || zero (output[0]))	/*non vertex */
      for (i = 0; i < Q->n; i++)
       {
        ss[i]=cpmp ("", output[i]);
        len=len+snprintf(NULL, 0, "%s ", ss[i] );
       }
  else
      for (i = 1; i < Q->n; i++)
       {
        ss[i]=cprat("", output[i], output[0]);
        len=len+snprintf(NULL, 0, "%s ", ss[i] );
       }

  sss=(char*)malloc((len+5)* sizeof(char*));
  len=0;

  if (Q->hull || zero (output[0]))      /*non vertex */
      for (i = 0; i < Q->n; i++)
       {
        len=len+sprintf(sss+len,"%s ",ss[i]);
        free(ss[i]);
       }
  else
    {                           /* vertex   */
      len=sprintf (sss, " 1 ");
      for (i = 1; i < Q->n; i++)
       {
        len=len+sprintf(sss+len, "%s ", ss[i] );
        free(ss[i]);
       }
    }

  if(Q->mplrs)
     lrs_post_output("vertex",sss);
  else
     fprintf (lrs_ofp, "\n%s",sss);

  free(ss);
  free(sss);

}
/**************************/
/* end of lrs_printoutput */
/**************************/

/****************/
/* lrs_lpoutput */
/****************/
void lrs_lpoutput(lrs_dic * P,lrs_dat * Q, lrs_mp_vector output)
{

  if(!Q->testlin && !Q->messages)
    return;

  lrs_mp Temp1, Temp2;
  long i;
  long messages=Q->messages;
  Q->messages=TRUE;

  lrs_getsolution(P,Q,Q->output,0);

  if(Q->testlin)
    {
     if(positive(P->objnum) || Q->unbounded)
       {
         if(Q->nlinearity == 0)
              lrs_warning(Q,"warning","*interior point found, no hidden linearities");
         else
           lrs_warning(Q,"warning","*relative interior point found, no hidden linearities");
         Q->redundphase=1;
         Q->hiddenlin=0;
       }
     else
       {
          if(zero(P->objnum))
            {
              lrs_warning(Q,"warning","\n*hidden linearities exist");
              Q->redundphase=0;
              Q->hiddenlin=1;
            }
          else if(negative(P->objnum))
               lrs_warning(Q,"warning", "\n*original problem is infeasible\n");
          return;
       }
    }


    if((Q->testlin && !Q->fel) || Q->verbose) 
     {
        if(!zero(output[0]))
          {
             if(!Q->testlin)
                fprintf (lrs_ofp, "\n\n*Primal: ");
             else
              {
               if(!Q->mplrs)
                fprintf (lrs_ofp, "\n* ");
              }
             char* buf="*";
             for (i = 1; i < Q->n-Q->testlin; i++) /* do not print artificial var for testlin*/
              {
               if(Q->mplrs)
               {
                char *sss=(char *)malloc(20+sizeof(long));
                sprintf(sss,"x_%ld=",i);
                char *ss=cprat (sss, output[i], output[0]);
                char *buf1 = malloc((strlen(ss) + strlen(buf) + 10) * sizeof(char));
                sprintf(buf1,"%s %s",buf,ss);
                if(i == Q->n-Q->testlin-1)
                    lrs_warning(Q,"warning",buf1);
                if(i>1)
                    free(buf); 
                free(ss); free(sss);
                buf=buf1;
               }
               else
               {
                fprintf(lrs_ofp,"x_%ld=",i);
                prat ("", output[i], output[0]);
               }
              }   /*for i=1 ...  */
             if(Q->mplrs)
               free(buf);
           }

         if(Q->unbounded && Q->testlin && !Q->hull)
          if(!Q->mplrs)   /* fix the output below */
          {
             lrs_getsolution(P,Q,Q->output,Q->n-1);
             fprintf(lrs_ofp,"\n* ");
             for (i = 1; i < Q->n-1; i++) /* do not print artificial var */
             {
               fprintf(lrs_ofp,"x_%ld=",i);
               itomp(ONE,output[0]);
               prat ("", output[i], output[0]);
             }
             fprintf(lrs_ofp,": ray");
           }

        Q->messages=messages;    /* reset to orinal value */

        if(Q->unbounded || Q->testlin)
           return;

        prat ("\n*Obj=",P->objnum, P->objden);
        fprintf (lrs_ofp, "    pivots=%ld ",Q->count[3]);
        if(Q->nlinearity > 0)
            fprintf (lrs_ofp, "\n\n*Linearities in input file - partial dual solution only");
        fprintf (lrs_ofp, "\n\n*Dual: ");
        lrs_alloc_mp (Temp2);
        for (i = 0; i < P->d; i++)
    	    {
    	          fprintf(lrs_ofp,"y_%ld=",Q->inequality[P->C[i]-Q->lastdv]);
    	          changesign(P->A[0][P->Col[i]]);
                  mulint(Q->Lcm[P->Col[i]],P->A[0][P->Col[i]],Temp1);
                  mulint(Q->Gcd[P->Col[i]],P->det,Temp2);
                  prat("",Temp1,Temp2);
    	          changesign(P->A[0][P->Col[i]]);
            }
         lrs_clear_mp (Temp1);
         lrs_clear_mp (Temp2);
     }                     /* if verbose */
 }
/***********************/
/* end of lrs_lpoutput */
/***********************/
void 
lrs_printrow (const char *name, lrs_dat * Q, lrs_mp_vector output, long rowd)
/* print a row of A matrix in output in "original" form  */
/* rowd+1 is the dimension of output vector                */
/* if input is H-rep. output[0] contains the RHS      */
/* if input is V-rep. vertices are scaled by 1/output[1] */
{
  char *sss;
  char **ss;

  long i;
  long len=0;

  ss = (char **)malloc((1+Q->n) * sizeof(char*));

/* first compute length */

  len=len+snprintf(NULL, 0, "%s", name);

  if (!Q->hull || zero (output[1]))
      for (i = Q->hull; i <= rowd; i++)
       {
         ss[i]=cpmp ("", output[i]);
         len=len+snprintf(NULL, 0, "%s ", ss[i] );
       }
  else
      for (i = 2; i <= rowd; i++)
        {
          ss[i]=cprat("", output[i], output[1]);
          len=len+snprintf(NULL, 0, "%s ", ss[i] );
        }

/* now build output string */

  sss=(char*)malloc((len+5)* sizeof(char*));
  len=sprintf(sss, "%s",name);

  if (!Q->hull || zero (output[1]))  
      for (i = Q->hull; i <= rowd; i++)
       {
         len=len+sprintf(sss+len,"%s ",ss[i]);
         free(ss[i]);
       }
  else
    {				/* vertex */
      len=len+sprintf(sss+len," 1 ");
      for (i = 2; i <= rowd; i++)
       {
        len=len+sprintf(sss+len,"%s ",ss[i]);
        free(ss[i]);
       }
    }

  if(Q->mplrs)
    {
     if(strcmp(name,"")==0)     /* consumer printing output */
        fprintf (lrs_ofp, "\n%s",sss);
     else
        lrs_post_output("flush",sss);
//      fprintf (lrs_ofp, "\n%s",sss);
    }
  else
     fprintf (lrs_ofp, "\n%s",sss);

  free(ss);
  free(sss);

   

  return;

}				/* end of lrs_printrow */

long 
lrs_getsolution (lrs_dic * P, lrs_dat * Q, lrs_mp_vector output, long col)
   /* check if column indexed by col in this dictionary */
   /* contains output                                   */
   /* col=0 for vertex 1....d for ray/facet             */
{

	
  long j;			/* cobasic index     */
  
  lrs_mp_matrix A = P->A;
  long *Row = P->Row;

  if (col == ZERO)		/* check for lexmin vertex */
    {
     j = lrs_getvertex (P, Q, output);
     return j;
    }

/*  check for rays: negative in row 0 , positive if lponly */

  if (Q->lponly)
    {
      if (!positive (A[0][col]))
	return FALSE;
    }

  else if (!negative (A[0][col]))
    return FALSE;

/*  and non-negative for all basic non decision variables */

  j = Q->lastdv + 1;
  while (j <= P->m && !negative (A[Row[j]][col]))
    j++;

  if (j <= P->m)
    return FALSE;
 /* 2021.5.21 check for max depth output */
  if (lexmin (P, Q, col))
    {
     if(P->depth > Q->count[8])
       Q->count[8] = P->depth;
     return lrs_getray (P, Q, col, Q->n, output);
    }

  if (Q->geometric || Q->allbases || Q->lponly)
    return lrs_getray (P, Q, col, Q->n, output);

  return FALSE;			/* no more output in this dictionary */

}				/* end of lrs_getsolution */

void
lrs_print_header(const char *name)
{
  if(lrs_ofp == NULL)
    lrs_ofp=stdout;
#ifdef LRS_QUIET
  return;
#endif
  fprintf (lrs_ofp,"%s:", name);
  fprintf (lrs_ofp,TITLE);
  fprintf (lrs_ofp,VERSION);
  fprintf (lrs_ofp,"(");
  fprintf (lrs_ofp,BIT);
  fprintf (lrs_ofp,",");
  fprintf (lrs_ofp,ARITH);
#ifdef MA
fprintf (lrs_ofp,",hybrid_arithmetic");
#endif
#ifdef LRSLONG
#ifndef SAFE
  fprintf (lrs_ofp,",no_overflow_checking");
#endif
#endif
#ifdef GMP
#ifdef MGMP
   fprintf(lrs_ofp,",mini-gmp");
#else 
   fprintf(lrs_ofp,",_gmp_v.%d.%d",__GNU_MP_VERSION,__GNU_MP_VERSION_MINOR);
#endif
#elif defined(FLINT)
   fprintf(lrs_ofp,"_%dbit_flint_v.%s", FLINT_BITS, FLINT_VERSION);
#endif
  fprintf (lrs_ofp,")");
}

long
lrs_init (const char *name)       /* returns TRUE if successful, else FALSE */
{

#ifndef MPLRS
#ifndef LRS_QUIET
  lrs_print_header(name);
#endif
#endif


  if (!lrs_mp_init (0, stdin, stdout))  /* initialize arithmetic */
    return FALSE;

  lrs_Q_count = 0;
  lrs_checkpoint_seconds = 0;
#ifndef SIGNALS
  setup_signals ();
#endif
  return TRUE;
}

void 
lrs_close (const char *name)
{

#ifdef MPLRS
  return;
#endif

#ifdef LRS_QUIET
  fprintf (lrs_ofp, "\n");
  if (lrs_ofp != stdout)
   {
    fclose (lrs_ofp);
    lrs_ofp=NULL;
   }
  return;
#endif 

fprintf (lrs_ofp, "\n*");
lrs_print_header(name);

#ifdef LRSLONG
#ifndef SAFE
  fprintf (lrs_ofp, "\n*caution: no overflow checking on long integer arithemtic");
#endif
#endif

#ifdef MP   
  fprintf (lrs_ofp, " max decimal digits=%ld/%ld", DIG2DEC (lrs_record_digits), DIG2DEC (lrs_digits));
#endif

#ifndef TIMES
  ptimes ();
#endif

  if (lrs_ofp != stdout)
   {
    fclose (lrs_ofp);
    lrs_ofp=NULL;
   }
}

/***********************************/
/* allocate and initialize lrs_dat */
/***********************************/
lrs_dat *
lrs_alloc_dat (const char *name)
{
  lrs_dat *Q;
  long i;


  if (lrs_Q_count >= MAX_LRS_Q)
    {
      fprintf (stderr,
	       "Fatal: Attempt to allocate more than %ld global data blocks\n", MAX_LRS_Q);
      return NULL;

    }

  Q = (lrs_dat *) malloc (sizeof (lrs_dat));
  if (Q == NULL)
    return Q;			/* failure to allocate */

  lrs_Q_list[lrs_Q_count] = Q;
  Q->id = lrs_Q_count;
  lrs_Q_count++;
  Q->name=(char *) CALLOC ((unsigned) strlen(name)+1, sizeof (char));
  strcpy(Q->name,name); 

/* initialize variables */
  Q->mplrs=FALSE;
  Q->plrs=FALSE;
  Q->messages=TRUE;
#ifdef MPLRS
  Q->mplrs=TRUE;
#endif
#ifdef PLRS
  Q->plrs=TRUE;
#endif
#ifdef LRS_QUIET
  Q->messages=FALSE;
#endif
  strcpy(Q->fname,""); /* name of program, filled in later */ 
  Q->m = 0L;
  Q->n = 0L;
  Q->inputd = 0L;
  Q->deepest = 0L;
  Q->nlinearity = 0L;
  Q->nredundcol = 0L;
  Q->runs = 0L;
  Q->subtreesize=MAXD;
  Q->seed = 1234L;
  Q->totalnodes = 0L;
  for (i = 0; i < 10; i++)
    {
      Q->count[i] = 0L;
      Q->cest[i] = 0.0;
      if(i < 5)
	Q->startcount[i] = 0L;
    }
  Q->count[2] = 1L;		/* basis counter */
  Q->startcount[2] = 0L;	/* starting basis counter */
/* initialize flags */
  Q->allbases = FALSE;
  Q->bound = FALSE;            /* upper/lower bound on objective function given */
  Q->countonly = FALSE;        /* produce the usual output */
  Q->debug = FALSE;
  Q->frequency = 0L;
  Q->dualdeg = FALSE;          /* TRUE if dual degenerate starting dictionary */
  Q->geometric = FALSE;
  Q->getvolume = FALSE;
  Q->homogeneous = TRUE;
  Q->polytope = FALSE;
  Q->triangulation = FALSE;
  Q->hull = FALSE;
  Q->incidence = FALSE;
  Q->nincidence=0;
  Q->maxincidence=MAXD;
  Q->minprunedepth=MAXD;
  Q->lponly = FALSE;
  Q->maxdepth = MAXD;
  Q->mindepth = -MAXD;
  Q->maxoutput = 0L;
  Q->maxcobases = 0L;     /* after maxcobases have been found unexplored subtrees reported */

  Q->redund = FALSE;
  Q->nash   = FALSE;
  Q->fel    = FALSE;

  Q->nonnegative = FALSE;
  Q->printcobasis = FALSE;
  Q->printslack = FALSE;
  Q->truncate = FALSE;          /* truncate tree when moving from opt vertex        */
  Q->extract=FALSE;
  Q->verbose=FALSE;
  Q->voronoi = FALSE;
  Q->maximize = FALSE;		/*flag for LP maximization                          */
  Q->minimize = FALSE;		/*flag for LP minimization                          */
  Q->restart = FALSE;		/* TRUE if restarting from some cobasis             */
  Q->givenstart = FALSE;	/* TRUE if a starting cobasis is given              */
  Q->strace = -1L;		/* turn on  debug at basis # strace */
  Q->etrace = -1L;		/* turn off debug at basis # etrace */
  Q->newstart=FALSE;
  Q->giveoutput=TRUE;           /* set to false for first output after restart      */
  Q->redundphase=FALSE;       /* set to true when mplrs skips verifying output    */
  Q->hiddenlin=TRUE;            /* set to true hidden linearities may exist         */
  Q->nextineq=15;               /* start redundancy testing from this row           */
  Q->testlin=0;             /* redund mode: test for hidden linearities         */
  Q->fullredund=TRUE;           /* minrep/redund mode: test all rows                */
  Q->facet=NULL;
  Q->redundcol=NULL;
  Q->inequality=NULL;
  Q->linearity=NULL;
  Q->vars=NULL;
  Q->startcob=NULL;
  Q->minratio=NULL;
  Q->temparray=NULL;
  Q->redineq=NULL;
  Q->Ain=NULL;
  Q->olddic=NULL;
  Q->tid=0;                     /* thread index */
  Q->child=0;
  Q->threads=0;

  Q->saved_flag = 0;		/* no cobasis saved initially, db */
  lrs_alloc_mp (Q->Nvolume);
  lrs_alloc_mp (Q->Dvolume);
  lrs_alloc_mp (Q->sumdet);
  lrs_alloc_mp (Q->saved_det);
  lrs_alloc_mp (Q->boundn);
  lrs_alloc_mp (Q->boundd);
  itomp (ZERO, Q->Nvolume);
  itomp (ONE, Q->Dvolume);
  itomp (ZERO, Q->sumdet);
  Q->unbounded = FALSE;

  return Q;
}				/* end of allocate and initialize lrs_dat */


/*******************************/
/*  lrs_read_dat               */
/*******************************/
long 
lrs_read_dat (lrs_dat * Q, int argc, char *argv[])
{
  char name[1000];
  char writemode[2]="w";           
  long dec_digits = DEFAULT_DIGITS;
  long infilenum=0;                /*input file number to open if any        */
  long firstline = TRUE;	/*flag for picking off name at line 1     */
  long i;
  int c;			/* for fgetc */
  int messages = Q->messages;   /* print output for each option */

  *tmpfilename='\0';

  strcpy(outfilename, "\0");

  if(argc > 1 )
    {
       infilenum=1;
       if(Q->nash && argc ==2)                     /* legacy code to open second nash input file  */
	       infilenum=2;
       if(Q->nash && argc ==4)                     /* legacy code for nash output file            */
               strcpy(outfilename,argv[3]);
    }

  if (infilenum > 0 && (lrs_ifp = fopen (argv[infilenum], "r")) == NULL)  /* command line overides stdin   */
    {
       fprintf (stderr,"\n*bad input file name\n");
       return (FALSE);
    }

  if (infilenum==1)
    {
       strcpy(infilename,argv[1]);
       if(!Q->mplrs && messages && overflow == 0 )
	       printf ("\n*Input taken from  %s", infilename);
       fflush(stdout);
    }

#ifdef LRSLONG
  if(infilenum==0)         /* stdin gets written to a temporary file */
       {
         strcpy(tmpfilename,"/tmp/lrs_stdinXXXXXX");
         if(mkstemp(tmpfilename)==-1)
          {
           fprintf (stderr,"\n*could not open temporary file for stdin\n");
           return (FALSE);
           }
         lrs_stdin_to_file(tmpfilename);
         lrs_ifp=fopen (tmpfilename, "r");
         strcpy(infilename,tmpfilename);
       }

  lrs_file_to_cache(lrs_ifp);

#endif

  if(argc > 2)              /* lrs has commandline arguments for newstart */
   {
     if (!Q->nash )  
     {
     i=2;
     while (i < argc)       /* add command line arguments here */
      {                  
        if(strcmp(argv[i],"-newstart")==0) /* newstart not currently used ... */ 
           {
           strcpy(writemode,"a");
           Q->newstart=TRUE;
           }
        else                      /* command line argument overides stdout   */
            strcpy(outfilename,argv[i++]);
       }
     }
     if(strcmp(outfilename,"\0") != 0 )
     {
      if(strcmp(outfilename,"-") == 0 )
        lrs_ofp = stdout;
      else
       {
        if ((lrs_ofp = fopen (outfilename, writemode)) == NULL)
          {
           fprintf (stderr,"\n*bad output file name %s\n",outfilename);
           return (FALSE);
          }
        else
          if(overflow == 0)
              printf ("\n*Output sent to file %s\n", outfilename);
       }
      }
    }

/*2020.5.19 new redund handling, thanks to DB */
/* symbolic link from redund to lrs needed    */
/* similar links if lrs1, lrs2 or lrsgmp used */
/* any redund option in input will overide    */

   if(!Q->mplrs && lrs_ofp != stdout) /* headers for the output file also */
     {
        char *name;
        name=(char *) malloc(strlen(Q->fname)+5);
        strcpy(name,"*");
        strcat(name,Q->fname);
        lrs_print_header(name);  
        free(name);
     }

/* process input file */
  if( fscanf (lrs_ifp, "%s", name) == EOF)
	    {
	      fprintf (stderr, "\n*no begin line");
	      return (FALSE);
	    }

  while (strcmp (name, "begin") != 0)	/*skip until "begin" found processing options */
    {
      if (strncmp (name, "*", 1) == 0)	/* skip any line beginning with * */
	{
	  c = name[0];
	  while (c != EOF && c != '\n')
	    c = fgetc (lrs_ifp);
	}

      else if (strcmp (name, "H-representation") == 0)
	Q->hull = FALSE;
      else if ((strcmp (name, "hull") == 0) || (strcmp (name, "V-representation") == 0))
        {
           if(Q->mplrs && Q->fel)
               {
                lrs_post_output("flush","\n*project/eliminate are not mplrs options for a V-representation");
                lrs_post_output("flush","*for large problems use extract option with lrs then 'mplrs -minrep'\n");
                return(FALSE);
               }

	   Q->hull = TRUE;
           Q->polytope = TRUE;		/* will be updated as input read */
        }
      else if (strcmp (name, "digits") == 0)
	{
	  if (fscanf (lrs_ifp, "%ld", &dec_digits) == EOF)
	    {
	      fprintf (stderr, "\n*no begin line");
	      return (FALSE);
	    }
          if  (!lrs_set_digits(dec_digits))
             return (FALSE);

	}

      else if (strcmp (name, "testlin") == 0)
	{
          if(Q->fel)
           {
             if(!Q->mplrs)
                lrs_warning(Q,"warning","\n*testlin ignored in fel mode");
           }
          else if (!(Q->mplrs && Q->redund))
           {
            Q->testlin=TRUE;
            Q->plrs=FALSE;
            lrs_warning(Q,"warning","\n*testlin");
           }
	}

      else if (strcmp (name, "linearity") == 0)
	{
	  if (!readlinearity (Q))
	    return FALSE;
	}
      else if (strcmp (name, "nonnegative") == 0)
	{
         if(Q->nash)
	  fprintf (stderr, "\nNash incompatibile with nonnegative option - skipped");
         else
	  Q->nonnegative = TRUE;
        }
      else if (firstline && !Q->mplrs)    /* print firstline of input file */
	{
          lrs_warning(Q,"warning",name);
	  firstline = FALSE;
	}

      if (fscanf (lrs_ifp, "%s", name) == EOF)
	{
	  fprintf (stderr, "\n*no begin line\n");
	  return (FALSE);
	}

    }				/* end of while */


  if (fscanf (lrs_ifp, "%ld %ld %s", &Q->m, &Q->n, name) == EOF)
    {
      fprintf (stderr, "\n*no data in file\n");
      return (FALSE);
    }
  if(Q->tid==1)    /* consumer does not do the artificial LP */
     Q->testlin=0;

  if(Q->debug)
     fprintf(lrs_ofp,"\n+++Q->rank=%ld Q->testlin=%ld Q->redundphase=%ld",Q->tid,Q->testlin,Q->redundphase);

  if(Q->testlin && !Q->redundphase)
            Q->n++;  /* col of ones will be appended for rank=0 proc only   */   

  if (strcmp (name, "integer") != 0 && strcmp (name, "rational") != 0)
    {
      fprintf (stderr,"\n*data type must be integer or rational\n");
      return (FALSE);
    }


  if (Q->m == 0)
    {
      fprintf (stderr, "\n*no input given\n");	/* program dies ungracefully */
      return (FALSE);
    }


  /* inputd may be reduced in preprocessing of linearities and redund cols */

  return TRUE;
}				/* end of lrs_read_dat */

/****************************/
/* set up lrs_dic structure */
/****************************/
long 
lrs_read_dic (lrs_dic * P, lrs_dat * Q)
/* read constraint matrix and set up problem and dictionary  */

{
  lrs_mp Temp,Tempn,Tempd, mpone, mptwo;

  long i, j, m, d;;
  long allzero=1;
  char name[100];
  char mess[200];
  char *ss;
  int c; /* fgetc actually returns an int. db */
  long redundstart, redundend;
  long dualperturb=FALSE;    /* dualperturb=TRUE: objective function perturbed */
/* assign local variables to structures */

  lrs_mp_matrix A;
  lrs_mp_vector Gcd, Lcm;

  lrs_mp_vector oD=Q->output;  /* Denom for obj fun, temp use of output */
  long hull = Q->hull;
  long messages=Q->messages;

  lrs_alloc_mp(Temp); lrs_alloc_mp(mpone);
  lrs_alloc_mp(Tempn); lrs_alloc_mp(Tempd); lrs_alloc_mp(mptwo);
  A = P->A;
  m = Q->m;
  d = Q->inputd;
  Gcd = Q->Gcd;
  Lcm = Q->Lcm;

  itomp (ONE, mpone);
  itomp(2L,mptwo);
  itomp (ONE, A[0][0]);
  itomp (ONE, Lcm[0]);
  itomp (ONE, Gcd[0]);
  for (i = 1; i <= m; i++)	/* read in input matrix row by row */
    {
      itomp (ONE, Lcm[i]);	/* Lcm of denominators */
      itomp (ZERO, Gcd[i]);	/* Gcd of numerators */
      for (j = hull; j <= d; j++)	/* hull data copied to cols 1..d */
	{
/*2022.4.25  added column to test for full dimension  */
         if(j==d && Q->testlin)
           {
             itomp(-1, A[i][d]);
             itomp( 1, A[0][d]);
           }
         else
	   if (readrat (A[i][j], A[0][j]))
	     lcm (Lcm[i], A[0][j]);	/* update lcm of denominators */
    
         if(overflow_detected)
            {
              lrs_warning(Q,"warning","*integer overflow reading input");
              goto end_read_dic;
            }
         copy (Temp, A[i][j]);
	 gcd (Gcd[i], Temp);	/* update gcd of numerators   */
	}
      if (hull)
	{
	  itomp (ZERO, A[i][0]);	/*for hull, we have to append an extra column of zeroes */
	  if (!one (A[i][1]) || !one (A[0][1]))		/* all rows must have a one in column one */
	    Q->polytope = FALSE;
	}
      if (!zero (A[i][hull]))	/* for H-rep, are zero in column 0     */
	Q->homogeneous = FALSE;	/* for V-rep, all zero in column 1     */

      storesign (Gcd[i], POS);
      storesign (Lcm[i], POS);
      if (mp_greater (Gcd[i], mpone) || mp_greater (Lcm[i], mpone))
	for (j = 0; j <= d; j++)
	  {
	    exactdivint (A[i][j], Gcd[i], Temp);	/*reduce numerators by Gcd  */
	    mulint (Lcm[i], Temp, Temp);	/*remove denominators */
	    exactdivint (Temp, A[0][j], A[i][j]);	/*reduce by former denominator */
	  }

    }				/* end of for i=       */
  if(Q->testlin && !Q->redundphase) /* setup extra col for hiddenlin test */
   {
    for (i = 1; i <= m; i++) /* zero row treated like linearity */
     {
      allzero=1;
      for(j=0;j<d;j++)
       if(!zero(A[i][j]))
        {
         allzero=0;
         break;
        }
      if(allzero)
        itomp(ZERO, A[i][d]);
     }   /* zero row */
    for(j=0;j<d;j++)
      itomp(ZERO, A[0][j]);
    itomp(ONE, A[0][d]);
    for(i=0;i<Q->nlinearity;i++)
     itomp(ZERO,A[Q->linearity[i]][d]);
    Q->lponly=TRUE;
    Q->maximize=TRUE;
   }            /* setup extra col ... */
/* 2010.4.26 patch */
  if(Q->nonnegative)    /* set up Gcd and Lcm for nonexistent nonnegative inequalities */
      for (i=m+1;i<=m+d;i++)
          { itomp (ONE, Lcm[i]);
            itomp (ONE, Gcd[i]);
          }
  
  if (Q->homogeneous && Q->verbose)
    {
      lrs_warning(Q,"warning","*Input is homogeneous, column 1 not treated as redundant");
    }


/* read in flags */
  while (fscanf (lrs_ifp, "%s", name) != EOF)
    {
      if (strncmp (name, "*", 1) == 0)	/* skip any line beginning with * */
	{
	  c = name[0];
	  while (c != EOF && c != '\n')
	    c = fgetc (lrs_ifp);
	}


      if (strcmp (name, "checkpoint") == 0)
	{
	  long seconds;

	  if(fscanf (lrs_ifp, "%ld", &seconds) == EOF)
            {
              fprintf (lrs_ofp, "\nInvalid checkpoint option");
              return (FALSE);
            }

#ifndef SIGNALS
	  if (seconds > 0)
	    {
	      lrs_checkpoint_seconds = seconds;
	      errcheck ("signal", signal (SIGALRM, timecheck));
	      alarm (lrs_checkpoint_seconds);
	    }
#endif
	}

      if (strcmp (name, "debug") == 0)
	{
          Q->etrace =0;
	  if(fscanf (lrs_ifp, "%ld %ld", &Q->strace, &Q->etrace)==EOF)
             Q->strace =0;
          if(!Q->mplrs)
	     fprintf (lrs_ofp, "\n*%s from B#%ld to B#%ld", name, Q->strace, Q->etrace);
          Q->debug=TRUE;
	  if (Q->strace <= 1)
	    Q->debug = TRUE;
	}
      if (strcmp (name, "startingcobasis") == 0)
	{
          if(Q->nonnegative)
	      lrs_warning(Q,"warning", "*startingcobasis incompatible with nonnegative option:skipped");
          else
            {    
              if(!Q->mplrs && messages)
	          fprintf (lrs_ofp, "\n*startingcobasis");
	      Q->givenstart = TRUE;
	      if (!readfacets (Q, Q->inequality))
	          return FALSE;
	    }
        }


      if (strcmp (name, "restart")==0 ) 
	{
          if(Q->mplrs)
            {
             fprintf (lrs_ofp, "\n\n*** %s is an lrs option only\n", name);
             return(FALSE);
            }
	  Q->restart = TRUE;
          Q->giveoutput = FALSE;          /* first time only */
          if(Q->voronoi)
           {
             if(fscanf (lrs_ifp, "%ld %ld %ld %ld", &Q->count[1], &Q->count[0], &Q->count[2], &P->depth)==EOF)
               return FALSE; 
            if(!Q->mplrs && messages)
               fprintf (lrs_ofp, "\n*%s V#%ld R#%ld B#%ld h=%ld data points", name, Q->count[1], Q->count[0], Q->count[2], P->depth);
            }
          else if(hull)
            {
	    if( fscanf (lrs_ifp, "%ld %ld %ld", &Q->count[0], &Q->count[2], &P->depth)==EOF)
               return(FALSE);
            if(!Q->mplrs && messages)
	       fprintf (lrs_ofp, "\n*%s F#%ld B#%ld h=%ld vertices/rays", name, Q->count[0], Q->count[2], P->depth);
            }
          else
            {
	     if(fscanf (lrs_ifp, "%ld %ld %ld %ld", &Q->count[1], &Q->count[0], &Q->count[2], &P->depth)==EOF)
               return FALSE;
             if(!Q->mplrs && messages)
	       fprintf (lrs_ofp, "\n*%s V#%ld R#%ld B#%ld h=%ld facets", name, Q->count[1], Q->count[0], Q->count[2], P->depth);
            }
	  /* store starting counts to calculate totals of plrs/mplrs subjob */
	  for (i = 0; i<5; i++)
	    Q->startcount[i] = Q->count[i];
	  if (!readfacets (Q, Q->facet))
	    return FALSE;
	}			/* end of restart */

/* The next flags request a LP solution only */

    if(Q->mplrs)
       {
        if (strncmp (name,"lponly",6)== 0)
         {
          fprintf (lrs_ofp, "\n\n*** %s is an lrs option only\n", name);
          return(FALSE);
         }
       }
    else
       {
        if (strcmp (name, "lponly") == 0 || strcmp (name, "lponly_d") == 0 )

	{
	    if (Q->hull || Q->testlin || Q->redund || Q->fel)
	      fprintf (lrs_ofp, "\n*lponly  option not valid -skipped");
	    else
               {
                 Q->lponly = 1;    /*Dantzig's rule is default */
                 fprintf (lrs_ofp, "\n*lponly mode: Dantzig's rule");
               }
	}

        if (strcmp (name, "lponly_r") == 0)

          {
            if (Q->hull || Q->testlin || Q->redund || Q->fel)
              fprintf (lrs_ofp, "\n*lponly  option not valid -skipped");
            else
               {
                 Q->lponly = 2;    /*random edge rule */
                 fprintf (lrs_ofp, "\n*lponly mode: random edge rule");
               }
          }

        if (strcmp (name, "lponly_rd") == 0)

          {
            if (Q->hull || Q->testlin || Q->redund || Q->fel)
              fprintf (lrs_ofp, "\n*lponly  option not valid -skipped");
            else
               {
                 Q->lponly = 3;    /*random edge/Dantzig hybrid  */
                 fprintf (lrs_ofp, "\n*lponly mode: random edge/Dantzig hybrid");
               }
          }

        if (strcmp (name, "lponly_b") == 0 )

          {
            if (Q->hull || Q->testlin || Q->redund || Q->fel)
              fprintf (lrs_ofp, "\n*lponly  option not valid -skipped");
            else
               {
                 Q->lponly = 4;    /*Bland' rule  */
                 fprintf (lrs_ofp, "\n*lponly mode: Bland's rule");
               }
          }
      }     /* else !Q->mplrs */
/* The LP will be solved after initialization to get starting vertex   */



      if (strcmp (name, "maximize") == 0 || strcmp (name, "minimize") == 0)
	{
          if(!Q->testlin && !Q->fel && !Q->redund )
            {
              if(Q->hull)   /*2021.1.18 lrs will find the max/min lines in input file */
                {
                 if(Q->mplrs)
                  {
                   fprintf (lrs_ofp, "\n\n*%s for V-representation is an lrs option only\n", name);
                   return(FALSE);
                  }
                 Q->lponly=TRUE;
                }
              if (strcmp (name, "maximize") == 0)
		  Q->maximize = TRUE;
              else
		  Q->minimize = TRUE;
              lrs_warning(Q,"warning",name);

              if(dualperturb)   /* apply a perturbation to objective function */
                {
	          lrs_warning(Q,"warning","*Objective function perturbed");
                  copy(Temp,mptwo);
                }
	      for (j = Q->hull; j <= d; j++)
	   	{
		  if (readrat (A[0][j], oD[j]) || dualperturb )
		    {
                      if(dualperturb && j > 0 && j < d-Q->hull )
                        {
                         linrat(A[0][j], oD[j],ONE,mpone,Temp,ONE,Tempn,Tempd);
                         copy(A[0][j],Tempn);
                         copy(oD[j],Tempd);
                         mulint(mptwo,Temp,Temp);
                        }
		      reduce (A[0][j], oD[j]);
		      lcm (Q->Lcm[0], oD[j]);	/* update lcm of denominators */
		    }
		 }

	       storesign (Q->Lcm[0], POS);
	       if (mp_greater (Q->Lcm[0], mpone))
		for (j = Q->hull; j <= d; j++)
		  {
		    mulint (Q->Lcm[0], A[0][j], A[0][j]);	/*remove denominators */
		    copy (Temp, A[0][j]);
		    exactdivint (Temp, oD[j], A[0][j]);
		  }
              if(messages)
                  for(j = Q->hull; j <= d; j++)
                   pmp("",A[0][j]);

              for (j = Q->hull; j <= d; j++)
		  if (!Q->maximize)
		    changesign (A[0][j]);

	      if (Q->debug)
		printA (P, Q);
          }                     /* not used for redund, minrep, fel */
	}			/* end of LP setup */
      if (strcmp (name, "volume") == 0)
	{
	  lrs_warning(Q,"warning", "*volume");
	  Q->getvolume = TRUE;
	}
      if (strcmp (name, "geometric") == 0)
	{
          if (hull && !Q->voronoi)
	    lrs_warning(Q,"warning", "*geometric - option for H-representation or voronoi only, skipped");
          else
           {
            lrs_warning(Q,"warning", "*geometric");
	    Q->geometric = TRUE;
           }
	}
      if (strcmp (name, "allbases") == 0)
	{
   	  lrs_warning (Q,"warning", "*allbases");    
	  Q->allbases = TRUE;
        }

      if (strcmp (name, "countonly") == 0)
	{
	  lrs_warning (Q,"warning", "*countonly");
	  Q->countonly = TRUE;
	}

      if (strcmp (name, "triangulation") == 0)
	{
              if (hull)
                {
	        lrs_warning (Q,"warning","*triangulation");
	        Q->triangulation = TRUE;
                Q->getvolume = TRUE;
                }
              else
                printf ("\n*triangulation only valid for V-representations: skipped");
	}
      if (strcmp (name, "dualperturb") == 0)
	{
	  dualperturb = TRUE;
	}

      if (strcmp (name, "maxincidence") == 0)
        {
         if(fscanf (lrs_ifp, "%lld %lld", &Q->maxincidence,&Q->minprunedepth)==EOF)
            lrs_warning (Q,"warning", "*maxincidence: 2 parameters needed");
         else
          {
               Q->incidence=TRUE;
               sprintf(mess,"*%s %lld %lld",name, Q->maxincidence,Q->minprunedepth);
               lrs_warning(Q,"warning",mess);
          }

        }
      if (strcmp (name, "incidence") == 0)
	{
          lrs_warning (Q,"warning", "*incidence");
	  Q->incidence = TRUE;
	}

      if (strcmp (name, "#incidence") == 0) /* number of incident inequalities only */
	{
	  Q->printcobasis = TRUE;
	}

      if (strcmp (name, "printcobasis") == 0)
	{
	  if(fscanf (lrs_ifp, "%ld", &Q->frequency)==EOF)
/*2010.7.7  set default to zero = print only when outputting vertex/ray/facet */
             Q->frequency=0;
          if (Q->frequency > 0)
                sprintf(mess,"*%s %ld",name, Q->frequency);
          else
                sprintf(mess,"%s",name);
          lrs_warning(Q,"warning",mess);
	  Q->printcobasis = TRUE;
	}

      if (strcmp (name, "integervertices") == 0)   /* when restarting reinitialize */
        {
          if(fscanf (lrs_ifp, "%ld", &Q->count[4])==EOF)
             Q->count[4]=0;
        }

      if (strcmp (name, "printslack") == 0)
	{
	  Q->printslack = TRUE;
	}

      if (strcmp (name, "threads") == 0)
        {
          if(fscanf (lrs_ifp, "%ld", &Q->threads)==EOF)
              Q->threads=1;
          if(Q->threads < 1)
              Q->threads=1;
        }


      if (strcmp (name, "cache") == 0)
	{
	  if(fscanf (lrs_ifp, "%ld", &dict_limit)==EOF)
              dict_limit=1;
	  if (dict_limit < 1)
	    dict_limit = 1;
          sprintf(mess,"*%s %ld",name, dict_limit);
          lrs_warning(Q,"warning",mess);

	}
      if (strcmp (name, "linearity") == 0)
	{
	  if (!readlinearity (Q))
	    return FALSE;
	}

      if (strcmp (name, "maxdepth") == 0)
	{
          Q->maxdepth=MAXD;
	  if(fscanf (lrs_ifp, "%lld", &Q->maxdepth)==EOF)
                    Q->maxdepth=MAXD;
/*2021.5.19 implement in mplrs */
          sprintf(mess,"*%s %lld",name, Q->maxdepth);
          lrs_warning(Q,"warning",mess);
	}

      if (strcmp (name, "mindepth") == 0)
	{
          Q->mindepth=0;
	  if(fscanf (lrs_ifp, "%lld", &Q->mindepth)==EOF)
                    Q->mindepth=0;
          if(Q->mplrs)       /* taken from control line */
             lrs_warning(Q,"warning","*mindepth option skipped in mplrs");
          else
	     fprintf (lrs_ofp, "\n*%s  %lld", name, Q->mindepth);
	}

      if (strcmp (name, "maxoutput") == 0)
	{
	  if(fscanf (lrs_ifp, "%ld", &Q->maxoutput)==EOF)
             Q->maxoutput = 100;
          if(Q->mplrs)       /* taken from control line */
             lrs_warning(Q,"warning","*maxoutput option skipped in mplrs");
          else
	     fprintf (lrs_ofp, "\n*%s  %ld", name, Q->maxoutput);
	}

      if (strcmp (name, "maxcobases") == 0)
	{
	  if(fscanf (lrs_ifp, "%ld", &Q->maxcobases)==EOF)
             Q->maxcobases = 1000;
          if(Q->mplrs)       /* taken from control line */
             lrs_warning(Q,"warning","*maxcobases option skipped - supplied on control line in mplrs");
          else
             fprintf (lrs_ofp, "\n*%s  %ld", name, Q->maxcobases);
	}


/*2019.8.24    bounds for redund */
      if (strcmp (name, "redund") == 0 && strcmp("fel",Q->fname)!=0)
        {
        if(!Q->redund && messages)
            lrs_warning (Q,"warning", "*switching to redund mode");

        for (i = 1; i <= Q->m; i++)   /*reset any previous redund option except =2 values */
           if (Q->redineq[i] != 2)
               Q->redineq[i]=0;
        Q->redineq[0]=1;

        if( fscanf (lrs_ifp, "%ld %ld", &redundstart, &redundend)==EOF)
            {
              redundstart=1;
              redundend=m;
            }
        if (redundstart <1 || redundstart > redundend )
              redundstart=1;
        if (redundend < 1 || redundend > m  )
              redundend=Q->m;
        for (i=redundstart;i<=redundend;i++)
              Q->redineq[i]=1;
        if(redundstart > 1 || redundend < m )
             Q->fullredund=FALSE;   /*not testing all rows */
        if(messages)
         if (!Q->mplrs || !Q->redund)
           {
            sprintf (mess, "*%s  %ld %ld", name, redundstart, redundend);
            lrs_warning(Q,"warning",mess);
           }
        if(strcmp("fel",Q->fname)==0)
          Q->redund=FALSE;
        else
          {
             Q->fel=FALSE; Q->redund=TRUE; Q->extract=FALSE;         
          }
        }

      if (strcmp (name, "redund_list") == 0)
        {
         if(!Q->redund && messages)
             lrs_warning (Q,"warning", "*switching to redund mode");

         readredund(Q);

        if(!Q->mplrs && (strcmp("fel",Q->fname)==0))
          Q->redund=FALSE;
        else
          {
             Q->fel=FALSE; Q->redund=TRUE; Q->extract=FALSE;         
          }
        }

      if (strcmp (name, "truncate") == 0)
        {
         if (hull)
            lrs_warning(Q,"warning","*truncate - option for H-representation only, skipped");
         else
           {
            Q->truncate = TRUE;
            lrs_warning(Q,"warning","*truncate");
           }
        }

     if (strcmp (name, "project") == 0 || strcmp (name, "eliminate") == 0)     /* fel */
       {
        if(Q->mplrs && Q->hull)
               {
                lrs_post_output("flush","\n*project/eliminate are not mplrs options for V-representations");
                lrs_post_output("flush","*for large problems use extract option with lrs then 'mplrs -minrep'\n");
                return(FALSE);
               }

        if( Q->fel || (strncmp("lrs",Q->fname,3)==0 && !Q->testlin) || strcmp("mplrs-internal",Q->fname)==0)
           {
            if( messages )
               lrs_warning (Q,"warning", "\n*switching to fel mode");
            Q->fel=TRUE; Q->redund=FALSE; Q->extract=FALSE;         
           }
        else
          {
            if(messages)
                lrs_warning (Q,"warning","*project/eliminate option skipped");
          }
fflush(lrs_ofp);

        if(!readvars(Q,name))
             {
               free(Q->vars);
               Q->vars=NULL;
               return FALSE;
             }
       }

     if (strcmp (name, "extract") == 0 )  /* redundancies are not removed */
       {
        if(Q->mplrs)
         {
          fprintf (lrs_ofp, "\n\n*%s is an lrs option only\n", name);
          return(FALSE);
         }
        if(Q->nash)
          fprintf (lrs_ofp, "\n*%s is an lrs option only - skipped\n", name);
        else
         {
           if(messages)
                 lrs_warning (Q,"warning", "\n*switching to extract mode");

           Q->fel=FALSE; Q->redund=FALSE; Q->extract=TRUE;         

           if(!readvars(Q,name))
            {
             free(Q->vars);
             Q->vars=NULL;
             return FALSE;
            }
          }
       }

      if (strcmp (name, "verbose") == 0)
          Q->verbose = TRUE;

      if (strcmp (name, "bound") == 0)
         {
          readrat(Q->boundn,Q->boundd);
          Q->bound = TRUE;
         }

      if (strcmp (name, "nonnegative") == 0)
	  lrs_warning(Q,"warning","*nonnegative - option must come before begin line - skipped");

      if (strcmp (name, "seed") == 0)
	{
	  if(fscanf (lrs_ifp, "%ld", &Q->seed)==EOF)
               Q->seed = 3142;
          sprintf(mess,"*seed=%ld",Q->seed);
          lrs_warning(Q,"warning",mess);
          srand(Q->seed);
	}

      if (strcmp (name, "estimates") == 0)
	{
          if(Q->mplrs)
           {
            fprintf (lrs_ofp, "\n\n*** %s is an lrs option only\n", name);
            return(FALSE);
           }
          Q->plrs=FALSE;  /* both plrs and mplrs should do estimates! */
	  if(fscanf (lrs_ifp, "%ld", &Q->runs)==EOF)
             Q->runs=1;
          if(messages)
	 	 fprintf (lrs_ofp, "\n*%s %ld", name, Q->runs);
	}

// 2015.2.9   Estimates will continue until estimate is less than subtree size
      if (strcmp (name, "subtreesize") == 0)
        {
          if(fscanf (lrs_ifp, "%lld", &Q->subtreesize)==EOF)
             Q->subtreesize=MAXD;
          if(messages)
          	fprintf (lrs_ofp, "\n*%s %lld", name, Q->subtreesize);
          if(Q->mplrs)
           {
            fprintf (lrs_ofp, "\n\n*** %s is an lrs option only\n", name);
            return(FALSE);
           }
        }



      if ((strcmp (name, "voronoi") == 0) || (strcmp (name, "Voronoi") == 0))
	{
	  if (!hull)
             lrs_warning(Q,"warning","*voronoi requires V-representation - option skipped");
	  else
	    {
              lrs_warning(Q,"warning","*voronoi");
	      Q->voronoi = TRUE;
	      Q->polytope = FALSE;
	    }
	}

    }				/* end of while for reading flags */

  if(Q->bound)
    {
      if(Q->maximize)
         ss=cprat("*Lower bound on objective function:",Q->boundn,Q->boundd);
      else
         ss=cprat("*Upper bound on objective function:",Q->boundn,Q->boundd);
     lrs_warning(Q,"warning",ss);
     free(ss);
    }

/* Certain options are incompatible, this is fixed here */

  if (Q->restart && Q->maxcobases > 0) //2015.4.3 adjust for restart
               Q->maxcobases = Q->maxcobases + Q->count[2];

  if (Q->incidence)
      Q->printcobasis = TRUE;

  if(Q->allbases && Q->printcobasis && Q->frequency == 0)
    Q->frequency =1;     /* otherwise only lexmin are  printed */     

  if (Q->debug)
    {
      printA (P, Q);
      fprintf (lrs_ofp, "\nexiting lrs_read_dic");
    }
  fflush(lrs_ofp); fflush(stdout);

  if( Q->lponly || Q->mplrs || Q->fel || Q->extract 
         || Q->redund || Q->testlin )
   {
    Q->threads=1;             /* multithreading not implemented */
    Q->plrs=FALSE;
   }
  for(i=0;i<Q->nlinearity;i++) /* didn't know m when we read these*/
     if(Q->linearity[i]<1 || Q->linearity[i]>Q->m)
      {
       lrs_warning(Q,"warning","*Linearity indices must be in range 1..m");
       return FALSE;
      }

/*removing tmpfiles */
end_read_dic:
  if(Q->debug)
    fprintf(lrs_ofp,"\n*end_read_dic: overflow=%ld",overflow);

  if ( overflow > 0 )  /* we made a temporary file for overflow or stdin */
      if(remove(infilename) != 0)
         lrs_warning(Q,"warning","*Could not delete temporary file");



  if (*tmpfilename != '\0' )  /* we made a temporary file for stdin  */
        if(remove(tmpfilename) != 0)
         lrs_warning(Q,"warning", "*Could not delete temporary file");
  *tmpfilename = '\0';

  fclose(lrs_ifp);
  lrs_ifp=NULL;

  lrs_clear_mp(Temp); lrs_clear_mp(mpone);
  lrs_clear_mp(Tempn); lrs_clear_mp(Tempd); lrs_clear_mp(mptwo);
  return TRUE;

}      /* lrs_read_dic */


/* In lrs_getfirstbasis and lrs_getnextbasis we use D instead of P */
/* since the dictionary P may change, ie. &P in calling routine    */

#define D (*D_p)

long 
lrs_getfirstbasis (lrs_dic ** D_p, lrs_dat * Q, lrs_mp_matrix * Lin, long no_output)
/* gets first basis, FALSE if none              */
/* P may get changed if lin. space Lin found    */
/* no_output is TRUE supresses output headers   */
/* 2017.12.22  could use no_output=2 to get early exit for criss-cross method */
{
  lrs_mp scale, Temp;

  long i, j, k;

/* assign local variables to structures */

  lrs_mp_matrix A;
  long *B, *C, *Col;
  long *inequality;
  long *linearity;
  long hull = Q->hull;
  long m, d, lastdv, nlinearity, nredundcol;


  if (Q->lponly || Q->child > 0 )
    no_output = TRUE;
  m = D->m;
  d = D->d;

	
  lastdv = Q->lastdv;

  nredundcol = 0L;		/* will be set after getabasis        */
  nlinearity = Q->nlinearity;	/* may be reset if new linearity read or in getabasis*/
  linearity = Q->linearity;

	

  A = D->A;
  B = D->B;
  C = D->C;
  Col = D->Col;
  inequality = Q->inequality;


/**2020.6.17 with extract just select columns and quit */

  if(Q->extract)
    if (Q->hull || Q->nlinearity==0)
     {
       extractcols(D,Q);
       return FALSE;
     }

  lrs_alloc_mp(Temp); lrs_alloc_mp(scale);

  if(Q->verbose)
    {
       if (Q->nlinearity > 0 && Q->nonnegative)
          {
	    fprintf (lrs_ofp, "\n*linearity and nonnegative options incompatible");
	    fprintf (lrs_ofp, " - all linearities are skipped");
	    fprintf (lrs_ofp, "\n*add nonnegative constraints explicitly and ");
	    fprintf (lrs_ofp, " remove nonnegative option");
           }

       if (Q->nlinearity && Q->voronoi)
            fprintf (lrs_ofp, "\n*linearity and Voronoi options set - results unpredictable");

       if (Q->lponly && !Q->maximize && !Q->minimize)
    	    fprintf (lrs_ofp, "\n*LP has no objective function given - assuming all zero");

    }

  if (Q->runs > 0)		/* arrays for estimator */
    {
      Q->isave = (long *) CALLOC ((unsigned) (m * d), sizeof (long));
      Q->jsave = (long *) CALLOC ((unsigned) (m * d), sizeof (long));
    }
/* default is to look for starting cobasis using linearies first, then     */
/* filling in from last rows of input as necessary                         */
/* linearity array is assumed sorted here                                  */
/* note if restart/given start inequality indices already in place         */
/* from nlinearity..d-1                                                    */
  for (i = 0; i < nlinearity; i++)	/* put linearities first in the order */
    inequality[i] = linearity[i];

  k = 0;			/* index for linearity array   */

  if (Q->givenstart)
    k = d;
  else
    k = nlinearity;
  for (i = m; i >= 1; i--)
    {
      j = 0;
      while (j < k && inequality[j] != i)
	j++;			/* see if i is in inequality  */
      if (j == k)
	inequality[k++] = i;
    }
  if (Q->debug)
    {
	      fprintf (lrs_ofp, "\n*Starting cobasis uses input row order");
	      for (i = 0; i < m; i++)
		fprintf (lrs_ofp, " %ld", inequality[i]);
	
    }
/* for voronoi convert to h-description using the transform                  */
/* a_0 .. a_d-1 -> (a_0^2 + ... a_d-1 ^2)-2a_0x_0-...-2a_d-1x_d-1 + x_d >= 0 */
/* note constant term is stored in column d, and column d-1 is all ones      */
/* the other coefficients are multiplied by -2 and shifted one to the right  */
  if (Q->debug)
    printA (D, Q);
  if (Q->voronoi)
    {
      Q->hull = FALSE;
      hull = FALSE;
      for (i = 1; i <= m; i++)
	{
	  if (zero (A[i][1]))
	    {
              printf("\nWith voronoi option column one must be all one\n");
	      return (FALSE);
	    }
	  copy (scale, A[i][1]);	/*adjust for scaling to integers of rationals */
	  itomp (ZERO, A[i][0]);
	  for (j = 2; j <= d; j++)	/* transform each input row */
	    {
	      copy (Temp, A[i][j]);
	      mulint (A[i][j], Temp, Temp);
	      linint (A[i][0], ONE, Temp, ONE);
	      linint (A[i][j - 1], ZERO, A[i][j], -TWO);
	      mulint (scale, A[i][j - 1], A[i][j - 1]);
	    }			/* end of for (j=1;..) */
	  copy (A[i][d], scale);
	  mulint (scale, A[i][d], A[i][d]);
	}/* end of for (i=1;..) */
        if (Q->debug)
		printA (D, Q);
    }				/* end of if(voronoi)     */
  if (!Q->maximize && !Q->minimize)
    for (j = 0; j <= d; j++)
      itomp (ZERO, A[0][j]);

/* Now we pivot to standard form, and then find a primal feasible basis       */
/* Note these steps MUST be done, even if restarting, in order to get         */
/* the same index/inequality correspondance we had for the original prob.     */
/* The inequality array is used to give the insertion order                   */
/* and is defaulted to the last d rows when givenstart=FALSE                  */

  if(Q->nonnegative) 
   {
/* no need for initial pivots here, labelling already done */
     Q->lastdv = d;
     Q->nredundcol = 0;
   }
  else
  {
     if (!getabasis (D, Q, inequality))
          return FALSE;
/* bug fix 2009.12.2 */
     nlinearity=Q->nlinearity;   /*may have been reset if some lins are redundant*/
  }
  
 if(overflow_detected)
    {
      if(Q->verbose && !Q->mplrs)
        lrs_warning(Q,"warning","*overflow getfirstbasis");
      return 1;
    }

/* 2020.2.2 */
/* extract option asked to remove all linearities and output the reduced A matrix */
/* should be followed by redund to get minimum representation                     */

  if (Q->extract)
     {
       linextractcols(D,Q);
       return FALSE;
     }

  if(Q->debug)
  {
    	fprintf(lrs_ofp,"\nafter getabasis");
    	printA(D, Q);
  }
  nredundcol = Q->nredundcol;
  lastdv = Q->lastdv;
  d = D->d;



/********************************************************************/
/* now we start printing the output file  unless no output requested */
/********************************************************************/


  if (Q->count[2]==1 && no_output==0 )   /* don't reprint after newstart */
  {
      int len=0;
      char *header;

      header=(char *)malloc((100+20*Q->n)*sizeof(char));

      if (Q->voronoi)
	  len=sprintf (header, "*Voronoi Diagram: Voronoi vertices and rays are output");
      else
       {
        if (hull)
	  len=sprintf (header, "H-representation");
        else
	  len=sprintf (header, "V-representation");
       }

/* Print linearity space                 */
/* Don't print linearity if first column zero in hull computation */

      if (hull && Q->homogeneous)
	k = 1;			/* 0 normally, 1 for homogeneous case     */
      else
	k = 0;

      if (nredundcol > k)
	{
	  	len=len+sprintf (header+len, "\nlinearity %ld ", nredundcol - k);	/*adjust nredundcol for homog. */
	  	for (i = 1; i <= nredundcol - k; i++)
	    		len=len+sprintf (header+len, " %ld", i);
	}			/* end print of linearity space */

      	len=len+sprintf (header+len, "\nbegin");
      	len=len+sprintf (header+len, "\n***** %ld rational", Q->n);

        if(Q->mplrs)
          lrs_post_output("header",header);
        else
          if(Q->messages)
           fprintf(lrs_ofp,"\n%s",header);

       free(header);	
  }

			/* end of if !no_output .......   */


/* Reset up the inequality array to remember which index is which input inequality */
/* inequality[B[i]-lastdv] is row number of the inequality with index B[i]              */
/* inequality[C[i]-lastdv] is row number of the inequality with index C[i]              */

  for (i = 1; i <= m; i++)
    inequality[i] = i;
  if (nlinearity > 0)		/* some cobasic indices will be removed */
    {
      for (i = 0; i < nlinearity; i++)	/* remove input linearity indices */
	inequality[linearity[i]] = 0;
      k = 1;			/* counter for linearities         */
      for (i = 1; i <= m - nlinearity; i++)
	{
	  while (k <= m && inequality[k] == 0)
	    k++;		/* skip zeroes in corr. to linearity */
	  inequality[i] = inequality[k++];
	}
    }
				/* end if linearity */

  if (Q->debug)
    {
      fprintf (lrs_ofp, "\ninequality array initialization:");
      for (i = 1; i <= m - nlinearity; i++)
	fprintf (lrs_ofp, " %ld", inequality[i]);
	
    }




  if (nredundcol > 0)
    {
      const unsigned int Qn = Q->n;
      *Lin = lrs_alloc_mp_matrix (nredundcol, Qn);

      for (i = 0; i < nredundcol; i++)
	{
		

	  if (!(Q->homogeneous && Q->hull && i == 0))	/* skip redund col 1 for homog. hull */
	    {
		
	      lrs_getray (D, Q, Col[0], D->C[0] + i - hull, (*Lin)[i]);		/* adjust index for deletions */
	    }

	  if (!removecobasicindex (D, Q, 0L))
	    {
	      lrs_clear_mp_matrix (*Lin, nredundcol, Qn);
	      return FALSE;
	    }
	}
	

    }				/* end if nredundcol > 0 */



  	if (Q->lponly || Q->nash ){
		if (Q->verbose && !Q->testlin)
		{
			fprintf (lrs_ofp, "\nNumber of pivots for starting dictionary: %ld",Q->count[3]);
			if(Q->lponly && Q->debug)
			     printA (D, Q);
		}
        }

/*2017.12.22   If you want to do criss-cross now is the time ! */


/* Do dual pivots to get primal feasibility */
  if (!primalfeasible (D, Q))
    {
     if (overflow_detected)
          return FALSE;
     if(!Q->mplrs)
          fprintf (lrs_ofp, "\nend");
     lrs_warning(Q,"warning", "\nNo feasible solution");
     if (Q->nash && Q->verbose )
      {
          fprintf (lrs_ofp, "\nNumber of pivots for feasible solution: %ld",Q->count[3]);
          fprintf (lrs_ofp, " - No feasible solution");
      }
      return FALSE;
    }

  if (Q->lponly || Q->nash )
      if (Q->verbose && !Q->testlin)
     {
      fprintf (lrs_ofp, "\nNumber of pivots for feasible solution: %ld",Q->count[3]);
      if(Q->lponly && Q->debug)
	      printA (D, Q);
     }



/* Now solve LP if objective function was given */
  if (Q->maximize || Q->minimize)
    {

      Q->unbounded = !lrs_solvelp (D, Q, Q->maximize);
      if (Q->lponly)		
        {

         if (Q->verbose && !Q->testlin)
          {
           fprintf (lrs_ofp, "\nNumber of pivots for optimum solution: %ld",Q->count[3]);
           if(Q->debug)
                printA (D, Q);
          }
         lrs_clear_mp(Temp); lrs_clear_mp(scale);
//          if(Q->testlin || Q->mplrs )   /*2024.2.16 what is this for?*/
         return TRUE;
        }

      else                         /* check to see if objective is dual degenerate */
       {
	  j = 1;
	  while (j <= d && !zero (A[0][j]))
	    j++;
	  if (j <= d)
	    Q->dualdeg = TRUE;
	}
    }
  else
/* re-initialize cost row to -det */
    {
      for (j = 1; j <= d; j++)
	{
	  copy (A[0][j], D->det);
	  storesign (A[0][j], NEG);
	}

      itomp (ZERO, A[0][0]);	/* zero optimum objective value */
    }


/* reindex basis to 0..m if necessary */
/* we use the fact that cobases are sorted by index value */
  if (Q->debug)
    printA (D, Q);



  while (C[0] <= m)
    {
      i = C[0];
      j = inequality[B[i] - lastdv];
      inequality[B[i] - lastdv] = inequality[C[0] - lastdv];
      inequality[C[0] - lastdv] = j;
      C[0] = B[i];
      B[i] = i;
      reorder1 (C, Col, ZERO, d);
    }



  if (Q->debug)
    {
      fprintf (lrs_ofp, "\n*Inequality numbers for indices %ld .. %ld : ", lastdv + 1, m + d);
      for (i = 1; i <= m - nlinearity; i++)
	fprintf (lrs_ofp, " %ld ", inequality[i]);
      printA (D, Q);
    }



  if (Q->restart)
    {
      if (Q->debug)
	fprintf (lrs_ofp, "\nPivoting to restart co-basis");
      if (!restartpivots (D, Q))
	return FALSE;
      D->lexflag = lexmin (D, Q, ZERO);		/* see if lexmin basis */
      if (Q->debug)
	printA (D, Q);
    }

/* Check to see if necessary to resize */
/* bug fix 2018.6.7 new value of d required below */
  if (Q->inputd > D->d)
    *D_p = resize (D, Q);

  lrs_clear_mp(Temp); lrs_clear_mp(scale);
  return TRUE;
}
/********* end of lrs_getfirstbasis  ***************/

/*****************************************/
/* getnextbasis in reverse search order  */
/*****************************************/


long 
lrs_getnextbasis (lrs_dic ** D_p, lrs_dat * Q, long backtrack)
	 /* gets next reverse search tree basis, FALSE if none  */
	 /* switches to estimator if maxdepth set               */
	 /* backtrack TRUE means backtrack from here            */

{
  /* assign local variables to structures */
  long i = 0L, j = 0L;
  long m = D->m;
  long d = D->d;
  long saveflag;
  long cob_est=0;     /* estimated number of cobases in subtree from current node */


/* 2022.3.23 multithreading */

  if(Q->child > 0 && D->depth == 0 )
      j=Q->child-1;   /* first time only start at Q-> child */

  if (backtrack && D->depth == 0)
    return FALSE;                       /* cannot backtrack from root      */

  if (Q->maxoutput > 0 && Q->count[0]+Q->count[1]-Q->hull >= Q->maxoutput)
     return FALSE;                      /* output limit reached            */

  while ((j < d) || (D->B[m] != m))	/*main while loop for getnextbasis */
    {
      if (D->depth >= Q->maxdepth)
        {
          if (Q->runs > 0 && !backtrack )     /*get an estimate of remaining tree */
            {

//2015.2.9 do iterative estimation backtracking when estimate is small

                 saveflag=Q->printcobasis;
                 Q->printcobasis=FALSE;
                 cob_est=lrs_estimate (D, Q);
                 Q->printcobasis=saveflag;
                 if(cob_est <= Q->subtreesize) /* stop iterative estimation */
                  {
                    if(Q->verbose && cob_est > 0)   /* when zero we are at a leaf */
                       {  lrs_printcobasis(D,Q,ZERO);
                          fprintf(lrs_ofp," cob_est= %ld *subtree",cob_est);
                       }
                    backtrack=TRUE;
                  }

            }
            else    // either not estimating or we are backtracking

              if (!backtrack ) 
                 if(!lrs_leaf(D,Q))    /* 2015.6.5 cobasis returned if not a leaf */
                      lrs_return_unexplored(D,Q);

            backtrack = TRUE;

 
            if (Q->maxdepth == 0 && cob_est <= Q->subtreesize)	/* root estimate only */
	       return FALSE;	/* no nextbasis  */
       }     // if (D->depth >= Q->maxdepth)


      if (backtrack)		/* go back to prev. dictionary, restore i,j */
	{
	  backtrack = FALSE;

	  if (check_cache (D_p, Q, &i, &j))
	    {
	      if (Q->debug)
		fprintf (lrs_ofp, "\n Cached Dict. restored to depth %ld\n", D->depth);
	    }
	  else
	    {
	      D->depth--;
	      selectpivot (D, Q, &i, &j);
	      pivot (D, Q, i, j);
	      update (D, Q, &i, &j);	/*Update B,C,i,j */
	    }

	  if (Q->debug)
	    {
	      fprintf (lrs_ofp, "\n Backtrack Pivot: indices i=%ld j=%ld depth=%ld", i, j, D->depth);
	      printA (D, Q);
	    };

	  j++;			/* go to next column */
	}			/* end of if backtrack  */

      if (D->depth < Q->mindepth)
	break;

      /* try to go down tree */
/* 2022.3.23 */
      if(Q->child > 0 && D->depth == 0 )    /* multithread, only check one child at top */
        {
          if (!reverse (D, Q, &i, j) )  
             break;
          else
             Q->mindepth=1;
         }
       else
         while ((j < d) && (!reverse (D, Q, &i, j) || (Q->truncate && Q->minratio[D->m]==1)))
	    j++;

      if (j == d )
	backtrack = TRUE;
      else
	/*reverse pivot found */
	{
	  cache_dict (D_p, Q, i, j);
	  /* Note that the next two lines must come _after_ the 
	     call to cache_dict */

	  D->depth++;
	  if (D->depth > Q->deepest)
	    Q->deepest++;

	  pivot (D, Q, i, j);
	  update (D, Q, &i, &j);	/*Update B,C,i,j */

	  D->lexflag = lexmin (D, Q, ZERO);	/* see if lexmin basis */
	  Q->count[2]++;
	  Q->totalnodes++;

	  if (Q->strace == Q->count[2])
	    Q->debug = TRUE;
	  if (Q->etrace == Q->count[2])
	    Q->debug = FALSE;
	  return TRUE;		/*return new dictionary */
	}


    }				/* end of main while loop for getnextbasis */
  return FALSE;			/* done, no more bases */
}				/*end of lrs_getnextbasis */

/*************************************/
/* print out one line of output file */
/*************************************/

long 
lrs_getvertex (lrs_dic * P, lrs_dat * Q, lrs_mp_vector output)
/*Print out current vertex if it is lexmin and return it in output */
/* return FALSE if no output generated  */
{
  lrs_mp_matrix A = P->A;
  long i;
  long ind;			/* output index                                  */
  long ired;			/* counts number of redundant columns            */
/* assign local variables to structures */
  long *redundcol = Q->redundcol;
  long *count = Q->count;
  long *B = P->B;
  long *Row = P->Row;

  long lastdv = Q->lastdv;

  long hull;
  long lexflag;

	
  hull = Q->hull;
  lexflag = P->lexflag;
  if (lexflag || Q->allbases)
   {
    ++(Q->count[1]);
/* 2021.5.21 check for max depth output */
    if(P->depth > Q->count[8])
      Q->count[8] = P->depth;
   }

  if (Q->debug)
    printA (P, Q);

  if (Q->getvolume)
   {
    linint (Q->sumdet, 1, P->det, 1);  
    updatevolume (P, Q);
   }
  if(Q->triangulation)   /* this will print out a triangulation */
	lrs_printcobasis(P,Q,ZERO);


  /*print cobasis if printcobasis=TRUE and count[2] a multiple of frequency */
  /* or for lexmin basis, except origin for hull computation - ugly!        */

  if (Q->printcobasis)
      if ((lexflag && !hull)  || ((Q->frequency > 0) && (count[2] == (count[2] / Q->frequency) * Q->frequency)))
		lrs_printcobasis(P,Q,ZERO);

  if (hull)
    return FALSE;		/* skip printing the origin */

  if (!lexflag && !Q->allbases && !Q->lponly)	/* not lexmin, and not printing forced */
    return FALSE;


  /* copy column 0 to output */

  i = 1;
  ired = 0;
  copy (output[0], P->det);

  for (ind = 1; ind < Q->n; ind++)	/* extract solution */

    if ((ired < Q->nredundcol) && (redundcol[ired] == ind))
      /* column was deleted as redundant */
      {
	itomp (ZERO, output[ind]);
	ired++;
      }
    else
      /* column not deleted as redundant */
      {
	getnextoutput (P, Q, i, ZERO, output[ind]);
	i++;
      }

  reducearray (output, Q->n);
  if (lexflag && one(output[0]))
      ++Q->count[4];               /* integer vertex */


/* uncomment to print nonzero basic variables 

 printf("\n nonzero basis: vars");
  for(i=1;i<=lastdv; i++)
   {
    if ( !zero(A[Row[i]][0]) )
         printf(" %ld ",B[i]);
   }
*/

/* printslack inequality indices  */

   if (Q->printslack)
    {
       fprintf(lrs_ofp,"\nslack ineq:");
       for(i=lastdv+1;i<=P->m; i++)
         {
           if (!zero(A[Row[i]][0]))
                 fprintf(lrs_ofp," %ld ", Q->inequality[B[i]-lastdv]);
         }
    }



  return TRUE;
}				/* end of lrs_getvertex */

long 
lrs_getray (lrs_dic * P, lrs_dat * Q, long col, long redcol, lrs_mp_vector output)
/*Print out solution in col and return it in output   */
/*redcol =n for ray/facet 0..n-1 for linearity column */
/*hull=1 implies facets will be recovered             */
/* return FALSE if no output generated in column col  */
{
  long i;
  long ind;			/* output index                                  */
  long ired;			/* counts number of redundant columns            */
/* assign local variables to structures */
  long *redundcol = Q->redundcol;
  long *count = Q->count;
  long hull = Q->hull;
  long n = Q->n;

  long *B = P->B;
  long *Row = P->Row;
  long lastdv = Q->lastdv;

  if (Q->debug)
    {
      printA (P, Q);
      for (i = 0; i < Q->nredundcol; i++)
	fprintf (lrs_ofp, " %ld", redundcol[i]);
      fflush(lrs_ofp);
    }

  if (redcol == n)
    {
      ++count[0];
      if (Q->printcobasis)
		lrs_printcobasis(P,Q,col);

    }

  i = 1;
  ired = 0;

  for (ind = 0; ind < n; ind++)	/* print solution */
    {
      if (ind == 0 && !hull)	/* must have a ray, set first column to zero */
	itomp (ZERO, output[0]);

      else if ((ired < Q->nredundcol) && (redundcol[ired] == ind))
	/* column was deleted as redundant */
	{
	  if (redcol == ind)	/* true for linearity on this cobasic index */
	    /* we print reduced determinant instead of zero */
	    copy (output[ind], P->det);
	  else
	    itomp (ZERO, output[ind]);
	  ired++;
	}
      else
	/* column not deleted as redundant */
	{
	  getnextoutput (P, Q, i, col, output[ind]);
	  i++;
	}

    }
  reducearray (output, n);
/* printslack for rays: 2006.10.10 */
/* printslack inequality indices  */

   if (Q->printslack)
    {
       fprintf(lrs_ofp,"\nslack ineq:");
       for(i=lastdv+1;i<=P->m; i++)
         {
           if (!zero(P->A[Row[i]][col]))
                 fprintf(lrs_ofp," %ld ", Q->inequality[B[i]-lastdv]);
         }
    }

	

  return TRUE;
}				/* end of lrs_getray */


void 
getnextoutput (lrs_dic * P, lrs_dat * Q, long i, long col, lrs_mp out)
      /* get A[B[i][col] and copy to out */
{
  long row;
  long m = P->m;
  long d = P->d;
  long lastdv = Q->lastdv;
  lrs_mp_matrix A = P->A;
  long *B = P->B;
  long *Row = P->Row;
  long j;

  if (i == d && Q->voronoi)
    return;			/* skip last column if voronoi set */
  if (i == lastdv && Q->testlin)
    return;			/* skip last column if testlin set */

  row = Row[i];

  if (Q->nonnegative) 	/* if m+i basic get correct value from dictionary          */
                        /* the slack for the inequality m-d+i contains decision    */
                        /* variable x_i. We first see if this is in the basis      */
                        /* otherwise the value of x_i is zero, except for a ray    */
                        /* when it is one (det/det) for the actual column it is in */
    {
	for (j = lastdv+ 1; j <= m; j++)
           {
             if ( Q->inequality[B[j]-lastdv] == m-d+i )
	          {
                    copy (out, A[Row[j]][col]);
                    return;
                  }
           }
/* did not find inequality m-d+i in basis */
      if ( i == col )
            copy(out,P->det);
      else
            itomp(ZERO,out);
	
    }
  else
    copy (out, A[row][col]);

}				/* end of getnextoutput */


void
lrs_printcobasis (lrs_dic * P, lrs_dat * Q, long col)
/* col is output column being printed */
{
        char *ss, *sdet, *sin_det, *sz;
	long i;
	long rflag;			/* used to find inequality number for ray column */
	/* assign local variables to structures */
	lrs_mp_matrix A = P->A;
	lrs_mp Nvol, Dvol;		/* hold rescaled det of current basis */
	long *B = P->B;
	long *C = P->C;
	long *Col = P->Col;
	long *Row = P->Row;
	long *inequality = Q->inequality;
	long *temparray = Q->temparray;
	long *count = Q->count;
	long hull = Q->hull;
	long d = P->d;
	long lastdv = Q->lastdv;
	long m=P->m;
	long firstime=TRUE;
        long len=0;

	lrs_alloc_mp(Nvol); lrs_alloc_mp(Dvol);

/* convert lrs_mp to char, compute length of string ss and malloc*/

        sdet=cpmp(" det=", P->det);

        rescaledet (P, Q, Nvol, Dvol);  /* scales determinant in case input rational */

        sin_det=cprat("in_det=", Nvol,Dvol);
        itomp(ONE,P->objden); itomp(ONE,P->objnum);

        sz=cprat("z=", P->objnum, P->objden);

        len = snprintf(NULL, 0, "%s%s%s", sdet,sin_det,sz);

        ss=(char*)malloc(len+(d+m)*20);

/* build the printcobasis string */
    
        len=0;
	if (hull)
	len=len+sprintf (ss+len, "F#%ld B#%ld h=%ld vertices/rays ", count[0], count[2], P->depth);
	else if (Q->voronoi)
	len=len+sprintf (ss+len, "V#%ld R#%ld B#%ld h=%ld data points ", count[1], count[0], count[2], P->depth);
	else
	len=len+sprintf (ss+len, "V#%ld R#%ld B#%ld h=%ld facets ", count[1], count[0], count[2], P->depth);

	rflag = (-1);
	for (i = 0; i < d; i++)
	{
	temparray[i] = inequality[C[i] - lastdv];
	if (Col[i] == col)
	rflag = temparray[i];	/* look for ray index */
	}
	for (i = 0; i < d; i++)
	reorder (temparray, d);
	for (i = 0; i < d; i++)
	{
	len=len+sprintf (ss+len, " %ld", temparray[i]);

	if (!(col == ZERO) && (rflag == temparray[i])) /* missing cobasis element for ray */
	   len=len+sprintf (ss+len, "*");

	}

	/* get and print incidence information */
	if ( col == 0 )
	Q->nincidence = d;
	else
	Q->nincidence = d-1;

	for(i=lastdv+1;i<=m;i++)
	  if ( zero (A[Row[i]][0] ))
	    if( ( col == ZERO ) || zero (A[Row[i]] [col]) )
	      { 
	         Q->nincidence++;
	         if( Q->incidence )
	          {
		    if (firstime)
		     {
		      len=len+sprintf (ss+len," :");
		      firstime = FALSE;
		     }
		     len=len+sprintf(ss+len," %ld",inequality[B[i] - lastdv ] );
	          }
	       }
	 
	len=len+sprintf(ss+len," I#%ld",Q->nincidence);

        sprintf (ss+len,"%s %s %s ",sdet,sin_det,sz);

        if(Q->maxincidence == MAXD || Q->verbose)
           {
           if(Q->mplrs)
   	   lrs_post_output("cobasis", ss);
           else
              fprintf(lrs_ofp,"\n%s",ss);
           }

        free(ss); free(sdet); free(sin_det); free(sz);
	lrs_clear_mp(Nvol); lrs_clear_mp(Dvol);
  

}				/* end of lrs_printcobasis */


/*********************/
/* print final totals */
/*********************/
void 
lrs_printtotals (lrs_dic * P, lrs_dat * Q)
{
static int first_time=1;
/* print warnings */
if(first_time)
 {
    first_time=0;

    if (!Q->mplrs)
      fprintf(lrs_ofp,"\nend");

  if (Q->dualdeg)
   {
      lrs_warning(Q,"finalwarn","*Warning: Starting dictionary is dual degenerate");
      lrs_warning(Q,"finalwarn","*Complete enumeration may not have been produced");
      if (Q->maximize)
         lrs_warning(Q,"finalwarn","*Recommendation: Add dualperturb option before maximize in input file\n");
      else
         lrs_warning(Q,"finalwarn","*Recommendation: Add dualperturb option before minimize in input file\n");
    }

  if (Q->unbounded)
    {
      lrs_warning(Q,"finalwarn","*Warning: Starting dictionary contains rays");       
      lrs_warning(Q,"finalwarn","*Complete enumeration may not have been produced");  
      if (Q->maximize)
        lrs_warning(Q,"finalwarn","*Recommendation: Change or remove maximize option or add bounds\n");
      else
        lrs_warning(Q,"finalwarn","*Recommendation: Change or remove minimize option or add bounds\n");
    }

  if (Q->truncate)
     lrs_warning(Q,"finalwarn","*Tree truncated at each new vertex");
  }

  if(!Q->hull)
   {
     if ( Q->allbases )       
        lrs_warning(Q,"finalwarn","*Note! Duplicate vertices/rays may be present");
     else if (Q->count[0] > 1 && !Q->homogeneous)
        lrs_warning(Q,"finalwarn","*Note! Duplicate rays may be present");
   }

  if(Q->mplrs)
    {
     char  *vol;
     if (Q->hull && Q->getvolume)
      {
        rescalevolume (P, Q, Q->Nvolume, Q->Dvolume);
        vol=cprat("",Q->Nvolume, Q->Dvolume);
        lrs_post_output("volume", vol);
        free(vol);
      }
     return;
    }

  if(!Q->messages)
    return;

  long i;
  double x;
/* local assignments */
  double *cest = Q->cest;
  long *count = Q->count;
  long *inequality = Q->inequality;
  long *linearity = Q->linearity;
  long *temparray = Q->temparray;

  long *C = P->C;

  long hull = Q->hull;
  long homogeneous = Q->homogeneous;
  long nlinearity = Q->nlinearity;
  long nredundcol = Q->nredundcol;
  long d, lastdv;
  d = P->d;
  lastdv = Q->lastdv;
  if(Q->hull)    /* count[1] stores the number of linearities */
        Q->count[1]=Q->nredundcol - Q->homogeneous;

/* warnings for lrs only */
  if (Q->maxdepth < MAXD)
    fprintf (lrs_ofp, "\n*Tree truncated at depth %lld", Q->maxdepth);
  if (Q->maxcobases > 0L)
    fprintf (lrs_ofp, "\n*maxcobases = %ld", Q->maxcobases-Q->startcount[2]);
  if (Q->maxoutput > 0L)
    fprintf (lrs_ofp, "\n*Maximum number of output lines = %ld", Q->maxoutput);


/* next block with volume rescaling must come before estimates are printed */

  if (Q->getvolume && Q->runs == 0)
    {

      if( Q->debug)
         {
           fprintf (lrs_ofp, "\n*Sum of det(B)=");
           pmp ("", Q->sumdet);
         }

      rescalevolume (P, Q, Q->Nvolume, Q->Dvolume);

      if (Q->polytope)
	prat ("\n*Volume=", Q->Nvolume, Q->Dvolume);
      else
	prat ("\n*Pseudovolume=", Q->Nvolume, Q->Dvolume);
    }
  if (hull)     /* output things that are specific to hull computation */
    {
      fprintf (lrs_ofp, "\n*Totals: facets=%ld bases=%ld", count[0], count[2]);

      if (count[1] > 0 )	
       {
        fprintf (lrs_ofp, " linearities=%ld", count[1]);
        fprintf (lrs_ofp, " facets+linearities=%ld",count[1]+count[0]);
       }
     printf (" max_facet_depth=%ld", count[8]);
     if(lrs_ofp != stdout)
       {
           printf ("\n*Totals: facets=%ld bases=%ld", count[0], count[2]);

           if (count[1] > 0)
           {
            printf (" linearities=%ld", count[1]);
            printf (" facets+linearities=%ld",count[1]+count[0]);
           }
           printf (" max_facet_depth=%ld", count[8]);
       }


       if(Q->runs > 0)
       {
         fprintf (lrs_ofp, "\n*Estimates: facets=%.0f bases=%.0f", count[0] + cest[0], count[2] + cest[2]);
         if (Q->getvolume)
	  {
            rescalevolume (P, Q, Q->Nvolume, Q->Dvolume);
	    rattodouble (Q->Nvolume, Q->Dvolume, &x);
	    for (i = 2; i < d; i++)
	      cest[3] = cest[3] / i;	/*adjust for dimension */
            if(cest[3]==0)
               prat (" volume=", Q->Nvolume, Q->Dvolume);
            else
	       fprintf (lrs_ofp, " volume=%g", cest[3] + x);

           }

          fprintf (lrs_ofp, "\n*Total number of tree nodes evaluated: %ld", Q->totalnodes);
#ifndef TIMES
          fprintf (lrs_ofp, "\n*Estimated total running time=%.1f secs ",(count[2]+cest[2])/Q->totalnodes*get_time () );
#endif
         }


    }
  else         /* output things specific to vertex/ray computation */
    {
      fprintf (lrs_ofp, "\n*Totals: vertices=%ld rays=%ld bases=%ld", count[1], count[0], count[2]);

      fprintf (lrs_ofp, " integer_vertices=%ld  max_vertex_depth=%ld",count[4],count[8]);


      if (nredundcol > 0)
        fprintf (lrs_ofp, " linearities=%ld", nredundcol);
      if ( count[0] + nredundcol > 0 )
         {
           fprintf (lrs_ofp, " vertices+rays");
           if ( nredundcol > 0 )
              fprintf (lrs_ofp, "+linearities");
           fprintf (lrs_ofp, "=%ld",nredundcol+count[0]+count[1]);
         }

      if(lrs_ofp != stdout)
       { 
	printf ("\n*Totals: vertices=%ld rays=%ld bases=%ld", count[1], count[0], count[2]);

        printf (" integer_vertices=%ld max_vertex_depth=%ld",count[4],count[8]);

      if (nredundcol > 0)
           printf (" linearities=%ld", nredundcol);
      if ( count[0] + nredundcol > 0 )
         {
           printf (" vertices+rays");
           if ( nredundcol > 0 )
              printf ("+linearities");
           printf ("=%ld",nredundcol+count[0]+count[1]);
         }
        } /* end lrs_ofp != stdout */


      if (Q->runs  > 0)
        {
	fprintf (lrs_ofp, "\n*Estimates: vertices=%.0f rays=%.0f", count[1]+cest[1], count[0]+cest[0]);
	fprintf (lrs_ofp, " bases=%.0f integer_vertices=%.0f ",count[2]+cest[2], count[4]+cest[4]);

         if (Q->getvolume)
	   {
	     rattodouble (Q->Nvolume, Q->Dvolume, &x);
	     for (i = 2; i <= d-homogeneous; i++)
	       cest[3] = cest[3] / i;	/*adjust for dimension */
	     fprintf (lrs_ofp, " pseudovolume=%g", cest[3] + x);
	   }
         fprintf (lrs_ofp, "\n*Total number of tree nodes evaluated: %ld", Q->totalnodes);
#ifndef TIMES
         fprintf (lrs_ofp, "\n*Estimated total running time=%.1f secs ",(count[2]+cest[2])/Q->totalnodes*get_time () );
#endif
        }

    }				/* end of output for vertices/rays */

if(Q->verbose)
   {
     fprintf (lrs_ofp, "\n*Dictionary Cache: dict_limit=%ld dict_count=%ld misses= %ld/%ld   Tree Depth= %ld", dict_limit, dict_count, cache_misses, cache_tries, Q->deepest);
     if(lrs_ofp != stdout)
       printf ("\n*Dictionary Cache: max size= %ld misses= %ld/%ld   Tree Depth= %ld", dict_count, cache_misses, cache_tries, Q->deepest);
    }
  if(Q->maxincidence != MAXD)
     fprintf(lrs_ofp,"\n*maxincidence %lld %lld  used to prune search", Q->maxincidence,Q->minprunedepth);
  if(Q->debug)
     {
	fprintf (lrs_ofp, "\n*Input size m=%ld rows n=%ld columns", P->m, Q->n);
	
	if (hull)
	    fprintf (lrs_ofp, " working dimension=%ld", d - 1 + homogeneous);
	else
	    fprintf (lrs_ofp, " working dimension=%ld", d);
	
	fprintf (lrs_ofp, "\n*Starting cobasis defined by input rows");
	for (i = 0; i < nlinearity; i++)
	    temparray[i] = linearity[i];
	for (i = nlinearity; i < lastdv; i++)
            temparray[i] = inequality[C[i - nlinearity] - lastdv];
  	for (i = 0; i < lastdv; i++)
    	    reorder (temparray, lastdv);
  	for (i = 0; i < lastdv; i++)
    	   fprintf (lrs_ofp, " %ld", temparray[i]);
      }
  fprintf(lrs_ofp,"\n");
  return;


}				/* end of lrs_printtotals */
/************************/
/*  Estimation function */
/************************/
long  
lrs_estimate (lrs_dic * P, lrs_dat * Q)
		   /*returns estimate of subtree size (no. cobases) from current node    */
		   /*current node is not counted.                   */
		   /*cest[0]rays [1]vertices [2]bases [3]volume     */
                   /*    [4] integer vertices                       */
{

  lrs_mp_vector output;		/* holds one line of output; ray,vertex,facet,linearity */
  lrs_mp Nvol, Dvol;		/* hold volume of current basis */
  long estdepth = 0;		/* depth of basis/vertex in subtree for estimate */
  long i = 0, j = 0, k, nchild, runcount, col;
  double prod = 0.0;
  double cave[] =
  {0.0, 0.0, 0.0, 0.0, 0.0};
  double nvertices, nbases, nrays, nvol, nivertices;
  long rays = 0;
  double newvol = 0.0;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *isave = Q->isave;
  long *jsave = Q->jsave;
  double *cest = Q->cest;
  long d = P->d;
  lrs_alloc_mp(Nvol); lrs_alloc_mp(Dvol);
/* Main Loop of Estimator */

  output = lrs_alloc_mp_vector (Q->n);	/* output holds one line of output from dictionary     */

  for (runcount = 1; runcount <= Q->runs; runcount++)
    {				/* runcount counts number of random probes */
      j = 0;
      nchild = 1;
      prod = 1;
      nvertices = 0.0;
      nbases = 0.0;
      nrays = 0.0;
      nvol = 0.0;
      nivertices =0.0;

      while (nchild != 0)	/* while not finished yet */
	{

	  nchild = 0;
	  while (j < d)
	    {
	      if (reverse (P, Q, &i, j))
		{
		  isave[nchild] = i;
		  jsave[nchild] = j;
		  nchild++;
		}
	      j++;
	    }

	  if (estdepth == 0 && nchild == 0)
	    {
	      cest[0] = cest[0] + rays;		/* may be some rays here */
              lrs_clear_mp(Nvol); lrs_clear_mp(Dvol);
              lrs_clear_mp_vector(output, Q->n);
	      return(0L);		/*subtree is a leaf */
	    }

	  prod = prod * nchild;
	  nbases = nbases + prod;
	  if (Q->debug)
	    {
	      fprintf (lrs_ofp, "   degree= %ld ", nchild);
	      fprintf (lrs_ofp, "\nPossible reverse pivots: i,j=");
	      for (k = 0; k < nchild; k++)
		fprintf (lrs_ofp, "%ld,%ld ", isave[k], jsave[k]);
	    }

	  if (nchild > 0)	/*reverse pivot found choose random child */
	    {
	      k = myrandom (Q->seed, nchild);
	      Q->seed = myrandom (Q->seed, 977L);
	      i = isave[k];
	      j = jsave[k];
	      if (Q->debug)
		fprintf (lrs_ofp, "  selected pivot k=%ld seed=%ld ", k, Q->seed);
	      estdepth++;
	      Q->totalnodes++;	/* calculate total number of nodes evaluated */
	      pivot (P, Q, i, j);
	      update (P, Q, &i, &j);	/*Update B,C,i,j */
	      if (lexmin (P, Q, ZERO))	/* see if lexmin basis for vertex */
                {
		  nvertices = nvertices + prod;
                                                    /* integer vertex estimate */
                  if( lrs_getvertex(P,Q,output))
                      { --Q->count[1];
                       if  (one(output[0] ))
                         { --Q->count[4];
                           nivertices = nivertices + prod;
                         }
                      }
                }

	      rays = 0;
	      for (col = 1; col <= d; col++)
		if (negative (A[0][col]) && (lrs_ratio (P, Q, col) == 0) && lexmin (P, Q, col))
		  rays++;
	      nrays = nrays + prod * rays;	/* update ray info */

	      if (Q->getvolume)
		{
		  rescaledet (P, Q, Nvol, Dvol);	/* scales determinant in case input rational */
		  rattodouble (Nvol, Dvol, &newvol);
		  nvol = nvol + newvol * prod;	/* adjusts volume for degree              */
		}
	      j = 0;
	    }
	}
      cave[0] = cave[0] + nrays;
      cave[1] = cave[1] + nvertices;
      cave[2] = cave[2] + nbases;
      cave[3] = cave[3] + nvol;
      cave[4] = cave[4] + nivertices;

/*  backtrack to root and do it again */

      while (estdepth > 0)
	{
	  estdepth = estdepth - 1;
	  selectpivot (P, Q, &i, &j);
	  pivot (P, Q, i, j);
	  update (P, Q, &i, &j);	/*Update B,C,i,j */
	  if (Q->debug)
	    {
	      fprintf (lrs_ofp, "\n Backtrack Pivot: indices i,j %ld %ld ", i, j);
	      printA (P, Q);
	    }
	  j++;
	}

    }				/* end of for loop on runcount */

  j=(long) cave[2]/Q->runs;

//2015.2.9   Do not update totals if we do iterative estimating and subtree is too big
  if(Q->subtreesize == 0  || j <= Q->subtreesize )
       for (i = 0; i < 5; i++)
           cest[i] = cave[i] / Q->runs + cest[i];

  
  lrs_clear_mp(Nvol); lrs_clear_mp(Dvol);
  lrs_clear_mp_vector(output, Q->n);
  return(j);
}				/* end of lrs_estimate  */


/*********************************/
/* Internal functions            */
/*********************************/
/* Basic Dictionary functions    */
/******************************* */

long 
reverse (lrs_dic * P, lrs_dat * Q, long *r, long s)
/*  find reverse indices  */
/* TRUE if B[*r] C[s] is a reverse lexicographic pivot */
{
  long i, j, enter, row, col;

/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *B = P->B;
  long *C = P->C;
  long *Row = P->Row;
  long *Col = P->Col;
  long d = P->d;

  enter = C[s];
  col = Col[s];
  if (Q->debug)
    {
      fprintf (lrs_ofp, "\n+reverse: col index %ld C %ld Col %ld ", s, enter, col);
      fflush (lrs_ofp);
    }
  if (!negative (A[0][col]))
    {
      if (Q->debug)
	fprintf (lrs_ofp, " Pos/Zero Cost Coeff");
      Q->minratio[P->m]=0;  /* 2011.7.14 */
      return (FALSE);
    }

  *r = lrs_ratio (P, Q, col);
  if (*r == 0)			/* we have a ray */
    {
      if (Q->debug)
	fprintf (lrs_ofp, " Pivot col non-negative:  ray found");
      Q->minratio[P->m]=0;  /* 2011.7.14 */
      return (FALSE);
    }

  row = Row[*r];

/* check cost row after "pivot" for smaller leaving index    */
/* ie. j s.t.  A[0][j]*A[row][col] < A[0][col]*A[row][j]     */
/* note both A[row][col] and A[0][col] are negative          */

  for (i = 0; i < d && C[i] < B[*r]; i++)
    if (i != s)
      {
	j = Col[i];
	if (positive (A[0][j]) || negative (A[row][j]))		/*or else sign test fails trivially */
	  if ((!negative (A[0][j]) && !positive (A[row][j])) ||
	      comprod (A[0][j], A[row][col], A[0][col], A[row][j]) == -1)
	    {			/*+ve cost found */
	      if (Q->debug)
               {
		fprintf (lrs_ofp, "\nPositive cost found: index %ld C %ld Col %ld", i, C[i], j);
                fflush(lrs_ofp);
               }
              Q->minratio[P->m]=0;  /* 2011.7.14 */

	      return (FALSE);
	    }
      }
  if (Q->debug)
    {
      fprintf (lrs_ofp, "\n+end of reverse : indices r %ld s %ld \n", *r, s);
      fflush (stdout);
    }
  return (TRUE);
}				/* end of reverse */

long 
selectpivot (lrs_dic * P, lrs_dat * Q, long *r, long *s)
/* select pivot indices using lexicographic rule   */
/* returns TRUE if pivot found else FALSE          */
/* pivot variables are B[*r] C[*s] in locations Row[*r] Col[*s] */
{
  long j, col;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *Col = P->Col;
  long d = P->d;

  *r = 0;
  *s = d;
  j = 0;

/*find positive cost coef */
  while ((j < d) && (!positive (A[0][Col[j]])))
    j++;

  if (j < d)			/* pivot column found! */
    {
      *s = j;
      col = Col[j];

      /*find min index ratio */
      *r = lrs_ratio (P, Q, col);
      if (*r != 0)
	return (TRUE);		/* unbounded if *r=0 */
    }
  return (FALSE);
}				/* end of selectpivot        */
/******************************************************* */

void 
pivot (lrs_dic * P, lrs_dat * Q, long bas, long cob)
		     /* Qpivot routine for array A              */
		     /* indices bas, cob are for Basis B and CoBasis C    */
		     /* corresponding to row Row[bas] and column       */
		     /* Col[cob]   respectively                       */
{
  long r, s;
  long i, j;

/* assign local variables to structures */

  lrs_mp_matrix A = P->A;
  long *B = P->B;
  long *C = P->C;
  long *Row = P->Row;
  long *Col = P->Col;
  long d, m_A;

#ifndef LRSLONG
  lrs_mp Ns, Nt;
  lrs_alloc_mp(Ns); lrs_alloc_mp(Nt); 
#endif
  lrs_mp Ars;
  lrs_alloc_mp(Ars);

  d = P->d;
  m_A = P->m_A;
  Q->count[3]++;    /* count the pivot */

  r = Row[bas];
  s = Col[cob];

/* Ars=A[r][s]    */
  if (Q->debug)
      fprintf (lrs_ofp, "\n pivot  B[%ld]=%ld  C[%ld]=%ld ", bas, B[bas], cob, C[cob]);

  copy (Ars, A[r][s]);
  storesign (P->det, sign (Ars));	/*adjust determinant to new sign */


  for (i = 0; i <= m_A; i++)
   {
    if (i != r)
      for (j = 0; j <= d; j++)
	if (j != s)
	  {
/*          A[i][j]=(A[i][j]*Ars-A[i][s]*A[r][j])/P->det; */

#ifdef LRSLONG
	    qpiv(A[i][j],Ars,A[i][s],A[r][j],P->det);
            if(overflow_detected)
              return;
#else
	    mulint (A[i][j], Ars, Nt);
	    mulint (A[i][s], A[r][j], Ns);
	    decint (Nt, Ns);
	    exactdivint (Nt, P->det, A[i][j]);
            if(overflow_detected)
              return;
#endif
	  }			/* end if j ....  */
    } /*for i */

  if (sign (Ars) == POS)
    {
      for (j = 0; j <= d; j++)	/* no need to change sign if Ars neg */
	/*   A[r][j]=-A[r][j];              */
	if (!zero (A[r][j]))
	  changesign (A[r][j]);
    }				/* watch out for above "if" when removing this "}" ! */
  else
    for (i = 0; i <= m_A; i++)
      if (!zero (A[i][s]))
	changesign (A[i][s]);

  /*  A[r][s]=P->det;                  */

  copy (A[r][s], P->det);		/* restore old determinant */
  copy (P->det, Ars);
  storesign (P->det, POS);		/* always keep positive determinant */


  if (Q->debug)
    {
      fprintf (lrs_ofp, " depth=%ld ", P->depth);
      pmp ("det=", P->det);
      fflush(stdout);
    }
/* set the new rescaled objective function value */

  mulint (P->det, Q->Lcm[0], P->objden);
  mulint (Q->Gcd[0], A[0][0], P->objnum);

  if (!Q->maximize)
        changesign (P->objnum);
  if (zero (P->objnum))
        storesign (P->objnum, POS);
  else
	reduce (P->objnum,P->objden);


#ifndef LRSLONG
  lrs_clear_mp(Ns); lrs_clear_mp(Nt); 
#endif
  lrs_clear_mp(Ars);

}				/* end of pivot */

long 
primalfeasible (lrs_dic * P, lrs_dat * Q)
/* Do dual pivots to get primal feasibility */
/* Note that cost row is all zero, so no ratio test needed for Dual Bland's rule */
{
  long primalinfeasible = TRUE;
  long i, j;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *Row = P->Row;
  long *Col = P->Col;
  long m, d, lastdv;
  m = P->m;
  d = P->d;
  lastdv = Q->lastdv;

/*temporary: try to get new start after linearity */

  while (primalinfeasible)
    {
      i=lastdv+1;
      while (i <= m && !negative (A[Row[i]][0]) )
        i++;
      if (i <= m )
	{
	      j = 0;		/*find a positive entry for in row */
	      while (j < d && !positive (A[Row[i]][Col[j]]))
		j++;
	      if (j >= d)
		return (FALSE);	/* no positive entry */
	      pivot (P, Q, i, j);
	      update (P, Q, &i, &j);
              if(overflow_detected)
                 {
                  if(Q->debug)
                      lrs_warning(Q,"warning","*overflow primalfeasible");
                  return FALSE;
                 }
          
        }   
      else
         primalinfeasible = FALSE;
    }				/* end of while primalinfeasibile */
  return (TRUE);
}				/* end of primalfeasible */


long 
lrs_solvelp (lrs_dic * P, lrs_dat * Q, long maximize)
/* Solve primal feasible lp by Dantzig`s rule and lexicographic ratio test */
/* return TRUE if bounded, FALSE if unbounded                              */
{
  long i, j, k=0L;
  long notdone=TRUE;
/* assign local variables to structures */
  long d = P->d;

/* lponly=1 Dantzig,  =2 random,  =3 hybrid, =4 Bland */

  if(Q->lponly <=1)    /* Dantzig's rule */
     while (dan_selectpivot (P, Q, &i, &j))
      {
        pivot (P, Q, i, j);
        update (P, Q, &i, &j);	/*Update B,C,i,j */
        if(overflow_detected)
          {
           if(Q->verbose && !Q->mplrs)
             lrs_warning(Q,"warning","*overflow lrs_solvelp");
           return FALSE;
          }

      }

  if(Q->lponly ==2)    /* random edge rule */
     while (ran_selectpivot (P, Q, &i, &j))
      {
        pivot (P, Q, i, j);
        update (P, Q, &i, &j);	/*Update B,C,i,j */
      }

  if(Q->lponly ==3)    /* alternate Dantzig/randome rules */
     while (notdone)
      {
        if(k % 2)      /* odd for dantzig even for random */
          notdone=dan_selectpivot (P, Q, &i, &j);
        else
          notdone=ran_selectpivot (P, Q, &i, &j);

        if(notdone)
          {
            pivot (P, Q, i, j);
            update (P, Q, &i, &j);  /*Update B,C,i,j */
          }
         k++;
      }

  if(Q->lponly ==4)    /* Bland's rule - used for vertex enumeration */
     while (selectpivot (P, Q, &i, &j))
      {
        pivot (P, Q, i, j);
        update (P, Q, &i, &j);	/*Update B,C,i,j */
      }


  if (Q->debug)
    printA (P, Q);

  if (j < d && i == 0)		/* selectpivot gives information on unbounded solution */
    {
      if (Q->lponly && Q->messages)
       {
	fprintf (lrs_ofp, "\n*Unbounded solution");
        if(Q->debug && Q->testlin)
          printA(P,Q);
       }
      return FALSE;
    }
  return TRUE;
}				/* end of lrs_solvelp  */

long 
getabasis (lrs_dic * P, lrs_dat * Q, long order[])

/* Pivot Ax<=b to standard form */
/*Try to find a starting basis by pivoting in the variables x[1]..x[d]        */
/*If there are any input linearities, these appear first in order[]           */
/* Steps: (a) Try to pivot out basic variables using order                    */
/*            Stop if some linearity cannot be made to leave basis            */
/*        (b) Permanently remove the cobasic indices of linearities           */
/*        (c) If some decision variable cobasic, it is a linearity,           */
/*            and will be removed.                                            */

{
  long i, j, k;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *B = P->B;
  long *C = P->C;
  long *Row = P->Row;
  long *Col = P->Col;
  long *linearity = Q->linearity;
  long *redundcol = Q->redundcol;
  long m, d, nlinearity;
  long nredundcol = 0L;		/* will be calculated here */
  char mess[100];
  m = P->m;
  d = P->d;
  nlinearity = Q->nlinearity;

  if (Q->debug)
    {
      fprintf (lrs_ofp, "\ngetabasis from inequalities given in order");
      for (i = 0l; i < m; i++)
	fprintf (lrs_ofp, " %ld", order[i]);
    }
  for (j = 0l; j < m; j++)
  {
    i = 0l;
    while (i <= m && B[i] != d + order[j])
      i++;                    /* find leaving basis index i */
    if (j < nlinearity && i > m)      /* cannot pivot linearity to cobasis */
    {
       if (Q->debug)
        printA (P, Q);
       if(Q->messages)
        fprintf (lrs_ofp, "\nCannot find linearity in the basis");
       return FALSE;
    }
    if (i <= m)
    {                 /* try to do a pivot */
      k = 0l;
      while (C[k] <= d && zero (A[Row[i]][Col[k]])){
            k++;
    }
  if (C[k] <= d)
    {
       pivot (P, Q, i, k);
       if(overflow_detected)
        {
          if(Q->debug)
           lrs_warning(Q,"warning","*overflow in getabasis\n");
          return FALSE;
        }
       update (P, Q, &i, &k);

    }
  else if (j<nlinearity)
    {/* cannot pivot linearity to cobasis */
    if (zero (A[Row[i]][0]))
      {
       if(Q->messages)
        {
          sprintf (mess,"*Input linearity in row %ld is redundant--converted to inequality", order[j]);
          lrs_warning(Q,"warning",mess);
         }
       linearity[j]=0l;
       Q->redineq[order[j]]=-1;  /* check for redundancy if running redund */
      }
    else
     {
      if (Q->debug)
        printA (P, Q);
      lrs_warning(Q,"warning","*No feasible solution");
      return FALSE;
      }
    }
    }
  }

/* update linearity array to get rid of redundancies */
  i = 0;
  k = 0;			/* counters for linearities         */
  while (k < nlinearity)
    {
      while (k < nlinearity && linearity[k] == 0)
	k++;
      if (k < nlinearity)
	linearity[i++] = linearity[k++];
    }

  nlinearity = i;
/* bug fix, 2009.6.27 */    Q->nlinearity = i;

/* column dependencies now can be recorded  */
/* redundcol contains input column number 0..n-1 where redundancy is */
/* 2022.4.26   testlin=T adds extra column and basic variable that shoud not be considered */
  k = 0;      
  while (k < d && C[k] <= d)
    {
      if (C[k] <= d-Q->testlin)       /* decision variable still in cobasis */
	redundcol[nredundcol++] = C[k] - Q->hull;	/* adjust for hull indices */
	
      k++;
    }

/* now we know how many decision variables remain in problem */
  Q->nredundcol = nredundcol;
  Q->lastdv = d - nredundcol;
  if (Q->debug)
    {
      fprintf (lrs_ofp, "\nend of first phase of getabasis: ");
      fprintf (lrs_ofp, "lastdv=%ld nredundcol=%ld", Q->lastdv, Q->nredundcol);
      fprintf (lrs_ofp, "\nredundant cobases:");
      for (i = 0; i < nredundcol; i++)
	fprintf (lrs_ofp, " %ld", redundcol[i]);
      printA (P, Q);
    }

/* Remove linearities from cobasis for rest of computation */
/* This is done in order so indexing is not screwed up */

  for (i = 0; i < nlinearity; i++)
    {				/* find cobasic index */
      k = 0;
      while (k < d && C[k] != linearity[i] + d)
	k++;
      if (k >= d)
	{
	  lrs_warning(Q,"warning","\nError removing linearity");
	  return FALSE;
	}
      if (!removecobasicindex (P, Q, k))
	return FALSE;
      d = P->d;
    }
  if (Q->debug && nlinearity > 0)
    printA (P, Q);

/* Check feasability */
  if (Q->givenstart)
    {
      i = Q->lastdv + 1;
      while (i <= m && !negative (A[Row[i]][0]))
	i++;
      if (i <= m)
	fprintf (lrs_ofp, "\n*Infeasible startingcobasis - will be modified");
    }
  return TRUE;
}				/*  end of getabasis */

long 
removecobasicindex (lrs_dic * P, lrs_dat * Q, long k)
/* remove the variable C[k] from the problem */
/* used after detecting column dependency    */
{
  long i, j, cindex, deloc;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *B = P->B;
  long *C = P->C;
  long *Col = P->Col;
  long m, d;
  m = P->m;
  d = P->d;

  if (Q->debug)
    fprintf (lrs_ofp, "\nremoving cobasic index k=%ld C[k]=%ld Col[k]=%ld d=%ld m=%ld", k, C[k],Col[k],d,m);
  cindex = C[k];		/* cobasic index to remove              */
  deloc = Col[k];		/* matrix column location to remove     */

  for (i = 1; i <= m; i++)	/* reduce basic indices by 1 after index */
    if (B[i] > cindex)
      B[i]--;
  
  for (j = k; j < d; j++)	/* move down other cobasic variables    */
    {
      C[j] = C[j + 1] - 1;	/* cobasic index reduced by 1           */
      Col[j] = Col[j + 1];
    }

  if (deloc != d)               
    {
  /* copy col d to deloc */
      for (i = 0; i <= m-Q->nonnegative*Q->inputd; i++) /* nonnegative rows do not exist */
        copy (A[i][deloc], A[i][d]);

  /* reassign location for moved column */
      j = 0;
      while (Col[j] != d)
        j++;
      
    Col[j] = deloc;
  }

  P->d--;
  if (Q->debug)
    printA (P, Q);
  return TRUE;
}				/* end of removecobasicindex */

lrs_dic *
resize (lrs_dic * P, lrs_dat * Q)
	/* resize the dictionary after some columns are deleted, ie. inputd>d */
	/* a new lrs_dic record is created with reduced size, and items copied over */
{
  lrs_dic *P1;			/* to hold new dictionary in case of resizing */

  long i, j;
  long m, d, m_A;


  m = P->m;
  d = P->d;
  m_A = P->m_A;

/* get new dictionary record */

  P1 = new_lrs_dic (m, d, m_A);

/* copy data from P to P1    */
  P1->i = P->i;
  P1->j = P->j;
  P1->depth = P->depth;
  P1->m = P->m;
  P1->d = P1->d_orig = d;
  P1->lexflag = P->lexflag;
  P1->m_A = P->m_A;
  copy (P1->det, P->det);
  copy (P1->objnum, P->objnum);
  copy (P1->objden, P->objden);

  for (i = 0; i <= m; i++)
    {
      P1->B[i] = P->B[i];
      P1->Row[i] = P->Row[i];
    }
  for (i = 0; i <= m_A; i++)
    {
      for (j = 0; j <= d; j++)
	copy (P1->A[i][j], P->A[i][j]);
    }


  for (j = 0; j <= d; j++)
    {
      P1->Col[j] = P->Col[j];
      P1->C[j] = P->C[j];
    }

  if (Q->debug)
    {
      fprintf (lrs_ofp, "\nDictionary resized from d=%ld to d=%ld", Q->inputd, P->d);
      printA (P1, Q);
    }

  lrs_free_dic (P,Q);

/* Reassign cache pointers */

  Q->Qhead = P1;
  Q->Qtail = P1;
  P1->next = P1;
  P1->prev = P1;

  return P1;

}
/********* resize                    ***************/


long 
restartpivots (lrs_dic * P, lrs_dat * Q)
/* facet contains a list of the inequalities in the cobasis for the restart */
/* inequality contains the relabelled inequalities after initialization     */
{
  long i, j, k;
  long *Cobasic;		/* when restarting, Cobasic[j]=1 if j is in cobasis */
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *B = P->B;
  long *C = P->C;
  long *Row = P->Row;
  long *Col = P->Col;
  long *inequality = Q->inequality;
  long *facet = Q->facet;
  long nlinearity = Q->nlinearity;
  long m, d, lastdv;
  m = P->m;
  d = P->d;
  lastdv = Q->lastdv;

  Cobasic = (long *) CALLOC ((unsigned) m + d + 2, sizeof (long));

  if (Q->debug)
    fprintf(lrs_ofp,"\nCobasic flags in restartpivots");
  /* set Cobasic flags */
  for (i = 0; i < m + d + 1; i++)
    Cobasic[i] = 0;
  for (i = 0; i < d; i++)	/* find index corresponding to facet[i] */
    {
      j = 1;
      while (facet[i +nlinearity] != inequality[j])
	j++;
      Cobasic[j + lastdv] = 1;
      if (Q->debug)
        fprintf(lrs_ofp," %ld %ld;",facet[i+nlinearity],j+lastdv);
    }

  /* Note that the order of doing the pivots is important, as */
  /* the B and C vectors are reordered after each pivot       */

/* Suggested new code from db starts */
  i=m;
  while (i>d){
    while(Cobasic[B[i]]){
      k = d - 1;
      while ((k >= 0) && (zero (A[Row[i]][Col[k]]) || Cobasic[C[k]])){
       k--;
      }
      if (k >= 0)  {
           /*db asks: should i really be modified here? (see old code) */
           /*da replies: modifying i only makes is larger, and so      */
           /*the second while loop will put it back where it was       */
           /*faster (and safer) as done below                          */
       long  ii=i;
       pivot (P, Q, ii, k);
       update (P, Q, &ii, &k);
       if(overflow_detected)
         {
           if(Q->verbose && !Q->mplrs)
             lrs_warning(Q,"warning","*overflow restartpivots");
           return FALSE;
         }

      } else {
       lrs_warning(Q,"warning","\nInvalid Co-basis - does not have correct rank");
       free(Cobasic);
       return FALSE;
      }
    }
    i--;
  }
/* Suggested new code from db ends */

/* check restarting from a primal feasible dictionary               */
  for (i = lastdv + 1; i <= m; i++)
    if (negative (A[Row[i]][0]))
      {
	lrs_warning(Q,"warning","\nTrying to restart from infeasible dictionary");
        free(Cobasic);
	return FALSE;
      }
  free(Cobasic);
  return TRUE;

}				/* end of restartpivots */


long 
lrs_ratio (lrs_dic * P, lrs_dat * Q, long col)	/*find lex min. ratio */
		  /* find min index ratio -aig/ais, ais<0 */
		  /* if multiple, checks successive basis columns */
		  /* recoded Dec 1997                     */
{
  long i, j, comp, ratiocol, basicindex, start, nstart, cindex, bindex;
  long firstime;		/*For ratio test, true on first pass,else false */
  lrs_mp Nmin, Dmin;
  long degencount, ndegencount;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *B = P->B;
  long *Row = P->Row;
  long *Col = P->Col;
  long *minratio = Q->minratio;
  long m, d, lastdv;

  m = P->m;
  d = P->d;
  lastdv = Q->lastdv;


  nstart=0;
  ndegencount=0;
  degencount = 0;
  minratio[P->m]=1;   /*2011.7.14 non-degenerate pivot flag */

  for (j = lastdv + 1; j <= m; j++)
    {
      /* search rows with negative coefficient in dictionary */
      /*  minratio contains indices of min ratio cols        */
      if (negative (A[Row[j]][col]))
       {
	minratio[degencount++] = j;
        if(zero (A[Row[j]][0]))
          minratio[P->m]=0;   /*2011.7.14 degenerate pivot flag */
       }
    }				/* end of for loop */
  if (Q->debug)
    {
      fprintf (lrs_ofp, "  Min ratios: ");
      for (i = 0; i < degencount; i++)
	fprintf (lrs_ofp, " %ld ", B[minratio[i]]);
    }
  if (degencount == 0)
    return (degencount);	/* non-negative pivot column */

  lrs_alloc_mp(Nmin); lrs_alloc_mp(Dmin);
  ratiocol = 0;			/* column being checked, initially rhs */
  start = 0;			/* starting location in minratio array */
  bindex = d + 1;		/* index of next basic variable to consider */
  cindex = 0;			/* index of next cobasic variable to consider */
  basicindex = d;		/* index of basis inverse for current ratio test, except d=rhs test */
  while (degencount > 1)	/*keep going until unique min ratio found */
    {
      if (B[bindex] == basicindex)	/* identity col in basis inverse */
	{
	  if (minratio[start] == bindex)
	    /* remove this index, all others stay */
	    {
	      start++;
	      degencount--;
	    }
	  bindex++;
	}
      else
	/* perform ratio test on rhs or column of basis inverse */
	{
	  firstime = TRUE;
	  /*get next ratio column and increment cindex */
	  if (basicindex != d)
	    ratiocol = Col[cindex++];
	  for (j = start; j < start + degencount; j++)
	    {
	      i = Row[minratio[j]];	/* i is the row location of the next basic variable */
	      comp = 1;		/* 1:  lhs>rhs;  0:lhs=rhs; -1: lhs<rhs */
	      if (firstime)
		firstime = FALSE;	/*force new min ratio on first time */
	      else
		{
		  if (positive (Nmin) || negative (A[i][ratiocol]))
		    {
		      if (negative (Nmin) || positive (A[i][ratiocol]))
			comp = comprod (Nmin, A[i][col], A[i][ratiocol], Dmin);
		      else
			comp = -1;
		    }

		  else if (zero (Nmin) && zero (A[i][ratiocol]))
		    comp = 0;

		  if (ratiocol == ZERO)
		    comp = -comp;	/* all signs reversed for rhs */
		}
	      if (comp == 1)
		{		/*new minimum ratio */
		  nstart = j;
		  copy (Nmin, A[i][ratiocol]);
		  copy (Dmin, A[i][col]);
		  ndegencount = 1;
		}
	      else if (comp == 0)	/* repeated minimum */
		minratio[nstart + ndegencount++] = minratio[j];

	    }			/* end of  for (j=start.... */
	  degencount = ndegencount;
	  start = nstart;
	}			/* end of else perform ratio test statement */
      basicindex++;		/* increment column of basis inverse to check next */
      if (Q->debug)
	{
	  fprintf (lrs_ofp, " ratiocol=%ld degencount=%ld ", ratiocol, degencount);
	  fprintf (lrs_ofp, "  Min ratios: ");
	  for (i = start; i < start + degencount; i++)
	    fprintf (lrs_ofp, " %ld ", B[minratio[i]]);
	}
    }				/*end of while loop */
  lrs_clear_mp(Nmin); lrs_clear_mp(Dmin);
  return (minratio[start]);
}				/* end of ratio */



long 
lexmin (lrs_dic * P, lrs_dat * Q, long col)
  /*test if basis is lex-min for vertex or ray, if so TRUE */
  /* FALSE if a_r,g=0, a_rs !=0, r > s          */
{
/*do lexmin test for vertex if col=0, otherwise for ray */
  long r, s, i, j;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *B = P->B;
  long *C = P->C;
  long *Row = P->Row;
  long *Col = P->Col;
  long m = P->m;
  long d = P->d;

  for (i = Q->lastdv + 1; i <= m; i++)
    {
      r = Row[i];
      if (zero (A[r][col]))	/* necessary for lexmin to fail */
	for (j = 0; j < d; j++)
	  {
	    s = Col[j];
	    if (B[i] > C[j])	/* possible pivot to reduce basis */
	      {
		if (zero (A[r][0]))	/* no need for ratio test, any pivot feasible */
		  {
		    if (!zero (A[r][s]))
		      return (FALSE);
		  }
		else if (negative (A[r][s]) && ismin (P, Q, r, s))
		  {
		    return (FALSE);
		  }
	      }			/* end of if B[i] ... */
	  }
    }
  if ((col != ZERO) && Q->debug)
    {
      fprintf (lrs_ofp, "\n lexmin ray in col=%ld ", col);
      printA (P, Q);
    }
  return (TRUE);
}				/* end of lexmin */

long 
ismin (lrs_dic * P, lrs_dat * Q, long r, long s)
/*test if A[r][s] is a min ratio for col s */
{
  long i;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long m_A = P->m_A;

  for (i = 1; i <= m_A; i++)
    if ((i != r) &&
	negative (A[i][s]) && comprod (A[i][0], A[r][s], A[i][s], A[r][0]))
      {
	return (FALSE);
      }

  return (TRUE);
}

void 
update (lrs_dic * P, lrs_dat * Q, long *i, long *j)
 /*update the B,C arrays after a pivot */
 /*   involving B[bas] and C[cob]           */
{

  long leave, enter;
/* assign local variables to structures */
  long *B = P->B;
  long *C = P->C;
  long *Row = P->Row;
  long *Col = P->Col;
  long m = P->m;
  long d = P->d;

  leave = B[*i];
  enter = C[*j];
  B[*i] = enter;
  reorder1 (B, Row, *i, m + 1);
  C[*j] = leave;
  reorder1 (C, Col, *j, d);
/* restore i and j to new positions in basis */
  for (*i = 1; B[*i] != enter; (*i)++);		/*Find basis index */
  for (*j = 0; C[*j] != leave; (*j)++);		/*Find co-basis index */
  if (Q->debug)
    printA(P,Q);
}				/* end of update */

long 
lrs_degenerate (lrs_dic * P, lrs_dat * Q)
/* TRUE if the current dictionary is primal degenerate */
/* not thoroughly tested   2000/02/15                  */
{
  long i;
  long *Row;

  lrs_mp_matrix A = P->A;
  long d = P->d;
  long m = P->m;

  Row = P->Row;

  for (i = d + 1; i <= m; i++)
    if (zero (A[Row[i]][0]))
      return TRUE;

  return FALSE;
}


/*********************************************************/
/*                 Miscellaneous                         */
/******************************************************* */

void 
reorder (long a[], long range)
/*reorder array in increasing order with one misplaced element */
{
  long i, temp;
  for (i = 0; i < range - 1; i++)
    if (a[i] > a[i + 1])
      {
	temp = a[i];
	a[i] = a[i + 1];
	a[i + 1] = temp;
      }
  for (i = range - 2; i >= 0; i--)
    if (a[i] > a[i + 1])
      {
	temp = a[i];
	a[i] = a[i + 1];
	a[i + 1] = temp;
      }

}				/* end of reorder */

void 
reorder1 (long a[], long b[], long newone, long range)
/*reorder array a in increasing order with one misplaced element at index newone */
/*elements of array b are updated to stay aligned with a */
{
  long temp;
  while (newone > 0 && a[newone] < a[newone - 1])
    {
      temp = a[newone];
      a[newone] = a[newone - 1];
      a[newone - 1] = temp;
      temp = b[newone];
      b[newone] = b[newone - 1];
      b[--newone] = temp;
    }
  while (newone < range - 1 && a[newone] > a[newone + 1])
    {
      temp = a[newone];
      a[newone] = a[newone + 1];
      a[newone + 1] = temp;
      temp = b[newone];
      b[newone] = b[newone + 1];
      b[++newone] = temp;
    }
}				/* end of reorder1 */


void 
rescaledet (lrs_dic * P, lrs_dat * Q, lrs_mp Vnum, lrs_mp Vden)
  /* rescale determinant to get its volume */
  /* Vnum/Vden is volume of current basis  */
{
  lrs_mp gcdprod;		/* to hold scale factors */
  long i;
/* assign local variables to structures */
  long *C = P->C;
  long *B = P->B;
  long m, d, lastdv;

  lrs_alloc_mp(gcdprod);
  m = P->m;
  d = P->d;
  lastdv = Q->lastdv;

  itomp (ONE, gcdprod);
  itomp (ONE, Vden);

  for (i = 0; i < d; i++)
    if (B[i] <= m)
      {
	mulint (Q->Gcd[Q->inequality[C[i] - lastdv]], gcdprod, gcdprod);
	mulint (Q->Lcm[Q->inequality[C[i] - lastdv]], Vden, Vden);
      }
  mulint (P->det, gcdprod, Vnum);
//  reduce (Vnum, Vden);
  lrs_clear_mp(gcdprod);
}				/* end rescaledet */

void 
rescalevolume (lrs_dic * P, lrs_dat * Q, lrs_mp Vnum, lrs_mp Vden)
/* adjust volume for dimension */
{
  lrs_mp temp, dfactorial;
/* assign local variables to structures */
  long lastdv = Q->lastdv;

  lrs_alloc_mp(temp); lrs_alloc_mp(dfactorial);

  /*reduce Vnum by d factorial  */
  getfactorial (dfactorial, lastdv);
  mulint (dfactorial, Vden, Vden);
  if (Q->hull && !Q->homogeneous)
    {				/* For hull option multiply by d to correct for lifting */
      itomp (lastdv, temp);
      mulint (temp, Vnum, Vnum);
    }

  reduce (Vnum, Vden);
  lrs_clear_mp(temp); lrs_clear_mp(dfactorial);
}


void 
updatevolume (lrs_dic * P, lrs_dat * Q)		/* rescale determinant and update the volume */
{
  lrs_mp tN, tD, Vnum, Vden;
  lrs_alloc_mp(tN); lrs_alloc_mp(tD); lrs_alloc_mp(Vnum); lrs_alloc_mp(Vden);
  rescaledet (P, Q, Vnum, Vden);
  copy (tN, Q->Nvolume);
  copy (tD, Q->Dvolume);
  linrat (tN, tD, ONE, Vnum, Vden, ONE, Q->Nvolume, Q->Dvolume);
  if (Q->debug)
    {
      prat ("\n*Volume=", Q->Nvolume, Q->Dvolume);
      pmp (" Vnum=", Vnum);
      pmp (" Vden=", Vden);
    }
  lrs_clear_mp(tN); lrs_clear_mp(tD); lrs_clear_mp(Vnum); lrs_clear_mp(Vden);

}				/* end of updatevolume */



/***************************************************/
/* Routines for redundancy checking                */
/***************************************************/

long 
checkredund (lrs_dic * P, lrs_dat * Q)
/* Solve primal feasible lp by least subscript and lex min basis method */
/* to check redundancy of a row in objective function                   */
/* return 0=nonredundant -1=strict redundant(interior) 1=non-strict redundant 2=unbounded possible linearity */
{
  lrs_mp Ns, Nt;
  long i, j;
  long r, s;

/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *Row, *Col;
  long d = P->d;

  lrs_alloc_mp(Ns); lrs_alloc_mp(Nt);
  itomp (ONE, Ns);   /* unnecessary but avoids compile warning */
  itomp (ONE, Nt);
  Row = P->Row;
  Col = P->Col;
  while (selectpivot (P, Q, &i, &j))
    {
      Q->count[2]++;

/* sign of new value of A[0][0]            */
/* is      A[0][s]*A[r][0]-A[0][0]*A[r][s] */

      r = Row[i];
      s = Col[j];

      mulint (A[0][s], A[r][0], Ns);
      mulint (A[0][0], A[r][s], Nt);

      if (mp_greater (Ns, Nt))
        {
          lrs_clear_mp(Ns); lrs_clear_mp(Nt);
          if(Q->debug && !Q->mplrs)
            fprintf(lrs_ofp,"\n*mp_greater: nonredundant");
	  return 0;		
        }

      pivot (P, Q, i, j);
      update (P, Q, &i, &j);	/*Update B,C,i,j */
      if(overflow_detected)
        {
         if(Q->verbose && !Q->mplrs)
             lrs_warning(Q,"warning","*overflow checkredund");
         return FALSE;
        }


    }
  lrs_clear_mp(Ns); lrs_clear_mp(Nt);

  if(positive(P->A[0][0]))
   {
      if(Q->debug)
         fprintf(lrs_ofp,"\n*positive objective: nonredundant");
      return 0;
   }


  if(j < d && i == 0)    /* unbounded is also non-redundant */
   {
    if(Q->debug)
      fprintf(lrs_ofp,"\n*unbounded-non redundant");
    return 2;
   }


/* 2020.6.8 check for strict redundancy and return -1 if so */
  if(Q->debug )
     pmp("\n*obj =",P->A[0][0]);
  if (negative(P->A[0][0]))
   return -1;
  else
   return 1;

}				/* end of checkredund  */

long 
checkcobasic (lrs_dic * P, lrs_dat * Q, long index)
/* TRUE if index is cobasic and nondegenerate                        */
/* FALSE if basic, or degen. cobasic, where it will get pivoted out  */

{

/* assign local variables to structures */

  lrs_mp_matrix A = P->A;
  long *B, *C, *Row, *Col;
  long d = P->d;
  long m = P->m;
  long debug = Q->debug;
  long i = 0;
  long j = 0;
  long s;
  long start=Q->lastdv+1;

  if(index < 0)
   {
    start = 1;   /*replace cobasic by basic variable */
    index=-index;
   }

  B = P->B;
  C = P->C;
  Row = P->Row;
  Col = P->Col;


  while ((j < d) && C[j] != index)
    j++;

  if (j == d)
    return FALSE;		/* not cobasic index */

/* index is cobasic */

  if (debug)
    fprintf (lrs_ofp, "\nindex=%ld cobasic", index);

  s = Col[j];
  i = start;

  while ((i <= m) &&
	 (zero (A[Row[i]][s]) || !zero (A[Row[i]][0])))
    i++;

  if (i > m)
    {
      if (debug)
	fprintf (lrs_ofp, " is non-degenerate");
      return TRUE;
    }
  if (debug)
    fprintf (lrs_ofp, " is degenerate B[i]=%ld C[j]=%ld", B[i],C[j]);
  pivot (P, Q, i, j);
  update (P, Q, &i, &j);	/*Update B,C,i,j */
  return FALSE;			/*index is no longer cobasic */

}				/* end of checkcobasic */

long 
checkindex (lrs_dic * P, lrs_dat * Q, long index, long phase)
/* phase=0 find hidden linearities  phase=1 redundant inequalities only */

/* 0 if index is non-redundant inequality    */
/*-1 if index is strict redundant inequality */
/* 1 if index is non-strict redundant ine    */
/* 2 if index is input linearity             */
/*NOTE: row is returned all zero if redundant!! */

{
  long i, j,k, res1=0, res2=0;
  lrs_mp_matrix A = P->A;
  long *Row = P->Row;
  long *B = P->B;
  long d = P->d;
  long m = P->m;
  long zeroonly=0;
  long allzero=1;

  if(index < 0)  /* used to zero out known redundant rows in mplrs */
    {
     zeroonly=1;
     index=-index;
    }

  if (Q->debug)
   {
    printA (P, Q);
    prawA(P,Q);
   }

/* each slack index must be checked for degeneracy */
/* if in cobasis, it is pivoted out if degenerate */

  if (checkcobasic (P, Q, index))
   {
    if(Q->debug)
         fprintf(lrs_ofp,"\n*checkcobasic res1=%ld",res1);
    return ZERO;
   }
/* index is basic   */
  j = 1;
  while ((j <= m) && (B[j] != index))
    j++;
  i = Row[j];
  /* copy row i to cost row, and set it to zero */

  for (k = 0; k <= d; k++)
    {
      
      if(!zero(A[i][k]))
        allzero=0;
      copy (A[0][k], A[i][k]);
      changesign (A[0][k]);
      itomp (ZERO, A[i][k]);
    }

  if(zeroonly)
    return 1;

/* zero row is strongly redundant */
  if(allzero)
   {
   if(Q->debug)
    {
      prawA(P,Q);
      printA(P,Q);
    }
    if(Q->debug)
      fprintf(lrs_ofp,"\n*zero row index=%ld j=%ld Row[j]=%ld",index,j,Row[j]);
    return -1;
   }

/* test for redundant inequalities */

  res1=checkredund (P, Q); /* res=0 non-red =-1 strong red =1 weak red =2 LP unbounded */

  if (!Q->mplrs )
   if(res1 ==1 || res1 == -1 )
         return res1;            /* redundant */
/* 2022.4.19 res1=2 is unbounded, possible linearity, not used at present */
   if(res1==2)
       res1=0;
   for (j = 0; j <= d; j++)
         changesign (A[0][j]);
   if(Q->debug)
      fprintf(lrs_ofp,"\n*phase=%ld",phase);
/* 2023.3.20   need to verify that this is skipped on mplrs second round */

   if( phase==0 )        /* find hidden linearities */
     {
        res2=checkredund (P, Q);
     
        if ( res2==1 || res2 == -1 )    /*  indicates linearity */
           if ( zero (P->A[0][0]))
                  res1=2 ;
      }             /* testlin */ 
     
   if(res1 == 0 || res1 ==2)
     for (j = 0; j <= d; j++)   /* restore tested row that was zeroed */
      copy (A[i][j], A[0][j]);
     
   return res1;

}				/* end of checkindex */


/***************************************************************/
/*                                                             */
/* f I/O routines                          */
/*                                                             */
/***************************************************************/

void
lprat (const char *name, long Nt, long Dt)
/*print the long precision rational Nt/Dt without reducing  */
{
  if ( Nt > 0 ) 
    fprintf (lrs_ofp, " ");
  fprintf (lrs_ofp, "%s%ld", name, Nt);
  if (Dt != 1)   
    fprintf (lrs_ofp, "/%ld", Dt);
  fprintf (lrs_ofp, " ");
}                               /* lprat */

long
lreadrat (long *Num, long *Den)
 /* read a rational string and convert to long    */
 /* returns true if denominator is not one        */
{
  char in[MAXINPUT], num[MAXINPUT], den[MAXINPUT];
  if(fscanf (lrs_ifp, "%s", in) == EOF)
     return(FALSE);
  atoaa (in, num, den);         /*convert rational to num/dem strings */
  *Num = atol (num);
  if (den[0] == '\0')
    {
      *Den = 1L;
      return (FALSE);
    }
  *Den = atol (den);
  return (TRUE);
}

void
lrs_getinput(lrs_dic *P,lrs_dat *Q,long *num,long *den, long m, long d)
/* code for reading data matrix in lrs/cdd format */
{
  long j,row;

  printf("\nEnter each row: b_i  a_ij j=1..%ld",d);
  for (row=1;row<=m;row++)
    {
      printf("\nEnter row %ld: ",row );
      for(j=0;j<=d;j++)
       {
         lreadrat(&num[j],&den[j]);
         lprat(" ",num[j],den[j]);
       }

       lrs_set_row(P,Q,row,num,den,GE);
     }

 printf("\nEnter objective row c_j j=1..%ld: ",d);
 num[0]=0; den[0]=1;
 for(j=1;j<=d;j++)
       {
         lreadrat(&num[j],&den[j]);
         lprat(" ",num[j],den[j]);
       }

 lrs_set_obj(P,Q,num,den,MAXIMIZE);
}


long 
readlinearity (lrs_dat * Q)	/* read in and check linearity list */
{
  long i, j;
  long nlinearity;
  if(fscanf (lrs_ifp, "%ld", &nlinearity)==EOF )
    {
      lrs_warning(Q,"warning","\nLinearity option invalid, no indices ");
      return (FALSE);
    } 
  if (nlinearity < 1)
    {
      lrs_warning(Q,"warning","\nLinearity option invalid, indices must be positive");
      return (FALSE);
    } 

  Q->linearity  = (long int*) CALLOC ((nlinearity + 1), sizeof (long));

  for (i = 0; i < nlinearity; i++)
    {
      if(fscanf (lrs_ifp, "%ld", &j)==EOF)
      {
      lrs_warning(Q,"warning","\nLinearity option invalid, missing indices");
      return (FALSE);
      } 
      Q->linearity[i] = j;	

    }
  for (i = 1; i < nlinearity; i++)	/*sort in order */
    reorder (Q->linearity, nlinearity);

  Q->nlinearity = nlinearity;
  Q->polytope = FALSE;
  return TRUE;
}				/* end readlinearity */

long
readredund (lrs_dat * Q)     /* read in and check linearity list */
{
  long i,j,k;
  char *mess;
  int len=0;

  if(fscanf (lrs_ifp, "%ld", &k)==EOF )
    {
      lrs_warning(Q,"warning","\nredund_list option invalid: no indices ");
      return (FALSE);
    }
  if ( k < 0 )
    {
      lrs_warning(Q,"warning","\nredund_list option invalid, first index must be >= 0");
      return (FALSE);
    }
  if ( k < Q->m)
           Q->fullredund=FALSE;

  for (i = 1; i <= Q->m; i++)   /*reset any previous redund option except =2 values */
      if (Q->redineq[i] != 2)
          Q->redineq[i]=0;
  Q->redineq[0]=1;
 
  for (i = 0; i < k; i++)
    {
      if(fscanf (lrs_ifp, "%ld", &j)==EOF)
      {
      lrs_warning(Q,"warning","\nredund_list option invalid: missing indices");
      fflush(lrs_ofp);
      return (FALSE);
      }

      if( j< 0 || j > Q->m)
      {
      fprintf (lrs_ofp,"\nredund_list option invalid: indices not between 1 and %ld",Q->m);
      return (FALSE);
      }
      Q->redineq[j] = 1;

    }

  if( Q->messages )
    if (!(Q->mplrs && Q->redund) )
     {
      mess=(char *)malloc(20*Q->m*sizeof(char));
      len=sprintf(mess,"*redund_list %ld ",k);
      for (i=1;i<=Q->m;i++)
         if(Q->redineq[i] == 1)
           len=len+sprintf(mess+len," %ld",i);
      lrs_warning(Q,"warning",mess);
      free(mess);
     }
  return TRUE;
}                               /* end readredund */



long 
readfacets (lrs_dat * Q, long facet[])
/* read and check facet list for obvious errors during start/restart */
/* this must be done after linearity option is processed!!           */
{
  long i, j;
  char str[1000000],*p,*e;

/* assign local variables to structures */
  long m, d;
  long *linearity = Q->linearity;
  m = Q->m;
  d = Q->inputd;

/* modified 2018.6.7 to fix bug restarting with less than full dimension input */
/* number of restart indices is not known at this point                        */

  j=Q->nlinearity;          /* note we place these after the linearity indices */

  if (fgets(str,1000000,lrs_ifp)==NULL)
        return FALSE;  /* pick up indices from the input line */
  for (p = str; ; p = e) {
        facet[j] = strtol(p, &e, 10);
        if (p == e)
            break;

        if(!Q->mplrs)
            fprintf(lrs_ofp," %ld",facet[j] );


/* 2010.4.26 nonnegative option needs larger range of indices */
          if(Q->nonnegative)
              if (facet[j] < 1 || facet[j] > m+d)
	      {
	        fprintf (lrs_ofp,"\n Start/Restart cobasic indices must be in range 1 .. %ld ", m+d);
  	        return FALSE;
	      }

          if(!Q->nonnegative)
             if (facet[j] < 1 || facet[j] > m)
	      {
	      fprintf (lrs_ofp, "\n Start/Restart cobasic indices must be in range 1 .. %ld ", m);
      	      return FALSE;
	      }

          for (i = 0; i < Q->nlinearity; i++)
	       if     (linearity[i] == facet[j])
	          {
	            fprintf (lrs_ofp, "\n Start/Restart cobasic indices should not include linearities");
	            return FALSE;
	          }
/*     bug fix 2011.8.1  reported by Steven Wu*/
          for (i = Q->nlinearity; i < j; i++)
/*     end bug fix 2011.8.1 */

	       if     (facet[i] == facet[j])
	        {
	          fprintf (lrs_ofp, "\n  Start/Restart cobasic indices must be distinct");
      	          return FALSE;
	        }
           j++;
   }
  return TRUE;
}				/* end of readfacets */

long
readvars (lrs_dat * Q,char *name)    
{
/* read in and check ordered list of vars for extract/project        */
/* extract mode: *vars is an ordered list of variables to be kept    */
/* fel mode:     *vars is an ordered list of variables to be removed */

  long i, j, len,nremove;
  long nvars=0;
  long k=0;

  long *vars;
  long *var;    /* binary representation of vars */

  long n=Q->n;
  Q->vars  =   (long int*) CALLOC ((n + 3), sizeof (long));
  var  =       (long int*) CALLOC ((n + 3), sizeof (long));

  vars=Q->vars;
  for (i=0;i<=n+2;i++)
    {
     vars[i]=0;
     var[i]=0;
    }

  if((fscanf (lrs_ifp, "%ld", &nvars)==EOF ) || nvars < 1)
    {
        fprintf (lrs_ofp, "\n*%s: incorrect or missing indices\n",name);
        free(var);
        return FALSE;
    }

  if( nvars > n-1)
    {
      nvars = n-1;
      fprintf (lrs_ofp, "\n*%s: too many indices, first %ld taken",name,n-1);
    }

  for (i = 0; i < nvars; i++)
    {
      
      if(fscanf (lrs_ifp, "%ld", &j)==EOF)
      {
        fprintf (lrs_ofp, "\n*%s: missing indices\n",name);
        free(var);
        return FALSE;
      }
       
      if(j>0 && j<n)
        {
          if(var[j]==1)
             fprintf (lrs_ofp, "\n*%s: duplicate index %ld skipped",name,j);
          else
           {
             vars[k++] = j;
             var[j]=1; 
           }
        }
      else
        {
          fprintf (lrs_ofp, "\n*%s: index %ld out of range 1 to %ld\n",name,j,n-1);
          free(var);
          return FALSE;
        }
    }

   i=0;
   while (i < n && vars[i] != 0 )
     i++;
   nvars=i;

   vars[n+1]=nvars;

   if( Q->messages ) /* need the leading * for mfel */
    if (!(Q->mplrs && Q->fel ) )
     {
      len=sprintf(Q->projmess,"*%s %ld  ",name,nvars);
      for(i=0;i<nvars;i++)
        len=len+sprintf(Q->projmess+len,"%ld ", vars[i]);
      lrs_warning(Q,"warning",Q->projmess);
     }
/* now we build a new string for output after the end with no *   */
   len=sprintf(Q->projmess,"%s %ld  ",name,nvars);
   for(i=0;i<nvars;i++)
        len=len+sprintf(Q->projmess+len,"%ld ", vars[i]);
   if(strcmp (name, "project") == 0) /* convert to project vars to remove vars */
    {
      for(i=0;i<nvars;i++)     
        vars[i]=0;
      nremove=0;
      for(i=1;i<n;i++)     
        if(!var[i])
          vars[nremove++]=i;
     vars[n+1]=nremove;         
     vars[n]=1;      /* used to control column selection rule =1 min cols in new matrix */
    }   /* convert project vars */

   free(var);

   if(Q->fel)      
     return TRUE;

/* if nlinearity>0 fill up list with remaining decision variables */

   if(!Q->hull && Q->nlinearity > 0)
    for (i=1;i<n;i++)
     {
       j=0;
       while (j<nvars && vars[j] != i)
         j++;
       if (j == nvars)
          vars[nvars++]=i;
     }
  return TRUE;
}                  /* readvars */        


long extractcols (lrs_dic * P, lrs_dat * Q)
{
/* 2020.6.17*/
/* extract option just pulls out the columns - extract mode */
/* project option also removes redundant rows -  fel mode   */

  long i,j,m,n;
  long ncols;
  long rows;
  lrs_mp_matrix A;
  long  *Col, *Row, *remain, *output, *redineq;
  
  lrs_dic *P1;
  Col = P->Col;
  Row = P->Row;
  remain=Q->vars;
  output=Q->temparray;
  m=P->m;
  n=Q->n;
  if(Q->fel)
    ncols=n-remain[n+1]-1;   
  else
    ncols=remain[n+1];

  for(j=0;j<n;j++)
      output[j]=0;

  for(j=0;j<n;j++)
      output[remain[j]]=1;

/* complement for fel mode - don't ask! */
  if(Q->fel)    
    for(j=1;j<n;j++)
      output[j]=1-output[j];

  if(Q->verbose)
    {
       fprintf(lrs_ofp,"\n*output");
       for (j=0;j<n;j++)
           fprintf(lrs_ofp," %ld",output[j]);
       fprintf(lrs_ofp,"\n*columns retained:");
       for (j=0;j<n;j++)
         if(output[j])
           fprintf(lrs_ofp," %ld",j);
    }

  if(Q->fel)        /* fel mode remove redundancy */
   {
    for(i=1;i<=m;i++ )  /* zero out removed cols */
      for(j=0;j<n;j++)
        if(!output[j])
           itomp(ZERO,P->A[Row[i]][Col[j]]);

    P1=lrs_getdic (Q);
    Q->Qhead=P;
    Q->Qtail=P;

    copy_dict (Q,P1,P);
    Q->Qhead=P1;
    Q->Qtail=P1; 
    Q->olddic=P;        /*in case of overflow */
    A = P1->A;

    redund_run(P1,Q);
    if(overflow_detected)
      {
       if(Q->debug)
        {
         fprintf(lrs_ofp,"\n*overflow in fel");
         return 1;
        }
      }
    redineq=Q->redineq;
    rows=0;
    for(i=1;i<=P->m_A; i++)
        if(redineq[i]==0 || redineq[i]==2 )
           rows++;

    Q->Qhead=P;
    Q->Qtail=P;

  }              /* end   if(Q->fel)...             */
 else            /* initialization for extract mode */
  {
    redineq=Q->redineq;
    rows=m;
    for(i=1;i<=m;i++ )
      redineq[i] = 0;
   }

  A=P->A;
  m=Q->m;

  if(Q->hull)
     fprintf(lrs_ofp,"\nV-representation");
  else
     fprintf(lrs_ofp,"\nH-representation");

  if (Q->nlinearity > 0)
    {
      fprintf(lrs_ofp, "\nlinearity %ld", Q->nlinearity);
         for(i=0; i<Q->nlinearity;i++)
           fprintf(lrs_ofp, " %ld", Q->linearity[i]);
    }

  fprintf(lrs_ofp,"\nbegin\n%ld %ld rational",rows,ncols+1);
  for(i=1;i<=m;i++ )
   {
    if(redineq[i] != 1 && redineq[i] != -1) /* 2023.10.30 added -1 here */
     {
      reducearray(A[Row[i]],n+Q->hull);   /*we already decremented n */
      fprintf(lrs_ofp,"\n");
      if(Q->hull)
       {
         for(j=0;j<n;j++)
            if(output[j])
              {
                if(zero(A[Row[i]][Col[0]]))
                  pmp("",A[Row[i]][Col[j]]);
                else
                  prat("",A[Row[i]][Col[j]],A[Row[i]][Col[0]]);
              }
       }
      else    /* no lifting */
       {
        pmp("",A[Row[i]][0]);
        for(j=1;j<n;j++)
           if(output[j])
                  pmp("",A[Row[i]][Col[j-1]]);
       }
     }
   }
  fprintf(lrs_ofp,"\nend");

  fprintf(lrs_ofp,"\n*columns retained:");
  for (j=0;j<n;j++)
    if(output[j])
      fprintf(lrs_ofp," %ld",j);

  fprintf(lrs_ofp,"\n");

  if(Q->debug)
    printA(P,Q);

  return 0;
}                     /* extractcols */

long linextractcols (lrs_dic * P, lrs_dat * Q)
/* 2020.2.2 */
/* extract option to output the reduced A matrix after linearities are removed */
/* should be followed by redund to get minimum representation                  */
{
  long  d,i,j,k,m,n;
  long  ii,jj;
  long  nlinearity=Q->nlinearity;
  lrs_mp_matrix A;
  long *B, *C, *Col, *Row, *remain;

  A = P->A;
  B = P->B;
  C = P->C;
  Col = P->Col;
  Row = P->Row;
  remain=Q->vars;

  m=P->m;
  n=Q->n;
  d=Q->inputd;

      fprintf(lrs_ofp,"\n*extract col order: ");

      for(j=0;j<n-1;j++)
         fprintf (lrs_ofp, "%ld ",remain[j]);

      for(k=0;k<n-1;k++)  /* go through input order for vars to remain */
      {
         i=1;
         while( i<= m)
          {
            if(B[i]== remain[k]) 
              {
               j=0;
               while(j+nlinearity<d && (C[j]<= d || zero(A[Row[i]][Col[j]])))
                  j++;
               if (j+nlinearity<d)
                  {
                    ii=i;
                    jj=j;
                    pivot(P,Q,ii,jj);
                    update(P,Q,&ii,&jj);
                    i=0;
                  }
              }              /* if B[i]...    */
            i++;
           }                 /* while i <= m  */
       }                     /* for k=0       */
   
      if(Q->hull)
           fprintf(lrs_ofp,"\n*columns retained:");
      else
        fprintf(lrs_ofp,"\n*columns retained: 0");
      for (j=0;j<d-nlinearity;j++)
         fprintf(lrs_ofp," %ld",C[j]-Q->hull);

      if(Q->hull)
         fprintf(lrs_ofp,"\nV-representation\nbegin");
      else         
         fprintf(lrs_ofp,"\nH-representation\nbegin");
      fprintf(lrs_ofp,"\n%ld %ld rational",m-nlinearity,d-nlinearity+1-Q->hull);

      for(i=nlinearity+1;i<=m;i++ )
         {
          reducearray(A[Row[i]],n-nlinearity);
          fprintf(lrs_ofp,"\n");
          if(!Q->hull)
            pmp("",A[Row[i]][0]);
          for(j=0;j<d-nlinearity;j++)
            pmp("",A[Row[i]][Col[j]]);
         }
      fprintf(lrs_ofp,"\nend");
      if(Q->hull)
        fprintf(lrs_ofp,"\n*columns retained:");
      else
        fprintf(lrs_ofp,"\n*columns retained: 0");
      for (j=0;j<d-nlinearity;j++)
         fprintf(lrs_ofp," %ld",C[j]-Q->hull);
      fprintf(lrs_ofp,"\n");

      if(Q->debug)
        printA(P,Q);

      return 0;
  }                     /* linextractcols  */


void 
printA (lrs_dic * P, lrs_dat * Q)	/* print the integer m by n array A 
					   with B,C,Row,Col vectors         */
{
  long i, j;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *B = P->B;
  long *C = P->C;
  long *Row = P->Row;
  long *Col = P->Col;
  long m, d;
  m = P->m;
  d = P->d;

  fprintf (lrs_ofp, "\n Basis    ");
  for (i = 0; i <= m; i++)
    fprintf (lrs_ofp, "%ld ", B[i]);
  fprintf (lrs_ofp, " Row ");
  for (i = 0; i <= m; i++)
    fprintf (lrs_ofp, "%ld ", Row[i]);
  fprintf (lrs_ofp, "\n Co-Basis ");
  for (i = 0; i <= d; i++)
    fprintf (lrs_ofp, "%ld ", C[i]);
  fprintf (lrs_ofp, " Column ");
  for (i = 0; i <= d; i++)
    fprintf (lrs_ofp, "%ld ", Col[i]);
  pmp (" det=", P->det);
  fprintf (lrs_ofp, "\n");
  i=0;
  while ( i<= m )
    {
      for (j = 0; j <= d; j++)
	pimat (P, i, j, A[Row[i]][Col[j]], "A");
      fprintf (lrs_ofp, "\n");
      if (i==0 && Q->nonnegative)  /* skip basic rows - don't exist! */
          i=d;
      i++;
  fflush (stdout);
    }
  fflush (stdout);
}

void 
prawA (lrs_dic * P, lrs_dat * Q)    /* raw matrix print */
{  
  long i, j;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long m, d;
  m = P->m;
  d = P->d;
  fprintf (lrs_ofp, "\n Raw A matrix");
  fprintf (lrs_ofp, "\n");
  i=0;
  while ( i<= m )
    {
      fprintf (lrs_ofp, "Row[%ld][0]=", i);
      pmp("",A[i][0]);
      for (j = 1; j <= d; j++)
        {
          fprintf (lrs_ofp, "[%ld]=", j);
          pmp("",A[i][j]);
         }
      fprintf (lrs_ofp, "\n");
      if (i==0 && Q->nonnegative)  /* skip basic rows - don't exist! */
          i=d;
      i++;
      fflush (stdout);
    }
}

void 
pimat (lrs_dic * P, long r, long s, lrs_mp Nt, const char *name)
 /*print the long precision integer in row r col s of matrix A */
{
  long *B = P->B;
  long *C = P->C;
  if (s == 0)
    fprintf (lrs_ofp, "%s[%ld][%ld]=", name, B[r], C[s]);
  else
    fprintf (lrs_ofp, "[%ld]=", C[s]);
  pmp ("", Nt);

}

/***************************************************************/
/*                                                             */
/*     Routines for caching, allocating etc.                   */
/*                                                             */
/***************************************************************/

/* From here mostly Bremner's handiwork */

static void
cache_dict (lrs_dic ** D_p, lrs_dat * global, long i, long j)
{
  if (dict_limit > 1)
    {
      /* save row, column indicies */
      (*D_p)->i = i;
      (*D_p)->j = j;

/* Make a new, blank spot at the end of the queue to copy into                     */ 


      pushQ (global, (*D_p)->m, (*D_p)->d, (*D_p)->m_A);

/*2019.6.7 This ought not to happen but it does */

  if( global->Qtail== *D_p)
    return;

      copy_dict (global, global->Qtail, *D_p);	/* Copy current dictionary */
    }
  *D_p = global->Qtail;
}

void 
copy_dict (lrs_dat * global, lrs_dic * dest, lrs_dic * src)
{
  long m = src->m;
  long m_A = src->m_A;        /* number of rows in A */
  long d = src->d;
  long r,s;

  if( dest == src)
    {
     if(global->mplrs)
        lrs_post_output("warning", "*copy_dict has dest=src -ignoring copy");
     else
        fprintf(stderr,"*copy_dict has dest=src -ignoring copy");
     return;
    }

#if defined(GMP) || defined(FLINT)

  for ( r=0;r<=m_A;r++)
    for( s=0;s<=d;s++)
       copy(dest->A[r][s],src->A[r][s]);

#else            /* fast copy for MP and LRSLONG arithmetic */
  /* Note that the "A" pointer trees need not be copied, since they
     always point to the same places within the corresponding space
*/
/* I wish I understood the above remark. For the time being, do it the easy way for Nash */
/* Looking at lrs_alloc_mp_matrix for MP and LRSLONG, A[0][0] is the 
 * start of araw, which holds the actual values and so the memcpy below 
 * copies the values.  The pointer trees (A, A[i], A[i][j]) already point
 * to the appropriate places: we don't want to change the pointers, only the
 * values.  lrs_alloc_mp_matrix is different for GMP.
 */
  if(global->nash)
  {
  for ( r=0;r<=m_A;r++)
    for( s=0;s<=d;s++)
       copy(dest->A[r][s],src->A[r][s]);
  }
  else
#ifdef B128
  memcpy (dest->A[0][0], (global->Qtail->prev)->A[0][0],
          (d + 1) * (lrs_digits + 1) * (m_A + 1) * sizeof (__int128));
#else
  memcpy (dest->A[0][0], (global->Qtail->prev)->A[0][0],
          (d + 1) * (lrs_digits + 1) * (m_A + 1) * sizeof (long long));
#endif

#endif

  dest->i = src->i;
  dest->j = src->j;
  dest->m = m;
  dest->d = d;
  dest->d_orig = src->d_orig;
  dest->m_A  = src->m_A;

  dest->depth = src->depth;
  dest->lexflag = src->lexflag;

  copy (dest->det, src->det);
  copy (dest->objnum, src->objnum);
  copy (dest->objden, src->objden);

  if (global->debug)
    fprintf (lrs_ofp, "\nSaving dict at depth %ld\n", src->depth);

  memcpy (dest->B, src->B, (m + 1) * sizeof (long));
  memcpy (dest->C, src->C, (d + 1) * sizeof (long));
  memcpy (dest->Row, src->Row, (m + 1) * sizeof (long));
  memcpy (dest->Col, src->Col, (d + 1) * sizeof (long));
}

/* 
 * pushQ(lrs_dat *globals,m,d):
 * this routine ensures that Qtail points to a record that 
 * may be copied into.
 *
 * It may create a new record, or it may just move the head pointer
 * forward so that know that the old record has been overwritten.
 */
#if 0
#define TRACE(s) fprintf(stderr,"\n%s %p %p\n",s,global->Qhead,global->Qtail);
#else
#define TRACE(s)
#endif

static void
pushQ (lrs_dat * global, long m, long d ,long m_A)
{

  if ((global->Qtail->next) == global->Qhead)
    {
      /* the Queue is full */
      if (dict_count < dict_limit)
	{
	  /* but we are allowed to create more */
	  lrs_dic *p;

	  p = new_lrs_dic (m, d, m_A);

	  if (p)
	    {

	      /* we successfully created another record */

	      p->next = global->Qtail->next;
	      (global->Qtail->next)->prev = p;
	      (global->Qtail->next) = p;
	      p->prev = global->Qtail;
/*2023.1.25*/
	      dict_count++;
	      global->Qtail = p;

	      TRACE ("Added new record to Q");

	    }
	  else
	    {
	      /* virtual memory exhausted. bummer */
	      global->Qhead = global->Qhead->next;
	      global->Qtail = global->Qtail->next;

	      TRACE ("VM exhausted");
	    }
	}
      else
	{
	  /*
	   * user defined limit reached. start overwriting the
	   * beginning of Q 
	   */
	  global->Qhead = global->Qhead->next;
	  global->Qtail = global->Qtail->next;
	  TRACE ("User  limit");

	}
    }

  else
    {
      global->Qtail = global->Qtail->next;
      TRACE ("Reusing");
    }
}

lrs_dic *
lrs_getdic(lrs_dat *Q)
/* create another dictionary for Q without copying any values */
/* derived from lrs_alloc_dic,  used by nash.c                */
{
lrs_dic *p;

  long m;

  m = Q->m;

/* nonnegative flag set means that problem is d rows "bigger"     */
/* since nonnegative constraints are not kept explicitly          */


  if(Q->nonnegative)
    m = m+Q->inputd;

  p = new_lrs_dic (m, Q->inputd, Q->m);
  if (!p)
    return NULL;

  p->next = p;
  p->prev = p;
  Q->Qhead = p;
  Q->Qtail = p;
  return p;
}


#define NULLRETURN(e) if (!(e)) return NULL;

static lrs_dic *
new_lrs_dic (long m, long d, long m_A)
{
  lrs_dic *p;
  NULLRETURN (p = (lrs_dic *) malloc (sizeof (lrs_dic)));

  NULLRETURN (p->B = (long int*) calloc ((m + 1), sizeof (long)));
  NULLRETURN (p->Row = (long int*) calloc ((m + 1), sizeof (long)));

  NULLRETURN (p->C =  (long int*) calloc ((d + 1), sizeof (long)));
  NULLRETURN (p->Col = (long int*) calloc ((d + 1), sizeof (long)));

#if defined(GMP) || defined(FLINT)
  lrs_alloc_mp(p->det);
  lrs_alloc_mp(p->objnum);
  lrs_alloc_mp(p->objden);
#endif

  p->d_orig=d;
  p->A=lrs_alloc_mp_matrix(m_A,d);


  return p;
}

void 
lrs_free_dic (lrs_dic * P, lrs_dat *Q)
{
/* do the same steps as for allocation, but backwards */
/* gmp variables cannot be cleared using free: use lrs_clear_mp* */
  lrs_dic *P1;

  if (Q == NULL )
  {
   if(Q->mplrs)
      lrs_post_output("warning","*lrs_free_dic trying to free null Q : skipped");
   else
      fprintf(stderr,"*lrs_free_dic trying to free null Q : skipped");
   return;
  }

  if (P == NULL )
  {
   if(Q->mplrs)
      lrs_post_output("warning","*lrs_free_dic trying to free null P : skipped");
   else
      fprintf(stderr,"*lrs_free_dic trying to free null P : skipped");
   return;
  }
/* repeat until cache is empty */

  do
/*2023.1.26   This clears cache only */
  {
    /* I moved these here because I'm not certain the cached dictionaries
       need to be the same size. Well, it doesn't cost anything to be safe. db */

  long d = P->d_orig;
  long m_A = P->m_A;


  lrs_clear_mp_matrix (P->A,m_A,d);

/* "it is a ghastly error to free something not assigned my malloc" KR167 */
/* so don't try: free (P->det);                                           */

  lrs_clear_mp (P->det);      
  lrs_clear_mp (P->objnum);      
  lrs_clear_mp (P->objden);      

  free (P->Row);
  free (P->Col);
  free (P->C);
  free (P->B);


/* go to next record in cache if any */
  P1 =P->next;
  free (P);
  P=P1;
  }while (Q->Qhead != P );

Q->Qhead=NULL;
Q->Qtail=NULL;

}

void 
lrs_free_dic2 (lrs_dic * P, lrs_dat *Q)
{
/* do the same steps as for allocation, but backwards */
/* same as lrs_free_dic except no cache for P */
    /* I moved these here because I'm not certain the cached dictionaries
       need to be the same size. Well, it doesn't cost anything to be safe. db */

  long d = P->d_orig;
  long m_A = P->m_A;


  lrs_clear_mp_matrix (P->A,m_A,d);

/* "it is a ghastly error to free something not assigned my malloc" KR167 */
/* so don't try: free (P->det);                                           */

  lrs_clear_mp (P->det);      
  lrs_clear_mp (P->objnum);      
  lrs_clear_mp (P->objden);      

  free (P->Row);
  free (P->Col);
  free (P->C);
  free (P->B);

  free (P);

}

void
lrs_free_dat ( lrs_dat *Q )
{

  int i=0;

  if (Q == NULL)
  {
   if(Q->mplrs)
      lrs_post_output("warning","*lrs_free_dat trying to free null Q : skipped");
   else
      fprintf(stderr,"*lrs_free_dat trying to free null Q : skipped");
   return;
  }
  

/* most of these items were allocated in lrs_alloc_dic */

  lrs_clear_mp_vector (Q->Gcd,Q->m);
  lrs_clear_mp_vector (Q->Lcm,Q->m);
  lrs_clear_mp_vector (Q->output,Q->n+1);

  lrs_clear_mp (Q->sumdet);
  lrs_clear_mp (Q->Nvolume);
  lrs_clear_mp (Q->Dvolume);
  lrs_clear_mp (Q->saved_det);
  lrs_clear_mp (Q->boundd);
  lrs_clear_mp (Q->boundn);

  free (Q->facet);
  free (Q->redundcol);
  free (Q->inequality);
  free (Q->linearity);
  free (Q->vars);
  free (Q->startcob);
  free (Q->minratio);
  free (Q->redineq);
  free (Q->temparray);
  free (Q->projmess);

  free (Q->name);  
  free (Q->saved_C);

/*2020.8.1 DA: lrs_Q_list is not a stack but a list, so have to delete Q */

  if(dict_limit > 1)
   {
     while(i<lrs_Q_count && lrs_Q_list[i] != Q)
        i++;
     if(i==lrs_Q_count)
       {
        if(Q->verbose)
           lrs_warning(Q,"warning","lrs_free_dat(Q) not in global list - skipped");
       }
     else
       while(i<lrs_Q_count)
        {
         lrs_Q_list[i] = lrs_Q_list[i+1];
         i++;
         }
    }
     
  lrs_Q_count--;
  free(Q);
}


static long
check_cache (lrs_dic ** D_p, lrs_dat * Q, long *i_p, long *j_p)
{
/* assign local variables to structures */


  cache_tries++;

  if (Q->Qtail == Q->Qhead)
    {
      TRACE ("cache miss");
      /* Q has only one element */
      cache_misses++;
      return 0;

    }
  else
    {
      Q->Qtail = Q->Qtail->prev;

      *D_p = Q->Qtail;

      *i_p = Q->Qtail->i;
      *j_p = Q->Qtail->j;

      TRACE ("restoring dict");
      return 1;
    }
}


lrs_dic *
lrs_alloc_dic (lrs_dat * Q)
/* allocate and initialize lrs_dic */
{

  lrs_dic *p;
  long i, j;
  long m, d, m_A;

  if (Q->hull)                       /* d=col dimension of A */
    Q->inputd = Q->n;                /* extra column for hull */
  else
    Q->inputd = Q->n - 1;

  m = Q->m;
  d = Q->inputd;
  m_A = m;   /* number of rows in A */

  if(m > MAX_ROWS)
    {
     fprintf(lrs_ofp,"\n*trying to allocate dictionary with %ld rows exceding MAX_ROWS=%ld\n",m,MAX_ROWS);
     return NULL;
    }
/* nonnegative flag set means that problem is d rows "bigger"     */
/* since nonnegative constraints are not kept explicitly          */

  if(Q->nonnegative)
    m = m+d;
     


  p = new_lrs_dic (m, d, m_A);
  if (!p)
    return NULL;
  p->next = p;
  p->prev = p;
  Q->Qhead = p;
  Q->Qtail = p;

/* Initializations */

  p->d = p->d_orig = d;
  p->m = m;
  p->m_A  = m_A;
  p->depth = 0L;
  p->lexflag = TRUE;
  itomp (ONE, p->det);
  itomp (ZERO, p->objnum);
  itomp (ONE, p->objden);

/*m+d+1 is the number of variables, labelled 0,1,2,...,m+d  */
/*  initialize array to zero   */
  for (i = 0; i <= m_A; i++)
    for (j = 0; j <= d; j++)
      itomp (ZERO, p->A[i][j]);

  Q->inequality = (long int*) CALLOC ((m + d + 1), sizeof (long));
  Q->facet =  (long int*) CALLOC ((unsigned) m + d + 1, sizeof (long));
  Q->redundcol = (long int*) CALLOC ((m + d + 1), sizeof (long));
  Q->minratio = (long int*) CALLOC ((m+d + 1), sizeof (long));
                         /*  2011.7.14  minratio[m]=0 for degen =1 for nondegen pivot*/
  Q->redineq  = (long int*) CALLOC ((m + d + 1), sizeof (long));
  Q->projmess=(char *)malloc(20+20*Q->n*sizeof(char));
  strcpy(Q->projmess,"");
  Q->temparray = (long int*) CALLOC ((unsigned) m + d + 1, sizeof (long));
  if (Q->nlinearity == ZERO)   /* linearity may already be allocated */
      Q->linearity  = (long int*) CALLOC ((m + d + 1), sizeof (long));
  else
    { /* we may add linearities so need to resize it */
      for(i=0;i<Q->nlinearity;i++)
        Q->temparray[i]=Q->linearity[i];
      free(Q->linearity);
      Q->linearity  = (long int*) CALLOC ((m + d + 1), sizeof (long));
      for(i=0;i<Q->nlinearity;i++)
        Q->linearity[i]=Q->temparray[i];
    }

  Q->inequality[0] = 2L;
  Q->Gcd = lrs_alloc_mp_vector(m);
  Q->Lcm = lrs_alloc_mp_vector(m);
  Q->output = lrs_alloc_mp_vector(Q->n+1);
  Q->saved_C = (long int*) CALLOC (d + 1, sizeof (long));

  Q->lastdv = d;      /* last decision variable may be decreased */
                      /* if there are redundant columns          */

  for (i = 0; i < m+d+1; i++)
   {
    Q->redineq[i]=1;
    Q->inequality[i]=0;
   }


/*initialize basis and co-basis indices, and row col locations */
/*if nonnegative, we label differently to avoid initial pivots */
/* set basic indices and rows */
 if(Q->nonnegative)
  for (i = 0; i <= m; i++)
    {
      p->B[i] = i;
      if (i <= d )
          p->Row[i]=0; /* no row for decision variables */
      else 
          p->Row[i]=i-d;
    }
 else
   for (i = 0; i <= m; i++)
    {
      if (i == 0 )
          p->B[0]=0;
      else
          p->B[i] = d + i;
      p->Row[i] = i;
    }

  for (j = 0; j < d; j++)
    {
      if(Q->nonnegative)
          p->C[j] = m+j+1;
      else
          p->C[j] = j + 1;
      p->Col[j] = j + 1;
    }
  p->C[d] = m + d + 1;
  p->Col[d] = 0;
  return p;
}				/* end of lrs_alloc_dic */


/* 
   this routine makes a copy of the information needed to restart, 
   so that we can guarantee that if a signal is received, we 
   can guarantee that nobody is messing with it.
   This as opposed to adding all kinds of critical regions in 
   the main line code.

   It is also used to make sure that in case of overflow, we
   have a valid cobasis to restart from.
 */
static void
save_basis (lrs_dic * P, lrs_dat * Q)
{
  int i;
/* assign local variables to structures */
  long *C = P->C;
  long d;

#ifndef SIGNALS
  sigset_t oset, blockset;
  sigemptyset (&blockset);
  sigaddset (&blockset, SIGTERM);
  sigaddset (&blockset, SIGHUP);
  sigaddset (&blockset, SIGUSR1);

  errcheck ("sigprocmask", sigprocmask (SIG_BLOCK, &blockset, &oset));
#endif
  d = P->d;

  Q->saved_flag = 1;

  for (i = 0; i < 5; i++)
    Q->saved_count[i] = Q->count[i];

  for (i = 0; i < d + 1; i++)
    Q->saved_C[i] = C[i];

  copy (Q->saved_det, P->det);

  Q->saved_d = P->d;
  Q->saved_depth = P->depth;

#ifndef SIGNALS
  errcheck ("sigproceask", sigprocmask (SIG_SETMASK, &oset, 0));
#endif
}


/* print out the saved copy of the basis */
void 
print_basis (FILE * fp, lrs_dat * Q)
{
  int i;
/* assign local variables to structures */
  fprintf (fp, "lrs_lib: State #%ld: (%s)\t", Q->id, Q->name);

  if (Q->saved_flag)
    {

/* legacy output which is not actually correct for V-representations as V# is not used */
/*
      fprintf (fp, "V#%ld R#%ld B#%ld h=%ld facets ",
	       Q->saved_count[1],
	       Q->saved_count[0],
	       Q->saved_count[2],
	       Q->saved_depth);
      for (i = 0; i < Q->saved_d; i++)
	fprintf (fp, "%ld ",
		 Q->inequality[Q->saved_C[i] - Q->lastdv]);
      pmp (" det=", Q->saved_det);
      fprintf (fp, "\n");
*/

      if( Q->hull)
           fprintf (fp, "\nrestart %ld %ld %ld ",
               Q->saved_count[0],
               Q->saved_count[2],
               Q->saved_depth);
      else
           fprintf (fp, "\nrestart %ld %ld %ld %ld ",
               Q->saved_count[1],
               Q->saved_count[0],
               Q->saved_count[2],
               Q->saved_depth);

      for (i = 0; i < Q->saved_d; i++)
        fprintf (fp, "%ld ",
                 Q->inequality[Q->saved_C[i] - Q->lastdv]);
      if(Q->saved_count[4] >0)
         fprintf (fp, "\nintegervertices %ld", Q->saved_count[4]);
      fprintf (fp, "\n");


    }
  else
    {
      fprintf (fp, "lrs_lib: Computing initial basis\n");
    }


  fflush (fp);
}

#ifndef SIGNALS

static void 
lrs_dump_state ()
{
  long i;

  fprintf (lrs_ofp, "\n\nlrs_lib: checkpointing:\n");

#ifdef MP
  fprintf (stderr, "lrs_lib: Current digits at %ld out of %ld\n",
	   DIG2DEC (lrs_record_digits),
	   DIG2DEC (lrs_digits));
#endif

  for (i = 0; i < lrs_Q_count; i++)
    {
      print_basis (lrs_ofp, lrs_Q_list[i]);
    }
  fprintf (lrs_ofp, "lrs_lib: checkpoint finished\n");
}

/*
   If given a signal
   USR1            print current cobasis and continue
   TERM            print current cobasis and terminate
   INT (ctrl-C) ditto
   HUP                     ditto
 */
static void
setup_signals ()
{
  errcheck ("signal", signal (SIGTERM, die_gracefully));
  errcheck ("signal", signal (SIGALRM, timecheck));
  errcheck ("signal", signal (SIGHUP, die_gracefully));
  errcheck ("signal", signal (SIGINT, die_gracefully));
  errcheck ("signal", signal (SIGUSR1, checkpoint));
}

static void
timecheck ()
{
  lrs_dump_state ();
  errcheck ("signal", signal (SIGALRM, timecheck));
  alarm (lrs_checkpoint_seconds);
}

static void
checkpoint ()
{
  lrs_dump_state ();
  errcheck ("signal", signal (SIGUSR1, checkpoint));
}

static void
die_gracefully ()
{
  lrs_dump_state ();

  exit (1);
}

#endif

#ifndef TIMES
/* 
 * Not sure about the portability of this yet, 
 *              - db
 */
#include <sys/resource.h>
#define double_time(t) ((double)(t.tv_sec)+(double)(t.tv_usec)/1000000)

static void
ptimes ()
{
  struct rusage rusage;
  getrusage (RUSAGE_SELF, &rusage);
  fprintf (lrs_ofp, "\n*%0.3fu %0.3fs %ldKb %ld flts %ld swaps %ld blks-in %ld blks-out \n",
	   double_time (rusage.ru_utime),
	   double_time (rusage.ru_stime),
	   rusage.ru_maxrss, rusage.ru_majflt, rusage.ru_nswap,
	   rusage.ru_inblock, rusage.ru_oublock);
  if(lrs_ofp != stdout)
     printf ("\n*%0.3fu %0.3fs %ldKb %ld flts %ld swaps %ld blks-in %ld blks-out \n",
	   double_time (rusage.ru_utime),
	   double_time (rusage.ru_stime),
	   rusage.ru_maxrss, rusage.ru_majflt, rusage.ru_nswap,
	   rusage.ru_inblock, rusage.ru_oublock);
}

static double get_time()
{
  struct rusage rusage;
  getrusage (RUSAGE_SELF, &rusage);
  return   ( double_time (rusage.ru_utime));

}

#endif

/* Routines based on lp_solve */

void
lrs_set_row(lrs_dic *P, lrs_dat *Q, long row, long num[], long den[], long ineq)
/* convert to lrs_mp then call lrs_set_row */
{
 lrs_mp_vector Num, Den;
 long d;
 long j;
 
  d = P->d;

  Num=lrs_alloc_mp_vector(d+1);
  Den=lrs_alloc_mp_vector(d+1);

  for (j=0;j<=d;j++)
  {
  itomp(num[j],Num[j]);
  itomp(den[j],Den[j]);
  }
 
  lrs_set_row_mp(P,Q,row,Num,Den,ineq);

  lrs_clear_mp_vector(Num,d+1);
  lrs_clear_mp_vector(Den,d+1);

}

void
lrs_set_row_mp(lrs_dic *P, lrs_dat *Q, long row, lrs_mp_vector num, lrs_mp_vector den, long ineq)
/* set row of dictionary using num and den arrays for rational input */
/* ineq = 1 (GE)   - ordinary row  */
/*      = 0 (EQ)   - linearity     */
{
  lrs_mp Temp, mpone;
  lrs_mp_vector oD;             /* denominator for row  */

  long i, j;

/* assign local variables to structures */

  lrs_mp_matrix A;
  lrs_mp_vector Gcd, Lcm;
  long hull;
  long m, d;
  lrs_alloc_mp(Temp); lrs_alloc_mp(mpone);
  hull = Q->hull;
  A = P->A;
  m = P->m;
  d = P->d;
  Gcd = Q->Gcd;
  Lcm = Q->Lcm;

  oD = lrs_alloc_mp_vector (d);
  itomp (ONE, mpone);
  itomp (ONE, oD[0]);

  i=row;
  itomp (ONE, Lcm[i]);      /* Lcm of denominators */
  itomp (ZERO, Gcd[i]);     /* Gcd of numerators */
  for (j = hull; j <= d; j++)       /* hull data copied to cols 1..d */
        {
          copy( A[i][j],num[j-hull]);
          copy(oD[j],den[j-hull]);
          if (!one(oD[j]))
            lcm (Lcm[i], oD[j]);      /* update lcm of denominators */
          copy (Temp, A[i][j]);
          gcd (Gcd[i], Temp);   /* update gcd of numerators   */
        }

  if (hull)
        {
          itomp (ZERO, A[i][0]);        /*for hull, we have to append an extra column of zeroes */
          if (!one (A[i][1]) || !one (oD[1]))         /* all rows must have a one in column one */
            Q->polytope = FALSE;
        }
  if (!zero (A[i][hull]))   /* for H-rep, are zero in column 0     */
        Q->homogeneous = FALSE; /* for V-rep, all zero in column 1     */

  storesign (Gcd[i], POS);
  storesign (Lcm[i], POS);
  if (mp_greater (Gcd[i], mpone) || mp_greater (Lcm[i], mpone))
        for (j = 0; j <= d; j++)
          {
            exactdivint (A[i][j], Gcd[i], Temp);        /*reduce numerators by Gcd  */
            mulint (Lcm[i], Temp, Temp);        /*remove denominators */
            exactdivint (Temp, oD[j], A[i][j]);       /*reduce by former denominator */
          }

  if ( ineq == EQ )        /* input is linearity */
     {
      Q->linearity[Q->nlinearity]=row;
      Q->nlinearity++;
     }

/* 2010.4.26   Set Gcd and Lcm for the non-existant rows when nonnegative set */


  if(Q->nonnegative && row==m)
      for(j=1;j<=d;j++)
         { itomp (ONE, Lcm[m+j]);
           itomp (ONE, Gcd[m+j]);
         }


  lrs_clear_mp_vector (oD,d);
  lrs_clear_mp(Temp); lrs_clear_mp(mpone);
}          /* end of lrs_set_row_mp */

void 
lrs_set_obj(lrs_dic *P, lrs_dat *Q, long num[], long den[], long max)
{
  long i;

  if (max == MAXIMIZE)
       Q->maximize=TRUE;
  else
       {
       Q->minimize=TRUE;
       for(i=0;i<=P->d;i++)
         num[i]=-num[i];
       }

  lrs_set_row(P,Q,0L,num,den,GE);
}

void
lrs_set_obj_mp(lrs_dic *P, lrs_dat *Q, lrs_mp_vector num, lrs_mp_vector den, long max)
{
  long i;

  if (max == MAXIMIZE)
       Q->maximize=TRUE;
  else
       {
       Q->minimize=TRUE;
       for(i=0;i<=P->d;i++)
         changesign(num[i]);
       }

  lrs_set_row_mp(P,Q,0L,num,den,GE);
}


long
lrs_solve_lp(lrs_dic *P, lrs_dat *Q)
/* user callable function to solve lp only */
{
  lrs_mp_matrix Lin;		/* holds input linearities if any are found             */
  long col;

  Q->lponly = TRUE;

  if (!lrs_getfirstbasis (&P, Q, &Lin, FALSE))
    return FALSE;

  if(overflow_detected)
    return 1;

/* There may have been column redundancy                */
/* If so the linearity space is obtained and redundant  */
/* columns are removed. User can access linearity space */
/* from lrs_mp_matrix Lin dimensions nredundcol x d+1   */

  for (col = 0; col < Q->nredundcol; col++)	/* print linearity space               */
    lrs_printoutput (Q, Lin[col]);   	        /* Array Lin[][] holds the coeffs.     */

  return TRUE;
} /* end of lrs_solve_lp */

long
dan_selectpivot (lrs_dic * P, lrs_dat * Q, long *r, long *s)
/* select pivot indices using dantzig simplex method             */
/* largest coefficient with lexicographic rule to avoid cycling  */
/* Bohdan Kaluzny's handiwork                                    */
/* returns TRUE if pivot found else FALSE                        */
/* pivot variables are B[*r] C[*s] in locations Row[*r] Col[*s]  */
{
  long j,k,col;
  lrs_mp coeff;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *Col = P->Col;
  long d = P->d;

/*  printf("\n*dantzig"); */
  lrs_alloc_mp (coeff);
  *r = 0;
  *s = d;
  j = 0;
  k = 0;

  itomp(0,coeff);
/*find positive cost coef */
  while (k < d)
     {
       if(mp_greater(A[0][Col[k]],coeff))
        {
          j = k;
          copy(coeff,A[0][Col[j]]);
        }
      k++;
     }

  if (positive(coeff))                  /* pivot column found! */
    {
      *s = j;
      col = Col[j];

      /*find min index ratio */
      *r = lrs_ratio (P, Q, col);
      if (*r != 0)
        {
        lrs_clear_mp(coeff);
        return (TRUE);          /* pivot found */
        }
    }
  lrs_clear_mp(coeff);
  return (FALSE);
}                               /* end of dan_selectpivot        */



long 
ran_selectpivot (lrs_dic * P, lrs_dat * Q, long *r, long *s)
/* select pivot indices using random edge rule                   */
/* largest coefficient with lexicographic rule to avoid cycling  */
/* pivot variables are B[*r] C[*s] in locations Row[*r] Col[*s]  */
{
  long i,j,k,col,t;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *Col = P->Col;
  long d = P->d;
  long *perm;

  perm = (long *) calloc ((d + 1), sizeof (long)); 
  *r = 0;
  *s = d;
  k = 0;
/*  printf("\n*random edge"); */


/* generate random permutation of 0..d-1 */
  for (i = 0; i < d; i++) perm[i] = i;

  for ( i = 0; i < d; i++) 
    {
	j = rand() % (d-i) + i;
	t = perm[j]; perm[j] = perm[i]; perm[i] = t; // Swap i and j
     }
  if(Q->debug)
    {
        printf("\n perm: ");
        for (i = 0; i < d; i++) printf(" %ld",perm[i]);
    }

/*find first positive cost coef according to perm */
  while (k < d && !positive(A[0][Col[perm[k]]])) 
      k++;

  if ( k<d )			/* pivot column found! */
    {
      j=perm[k];
      *s = j;
      col = Col[j];

      /*find min index ratio */
      *r = lrs_ratio (P, Q, col);
      if (*r != 0)
        {
          free(perm);
	  return (TRUE);		/* pivot found */
        }
    }
  free(perm);
  return (FALSE);
}				/* end of ran_selectpivot        */


long
phaseone (lrs_dic * P, lrs_dat * Q)
/* Do a dual pivot to get primal feasibility (pivot in x_0)*/
/* Bohdan Kaluzny's handiwork                                    */
{
  long i, j, k;
/* assign local variables to structures */
  lrs_mp_matrix A = P->A;
  long *Row = P->Row;
  long *Col = P->Col;
  long m, d;
  lrs_mp b_vector;
  lrs_alloc_mp (b_vector);
  m = P->m;
  d = P->d;
  i = 0;
  k = d+1;

  itomp(0,b_vector);

  fprintf (lrs_ofp, "\nLP: Phase One: Dual pivot on artificial variable");

/*find most negative b vector */
  while (k <= m)
     {
       if(mp_greater(b_vector,A[Row[k]][0]))
        {
          i = k;
          copy(b_vector,A[Row[i]][0]);
        }
      k++;
     }

  if (negative(b_vector))                       /* pivot row found! */
    {
      j = 0;            /*find a positive entry for in row */
      while (j < d && !positive (A[Row[i]][Col[j]]))
        j++;
      if (j >= d)
        {
          lrs_clear_mp (b_vector);
          return (FALSE);       /* no positive entry */
        }
      pivot (P, Q, i, j);
      update (P, Q, &i, &j);
      if(overflow_detected)
        {
         if(Q->verbose && !Q->mplrs)
            lrs_warning(Q,"warning","*overflow phaseone");
         return FALSE;
        }

    }
  lrs_clear_mp (b_vector);
  return (TRUE);
}


long
lrs_set_digits(long dec_digits)
{
/* convert user specified decimal digits to mp digits */

  if (dec_digits > 0)
    lrs_digits = DEC2DIG (dec_digits);
  if (lrs_digits > MAX_DIGITS)
    {
      fprintf (lrs_ofp, "\nDigits must be at most %ld\nChange MAX_DIGITS and recompile",
	       DIG2DEC (MAX_DIGITS));
      fflush(stdout);
      return (FALSE);
    }
  return (TRUE);
}

long
lrs_checkbound(lrs_dic *P, lrs_dat *Q)
{
/* check bound on objective and return TRUE if exceeded */

  if(!Q->bound)
     return FALSE;

  if( Q->maximize && comprod(Q->boundn,P->objden,P->objnum,Q->boundd) == 1 )
       {
        if(Q->verbose)
             {
              prat(" \nObj value: ",P->objnum,P->objden);
              fprintf(lrs_ofp," Pruning ");
              }
        return TRUE;
       }
  if( Q->minimize && comprod(Q->boundn,P->objden,P->objnum,Q->boundd) == -1 )
       {
        if(Q->verbose)
             {
              prat(" \nObj value: ",P->objnum,P->objden);
              fprintf(lrs_ofp," Pruning ");
              }
         return TRUE;
       }
  return FALSE;
}


long
lrs_leaf(lrs_dic *P, lrs_dat *Q)
{
/* check if current dictionary is a leaf of reverse search tree */
  long    col=0;
  long    tmp=0;

  while (col < P->d && !reverse(P,Q,&tmp,col))
                 col++;
  if(col < P->d) 
     return 0;            /* dictionary is not a leaf */
  else
     return 1;
}

/* prevent output flushes in mplrs */
void lrs_open_outputblock(void)
{
#ifdef MPLRS
	open_outputblock();
#endif
}

/* re-enable output flushes in mplrs */
void lrs_close_outputblock(void)
{
#ifdef MPLRS
	close_outputblock();
#endif
}

void lrs_post_output(const char *type, const char *data)
{
#ifdef MPLRS
     post_output(type,data);
#endif
}

void lrs_return_unexplored(lrs_dic *P,lrs_dat *Q) /* send cobasis data for unexplored nodes */

{

#ifdef MPLRS
lrs_restart_dat R;
int i;
        if((Q->mindepth != 0) && (P->depth == Q->maxdepth))   /*2021.5.19 implement maxdepth in mplrs */
          return;                                             /*don't send back to job queue!         */

        for (i = 0; i < P->d; i++)
              Q->temparray[i] = Q->inequality[P->C[i] - Q->lastdv];
        R.facet=Q->temparray;
        R.d=P->d;
        R.depth=P->depth;
        update_R(P,Q,&R);
        post_R(&R);
#else
    if(Q->debug)
        {
        lrs_printcobasis(P,Q,ZERO);
        fprintf(lrs_ofp," *unexplored");
        }
#endif
}


/* replace by user overflow routine if not using lrsv2_main() */

void lrs_overflow(int parm)
{
#ifdef NASH
#ifdef B128
   fprintf(lrs_ofp,"\n*arithmetic overflow, suggest lrsnash\n");
#else
   fprintf(lrs_ofp,"\n*arithmetic overflow, suggest lrsnash2 or lrsnash\n");
#endif
   lrs_exit(1);
#endif
      overflow_detected=1;
}

void lrsv2_overflow(char *tmp, lrs_dic *P, lrs_dat *Q)
{

if(Q->nash)
 {
   fprintf(lrs_ofp,"Nash overflow\n");
   lrs_exit(1);
 }
#ifdef MPLRS 
  overflow=1;     
  return;              /* return to lrsv2_main */  
#endif

#ifdef MP
  overflow=1;
  fprintf (stdout, "\n*lrsmp: overflow at digits=%ld", DIG2DEC (lrs_digits));
  fprintf (stdout, "\n*use 'digits n' option with  n > %ld\n", DIG2DEC (lrs_digits));
  return;
#endif

int try_restart=FALSE;

  if (lrs_Q_list[0] == NULL)
  {
     fprintf(stderr,"*lrs_overflow has null Q ");
     lrs_exit(1);
  }

/* db's cunningly hidden locations */
/* now sadly not used              */
/*
  Q = lrs_Q_list[lrs_Q_count-1];     
  P = Q->Qhead;
*/

/* lrs, redund,fel restarted at the moment */


#ifdef MA
 if(!Q->mplrs)
       try_restart=TRUE;
#endif


  if(lrs_ifp != NULL)
      fclose(lrs_ifp);

  if (!try_restart )  /* hard exit */ 
   { 
     fflush(lrs_ofp); fflush(stderr);
     if (strcmp(BIT,"64bit")==0 )
        {
          fprintf(lrs_ofp,"\n*64bit integer overflow: try running 128bit or gmp versions\n");
          if (lrs_ofp != stdout)
             fprintf(stderr,"\n*64bit integer overflow: try running 128bit or gmp versions\n");
        }
     else
        {
         fprintf(stderr,"\n*128bit integer overflow: try running gmp version\n");
         if (lrs_ofp != stdout)
             fprintf(lrs_ofp,"\n*128bit integer overflow: try running gmp version\n");
        }
      return;
   }

/* try to restart */
      if(overflow == 0)                 /*  first overflow */
       {
        fflush(lrs_ofp);
        if (*tmpfilename != '\0' )  /* we made a temporary file for stdin  */
         {
           if(remove(tmpfilename) != 0)
              fprintf (lrs_ofp, "\nCould not delete temporary file");
         }
#ifdef WIN
        strncpy(tmpfilename,"lrs_restartXXXXXX",PATH_MAX);
#else
        strncpy(tmpfilename,"/tmp/lrs_restartXXXXXX",PATH_MAX);
#endif
        /* XXX in principle this file descriptor should be used instead of the name */
        tmpfd = mkstemp(tmpfilename);
        }
      else
        strcpy(tmpfilename,infilename);

   overflow=1L;
   lrs_cache_to_file(tmpfilename," ");

   if(Q->fel || Q->redund)
     if(Q->Ain != NULL)
       lrs_clear_mp_matrix(Q->Ain,Q->m,Q->n);

   Q->m=P->m;

   if(tmp != NULL)
     strcpy(tmp,tmpfilename);

   if (lrs_ofp != NULL && lrs_ofp != stdout )
     {
      fclose (lrs_ofp);
      lrs_ofp=NULL;
     }
   close(tmpfd);
   return;

}   /* lrsv2_overflow */


void lrs_exit(int i)
{  
  fflush(stdout);
  exit(i);
}

void lrs_free_all_memory(lrs_dic * P, lrs_dat * Q)
{

  if(Q->runs > 0)
    { 
      free(Q->isave);
      free(Q->jsave);
    }
  if(P != NULL)                     /* may not have allocated P yet */
    {
      long savem=P->m;            /* need this to clear Q*/
      lrs_free_dic (P,Q);           /* deallocate lrs_dic */
      Q->m=savem;
    }
  lrs_free_dat (Q);             /* deallocate lrs_dat */
#ifdef LRSLONG
  free(infile);                 /* we cached input file for possible restart */
#endif
  return;
}

long lrs_stdin_to_file(char *filename)
{
    FILE *fptr1, *fptr2;
    char c;

    fptr1 = stdin;
    fptr2 = fopen(filename, "w");
    if (fptr2 == NULL)
    {
        printf("Cannot open file %s \n", filename);
        exit(0);
    }

    c = fgetc(fptr1);
    while (c != EOF)
    {
        fputc(c, fptr2);
        c = fgetc(fptr1);
    }

    fclose(fptr2);
    fptr2=NULL;

    return 0;
}

long lrs_file_to_cache(FILE *ifp)
{
    long ret;    

if (ifp != NULL) 
    if (fseek(ifp, 0L, SEEK_END) == 0) 
     {
        ret = ftell(ifp);
        if (ret == -1) 
           {
            fputs("*Error reading file", stderr);
            return 1;
           }
        infileLen = ret;
        infile = (char *) malloc(sizeof(char) * (infileLen + 1));

        if (fseek(ifp, 0L, SEEK_SET) != 0) 
           {
            fputs("*Error resetting input file", stderr);
            return 1;
           }
        infileLen = fread(infile, sizeof(char), infileLen, ifp);
        if ( ferror( ifp ) != 0 ) 
          {
            fputs("*Error reading input file", stderr);
            return 1;
           }
        else 
            infile[infileLen++] = '\0'; /* Just to be safe. */
      }
rewind(ifp);
return 0;
}

long lrs_cache_to_file(char *name,const char *restart)
{
FILE *ofp = fopen(name, "wb");

if (ofp == NULL)
    {
      printf("*Error opening output file %s",name);
      return 1;
    }
fwrite(infile, sizeof(char), infileLen, ofp);

fclose(ofp);
return 0;

}

void lrs_setup_R(lrs_dic *P, lrs_dat *Q, lrs_restart_dat *R)
{
 int i;

 R->d = P->d;       /* length of R->facets           */
 R->m = P->m;         /* number of input rows*/

 Q->startcob    = (long int*) CALLOC ((R->d + R->m + 1), sizeof (long));
 R->redineq     = (long int*) CALLOC ((R->m + 1), sizeof (long));

 for(i=0;i<R->d;i++)
   Q->startcob[i]=Q->inequality[i];

 for (i=0; i<= R->m; i++)
   R->redineq[i] = 1;

 R->redundphase=1;  /* will be set =0 if there are linearities */
 R->testlin=Q->testlin;

 if (Q->redund) 
   {
    R->redund=1;
    R->lrs=0;
    for(i=0;i<Q->nlinearity; i++)
      Q->redineq[Q->linearity[i]]=2;
    for (i=0; i<= R->m; i++)
      R->redineq[i] = Q->redineq[i];
   }

 if (Q->fel)
   {
    R->fel=1;
    R->lrs=0;
   }

 if((Q->redund || Q->fel)&& R->rank==1) 
           Q->testlin=1;

 R->printcobasis=Q->printcobasis;   /* mplrs renumbers B# in output */


}  /* lrs_setup_R */

lrs_dic *lrs_setup(int argc, char *argv[], lrs_dat **Q, lrs_restart_dat *R)
/* allocate lrs_dat Q, lrs_dic P, read in the problem data and make a copy of P  */
{
  lrs_dic *P;                     /* structure for holding current dictionary and indices */

  lrs_ifp = stdin;
  lrs_ofp = stdout;

  if ( !lrs_init(lrs_basename(argv[0])))
       return NULL;
 
  *Q = lrs_alloc_dat ("LRS globals");    /* allocate and init structure for static problem data */

  if (*Q == NULL)
    return NULL;

  strcpy((*Q)->fname,lrs_basename(argv[0]));
  
  if(strcmp("redund",(*Q)->fname)==0)
     (*Q)->redund=TRUE;

  if(strcmp("minrep",(*Q)->fname)==0)
    {
    
     (*Q)->redund=TRUE;
     if(R->rank==0)
        (*Q)->testlin=TRUE;     /* check for hidden linearities performed */
    }

  if(strcmp("fel",(*Q)->fname)==0)
     (*Q)->fel=TRUE;

  if((*Q)->mplrs)
    {
/* 2023.11.23 */
     if(R->rank == 0 )
       {
         if((*Q)->redund) /* sets up LP for testing hidden linearity */
             (*Q)->testlin=1;
         else             /* does a minrep for mplrs/fel             */
             (*Q)->testlin=R->testlin;

       }
     (*Q)->tid=R->rank;
     (*Q)->messages=R->messages;

     if((*Q)->redund )
      {
       if(R->rank == 1)
          (*Q)->redundphase=1;
      }

     }


  if (!lrs_read_dat (*Q, argc, argv))    /* read first part of problem data to get dimensions */
    return NULL;                         /* and problem type: H- or V- input representation   */

  P = lrs_alloc_dic (*Q);        /* allocate and initialize lrs_dic                      */
  if (P == NULL )
    return NULL;

  if (!lrs_read_dic (P, *Q))     /* read remainder of input to setup P and Q             */
       return NULL;

  return P;
}   /* lrs_setup */


lrs_dic *lrs_reset(lrs_dic *P_orig, lrs_dat *Q,  lrs_restart_dat *R)
{
  static long long inputmaxd=0;   /* hide maxd for restoring when using mplrs */
  lrs_dic *P;
  long i;

  itomp (ZERO, Q->Nvolume);
  itomp (ONE, Q->Dvolume);
  itomp (ZERO, Q->sumdet);
  if(inputmaxd ==0)
     inputmaxd=Q->maxdepth;

//2023.1.24 

  if(Q->plrs)
       P=lrs_alloc_dic (Q);
  else
       P=lrs_getdic (Q);

  Q->Qhead=P_orig;
  Q->Qtail=P_orig;
  if( P == P_orig)
   {
     if(Q->mplrs)
        lrs_post_output("warning", "*lrs_reset: copy_dict has dest=src -ignoring copy");
     else
        fprintf(stderr,"*lrs_reset: copy_dict has dest=src -ignoring copy");
   }
  copy_dict (Q,P,P_orig);       /* restore original input  */
  Q->Qhead=P;
  Q->Qtail=P;

/*if overiding, update Q from R   */

  if (R->lrs && R->overide == 1)
    {
      Q->messages=R->messages;
/*  2021.5.19  implement maxdepth in mplrs */
/*
      if(R->maxdepth == -1)
         Q->maxdepth=MAXD;
      else
*/
      Q->maxdepth=R->maxdepth;
      Q->mindepth=R->mindepth;
      Q->maxcobases=R->maxcobases;
      if (Q->maxcobases > 0)
               Q->maxcobases = Q->maxcobases + R->count[2];
      if(R->restart==1)
        {
          Q->restart=TRUE;
          if(!Q->lponly)
             Q->giveoutput=FALSE;   /* supress first output */

          for(i=0;i<R->d;i++)
            {
             Q->facet[i+Q->nlinearity]=R->facet[i];
             Q->inequality[i]=Q->startcob[i];
            }
          for(i=0;i<5;i++)
            {
              Q->count[i]=R->count[i];
              Q->startcount[i] = Q->count[i];  /* for mplrs subjob counts */
            }
        }

      P->depth = R->depth;
      R->maxdepth=inputmaxd;
    }

  Q->tid=R->rank;
  Q->redundphase=R->redundphase;

/* we test for hidden linearities in fel mode */

   if(R->redund)
      {
/* this restores original linearities that somehow got lost! */
/* definitely not needed for fel!           */
        if(!R->fel)
          for(i=0;i<Q->nlinearity;i++)
            {
             Q->redineq[Q->linearity[i]]=2;
             R->redineq[Q->linearity[i]]=2;
            }

        if(R->rank==1)  /* consumer will print output */
         {
           Q->redundphase=1;
           Q->testlin=0;
         }
/* rebuild linearities after first parallel phase */
        if(R->rank > 1 &&  Q->redundphase)
          {
           Q->nlinearity=0;
           for (i=1; i<= R->m; i++)
              {
                if (R->redineq[i] ==2)
                  Q->linearity[Q->nlinearity++]=i;
              }
           }
      }            /* if R->redund */


  return P;
}       /* lrs_reset */

void update_R(lrs_dic *P, lrs_dat *Q, lrs_restart_dat *R)
{
  int i;

  for (i=0;i<=4;i++)
    R->count[i]=Q->count[i];
  R->count[8]=Q->count[8];
  R->count[5]=Q->hull;
  if(Q->hull)  
    R->count[6]=Q->nredundcol-Q->homogeneous;
  else
    R->count[6]=Q->nredundcol;
  R->count[7]=Q->deepest;
  R->redundphase=Q->redundphase;
  return;
}

lrs_dat *copy_Q(lrs_dat *Q)
{
  lrs_dat *Q1;

  if( (Q1 = lrs_alloc_dat("LRS GLOBALS")) == NULL)
    {
     fprintf(lrs_ofp,"\n*Can't allocate memory for Q1");
     exit(1);
    }
  Q1->m = Q->m;
  Q1->n = Q->n;

  Q1->allbases = Q->allbases;
  Q->bound = Q->bound;
  Q1->countonly = Q->countonly;
  Q1->dualdeg = Q->dualdeg;
  Q1->debug = Q->debug;
  Q1->deepest = Q->deepest;
  Q1->fel = Q->fel;
  Q1->frequency = Q->frequency;
  Q1->geometric = Q->geometric;
  Q1->getvolume = Q->getvolume;
  Q1->homogeneous = Q->homogeneous;
  Q1->hull = Q->hull;
  Q1->incidence = Q->incidence;
  Q1->inputd = Q->inputd;
  Q1->lponly = Q->lponly;
  Q1->maxdepth = Q->maxdepth;
  Q1->maxincidence = Q->maxincidence;
  Q1->messages = Q->messages;
  Q1->mindepth = Q->mindepth;
  Q1->minprunedepth = Q->minprunedepth;
  Q1->maxoutput = Q->maxoutput;
  Q1->maxcobases = Q->maxcobases;
  Q1->mplrs = Q->mplrs;
  Q1->nredundcol = Q->nredundcol;
  Q1->nonnegative = Q->nonnegative;
  Q1->polytope = Q->polytope;
  Q1->printcobasis = Q->printcobasis;
  Q1->redund = Q->redund;
  Q1->runs = Q->runs;
  Q1->seed = Q->seed;
  Q1->threads = Q->threads;
  Q1->triangulation = Q->triangulation;
  Q1->verbose = Q-> verbose;

  return Q1;
}

void copy2_Q(lrs_dat *Q1, lrs_dat *Q)     /* fill in the arrays allocated in Q1 from Q */
{
 int i,m,d;
 m=Q->m;
 d=Q->inputd;

 copy_linearity(Q1,Q);

 for (i = 0; i <= m; i++)   
  {
   copy(Q1->Gcd[i],Q->Gcd[i]);
   copy(Q1->Lcm[i],Q->Lcm[i]);
  }

 for(i=0;i<m+d+1;i++)
  {
   Q1->inequality[i]=Q->inequality[i];
   Q1->facet[i]=Q->facet[i];
   Q1->redundcol[i]=Q->redundcol[i];
  }

}

 

#ifdef LRSLONG
#ifdef B128
long lrs2_main(int argc, char *argv[],lrs_dic **P_orig, lrs_dat **Q,long overf,long stage,char *tmp, lrs_restart_dat *R)
#else
long lrs1_main(int argc, char *argv[],lrs_dic **P_orig, lrs_dat **Q,long overf,long stage,char *tmp, lrs_restart_dat *R) 
#endif
#else
long lrsgmp_main(int argc, char *argv[],lrs_dic **P_orig, lrs_dat **Q,long overf,long stage,char *tmp, lrs_restart_dat *R)
#endif
{
  return lrsv2_main(argc,argv,P_orig,Q,overf,stage,tmp,R);
}


long lrs_main(int argc, char *argv[])
/* legacy version, replaced by lrsv2_main but still maintained */

{
  lrs_dic *P;
  lrs_dat *Q;
  lrs_restart_dat *R;
  char* tmp;          /* when overflow occurs a new input file name is returned */
  long overfl=0;     /*  =0 no overflow =1 restart */

/* =2 append restart       disabled 2023.12.13 since plrs doesn't use it */

  P=NULL;
  Q=NULL;
  R=NULL;
  tmp=NULL;

  R = lrs_alloc_restart();
  if (R == NULL)
    exit(1);

  overfl=lrsv2_main(argc,argv,&P,&Q,0,0,tmp,R);  /* set up, read input, no run   */

  if(overfl == -1)    /* lrs_setup failed due to bad input file etc. - no cleanup*/
    return 0;
  if(overfl == 0)
    overfl=lrsv2_main(argc,argv,&P,&Q,0,1,tmp,R);  /* standard lrs run - argc, argv, R not used */

  lrsv2_main(argc,argv,&P,&Q,0,2,tmp,R);  /* free memory and close, does not access argc, argv */

  free(R->facet);
  free(R->redineq);
  free(R);
  return 0;
}
#define Q (*Qin)
long lrsv2_main(int argc, char *argv[],lrs_dic **P_orig, lrs_dat **Qin,long overf,long stage,char *tmp, lrs_restart_dat *R)     

/* compiled independently with all supported arithmetic packages             */
/* should be called from one of lrsX_main where X is an arithmetic package   */

{
 lrs_dic *P;
 long i,n;
 long verbose;

 overflow_detected=0;  /* reinitialize as per DB suggestion*/

 /* initial call: allocate lrs_dat, lrs_dic and set up the problem - no run */
 if(stage==0)
    {
/*
      printf("\n*begin Stage 0 rank=%ld redundphase=%ld testlin=%ld R->redund=%ld R->fel=%ld argv[0]=%s",
          R->rank,R->redundphase,R->testlin,R->redund,R->fel,argv[0]);
*/
      *P_orig=lrs_setup(argc,argv,&Q,R);

      if(overflow_detected)
        goto over;

      if(*P_orig==NULL)
        { 
         fprintf(stderr,"\n*lrs_setup failed\n");
         return -1;
        }

      verbose=Q->verbose;
      if(Q->debug)
        fprintf(lrs_ofp,"\n*mid   Stage 0 rank=%ld redundphase=%ld testlin=%ld Q->redund=%ld Q->fel=%ld R->redund=%ld R->fel=%ld Q->fname=%s",
          Q->tid,Q->redundphase,Q->testlin,Q->redund,Q->fel,R->redund,R->fel,Q->fname);
      lrs_setup_R(*P_orig,Q,R);
      if(Q->debug)
        fprintf(lrs_ofp,"\n*end   Stage 0 rank=%ld redundphase=%ld testlin=%ld Q->redund=%ld Q->fel=%ld R->redund=%ld R->fel=%ld Q->fname=%s",
          Q->tid,Q->redundphase,Q->testlin,Q->redund,Q->fel,R->redund,R->fel,Q->fname);
      return 0;
    }

 verbose=Q->verbose;

 /* main work for lrs/redund/fel runs  */
if(stage==1) 
{
  if(overflow_detected)
        goto over;

  if(R->rank==1 && R->fel==1 && R->redundphase==0)  
    {
/*consumer just prints ouput*/
      R->fel=0; Q->fel=0;
      R->redund=1; Q->redund=1;
    }

  if(overflow_detected)
     goto over;

  if(Q->debug)
    fprintf(lrs_ofp,"\n*begin Stage 1 rank=%ld Q->redund=%ld Q->fel=%ld R->redund=%ld R->fel=%ld R->redundphase=%ld R->testlin=%ld Q->fname=%s",
       R->rank,Q->redund,Q->fel,R->redund,R->fel,R->redundphase,R->testlin,Q->fname);


 if(Q->plrs)
     P= *P_orig;
 else
     P=lrs_reset(*P_orig,Q,R);       /* restore P and reset Q from R   */

 if(Q->mplrs && R->rank==0 && R->fel && Q->testlin)
    {
     R->fel=0; R->redund=0;
    }

 if(Q->debug)
    fprintf(lrs_ofp,"\n*Begin Stage 1 rank=%ld Q->redund=%ld Q->fel=%ld R->redund=%ld R->fel=%ld R->redundphase=%ld R->testlin=%ld Q->fname=%s",
       R->rank,Q->redund,Q->fel,R->redund,R->fel,R->redundphase,R->testlin,Q->fname);


 n=Q->n;

 if(overf==2)
    Q->giveoutput=FALSE;      /* suppress first output         */

 if(R->fel )
   {                     
    if(Q->debug && Q->mplrs)
     {
      fprintf(lrs_ofp,"\n*begin fel Stage=1 R->redund=%ld R->rank=%ld R->redineq:",
         R->redund, R->rank);
      for (i=1; i<= R->m; i++)
             fprintf(lrs_ofp," %ld", R->redineq[i]);
     }
     if(Q->vars == NULL)
       {
        if(R->rank==0)
         {
           Q->messages=TRUE;
           lrs_warning(Q,"warning","*no project/eliminate option found - removing last column");
         }
        Q->vars  =   (long int*) CALLOC ((n + 3), sizeof (long)); 
        for (i=0;i<=n+2;i++)
           Q->vars[i]=1-Q->hull;  
        Q->vars[0]=n-1; Q->vars[n+1]=1;
        Q->fel=1;
       }
    put_linearities_first(Q, P);
    fel_run(P, Q, R);
    if(overflow_detected)
        goto over;
    if(Q->debug && Q->mplrs)
      {
        fprintf(lrs_ofp,"\n*end   fel Stage=1 R->redund=%ld R->rank=%ld R->redineq:",R->redund,R->rank);
        for (i=1; i<= R->m; i++)
             fprintf(lrs_ofp," %ld", R->redineq[i]);
        fprintf(lrs_ofp,"\n");
      }
    return 0;
  }
 if(R->redund)
   {
    for (i=0; i<= R->m; i++)
      Q->redineq[i] = R->redineq[i];
    if(Q->debug)
     {
      fprintf(lrs_ofp,"\n*begin Stage 1 redund R->redund=%ld R->rank=%ld Q->redineq:",
         R->redund, R->rank);
         for (i=1; i<= R->m; i++)
             fprintf(lrs_ofp," %ld", Q->redineq[i]);
     }
    redund_run(P,Q);
    if(overflow_detected)
       goto over;

    for (i=0; i<= R->m; i++)
       R->redineq[i] = Q->redineq[i];

/* sometimes we are doing a mplrs/fel run */
    R->redundphase=1-Q->hiddenlin;  
    R->testlin=0;

    if(Q->debug)
     {
      fprintf(lrs_ofp,"\n*end Stage 1 redund R->redund=%ld R->rank=%ld R->redundphase=%ld R->redineq:",
         R->redund, R->rank,R->redundphase);
      for (i=1; i<= R->m; i++)
       fprintf(lrs_ofp," %ld", R->redineq[i]);
     }

    return 0;
   }            /* if R->redund */

/* we are in lrs mode */

    if(Q->debug)
      fprintf(lrs_ofp,"\n*begin Stage 1 lrs R->redund=%ld R->rank=%ld Q->fname=%s",R->redund,R->rank,Q->fname);

   if(Q->hull && Q->lponly && !Q->testlin) /*2024.4.20  find optimizing input rows in V-rep */
     if(!(Q->mplrs && Q->tid==0)) /* not a mplrs/redund run */
        return lrs_check_inequality(P,Q);  

   if(!Q->plrs)   /* single thread lrs */
     { 

      Q->child=0;
      lrs_run(P,Q);               
      }
   else
      plrs_run(P,Q,R,tmp);

   if(overflow_detected)
      goto over;

   update_R(*P_orig,Q,R);		   /* update counts for mplrs */
   if(Q->testlin) 
         R->count[6]=0;

   if(Q->debug)
        fprintf(lrs_ofp,"\n*end Stage 1 lrs R->rank=%ld R->redundphase=%ld \n",R->rank,R->redundphase);
   return 0;

   } /* stage 1 */

   /* final cleanup */
  if(stage == 2 )
  {
    if(!Q->mplrs && verbose && overflow_detected)
      printf("\n*cleanup after overflow");

    Q->Qhead=*P_orig;
    Q->Qtail=*P_orig;

    if(overflow==0)
        lrs_close (Q->fname);
    lrs_free_all_memory(*P_orig,Q);

   for(i=0;i<lrs_Q_count;i++)     
    {
     Q=lrs_Q_list[0];
     if(Q->Qhead != NULL)
        lrs_free_dic(Q->Qhead,Q);
     lrs_free_dat(lrs_Q_list[0]);
    }

   return overflow;
  }

return 0;

over:
 Q->Qhead=*P_orig;
 Q->Qtail=*P_orig;
 lrsv2_overflow(tmp,*P_orig,Q);  /* prepare restart */
 return 1; 
} /* lrsv2_main */
#undef Q

long
plrs_run(lrs_dic *P, lrs_dat *Q, lrs_restart_dat *R, char *tmp)
{
#ifdef PLRS
/*2023.1.9*/                        /* multithread plrs but only used for V-H */
 lrs_dic *P1;    
 lrs_dat *Q1;
 long (*c)[10];   /* collect counts from parallel threads */
 long i,j,tdeepest=0,fdeepest=0;
 lrs_mp_vector Nvol,Dvol;
 long  d=Q->inputd;

 if(Q->threads == 0)
    Q->threads = (d < omp_get_max_threads()) ? d : omp_get_max_threads();

 fprintf(stderr,"\n*starting %ld threads\n",Q->threads);

 c=calloc(d+1,sizeof(*c));
 for(i=1;i<=d;i++)
     for(j=0;j<=9;j++)
        c[i][j]=0;
 
 Nvol = lrs_alloc_mp_vector(d+1);
 Dvol = lrs_alloc_mp_vector(d+1);
 fflush(lrs_ofp); fflush(stdout);
 Q1=copy_Q(Q);
 P1=lrs_reset(P,Q1,R);
 copy2_Q(Q1,Q);
 Q1->Qhead = P1;
 Q1->Qtail = P1;
 Q1->child=0;
 Q1->plrs=1;

/* get children of the root */
 i=lrs_run(P1,Q1);
 lrs_free_dat(Q1);
 if(i==1)      /* failure such as infeasible solution */
   return 1;

#pragma omp parallel for private(P1,Q1) schedule(dynamic,1) reduction(max:fdeepest,tdeepest) num_threads(Q->threads)

         for(i=1;i<=d;i++)    /* parallel reverse search for each child of root */
{
 if(!overflow_detected)
   {
    int tid = omp_get_thread_num();
    #pragma omp critical
    {
      Q1=copy_Q(Q);
      P1=lrs_reset(P,Q1,R);
      copy2_Q(Q1,Q);
    }
    Q1->messages=FALSE;
    Q1->Qhead = P1;
    Q1->Qtail = P1;
    Q1->tid=tid;
    Q1->child=i;
    lrs_run(P1,Q1);                  /* do reverse search    */
    copy(Nvol[i],Q1->Nvolume);
    copy(Dvol[i],Q1->Dvolume);
    for(j=0;j<=7;j++)
       c[i][j]=Q1->count[j];
    c[tid][9]=c[tid][9]+Q1->count[2]-1;   /* one extra per child */
    fdeepest=Q1->count[8];
    tdeepest=Q1->deepest; 
    if(i==1)  /* set Q values once only */
      {
         Q->nredundcol=Q1->nredundcol;
         Q->homogeneous=Q1->homogeneous;
      }
    #pragma omp critical
    {
      lrs_free_dat(Q1);
    }
   }
 }                                /* end of parallel for */
  if(Q->getvolume)
      {
          lrs_mp tN, tD;
          lrs_alloc_mp(tN); lrs_alloc_mp(tD); 
          itomp(ZERO,Q->Nvolume);
          itomp(ONE,Q->Dvolume);
          for(i=1;i<=d;i++)   
          {
           copy (tN, Q->Nvolume);
           copy (tD, Q->Dvolume);
           if(!zero(Dvol[i]))
              linrat (Nvol[i], Dvol[i], ONE, tN, tD, ONE, Q->Nvolume,Q->Dvolume);
          }
          lrs_clear_mp(tN); lrs_clear_mp(tD); 
       }

/*2023.1.9*/
  if(overflow_detected)
    {
      free(c);
      lrs_clear_mp_vector(Nvol,d+1);
      lrs_clear_mp_vector(Dvol,d+1);
      return 0;
    }
  for(j=0;j<=7;j++)
     for(i=1;i<=d;i++)
       Q->count[j]= Q->count[j] + c[i][j];
  Q->count[2]=Q->count[2]-d;               /* one basis extra per child */
  Q->count[8]=fdeepest;
  Q->deepest=tdeepest;
  lrs_clear_mp_vector(Nvol,d+1);
  lrs_clear_mp_vector(Dvol,d+1);

  lrs_printtotals(P,Q);
  if(Q->verbose)
   { 
     fprintf(lrs_ofp,"*threads=%ld  counts:",Q->threads);
     for(i=0;i<Q->threads;i++)
       fprintf(lrs_ofp," %ld",c[i][9]);
     fprintf(lrs_ofp,"\n*children=%ld counts:",d);
     for(i=1;i<=d;i++)
       fprintf(lrs_ofp," %ld",c[i][2]-1);
     if(lrs_ofp != stdout)
     {
      printf("\n*threads=%ld  counts:",Q->threads);
      for(i=0;i<Q->threads;i++)
           printf(" %ld",c[i][9]);
      printf("\n*children=%ld counts:",d);
      for(i=1;i<=d;i++)
           printf(" %ld",c[i][2]-1);
      printf("\n");
     }
    }
    free(c);
#endif

return 0;
}  /* plrs_run */

void
lrs_warning(lrs_dat *Q, char* type, char* ss)
{
  if(Q->messages)
   {
    if(Q->mplrs)
       lrs_post_output(type,ss);
    else
     {
       fprintf (lrs_ofp, "\n%s",ss);
       if(lrs_ofp != stdout)
         fprintf (stderr, "\n%s",ss);
     }
   }
}


/*********************************************************************************/
/*2022.1.18  For V-rep with lponly just find optimizing input rows               */
/*********************************************************************************/
long lrs_check_inequality(lrs_dic *P, lrs_dat *Q)
{
  lrs_mp_matrix A=P->A;
  lrs_mp tmp,opt,total;
  long m, d, i, j, count;

  lrs_alloc_mp(tmp); lrs_alloc_mp(total); lrs_alloc_mp(opt);
  itomp(ONE,tmp);           /*unnecessary but avoids warning */

  fprintf (lrs_ofp, "\n");

  m = P->m;
  d = P->d;
  itomp(0,opt);

  if (Q->nonnegative)  /* skip basic rows - don't exist! */
    m=m-d;

  for(i=1;i<=m;i++)
    {
      itomp(0,total);
      for (j = 1; j <= d; j++)
       {
        mulint(A[0][j],A[i][j],tmp);
        linint(total,1,tmp,1);
        if(Q->debug)
           pmp(" ",A[i][j]);
       }
      if(i==1)
        copy(opt,total);
      else
        if(mp_greater(total,opt) )
          copy(opt,total);
      if(Q->debug)
          {
           pmp("total",total);
           pmp("opt",opt);
           fprintf (lrs_ofp, "\n");
          }
    }
  fprintf(lrs_ofp,"\n*optimum row(s):");
  count=0;
  for(i=1;i<=m;i++)   /* once more to print optima */
    {
      itomp(0,total);
      for (j = 1; j <= d; j++)
       {
        mulint(A[0][j],A[i][j],tmp);
        linint(total,1,tmp,1);
       }
       if(!mp_greater(opt,total) )
        {
          count++;
          fprintf(lrs_ofp," %ld",i);
         }
    }

  if(Q->minimize)     /* lrs only maximizes */
      {
        changesign(opt);
        prat("\n*min value:",opt,P->det);
      }
  else
      pmp("\n*max value:",opt);
  fprintf(lrs_ofp," obtained by %ld row(s)",count);
  fprintf(lrs_ofp,"\n");
  lrs_clear_mp(tmp); lrs_clear_mp(opt);

  return 0;
}
/************************************************************************************/
/* Tallman Nkgau's functions for Fourier elimination modified by DA                 */
/************************************************************************************/


void fel_abort(char str[])
{
  printf("%s\n", str );
  fflush(stdout);
  exit(1);
}

 
lrs_dic *makecopy(lrs_dat *Q2,lrs_dic *P, lrs_dat *Q)
{
  lrs_dic *P2;

  Q2->m = Q->m;
  Q2->n = Q->n;
  Q2->nlinearity = 0;
  if (( P2 = lrs_alloc_dic(Q2)) == NULL)
    fel_abort("ERROR>Can't allocate dictionary space");
  copydicA(P2,P,-1,-1);
  return P2;
}

void linear_dep(lrs_dic *P, lrs_dat *Q, long *Dep)
{
  long d;
  long nlinearity;
  lrs_mp_matrix A;
  long i, j, k, row, col,m;

  d = P->d;
  nlinearity = Q->nlinearity;
  
  A = lrs_alloc_mp_matrix(nlinearity+1, d+2);
  for(i=0;i<nlinearity+1;i++)
    Dep[i] = 0;              /* assume lin. dep. */
  for(i=1;i<=nlinearity;i++)
    {
      for(j=0;j<=d;j++)
	copy(A[i][j], P->A[i][j]);
      itomp(ZERO, A[i][d+1]);
    }
  for(col=1;col<=d;col++)
    {
      row = -1;
      for(i=1;i<=nlinearity;i++)
	if ((zero(A[i][d+1]))&& !zero(A[i][col]))
	  {
	    row = i;
	    break;
	  }

      if (row > 0)
	for(k=1;k<=nlinearity;k++)
	  {
	    if ((zero(A[k][d+1]))&&(!zero(A[k][col])) && (k!=row))
	      {
		if (sign(A[k][col])*sign(A[row][col]) < 0)
		  {
		    copy(A[0][0], A[k][col]);
		    copy(A[0][1], A[row][col]);
		    
		    storesign(A[0][0], POS);
		    storesign(A[0][1], POS);
		    
		    for(i=0;i<=d;i++)
		      {
			mulint(A[0][0], A[row][i], A[0][2]);
			mulint(A[0][1], A[k][i], A[0][3]);
			addint(A[0][2], A[0][3], A[k][i]);
		      }
		  }
		else
		  {
		    copy(A[0][0], A[k][col]);
		    copy(A[0][1], A[row][col]);
		    
		    storesign(A[0][0], NEG);
		    storesign(A[0][1], POS);
		    
		    for(i=0;i<=d;i++)
		      {
			mulint(A[0][0], A[row][i], A[0][2]);
			mulint(A[0][1], A[k][i], A[0][3]);
			addint(A[0][2], A[0][3], A[k][i]);
		      }
		  }
		itomp(ONE, A[row][d+1]);

                if(Q->debug)
                 {
		   for(i=1;i<=nlinearity; i++)
		    {
		      for(m=0;m<=d;m++)
		        pmp("", A[i][m]);
		      fprintf(lrs_ofp, "\n");
		    }
		   fprintf(lrs_ofp, "\n");
                 }
	      }
	  }
    }
  for(row=1,i=0;row<=nlinearity;row++)
    {
      for(k=0,i=0;k<=d;k++)
	if(zero(A[row][k]))
	  i++;
      if (i==d+1)
	Dep[row] =1;
    }  
  lrs_clear_mp_matrix(A,nlinearity+1, d+2);
  
}
/********************************************************************************/
/* groups[i] = +1 if A[i][col] > 0, groups[i] = -1 if A[i][col] < 0,            */
/* groups[i] = 0 if A[i][col] = 0, groups[m+1] = # of rows with <0 entry        */
/* in column 'col', and groups[m+2] = # of rows with > 0 entry in column 'col'  */
/********************************************************************************/
void lrs_compute_groups(lrs_dat *Q, lrs_dic *P, long col, long *groups)
{
  long i, row;
  long m;

  m = Q->m;
  for(i=0;i<= m+2; i++)
    {
      groups[i] = 0;
    }
   for(row = 1; row <= Q->m; row++)
    {
      if (sign(P->A[row][col]) < 0)
	{
	  groups[row] = -1;
	  groups[m+1]++;
	}
      else if (zero(P->A[row][col]))
	{
	  groups[0]++;
	}
      else
	{
	  groups[row] = 1;
	  groups[m+2]++;
	}
    }
   if(Q->debug)
     printf("\n*signs in col=%ld: - =%ld + =%ld 0 =%ld",col,groups[m+1],groups[m+2],m-groups[m+1]-groups[m+2]);
   
}
long lrs_next_col(lrs_dat *Q, lrs_dic *P, long *remove)
/* in fel mode determine the next column to eliminate */
/* start with linearities, otherwise min new matrix size*/
{
  long row,col,plus,minus;
  long long size=MAXD;
  long minind=0;
  long i,j,ind;
  long n=Q->n;

  if(Q->debug)
   {
     fprintf(lrs_ofp,"\n*in n=%ld nlinearity=%ld remove" ,n,Q->nlinearity);
     for(i=0;i<=n+1;i++)
       fprintf(lrs_ofp," %ld",remove[i]);
   }

/* user chose order */

  if(remove[n]==0) 
   {
    ind=0;
    goto gotcol;
   }


/* try to use linearities */

  for(ind=0;ind<remove[n+1];ind++)
    {
      col=remove[ind];
      j=1;
      while(j <= Q->nlinearity && zero(P->A[j][col]))
         j++;
      if(j<=Q->nlinearity)
         goto gotcol;
    }

/* find col minimizing size of new matrix */

  for(ind=0;ind<remove[n+1];ind++)
   {
    col=remove[ind];
    plus=0; minus=0;
    for(row = 1; row <= Q->m; row++)
     {
      if (negative(P->A[row][col]) )
       minus++;
      else if (positive(P->A[row][col]))
       plus++;
      } 
     if(plus*minus < size)
       {
        size=plus*minus;
        minind=ind;
       }
     if(Q->debug)
       fprintf(lrs_ofp,"\n*col=%ld minind=%ld pm=%ld size=%lld",col,minind,plus*minus,size);
    }
   
  ind=minind;

gotcol:
  col=remove[ind];

  for(i=0;i<=remove[n+1];i++)
   {
    if(remove[i]>col)
       remove[i]--;
    if(i>ind)
       remove[i-1]=remove[i];
   }
  remove[i]=0;
  remove[n-1]=remove[n];   /* selection heuristic if any */
  remove[n]=remove[n+1]-1;


  if(Q->debug)
    {
     fprintf(lrs_ofp,"\n*out col=%ld n=%ld remove" ,col,n);
     for(i=0;i<=n;i++)
       fprintf(lrs_ofp," %ld",remove[i]);
     fflush(stdout);
    }
  return col;

}    /* lrs_next_col */

/*******************************************************************/
/* Function to copy matrix A  from dictionary P to dictionary P1.  */
/* The column (variable) with index 'skip' is left out.            */
/* Set skip to '-1' if no column is to be left out.                */
/* Adapted from function in lrslib.c.                  */
/*******************************************************************/
void copydicA(lrs_dic *P1, lrs_dic *P, long skip_row, long skip_col)
{
  long i, j;
  long d, m_A;

  d = P->d; /* dimension of space of variables */
  m_A = P->m_A;

  if (skip_col > 0)
    {
      if (skip_row > 0)
	{
	  for (i = 0; i < skip_row; i++)
	    {
	      for(j = 0; j < skip_col; j++)
		copy (P1->A[i][j], P->A[i][j]);
	      for(j = skip_col+1; j <= d; j++)
		copy (P1->A[i][j-1], P->A[i][j]);
	      
	    }
	  for (i = skip_row+1; i <= m_A; i++)
	    {
	      for(j = 0; j < skip_col; j++)
		copy (P1->A[i-1][j], P->A[i][j]);
	      for(j = skip_col+1; j <= d; j++)
		copy (P1->A[i-1][j-1], P->A[i][j]);
	      
	    }
	}
      else
	{
	  for (i = 0; i <= m_A; i++)
	    {
	      for(j = 0; j < skip_col; j++)
		copy (P1->A[i][j], P->A[i][j]);
	      for(j = skip_col+1; j <= d; j++)
		copy (P1->A[i][j-1], P->A[i][j]);
	      
	    }
	}

    }
  else      /* skip_col <= 0 */
    {
      if (skip_row > 0)
	{
	  for (i = 0; i < skip_row; i++)
	    for(j = 0; j <= d; j++)
	      copy (P1->A[i][j], P->A[i][j]);
	  for (i = skip_row+1; i <= m_A; i++)
	    for(j = 0; j <= d; j++)
	      copy (P1->A[i-1][j], P->A[i][j]);
	}
      else
	{
	  for (i = 0; i <= m_A; i++)
	    for(j = 0; j <= d; j++)
	      copy (P1->A[i][j], P->A[i][j]);
	 
	}
    }
}
/***************************************************************/
/* copy linearity from iQ to Q                                 */ 
/***************************************************************/
void copy_linearity(lrs_dat *Q, lrs_dat *iQ)
{
  long nlinearity;
  long i;
  long n=iQ->n;

  nlinearity = iQ->nlinearity;
  if (nlinearity > 0)
    {
      if(Q->linearity==NULL)
         Q->linearity = CALLOC ((nlinearity +1), sizeof (long));
      for(i=0; i < nlinearity; i++)
	Q->linearity[i] = iQ->linearity[i];
      Q->nlinearity = nlinearity;
      Q->polytope = FALSE;
    }

  if(iQ->vars != NULL )
   {
    Q->vars  =   (long int*) CALLOC ((n + 3), sizeof (long));
    for(i=0;i<=n+2;i++)
       Q->vars[i]=iQ->vars[i];
   }

}
/***************************************************************/
void put_linearities_first(lrs_dat *Q, lrs_dic *P)
{
  long nlinearity;
  long i, row;
  lrs_mp Temp;

  lrs_alloc_mp(Temp);

  nlinearity = Q->nlinearity;
  for(row=1; row <= nlinearity; row++)
    {
      if (Q->linearity[row-1] != row)
	{
	  for(i=0;i<=P->d; i++)
	    {
	      copy(Temp, P->A[row][i]);
	      copy(P->A[row][i], P->A[Q->linearity[row-1]][i]);
	      copy(P->A[Q->linearity[row-1]][i], Temp);
	    }
	  copy(Temp, Q->Gcd[row]);
	  copy(Q->Gcd[row], Q->Gcd[Q->linearity[row-1]]);
	  copy(Q->Gcd[Q->linearity[row-1]], Temp);
	  copy(Temp, Q->Lcm[row]);
	  copy(Q->Lcm[row], Q->Lcm[Q->linearity[row-1]]);
	  copy(Q->Lcm[Q->linearity[row-1]], Temp);
	  Q->linearity[row-1] = row;
	}
      
    }
  
  lrs_clear_mp(Temp);

}
/***************************************************************/
/* Function to compute redundancies. redineq[i] = 1 if row i   */
/* is redundant. Adapted from function in lrslib.c.*/
/***************************************************************/ 

long compute_redundancy(long *redineq, lrs_dic *P1, lrs_dat *Q1)
{
  lrs_dic *P;
  lrs_dat *Q;
  long ineq;
  long d, m;
  long lastdv, index;
  lrs_mp_matrix Lin;
  
  m = P1->m_A;
  d = P1->d;

  if( (Q = lrs_alloc_dat("LRS GLOBALS")) == NULL)
    fel_abort("ERROR>Can't allocate memory for structures");
  P=makecopy(Q,P1,Q1);

  if (lrs_getfirstbasis (&P, Q, &Lin, TRUE)==0 || overflow_detected)
      return (FALSE);

  m = P->m_A;
  d = P->d;
  lastdv = Q->lastdv;
  for(index = lastdv +1;index <= m+d; index++)
    {
      ineq = Q->inequality[index-lastdv];

      /*test for linearity or redundancy */
      redineq[ineq] = checkindex(P, Q, index, 0); 
      if(redineq[ineq] == -1)   /* indicates strictly redundant */
          redineq[ineq]=1;
    }
  lrs_free_dic(P,Q);
  lrs_free_dat(Q);
  return (TRUE);
}

/******************************************************************/
/* project out column=col                                         */
/******************************************************************/
long lrs_project_var(lrs_dic **iP, lrs_dat **iQ, long col, long verbose)
{
  lrs_dic *P, *P1;
  lrs_dat *Q, *Q1;
  
  long *tgroups;
  long j, k, l, row;

  /* could do with less of these  monsters */ 
  lrs_mp Temp, Temp1, Lcm, div1, div2, Temp2, Temp3, Temp4, Temp5;

  lrs_alloc_mp(Temp); lrs_alloc_mp(Temp1);lrs_alloc_mp(Temp2);
  lrs_alloc_mp(Temp3); lrs_alloc_mp(Temp4);lrs_alloc_mp(Temp5);
  lrs_alloc_mp(Lcm); lrs_alloc_mp(div1);lrs_alloc_mp(div2);

  itomp(ZERO,Temp);  itomp(ZERO,Temp1);  itomp(ZERO,Temp2);
  itomp(ZERO,Temp3);  itomp(ZERO,Temp4);  itomp(ZERO,Temp5);

 
  P=(*iP);
  Q=(*iQ);

  tgroups = CALLOC ((Q->m+4), sizeof (long));
  if (tgroups == NULL)
    fel_abort("ERROR>Can't allocate memory.");
  /* compute groupings tgroups[0] =  # of rows with '0' in column 'col'           */
  lrs_compute_groups(Q, P, col, tgroups);
  /* check for overflow */
  if (tgroups[Q->m +1] > 0)
    if (tgroups[Q->m + 2] > (MAXD/tgroups[Q->m +1]))
	fel_abort("ERROR>Overflow...too many rows produced.");
  
  /* create P1, Q1 */

  if( (Q1 = lrs_alloc_dat("LRS GLOBALS")) == NULL)
    fel_abort("ERROR>Can't allocate memory for structures");

  Q1->m = (tgroups[Q->m + 1]*tgroups[Q->m +2]) + tgroups[0];
  Q1->n = (Q->n)-1;
  if(!Q->mplrs )
     fprintf(lrs_ofp,"\n*allocating dictionary with %ld rows",Q1->m);
  if (( P1 = lrs_alloc_dic(Q1)) == NULL)
	fel_abort("ERROR>Can't allocate dictionary space");
  row = 1;
  
  for(j = 1; j <= Q->m; j++)
	{
	  if (tgroups[j] < 0) 
	for(k=1; k <= Q->m; k++)
	  {
		
		if (tgroups[k] > 0)
		  {
		copy(div1, P->A[j][col]);
		copy(div2, P->A[k][col]);
		storesign(div1, POS);
		copy(Lcm, div1);
		lcm(Lcm, div2);
		
		copy(Temp, Lcm);
		copy(Temp1, div1);
		divint(Temp, Temp1, Temp2);
		
		copy(Temp, Lcm);
		copy(Temp1, div2);
		divint(Temp, Temp1, Temp3);
		
		
		for(l=0;l< col; l++)  
		  {
			
			copy(Temp, P->A[j][l]);
			copy(Temp1, P->A[k][l]); 
			mulint(Temp,Temp2 ,Temp4);
			mulint(Temp1,Temp3,Temp5);
			addint(Temp4, Temp5, P1->A[row][l]);
			
		  }
		for(l=col+1;l<Q->n; l++)  
		  {
			
			copy(Temp, P->A[j][l]);
			copy(Temp1, P->A[k][l]); 
			mulint(Temp,Temp2 ,Temp4);
			mulint(Temp1,Temp3,Temp5);
			addint(Temp4, Temp5, P1->A[row][l-1]);
			
		  }
		reducearray(P1->A[row], Q1->n);
		
		row++;
		  } /* end if (tgroups[k]) */
	  } /* end for k */
	} /* end for j*/
  for(j=1;j<=Q->m;j++)
    {
        if (tgroups[j]==0)  /* just copy row, coefficient was '0' */
	{
	  for(l=0;l<col;l++)
		copy(P1->A[row][l], P->A[j][l]);
	  for(l=col+1;l<Q->n;l++)
		copy(P1->A[row][l-1], P->A[j][l]);
	  reducearray(P1->A[row], Q1->n);
	  row++;
	}
     }

/*  }  end else  */

  lrs_free_dic(P, Q);
  lrs_free_dat(Q);
  for(row=1;row<=Q1->m;row++)
    {
      l = 0;
      for(j=1;j<Q1->n; j++)
	if (zero(P1->A[row][j]))
	  l++;
     }
           
  free(tgroups);
  lrs_clear_mp(Temp); lrs_clear_mp(Temp1);lrs_clear_mp(Temp2);
  lrs_clear_mp(Temp3); lrs_clear_mp(Temp4);lrs_clear_mp(Temp5);
  lrs_clear_mp(Lcm); lrs_clear_mp(div1);lrs_clear_mp(div2);


/* returns system with redundancies */
  (*iP) = P1;
  (*iQ) = Q1;
  return 0;

}              /* end lrs_project_var */


/*********************************************************************/
/* This is the main loop of the Fourier Elimination code derived     */
/* an earlier code of Tallman Nkgau                                  */
/*********************************************************************/

long fel_run(lrs_dic *P, lrs_dat *Qin, lrs_restart_dat *R)
{
  lrs_dic  *P1,*P2,*P3;
  lrs_dat  *Q,*Q1,*Q2;
  
  long *redineq;
  long *remove;
  long *remainingvars;

  long i, j, k, l, m, col, row;
  long eqn;
  long m_begin;
  lrs_mp Temp, Temp1, div1;
  long messages=FALSE;

  long n = Qin->n;
  long remaining=n-1;    /* for multiple rounds,  number of remaining vars */
  long debug=Qin->debug;
  long verbose=Qin->verbose;
  long nlinearity = Qin->nlinearity;
  long mplrs=Qin->mplrs;
  long nremove=Qin->vars[n+1];
  long redundphase=Qin->redundphase;
  long rounds;
  long min=Qin->m;
  long nin=Qin->n;

  if(mplrs && R->rank != 1)    /* only get consumer messages */
    verbose=FALSE;


/* for hull we only strip out the columns then remove redundancies */
  if(Qin->hull)
     {
      extractcols(P,Qin);
      lrs_free_dic(P,Qin);
      return 0;
     }

/* for H-rep we perform Fourier Elimination removing redundancies */

  lrs_alloc_mp(Temp); lrs_alloc_mp(Temp1);lrs_alloc_mp(div1);
  itomp(ZERO,Temp);  itomp(ZERO,Temp1); itomp(ZERO,div1);

/* save Qin for later calls in mplrs */
/* make working copy in Q            */

  if( (Q = lrs_alloc_dat("LRS GLOBALS")) == NULL)
      fel_abort("ERROR>Can't allocate memory for structures");
 
  Q->m=Qin->m;    
  Q->n=Qin->n;
  Q->fel=TRUE;

/* P3 just used to allocate the rest of Q */

  if (( P3 = lrs_alloc_dic(Q)) == NULL)
             fel_abort("ERROR>Can't allocate dictionary space");

  remainingvars = (long int*) CALLOC ((n + 3), sizeof (long));
  for(i=1;i<=remaining;i++)  
    remainingvars[i] = i;

  copy_linearity(Q, Qin);
  remove=Q->vars;
  rounds=remove[n+1];
  if(Q->mplrs)
     rounds=1;
  for (i = 0; i < rounds; i++) /* main loop */
    {
      if(i>0)  /* after first iteration create redundant row free P from Q->Ain */                
        {
           if( (Q1 = lrs_alloc_dat("LRS GLOBALS")) == NULL)
              fel_abort("ERROR>Can't allocate memory for structures");
           redineq=Q->redineq;
           Q1->m=Q->m-Q->redineq[0];     /* redineq[0]= # redunds last round */
           Q1->n=Q->n;
           Q1->fel=TRUE;
           if (( P = lrs_alloc_dic(Q1)) == NULL)
             fel_abort("ERROR>Can't allocate dictionary space");
           l=0;
           for(k=1;k<=Q->m;k++)
              if(redineq[k] != 1 && redineq[k] != -1)
                  {
                     l++;
                     for(j=0;j<=P->d;j++)
                     copy(P->A[l][j], Q->Ain[k][j]);
                  }
           lrs_clear_mp_matrix(Q->Ain,min,nin);
           copy_linearity(Q1, Q);
           remove=Q1->vars;
           lrs_free_dat(Q );
           Q=Q1;
         }

      Q->verbose=verbose;
      Q->debug=debug;
      m_begin=Q->m;

      col=lrs_next_col(Q,P,remove);

      if(!Q->mplrs)
        fprintf(lrs_ofp,"\n\n*iteration %ld removing column %ld",i+1,col);

      eqn= -1;
      for(l=1; l <= nlinearity; l++)
	if (!zero(P->A[l][col])) 
	  {
             eqn = l; /* use this linearity row to eliminate col */
	     break;
	  } 
      if (eqn > 0)
	{
	  for(l=eqn-1;l<nlinearity-1;l++) /* reduce linearities */
	    Q->linearity[l] = Q->linearity[l+1] -1;
	  nlinearity--;
	  Q->nlinearity = nlinearity;
	  
	  for(j=0;j<=P->d;j++)
	     if ((j!=col) && !zero(P->A[eqn][j]))
		changesign(P->A[eqn][j]);

	  copy(div1, P->A[eqn][col]);
	  for(k=1;k<=P->m;k++)
            if (k!=eqn)
              {
		  for(j=0;j<=P->d;j++)
                     if (j!=col)
			{
			  if (zero(P->A[k][col]))
				break;
			  mulint(P->A[k][col], P->A[eqn][j], Temp);
			  mulint(P->A[k][j], div1, Temp1);
			  addint(Temp, Temp1, P->A[k][j]);
			  if (negative(div1))
                            if (!zero(P->A[k][j]))
				  changesign(P->A[k][j]);
			}
		
		  itomp(0L, P->A[k][col]);
		  reducearray(P->A[k], Q->n);
		}   /* for k=1 ... */

	  /* resize P */

          if( (Q1 = lrs_alloc_dat("LRS GLOBALS")) == NULL)
                fel_abort("ERROR>Can't allocate memory for structures");
          Q1->m = Q->m -1;
          Q1->n = Q->n -1;
          Q1->fel=TRUE;
          copy_linearity(Q1, Q);
          remove=Q1->vars;
          if (( P1 = lrs_alloc_dic(Q1)) == NULL)
                fel_abort("ERROR>Can't allocate dictionary space");
          copydicA(P1, P, eqn, col);
	}
	  else     /* eqn <= 0  eliminate inequality */
	{
	  if( (Q2 = lrs_alloc_dat("LRS GLOBALS")) == NULL)
		fel_abort("ERROR>Can't allocate memory for structures");
	  
	  Q2->m = Q->m - Q->nlinearity;
	  Q2->n = Q->n;
	  if (( P2 = lrs_alloc_dic(Q2)) == NULL)
		fel_abort("ERROR>Can't allocate dictionary space");  

	  for(row=Q->nlinearity+1;row<=Q->m; row++)
              for(l=0;l<=(Q->n)-1;l++)
		copy(P2->A[row-Q->nlinearity][l], P->A[row][l]);

          Q2->m = Q->m - Q->nlinearity;
          Q2->mplrs=mplrs;
          Q2->debug=Q->debug;

	  lrs_project_var(&P2, &Q2, col, messages && verbose);

          if(debug)
             fprintf(lrs_ofp,"\n*exited lrs_project_var: Q2->m=%ld",Q2->m);
          if (verbose)
             felprint(P2,Q2);
      
	  /* add back linearities getting new P, Q */
	  
	  if( (Q1 = lrs_alloc_dat("LRS GLOBALS")) == NULL)
		fel_abort("ERROR>Can't allocate memory for structures");
	  
	  Q1->m = Q->nlinearity + P2->m_A;;
	  Q1->n = Q->n - 1;
	  copy_linearity(Q1, Q);
          remove=Q1->vars;
	  if (( P1 = lrs_alloc_dic(Q1)) == NULL)
		fel_abort("ERROR>Can't allocate dictionary space");

	  for(row=1;row<=Q->nlinearity; row++)
            {
              Q1->redineq[row]=2;
              for(l=0;l<col;l++)/* skip column 'col' */
		copy(P1->A[row][l], P->A[row][l]);
              for(l=col+1;l<=(Q->n)-1;l++)
		copy(P1->A[row][l-1], P->A[row][l]);
            } 
          for(row=1;row<=P2->m_A; row++)
            {
              Q1->redineq[row+Q->nlinearity]=Q2->redineq[row];
              for(l=0;l<=(Q1->n)-1;l++)
		copy(P1->A[row+Q->nlinearity][l], P2->A[row][l]);
            }
	  lrs_free_dic(P2 , Q2 );
          lrs_free_dat(Q2 );

	}                                 /* eqn <= 0  eliminate inequality */
      
    if(i==0)
     {
      lrs_free_dic(P , Qin );            /* Qin retained for future rounds in mplrs */
      lrs_free_dic(P3, Q   );            /* P3 only used to allocate Q */
      lrs_free_dat(Q);
     }
    else   
     {
      lrs_free_dic(P , Q );
      lrs_free_dat(Q);
     }

    P=P1;
    Q=Q1;

/*2020.7.16 P,Q contain the new structure incl. redundancies and linearities*/

    m=Q->m; 
    R->m=m;
    Q->redundphase=redundphase;
    Q->verbose=verbose;
    if(mplrs &&  R->rank==0)       /* boss does no work */
      {
          lrs_free_dic(P1,Q1);
          free(R->redineq);
             R->redineq= (long int*) calloc ((m + 1), sizeof (long));
          for(k=0;k<=m;k++)
             R->redineq[k]=1;
          for (i = 0; i <Q->nlinearity; i++)
             R->redineq[Q->linearity[i]] = 2L;
          R->count[6]=Q->nlinearity;
          if(Q->debug)
           {
             fprintf(lrs_ofp,"\n**R->rank=%ld R->redineq:",R->rank);
             for(k=1;k<=m;k++)
                fprintf(lrs_ofp," %ld",R->redineq[k]);
             fprintf(lrs_ofp,"\n");
           }

          break;
      }    /* if mplrs */
      

    if(debug)
     {
       fprintf(lrs_ofp," \n*Rankk=%ld Q->m=%ld Q->redineq:\n",R->rank,Q->m);
       for(k=1;k<=m;k++)
         fprintf(lrs_ofp," %ld",Q->redineq[k]);
       fprintf(lrs_ofp,"\n");
     }

    m=Q->m;
    Q->fel=TRUE;
    Q->debug=debug;
    Q->tid= R->rank; 

    if(Q->mplrs)
      for(k=0;k<=m;k++)
          Q->redineq[k]=R->redineq[k];

    min=Q->m; nin=Q->n;           /* for deallocating Q->Ain later */

    redund_run(P,Q);
    if(overflow_detected)     /*2023.11.1*/
      {
       if(Q->debug)
         fprintf(lrs_ofp," \n*overflow in fel");
       lrs_free_dic(P,Q);
       goto cleanup;
      }

    if(Q->mplrs)
      for(k=0;k<=m;k++)
         R->redineq[k]=Q->redineq[k];

    if(debug)
     if (!mplrs || R->rank == 1)
      {
       fprintf(lrs_ofp," \n*after redund rank=%ld:redineq:\n",R->rank);
       for(k=1;k<=m;k++)
         fprintf(lrs_ofp," %ld",Q->redineq[k]);
       fprintf(lrs_ofp,"\n");
      }
  
    if ((!mplrs || R->rank==1) && verbose )
      {
        fprintf(lrs_ofp, "\n*number of  \t number after \t number of   \t remaining\tcolumn  \n");
        fprintf(lrs_ofp, "*inequalties\t removing col\t redundancies\t  rows   \tremoved \n");
        fprintf(lrs_ofp, "*%10ld     ", m_begin);
        fprintf(lrs_ofp, "%10ld    ", m);
        fprintf(lrs_ofp, "%10ld    ", Q->redineq[0]);  /* # redunds calculated in redund_print */
        fprintf(lrs_ofp, "%10ld    ", m-Q->redineq[0]);
        fprintf(lrs_ofp, "%10ld    ", col);
        fprintf(lrs_ofp, "\n---------------------------------------------------------------------\n");

      }
// 2023.11.15  after first run mplrs loses track of the original vars
   if(!Q->mplrs)
     {
      remaining--;
      for(j=col;j<=remaining;j++)  /*update remaining vars list */
          remainingvars[j]= remainingvars[j+1];
      if(remaining == 1)
         fprintf(lrs_ofp,"*original variable remaining:");
      else
         fprintf(lrs_ofp,"*original vars remaining:");
      for(j=1;j<=remaining;j++)  
          fprintf(lrs_ofp," %ld",remainingvars[j]);
      fprintf(lrs_ofp,"\n");
     }

   n=Q->n;

   if(i < nremove-1 && (!mplrs || R->rank==1))  /*remove option for next round */
     {
      if(remove[n]==0)         /* col removal in given order */
       {
        fprintf(lrs_ofp,"eliminate %ld  ",remove[n+1]);
        for (k = 0; k < remove[n+1];k++)                        
          fprintf(lrs_ofp," %ld",remove[k]);
       }
      else                     /* col removal in greedy order */
       {
        fprintf(lrs_ofp,"project %ld  ",n-nremove+i);
        for (k = 1; k < n;k++)                               
         {
          j=0;
          while(j<remove[n+1] && remove[j]!=k)
             j++;
          if(j==remove[n+1])
             fprintf(lrs_ofp," %ld",k);
         }
        
       }
     }

   R->m=Q->m;
   R->count[6]=Q->nlinearity;

   if(mplrs)         /* mplrs does one round only */
    {
     if(R->rank==1)
     break;
    }
  }                 /* for i=    main loop       */

cleanup:
fflush(lrs_ofp);
  if(!Q->mplrs)
      lrs_clear_mp_matrix(Q->Ain,min,nin);
  lrs_free_dat(Q);
  lrs_clear_mp(Temp); lrs_clear_mp(Temp1);lrs_clear_mp(div1);
  free(remainingvars);
  return overflow_detected;
}                              /* end of fel_run */
  
void felprint(lrs_dic *P,lrs_dat *Q)
 {
  long row;
  fprintf(lrs_ofp, "\nH-representation\n");
  if (Q->nlinearity > 0)
    {
      fprintf(lrs_ofp, "linearity %ld", Q->nlinearity);
      for(row=0; row<Q->nlinearity;row++)
	fprintf(lrs_ofp, " %ld", Q->linearity[row]);
      fprintf(lrs_ofp, "\n");
    }
  fprintf(lrs_ofp, "begin\n");
  fprintf(lrs_ofp, "%ld %ld %s", Q->m, Q->n, "rational");
  for(row=1;row<=Q->m; row++)
    lrs_printrow("", Q, P->A[row], P->d);
  fprintf(lrs_ofp, "\nend\n");
  
}

void redundmask(lrs_dat *Q, lrs_restart_dat *R)
/* 2023.12.15 obsolete, mplrs does this now */
{
     long i;
     long low=1; 
     long s=0;
     long  t=0;
     long m=Q->m;
     long hi=Q->m;
//   long verbose=Q->verbose;
     long debug=Q->debug;
     
     if(R->rank == 0)         /*master do no checks */
       hi=0;
     else if(R->rank == 1)    /*consumer, check everything */
       low=1;
     else
      {
       s=m/(R->size - 2);
       t=m % (R->size - 2);
       hi=0;
       for(i=2;i<=R->rank;i++)
         {
          low=hi+1;
          if(i<t+2)
            hi=low+s;
          else
            hi=low+s-1;
         }
       }
//    if(debug)
       {
         fprintf(lrs_ofp,"\n*rank=%ld size=%ld low=%ld hi=%ld ",R->rank,R->size,low,hi);
         if(debug)
           {
            fprintf(lrs_ofp,"\n*R->redineq: ");
            for(i=1;i<=m;i++)
            fprintf(lrs_ofp," %ld",R->redineq[i]);
           }
         fflush(lrs_ofp);
       }
/* note that mplrs only knows the new m value for the consumer rank=0 */
/* in all other cases we resize R                                     */
      if(R->rank != 1)
        {
           if(R->redineq != NULL)
              free(R->redineq);
           R->redineq= (long int*) calloc ((m + 1), sizeof (long));
           for(i=0;i<=Q->m;i++)
              R->redineq[i]=1;
           for (i = 0; i <Q->nlinearity; i++)
              R->redineq[Q->linearity[i]] = 2L;
    
         }

      for(i=1;i<=m;i++)
        if(i<low || i>hi)
           Q->redineq[i]=0;
        else
           Q->redineq[i]=R->redineq[i];
      return;
    }   /* redundmask */

/*****************end of fel routines***************************/

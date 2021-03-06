/* ====================================================================
 * Copyright (c) 1999-2001 Carnegie Mellon University.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced 
 * Research Projects Agency and the National Science Foundation of the 
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/************************************************
 * CMU ARPA Speech Project
 *
 * Copyright (c) 2000 Carnegie Mellon University.
 * ALL RIGHTS RESERVED.
 ************************************************
 * 
 * HISTORY
 *
 * 15-Jun-2004  Yitao Sun (yitao@cs.cmu.edu) at Carnegie Mellon University
 *              Modified utt_end() to save hypothesis in the kb structure.
 *
 * 30-Dec-2000  Rita Singh (rsingh@cs.cmu.edu) at Carnegie Mellon University
 *		Added utt_decode_block() to allow block-based decoding 
 *		and decoding of piped input.
 * 
 * 30-Dec-2000  Rita Singh (rsingh@cs.cmu.edu) at Carnegie Mellon University
 *		Moved all utt_*() routines into utt.c to make them independent
 *		of main() during compilation
 * 
 * 29-Feb-2000	M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon University
 * 		Modified to allow runtime choice between 3-state and 5-state
 *              HMM topologies (instead of compile-time selection).
 * 
 * 13-Aug-1999	M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon University
 * 		Added -maxwpf.
 * 
 * 10-May-1999	M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon University
 * 		Started.
 */

#ifdef SPEC_CPU_WINDOWS
#include <direct.h>		/* RAH, added */
#endif

#include "kb.h"
#include "corpus.h"
#include "utt.h"
#include "logs3.h"

#define _CHECKUNDERFLOW_ 1

#ifdef SPEC_CPU
extern long considered;  
long tot_considered=0;
FILE *confp;
int confp_open=0;
#endif

static int32 NO_UFLOW_ADD(int32 a, int32 b)
{
  int32 c;
#ifdef _CHECKUNDERFLOW_
  c= a + b;
  c= (c>0 && a <0 && b <0) ? MAX_NEG_INT32 : c;
#else
  c= a + b;
#endif 
  return c;
}

void matchseg_write (FILE *fp, kb_t *kb, glist_t hyp, char *hdr)
{
    gnode_t *gn;
    hyp_t *h;
    int32 ascr, lscr;
    dict_t *dict;
    
    ascr = 0;
    lscr = 0;
    
    for (gn = hyp; gn; gn = gnode_next(gn)) {
	h = (hyp_t *) gnode_ptr (gn);
	ascr += h->ascr;
	lscr += h->lscr;
    }
    
    dict = kbcore_dict(kb->kbcore);
    
    fprintf (fp, "%s%s S 0 T %d A %d L %d", (hdr ? hdr : ""), kb->uttid,
	     ascr+lscr, ascr, lscr);
    
    for (gn = hyp; gn && (gnode_next(gn)); gn = gnode_next(gn)) {
	h = (hyp_t *) gnode_ptr (gn);
	fprintf (fp, " %d %d %d %s", h->sf, h->ascr, h->lscr,
		 dict_wordstr(dict, h->id));
    }
    fprintf (fp, " %d\n", kb->nfr);
    fflush (fp);
}

void match_write (FILE *fp, kb_t *kb, glist_t hyp, char *hdr)
{
    gnode_t *gn;
    hyp_t *h;
    dict_t *dict;
    int counter=0;

    dict = kbcore_dict(kb->kbcore);

    for (gn = hyp; gn && (gnode_next(gn)); gn = gnode_next(gn)) {
      h = (hyp_t *) gnode_ptr (gn);
      if((!dict_filler_word(dict,h->id)) && (h->id!=dict_finishwid(dict)))
	fprintf(fp,"%s ",dict_wordstr(dict, dict_basewid(dict,h->id)));
      counter++;
    }
    if(counter==0) fprintf(fp," ");
    fprintf (fp, "(%s)\n", kb->uttid);
    fflush (fp);
}

/*
 * Begin search at bigrams of <s>, backing off to unigrams; and fillers. 
 * Update kb->lextree_next_active with the list of active lextrees.
 */
void utt_begin (kb_t *kb)
{
    kbcore_t *kbc;
    int32 n, pred;
    
    kbc = kb->kbcore;
    
    /* Insert initial <s> into vithist structure */
    pred = vithist_utt_begin (kb->vithist, kbc);
    assert (pred == 0);	/* Vithist entry ID for <s> */
    
    /* Enter into unigram lextree[0] */
    n = lextree_n_next_active(kb->ugtree[0]);
    assert (n == 0);
    lextree_enter (kb->ugtree[0], mdef_silphone(kbc->mdef), -1, 0, pred,
		   kb->beam->hmm);
    
    /* Enter into filler lextree */
    n = lextree_n_next_active(kb->fillertree[0]);
    assert (n == 0);
    lextree_enter (kb->fillertree[0], BAD_S3CIPID, -1, 0, pred, kb->beam->hmm);
    
    kb->n_lextrans = 1;
    
    kb_lextree_active_swap (kb);
}

void utt_end (kb_t *kb)
{
    int32 id, ascr, lscr;
    glist_t hyp;
    gnode_t *gn;
    hyp_t *h;
    FILE *fp, *latfp;
    dict_t *dict;
    int32 i;
    char *hyp_strptr;
    
    fp = stdout; //SPEC we use stdout instead of stderr to prefer buffered io
    dict = kbcore_dict (kb->kbcore);
    kb_freehyps(kb);
    
    if ((id = vithist_utt_end (kb->vithist, kb->kbcore)) >= 0) {
      if (cmd_ln_str("-bptbldir")) {
	char file[8192];
	
	sprintf (file, "%s/%s.bpt", cmd_ln_str ("-bptbldir"), kb->uttid);
	if ((latfp = fopen (file, "w")) == NULL) {
	  E_ERROR("fopen(%s,w) failed; using stdout\n", file);
	  latfp = stdout;
	}
	
	vithist_dump (kb->vithist, -1, kb->kbcore, latfp);
	if (latfp != stdout)
	  fclose (latfp);
      }
      
      hyp = vithist_backtrace (kb->vithist, id);
      
      /* Detailed backtrace */
      fprintf (fp, "\nBacktrace(%s)\n", kb->uttid);
      fprintf (fp, "%6s %5s %5s %11s %8s %4s\n",
	       "LatID", "SFrm", "EFrm", "AScr", "LScr", "Type");
      
      ascr = 0;
      lscr = 0;
      
      for (gn = hyp; gn; gn = gnode_next(gn)) {
	h = (hyp_t *) gnode_ptr (gn);
	fprintf (fp, "%6d %5d %5d %11d %8d %4d %s\n",
		 h->vhid, h->sf, h->ef, h->ascr, h->lscr, h->type,
		 dict_wordstr(dict, h->id));
	
	ascr += h->ascr;
	lscr += h->lscr;
	kb->hyp_seglen++;
	if (!dict_filler_word(dict,h->id) && (h->id!=dict_finishwid(dict))) {
	  kb->hyp_strlen +=
	    strlen(dict_wordstr(dict, dict_basewid(dict, h->id))) + 1;
	}
      }
      fprintf (fp, "       %5d %5d %11d %8d (Total)\n",0,kb->nfr,ascr,lscr);

      kb->hyp_segs = ckd_calloc(kb->hyp_seglen, sizeof(hyp_t *));
      kb->hyp_str = ckd_calloc(kb->hyp_strlen+1, sizeof(char));
      hyp_strptr = kb->hyp_str;

      /* Match */
      fprintf (fp, "\nFWDVIT: ");
      i = 0;
      for (gn = hyp; gn; gn = gnode_next(gn)) {
	h = (hyp_t *) gnode_ptr (gn);
	kb->hyp_segs[i++] = h;
	if(!dict_filler_word(dict,h->id) && (h->id!=dict_finishwid(dict))) {
	  strcat(hyp_strptr, dict_wordstr(dict, dict_basewid(dict,h->id)));
	  hyp_strptr += strlen(hyp_strptr);
	  strcat(hyp_strptr, " ");
	  hyp_strptr++;
	}
      }
//SPEC The calloc above has carefully allocated an extra byte, so
//     use *that* for the \0.  The original code was not valid for the
//     case where hyp_strlen=0!  jh 15 apr 2005
//    kb->hyp_str[kb->hyp_strlen - 1] = '\0';
      kb->hyp_str[kb->hyp_strlen] = '\0';
      fprintf (fp, "'%s' (%s)\n\n", kb->hyp_str, kb->uttid);
      
      /* Matchseg */
      if (kb->matchsegfp)
	matchseg_write (kb->matchsegfp, kb, hyp, NULL);
      matchseg_write (fp, kb, hyp, "FWDXCT: ");
      fprintf (fp, "\n");

#ifdef SPEC_CPU
//How many times did we consider an active item in mgau_eval? 
//jh 16 apr 2005
      if (!confp_open) {
        if ((confp = fopen ("considered.out", "w")) == NULL) {
          E_FATAL("fopen considered.out failed\n");
          }
        confp_open=1;
      }
      fprintf (confp, "%22d considered for utterance %s\n", considered, kb->uttid );
      tot_considered += considered;
      considered = 0;
#endif

      if (kb->matchfp)
	match_write (kb->matchfp, kb, hyp, NULL);

      
      if (cmd_ln_str ("-outlatdir")) {
	char str[16384];
	int32 ispipe;
	float64 logbase;
	
	sprintf (str, "%s/%s.%s",
		 cmd_ln_str("-outlatdir"), kb->uttid, cmd_ln_str("-latext"));
	E_INFO("Writing lattice file: %s\n", str);
	
	if ((latfp = fopen_comp (str, "w", &ispipe)) == NULL)
	  E_ERROR("fopen_comp (%s,w) failed\n", str);
	else {
	  /* Write header info */
	  getcwd (str, sizeof(str));
	  fprintf (latfp, "# getcwd: %s\n", str);
	  
	  /* Print logbase first!!  Other programs look for it early in the
	   * DAG */
	  logbase = cmd_ln_float32 ("-logbase");
	  fprintf (latfp, "# -logbase %e\n", logbase);
	  
	  fprintf (latfp, "# -dict %s\n", cmd_ln_str ("-dict"));
	  if (cmd_ln_str ("-fdict"))
	    fprintf (latfp, "# -fdict %s\n", cmd_ln_str ("-fdict"));
	  fprintf (latfp, "# -lm %s\n", cmd_ln_str ("-lm"));
	  fprintf (latfp, "# -mdef %s\n", cmd_ln_str ("-mdef"));
	  fprintf (latfp, "# -mean %s\n", cmd_ln_str ("-mean"));
	  fprintf (latfp, "# -var %s\n", cmd_ln_str ("-var"));
	  fprintf (latfp, "# -mixw %s\n", cmd_ln_str ("-mixw"));
	  fprintf (latfp, "# -tmat %s\n", cmd_ln_str ("-tmat"));
	  fprintf (latfp, "#\n");
	  
	  fprintf (latfp, "Frames %d\n", kb->nfr);
	  fprintf (latfp, "#\n");
	  
	  vithist_dag_write (kb->vithist, hyp, dict,
			     cmd_ln_int32("-outlatoldfmt"), latfp);
	  fclose_comp (latfp, ispipe);
	}
      }
      
      /* free the list containing hyps (we've saved the actual hyps
       * themselves).
       */
      glist_free (hyp);
    } else
      E_ERROR("%s: No recognition\n\n", kb->uttid);
    
#ifdef SPEC_CPU
/* do not attempt to output clock dependent info - SPEC does its own timing
#
* INFO: utt.c(305):  328 frm;  2675 sen, 28593 gau/fr, Sen 7.99 CPU  8.01 Clk [Ovrhd 1.01 CPU 0.99 Clk];   2874 hmm,  10 wd/fr, 1.12 CPU 1.11 Clk (pittsburgh.bigendian)
* INFO: utt.c(305):  328 frm;  2675 sen, 28593 gau/fr, Sen 8.45 CPU 13.13 Clk [Ovrhd 1.03 CPU 1.75 Clk];   2874 hmm,  10 wd/fr, 1.12 CPU 1.84 Clk (pittsburgh.bigendian)
*                     1          2         3                 4        5               6        7            8          9        10        11           12 
*/
    E_INFO("%4d frm;  %4d sen, %5d gau/fr, Sen %4.2f CPU %4.2f Clk [Ovrhd %4.2f CPU %4.2f Clk];  %5d hmm, %3d wd/fr, %4.2f CPU %4.2f Clk (%s)\n",
	   kb->nfr,
	   (kb->utt_sen_eval + (kb->nfr >> 1)) / kb->nfr,
	   (kb->utt_gau_eval + (kb->nfr >> 1)) / kb->nfr,

	   0.0,
           0.0,
	   0.0,

           0.0,
	   (kb->utt_hmm_eval + (kb->nfr >> 1)) / kb->nfr,
	   (vithist_n_entry(kb->vithist) + (kb->nfr >> 1)) / kb->nfr,

 	   0.0,
           0.0,
	   kb->uttid);
#else
    E_INFO("%4d frm;  %4d sen, %5d gau/fr, Sen %4.2f CPU %4.2f Clk [Ovrhd %4.2f CPU %4.2f Clk];  %5d hmm, %3d wd/fr, %4.2f CPU %4.2f Clk (%s)\n",
	   kb->nfr,
	   (kb->utt_sen_eval + (kb->nfr >> 1)) / kb->nfr,
	   (kb->utt_gau_eval + (kb->nfr >> 1)) / kb->nfr,

	   kb->tm_sen.t_cpu * 100.0 / kb->nfr, 
           kb->tm_sen.t_elapsed * 100.0 / kb->nfr,
	   kb->tm_ovrhd.t_cpu * 100.0 / kb->nfr, 

           kb->tm_ovrhd.t_elapsed * 100.0 / kb->nfr,
	   (kb->utt_hmm_eval + (kb->nfr >> 1)) / kb->nfr,
	   (vithist_n_entry(kb->vithist) + (kb->nfr >> 1)) / kb->nfr,

	   kb->tm_srch.t_cpu * 100.0 / kb->nfr, 
           kb->tm_srch.t_elapsed * 100.0 / kb->nfr,
	   kb->uttid);
#endif
    {
      int32 j, k;
      
      for (j = kb->hmm_hist_bins-1; (j >= 0) && (kb->hmm_hist[j] == 0); --j);
      E_INFO("HMMHist[0..%d](%s):", j, kb->uttid);
      for (i = 0, k = 0; i <= j; i++) {
	k += kb->hmm_hist[i];
	fprintf (stdout, " %d(%d)", kb->hmm_hist[i], (k*100)/kb->nfr); //SPEC stdout not stderr: prefer buffered io
      }
      fprintf (stdout, "\n");
      //SPEC fflush (stderr);
    }
    
    kb->tot_sen_eval += kb->utt_sen_eval;
    kb->tot_gau_eval += kb->utt_gau_eval;
    kb->tot_hmm_eval += kb->utt_hmm_eval;
    kb->tot_wd_exit += vithist_n_entry(kb->vithist);
    
    ptmr_reset (&(kb->tm_sen));
    ptmr_reset (&(kb->tm_srch));
    ptmr_reset (&(kb->tm_ovrhd));

#if (!defined(SPEC_CPU))
    if (! system("ps auxgw > /dev/null 2>&1")) {
      system ("ps aguxwww | grep /live | grep -v grep");
      system ("ps aguxwww | grep /dec | grep -v grep");
    }
#endif
    
    for (i = 0; i < kb->n_lextree; i++) {
      lextree_utt_end (kb->ugtree[i], kb->kbcore);
      lextree_utt_end (kb->fillertree[i], kb->kbcore);
    }
    
    vithist_utt_reset (kb->vithist);
    
    lm_cache_stats_dump (kbcore_lm(kb->kbcore));
    lm_cache_reset (kbcore_lm(kb->kbcore));
}


void utt_word_trans (kb_t *kb, int32 cf)
{
  int32 k, th;
  vithist_t *vh;
  vithist_entry_t *ve;
  int32 vhid, le, n_ci, score;
  int32 maxpscore;
  static int32 *bs = NULL, *bv = NULL, epl;
  s3wid_t wid;
  int32 p;
  dict_t *dict;
  mdef_t *mdef;
  maxpscore=MAX_NEG_INT32;

  
  vh = kb->vithist;
  th = kb->bestscore + kb->beam->hmm;	/* Pruning threshold */
  
  if (vh->bestvh[cf] < 0)
    return;
  
  dict = kbcore_dict(kb->kbcore);
  mdef = kbcore_mdef(kb->kbcore);
  n_ci = mdef_n_ciphone(mdef);
  
  /* Initialize best exit for each distinct word-final CIphone to NONE */
  if (! bs) {
    bs = (int32 *) ckd_calloc (n_ci, sizeof(int32));
    bv = (int32 *) ckd_calloc (n_ci, sizeof(int32));
    epl = cmd_ln_int32 ("-epl");
  }
  for (p = 0; p < n_ci; p++) {
    bs[p] = MAX_NEG_INT32;
    bv[p] = -1;
  }
  
  /* Find best word exit in this frame for each distinct word-final CI phone */
  vhid = vithist_first_entry (vh, cf);
  le = vithist_n_entry (vh) - 1;
  for (; vhid <= le; vhid++) {
    ve = vithist_id2entry (vh, vhid);
    if (! vithist_entry_valid(ve))
      continue;
    
    wid = vithist_entry_wid (ve);
    p = dict_last_phone (dict, wid);
    if (mdef_is_fillerphone(mdef, p))
      p = mdef_silphone(mdef);
    
    score = vithist_entry_score (ve);
    if (score > bs[p]) {
      bs[p] = score;
      bv[p] = vhid;
      if (maxpscore < score)
	{
	  maxpscore=score;
	  /*	E_INFO("maxscore = %d\n", maxpscore); */
	}
    }
  }
  
  /* Find lextree instance to be entered */
  k = kb->n_lextrans++;
  k = (k % (kb->n_lextree * epl)) / epl;
  
  /* Transition to unigram lextrees */
  for (p = 0; p < n_ci; p++) {
    if (bv[p] >= 0)
      if (kb->wend_beam==0 || bs[p]>-kb->wend_beam + maxpscore)
	{
	  /* RAH, typecast p to (s3cipid_t) to make compiler happy */
	  lextree_enter (kb->ugtree[k], (s3cipid_t) p, cf, bs[p], bv[p], th); 
	}

  }
  
  /* Transition to filler lextrees */
  lextree_enter (kb->fillertree[k], BAD_S3CIPID, cf, vh->bestscore[cf],
		 vh->bestvh[cf], th);
}


void computePhnHeur(mdef_t* md,kb_t* kb,int32 heutype)
{
  int32 nState;
  int32 i,j;
  int32 curPhn, curFrmPhnVar; /* variables for phoneme lookahead computation */

  nState=mdef_n_emit_state(md);

  /* Initializing all the phoneme heuristics for each phone to be 0*/
  for(j=0;j==md->cd2cisen[j];j++){ 
    curPhn=md->sen2cimap[j]; /*Just to save a warning*/
    kb->phn_heur_list[curPhn]=0;
  }

  /* 20040503: ARCHAN, the code can be reduced to 10 lines, it is so
     organized such that there is no overhead in checking the
     heuristic type in the inner loop.  
  */
  /* One trick we use is to use sen2cimap to check phoneme ending boundary */


  if(heutype==1){ /* Taking Max */  
    for(i=kb->pl_window_start;i<kb->pl_window_effective;i++) {
      curPhn=0;
      curFrmPhnVar=MAX_NEG_INT32;
      for(j=0;j==md->cd2cisen[j];j++) {
	if (curFrmPhnVar<kb->cache_ci_senscr[i][j]) 
	  curFrmPhnVar=kb->cache_ci_senscr[i][j];

	curPhn=md->sen2cimap[j];
	/* Update at the phone_end boundary */
	if (curPhn!= md->sen2cimap[j+1]) { 
	  kb->phn_heur_list[curPhn]=NO_UFLOW_ADD(kb->phn_heur_list[curPhn],curFrmPhnVar);
	  curFrmPhnVar=MAX_NEG_INT32;
	}
      }
    }
  }else if(heutype==2){ 
    for(i=kb->pl_window_start;i<kb->pl_window_effective;i++) {
      curPhn=0;
      curFrmPhnVar=MAX_NEG_INT32;
      for(j=0;j==md->cd2cisen[j];j++) {
	curFrmPhnVar=NO_UFLOW_ADD(kb->cache_ci_senscr[i][j],curFrmPhnVar);
	curPhn=md->sen2cimap[j];

	/* Update at the phone_end boundary */
	if (curPhn != md->sen2cimap[j+1]) { 
	  curFrmPhnVar/=nState; /* ARCHAN: I hate to do division ! */
	  kb->phn_heur_list[curPhn]=NO_UFLOW_ADD(kb->phn_heur_list[curPhn],
						 curFrmPhnVar);
	  curFrmPhnVar=MAX_NEG_INT32;
	}
      }
    }
  }else if(heutype==3){ 
    for(i=kb->pl_window_start;i<kb->pl_window_effective;i++) {
      curPhn=0;
      curFrmPhnVar=MAX_NEG_INT32;
      for(j=0;j==md->cd2cisen[j];j++) {
	if (curPhn==0 || curPhn != md->sen2cimap[j-1]) /* dangerous hack! */
	  kb->phn_heur_list[curPhn]=NO_UFLOW_ADD(kb->phn_heur_list[curPhn],kb->cache_ci_senscr[i][j]);

	curPhn=md->sen2cimap[j];

	if (curFrmPhnVar<kb->cache_ci_senscr[i][j]) 
	  curFrmPhnVar=kb->cache_ci_senscr[i][j];
	
	/* Update at the phone_end boundary */
	if (md->sen2cimap[j] != md->sen2cimap[j+1]) { 
	  kb->phn_heur_list[curPhn]=NO_UFLOW_ADD(kb->phn_heur_list[curPhn],curFrmPhnVar);
	  curFrmPhnVar=MAX_NEG_INT32;
	}
      }
    }
  }

#if 0
  for(j=0;j==md->cd2cisen[j];j++) {
    curPhn=md->cd2cisen[j];
    E_INFO("phoneme heuristics scores at phn %d is %d\n",j,	kb->phn_heur_list[mdef->sen2cimap[j]]);
  }
#endif

}

#ifndef SPEC_CPU

/* Invoked by ctl_process in libmisc/corpus.c */
void utt_decode (void *data, char *uttfile, int32 sf, int32 ef, char *uttid)
{
  kb_t *kb;
  kbcore_t *kbcore;
  mdef_t *mdef;
  dict_t *dict;
  dict2pid_t *d2p;
  mgau_model_t *mgau;
  subvq_t *svq;
  gs_t * gs;
  lextree_t *lextree;
  int32 besthmmscr, bestwordscr, th, pth, wth, maxwpf, maxhistpf, maxhmmpf, ptranskip;
  int32 i, j, f; 
  int32 n_hmm_eval, frm_nhmm, hb, pb, wb;

  FILE *hmmdumpfp;
  float32 factor;

  int32 pheurtype;
  
  E_INFO("Processing: %s\n", uttid);

  kb = (kb_t *) data;
  kbcore = kb->kbcore;
  mdef = kbcore_mdef (kbcore);
  dict = kbcore_dict (kbcore);
  d2p = kbcore_dict2pid (kbcore);
  mgau = kbcore_mgau (kbcore);	
  svq = kbcore_svq (kbcore);
  gs = kbcore_gs(kbcore);
  kb->uttid = uttid;
  
  hmmdumpfp = cmd_ln_int32("-hmmdump") ? stderr : NULL;
  maxwpf = cmd_ln_int32 ("-maxwpf");
  maxhistpf = cmd_ln_int32 ("-maxhistpf");
  maxhmmpf = cmd_ln_int32 ("-maxhmmpf");
  ptranskip = cmd_ln_int32 ("-ptranskip");
  pheurtype = cmd_ln_int32 ("-pheurtype");

  /* Read mfc file and build feature vectors for entire utterance */
  kb->nfr = feat_s2mfc2feat(kbcore_fcb(kbcore), uttfile, cmd_ln_str("-cepdir"),
			    sf, ef, kb->feat, S3_MAX_FRAMES);

  factor = log_to_logs3_factor();    
  for (i = 0; i < kb->hmm_hist_bins; i++)
    kb->hmm_hist[i] = 0;

  utt_begin (kb);

  n_hmm_eval = 0;
  kb->utt_sen_eval = 0;
  kb->utt_gau_eval = 0;

  /* initialization of ci-phoneme look-ahead scores */
  ptmr_start (&(kb->tm_sen));

  /* effective window is the same as pl_window because we're not in live
   * mode */
  kb->pl_window_effective = kb->pl_window > kb->nfr ? kb->nfr : kb->pl_window;
  kb->pl_window_start=0;
  for(f = 0; f < kb->pl_window_effective; f++){
    /*Compute the CI phone score at here */
    kb->cache_best_list[f]=MAX_NEG_INT32;
    approx_cont_mgau_ci_eval(mgau,kb->feat[f][0],kb->cache_ci_senscr[f],kb);
    for(i=0;i==mdef->cd2cisen[i];i++){
      if(kb->cache_ci_senscr[f][i]>kb->cache_best_list[f])
	kb->cache_best_list[f]=kb->cache_ci_senscr[f][i];
    }
  }
  ptmr_stop (&(kb->tm_sen));


  fflush(stderr);
  for (f = 0; f < kb->nfr; f++) {
    /* Acoustic (senone scores) evaluation */
    ptmr_start (&(kb->tm_sen));

    /* Find active senones and composite senones, from active lextree nodes */

    /*The active senones will also be changed in approx_cont_mgau_frame_eval */
    if (kb->sen_active) {
      memset (kb->ssid_active, 0, mdef_n_sseq(mdef) * sizeof(int32));
      memset (kb->comssid_active, 0, dict2pid_n_comsseq(d2p) * sizeof(int32));
      /* Find active senone-sequence IDs (including composite ones) */
      for (i = 0; i < (kb->n_lextree <<1); i++) {
	lextree = (i < kb->n_lextree) ? kb->ugtree[i] :
	  kb->fillertree[i - kb->n_lextree];
	lextree_ssid_active (lextree, kb->ssid_active, kb->comssid_active);
      }
      
      /* Find active senones from active senone-sequences */
      memset (kb->sen_active, 0, mdef_n_sen(mdef) * sizeof(int32));
      mdef_sseq2sen_active (mdef, kb->ssid_active, kb->sen_active);
      
      /* Add in senones needed for active composite senone-sequences */
      dict2pid_comsseq2sen_active (d2p, mdef, kb->comssid_active, kb->sen_active);
    }

    /* Always use the first buffer in the cache*/
    approx_cont_mgau_frame_eval(mgau,gs,svq,kb->beam->subvq,kb->feat[f][0],kb->sen_active,kb->ascr->sen,kb->cache_ci_senscr[kb->pl_window_start],kb,f);

    kb->utt_sen_eval += mgau_frm_sen_eval(mgau);
    kb->utt_gau_eval += mgau_frm_gau_eval(mgau);
    
    /* Evaluate composite senone scores from senone scores */
    dict2pid_comsenscr (kbcore_dict2pid(kbcore), kb->ascr->sen, kb->ascr->comsen);
    ptmr_stop (&(kb->tm_sen));
    
    /* Search */
    ptmr_start (&(kb->tm_srch));

    /* Compute phoneme heuristics */
    /* Determine which set of phonemes should be active in next stage using the lookahead information*/
    /* Notice that this loop can be further optimized by implementing it incrementally*/
    /* ARCHAN and JSHERWAN Eventually, this is implemented as a function */

    if(pheurtype!=0){
      computePhnHeur(mdef,kb,pheurtype);
    }
    /* Evaluate active HMMs in each lextree; note best HMM state score */
    besthmmscr = MAX_NEG_INT32;
    bestwordscr = MAX_NEG_INT32;
    frm_nhmm = 0;
    for (i = 0; i < (kb->n_lextree <<1); i++) {
      lextree = (i < kb->n_lextree) ? kb->ugtree[i] : kb->fillertree[i - kb->n_lextree];

      if (hmmdumpfp != NULL)
	fprintf (hmmdumpfp, "Fr %d Lextree %d #HMM %d\n", f, i, lextree->n_active);
      lextree_hmm_eval (lextree, kbcore, kb->ascr, f, hmmdumpfp);

      if (besthmmscr < lextree->best)
	besthmmscr = lextree->best;
      if (bestwordscr < lextree->wbest)
	bestwordscr = lextree->wbest;
#if 0
      E_INFO("lextree->best %d\n",lextree->best);
      E_INFO("best score %d at time %d, tree %d: af compute repl.\n",besthmmscr,f,i);
#endif
      n_hmm_eval += lextree->n_active;
      frm_nhmm += lextree->n_active;
    }
    if (besthmmscr > 0) {
      E_ERROR("***ERROR*** Fr %d, best HMM score > 0 (%d); int32 wraparound?\n",
	      f, besthmmscr);
    }
    kb->hmm_hist[frm_nhmm / kb->hmm_hist_binsize]++;
    
    /* Set pruning threshold depending on whether number of active HMMs within limit */
    if (frm_nhmm > (maxhmmpf + (maxhmmpf >> 1))) {
      int32 *bin, nbin, bw;
      
      /* Use histogram pruning */
      nbin = 1000;
      bw = -(kb->beam->hmm) / nbin;
      bin = (int32 *) ckd_calloc (nbin, sizeof(int32));
      
      for (i = 0; i < (kb->n_lextree <<1); i++) {
	lextree = (i < kb->n_lextree) ?
	  kb->ugtree[i] : kb->fillertree[i - kb->n_lextree];
	
	lextree_hmm_histbin (lextree, besthmmscr, bin, nbin, bw);
      }
      
      for (i = 0, j = 0; (i < nbin) && (j < maxhmmpf); i++, j += bin[i]);
      ckd_free ((void *) bin);
      
      /* Determine hmm, phone, word beams */
      hb = -(i * bw);
      pb = (hb > kb->beam->ptrans) ? hb : kb->beam->ptrans;
      wb = (hb > kb->beam->word) ? hb : kb->beam->word;
#if 0
      E_INFO("Fr %5d, #hmm= %6d, #bin= %d, #hmm= %6d, beam= %8d, pbeam= %8d, wbeam= %8d\n",
	     f, frm_nhmm, i, j, hb, pb, wb);
#endif
    } else {
      hb = kb->beam->hmm;
      pb = kb->beam->ptrans;
      wb = kb->beam->word;
    }
    kb->bestscore = besthmmscr;
    kb->bestwordscore = bestwordscr;
    th = kb->bestscore + hb;		/* HMM survival threshold */
    pth = kb->bestscore + pb;		/* Cross-HMM transition threshold */
    wth = kb->bestwordscore + wb;		/* Word exit threshold */
    
    /*
     * For each lextree, determine if the active HMMs remain active for next
     * frame, propagate scores across HMM boundaries, and note word exits.
     */
      /* By ARCHAN 20040510, this is a tricky problem caused by the Intel compiler. 
	 The old code was something like this.

	 for(i...){
	    if ((ptranskip < 1) || ((f % ptranskip) != 0)) bla 
	    else flu 
	 }

	 In gcc, when the first condition is checked, the second
	 condition will not be checked at all.  This is a usual C
	 programmer trick. However, ICC, will invoke the code no
	 matter how if I put the two conditions into the same for
	 loop.  That's why I need to change the code structure to 
	 the following.  
	 if(ptranskip==0){
	    for(i...){
	       bla
	    }
	 }else{
	    for(i...){
	       if ((f % ptranskip) != 0) bla 
	    else flu 
	 }

	 I will guess this problem might be caused by the use of
	 sceduling. In such case, both conditional will be called into 
	 the program and will be checked. No matter what, I will consider
	 that as a bug of ICC. 
      */

    if(ptranskip==0){
      for (i = 0; i < (kb->n_lextree <<1); i++) {
	lextree = (i < kb->n_lextree) ? kb->ugtree[i] : kb->fillertree[i - kb->n_lextree];
	lextree_hmm_propagate (lextree, kbcore, kb->vithist, f, th, pth, wth,kb->phn_heur_list,kb->pl_beam,pheurtype);
      }
    }else{
      for (i = 0; i < (kb->n_lextree <<1); i++) {
	lextree = (i < kb->n_lextree) ? kb->ugtree[i] : kb->fillertree[i - kb->n_lextree];
	if ((f % ptranskip) != 0)
	  lextree_hmm_propagate (lextree, kbcore, kb->vithist, f, th, pth, wth,kb->phn_heur_list,kb->pl_beam,pheurtype);
	else						
	  lextree_hmm_propagate (lextree, kbcore, kb->vithist, f, th, wth, wth,kb->phn_heur_list,kb->pl_beam,pheurtype);
      }
    }

	
    /*Slide the window and compute the next frame of ci phone scores,
      Shift the scores in the cache by one frame and compute the new frames of CI senone score 
      if necessary and always put it at the end*/

    /*
     * Shift the window for look-ahead of one frame, update scores in the buffer. 
     */ 

    if(f<kb->nfr-kb->pl_window_effective){
      for(i=0;i<kb->pl_window_effective-1;i++){
	kb->cache_best_list[i]=kb->cache_best_list[i+1];
	for(j=0;j==mdef->cd2cisen[j];j++){
	  kb->cache_ci_senscr[i][j]=kb->cache_ci_senscr[i+1][j];
	}
      }
      
      approx_cont_mgau_ci_eval(mgau,kb->feat[f+kb->pl_window_effective][0],kb->cache_ci_senscr[kb->pl_window_effective-1],kb);
      
      kb->cache_best_list[kb->pl_window_effective-1]=MAX_NEG_INT32;
      for(i=0;i==mdef->cd2cisen[i];i++){
	if(kb->cache_ci_senscr[kb->pl_window_effective-1][i]>kb->cache_best_list[kb->pl_window_effective-1])
	  kb->cache_best_list[kb->pl_window_effective-1]=kb->cache_ci_senscr[kb->pl_window_effective-1][i];
      }
    } else {
      /* We are near the end of the block, so shrink the window from the left*/
      kb->pl_window_start++;
    }
	
    /* Limit vithist entries created this frame to specified max */
    vithist_prune (kb->vithist, dict, f, maxwpf, maxhistpf, wb);
    
    /* Cross-word transitions */
    utt_word_trans (kb, f);
    
    /* Wind up this frame */
    vithist_frame_windup (kb->vithist, f, NULL, kbcore);
    
    kb_lextree_active_swap (kb);
    
    ptmr_stop (&(kb->tm_srch));

    if ((f % 100) == 0) {
      fprintf (stderr, ".");
      fflush (stderr);
    }

  }

  
  kb->utt_hmm_eval = n_hmm_eval;
  
  utt_end (kb);
  kb->tot_fr += kb->nfr;
  
  fprintf (stdout, "\n");


}

#endif /* SPEC_CPU */

#if 1
/* ARCHAN: The speed up version of the function */


/* This function decodes a block of incoming feature vectors.
 * Feature vectors have to be computed by the calling routine.
 * The utterance level index of the last feature vector decoded
 * (before the current block) must be passed. 
 * The current status of the decode is stored in the kb structure that 
 * is passed in.
 */

void utt_decode_block (float ***block_feat,   /* Incoming block of featurevecs */
		       int32 block_nfeatvec, /* No. of vecs in cepblock */
		       int32 *curfrm,	     /* Utterance level index of
						frames decoded so far */
		       kb_t *kb,	     /* kb structure with all model
						and decoder info */
		       int32 maxwpf,	     /* Max words per frame */
		       int32 maxhistpf,	     /* Max histories per frame */
		       int32 maxhmmpf,	     /* Max active HMMs per frame */
		       int32 ptranskip,	     /* intervals at which wbeam
						is used for phone transitions */
		       FILE *hmmdumpfp)      /* dump file */
{
  kbcore_t *kbcore;
  mdef_t *mdef;
  dict_t *dict;
  dict2pid_t *d2p;
  mgau_model_t *mgau;
  subvq_t *svq;
  gs_t * gs;
  lextree_t *lextree;
  int32 besthmmscr, bestwordscr, th, pth, wth; 
  int32  i, j, t;
  int32  n_hmm_eval;
  int32 frmno; 
  int32 frm_nhmm, hb, pb, wb;
  int32 f;

  int32 pheurtype;
  pheurtype = cmd_ln_int32 ("-pheurtype");

  kbcore = kb->kbcore;
  mdef = kbcore_mdef (kbcore);
  dict = kbcore_dict (kbcore);
  d2p = kbcore_dict2pid (kbcore);
  mgau = kbcore_mgau (kbcore);
  svq = kbcore_svq (kbcore);
  gs = kbcore_gs(kbcore);
  
  frmno = *curfrm;
  
  for (i = 0; i < kb->hmm_hist_bins; i++)
    kb->hmm_hist[i] = 0;
  n_hmm_eval = 0;
  
  ptmr_start (&(kb->tm_sen));

  /* the effective window is the min of (kb->pl_window, block_nfeatvec) */
  kb->pl_window_effective = kb->pl_window > block_nfeatvec ? block_nfeatvec : kb->pl_window;
  kb->pl_window_start=0;

  for(f = 0; f < kb->pl_window_effective; f++){
    /*Compute the CI phone score at here */
    kb->cache_best_list[f]=MAX_NEG_INT32;
    approx_cont_mgau_ci_eval(mgau,block_feat[f][0],kb->cache_ci_senscr[f],kb);

    for(i=0;i==mdef->cd2cisen[i];i++){
      if(kb->cache_ci_senscr[f][i]>kb->cache_best_list[f])
	kb->cache_best_list[f]=kb->cache_ci_senscr[f][i];
    }
  }

  ptmr_stop (&(kb->tm_sen));



  for (t = 0; t < block_nfeatvec; t++,frmno++) {

    /* Acoustic (senone scores) evaluation */
    ptmr_start (&(kb->tm_sen));

    /* Find active senones and composite senones, from active lextree nodes */

    /*The active senones will also be changed in approx_cont_mgau_frame_eval */
    if (kb->sen_active) {
      memset (kb->ssid_active, 0, mdef_n_sseq(mdef) * sizeof(int32));
      memset (kb->comssid_active, 0, dict2pid_n_comsseq(d2p) * sizeof(int32));
      /* Find active senone-sequence IDs (including composite ones) */
      for (i = 0; i < (kb->n_lextree <<1); i++) {
	lextree = (i < kb->n_lextree) ? kb->ugtree[i] :
	  kb->fillertree[i - kb->n_lextree];
	lextree_ssid_active (lextree, kb->ssid_active, kb->comssid_active);
      }
      
      /* Find active senones from active senone-sequences */
      memset (kb->sen_active, 0, mdef_n_sen(mdef) * sizeof(int32));
      mdef_sseq2sen_active (mdef, kb->ssid_active, kb->sen_active);
      
      /* Add in senones needed for active composite senone-sequences */
      dict2pid_comsseq2sen_active (d2p, mdef, kb->comssid_active, kb->sen_active);
    }

    /*subvq_frame_eval (svq, mgau, kb->beam->subvq, block_feat[t], 
      kb->sen_active, kb->ascr->sen);*/
     
    /* Always use the first buffer in the cache*/
    approx_cont_mgau_frame_eval(mgau,gs,svq,kb->beam->subvq,block_feat[t][0],kb->sen_active,kb->ascr->sen,kb->cache_ci_senscr[kb->pl_window_start],kb,t);
    kb->utt_sen_eval += mgau_frm_sen_eval(mgau);
    kb->utt_gau_eval += mgau_frm_gau_eval(mgau);
    
    /* Evaluate composite senone scores from senone scores */
    dict2pid_comsenscr (kbcore_dict2pid(kbcore), kb->ascr->sen, kb->ascr->comsen);
    ptmr_stop (&(kb->tm_sen));

    /* Search */
    ptmr_start (&(kb->tm_srch));
    
    /* Compute phoneme heuristics */
    /* Determine which set of phonemes should be active in next stage using the lookahead information*/
    /* Notice that this loop can be further optimized by implementing it incrementally*/
    /* ARCHAN and JSHERWAN Eventually, this is implemented as a function */

    if(pheurtype!=0){
      computePhnHeur(mdef,kb,pheurtype);
    }

    /* Commented in live mode temporarily */
    /* Determine which set of phonemes should be active in next stage using the lookahead information*/

    /*for(i=0;i<mdef_n_ciphone(mdef);i++) {
	    kb->active_phonemes_list[i]=0;
	  }
	  for(i=0;i<kb->pl_window_effective;i++) {
	    for(j=0;j==mdef->cd2cisen[j];j++) {
	      if(kb->cache_ci_senscr[i][j]> kb->cache_best_list[i] - kb->pl_beam)
	          kb->active_phonemes_list[mdef->sen2cimap[j]]=1;
	    }
	  }*/

    /* Evaluate active HMMs in each lextree; note best HMM state score */
    besthmmscr = MAX_NEG_INT32;
    bestwordscr = MAX_NEG_INT32;
    frm_nhmm = 0;
    for (i = 0; i < (kb->n_lextree <<1); i++) {
      lextree = (i < kb->n_lextree) ? kb->ugtree[i] : 
	kb->fillertree[i - kb->n_lextree];
      
      if (hmmdumpfp != NULL)
	fprintf (hmmdumpfp, "Fr %d Lextree %d #HMM %d\n", frmno, i, 
		 lextree->n_active);
      lextree_hmm_eval (lextree, kbcore, kb->ascr, frmno, hmmdumpfp);
      
      if (besthmmscr < lextree->best)
	besthmmscr = lextree->best;
      if (bestwordscr < lextree->wbest)
	bestwordscr = lextree->wbest;
      
      n_hmm_eval += lextree->n_active;
      frm_nhmm += lextree->n_active;
    }
    if (besthmmscr > 0) {
      E_ERROR("***ERROR*** Fr %d, best HMM score > 0 (%d); int32 wraparound?\n",
	      frmno, besthmmscr);
    }
    
    kb->hmm_hist[frm_nhmm / kb->hmm_hist_binsize]++;
    
    /* Set pruning threshold depending on whether number of active HMMs 
     * is within limit 
     */
    if (frm_nhmm > (maxhmmpf + (maxhmmpf >> 1))) {
      int32 *bin, nbin, bw;
      
      /* Use histogram pruning */
      nbin = 1000;
      bw = -(kb->beam->hmm) / nbin;
      bin = (int32 *) ckd_calloc (nbin, sizeof(int32));
      
      for (i = 0; i < (kb->n_lextree <<1); i++) {
	lextree = (i < kb->n_lextree) ?
	  kb->ugtree[i] : kb->fillertree[i - kb->n_lextree];
	
	lextree_hmm_histbin (lextree, besthmmscr, bin, nbin, bw);
      }
      
      for (i = 0, j = 0; (i < nbin) && (j < maxhmmpf); i++, j += bin[i]);
      ckd_free ((void *) bin);
      
      /* Determine hmm, phone, word beams */
      hb = -(i * bw);
      pb = (hb > kb->beam->ptrans) ? hb : kb->beam->ptrans;
      wb = (hb > kb->beam->word) ? hb : kb->beam->word;
    } else {
      hb = kb->beam->hmm;
      pb = kb->beam->ptrans;
      wb = kb->beam->word;
    }
    
    kb->bestscore = besthmmscr;
    kb->bestwordscore = bestwordscr;
    th = kb->bestscore + hb;	/* HMM survival threshold */
    pth = kb->bestscore + pb;	/* Cross-HMM transition threshold */
    wth = kb->bestwordscore + wb;	/* Word exit threshold */

    /*
     * For each lextree, determine if the active HMMs remain active for next
     * frame, propagate scores across HMM boundaries, and note word exits.
     */
      
    /* Hack! Use narrow phone transition beam (wth) every few frames */
    /* ARCHAN 20040509 : please read the comment in utt_decode to see
       why this loop is implemented like this */

    if(ptranskip==0){
      for (i = 0; i < (kb->n_lextree <<1); i++) {
	lextree = (i < kb->n_lextree) ? kb->ugtree[i] : kb->fillertree[i - kb->n_lextree];
	lextree_hmm_propagate(lextree, kbcore, kb->vithist, frmno,
			      th, pth, wth,kb->phn_heur_list,kb->pl_beam,pheurtype);
      }
    }else{
      for (i = 0; i < (kb->n_lextree <<1); i++) {
	lextree = (i < kb->n_lextree) ? kb->ugtree[i] : kb->fillertree[i - kb->n_lextree];
	
	if ((frmno % ptranskip) != 0)
	  lextree_hmm_propagate(lextree, kbcore, kb->vithist, frmno,
				th, pth, wth,kb->phn_heur_list,kb->pl_beam,pheurtype);
	else
	  lextree_hmm_propagate(lextree, kbcore, kb->vithist, frmno,
				th, wth, wth,kb->phn_heur_list,kb->pl_beam,pheurtype);
      }
    }
    
	/* if the current block's current frame (t) is less than the total frames in this block minus the effective window */
	if(t<block_nfeatvec-kb->pl_window_effective){
		for(i=0;i<kb->pl_window_effective-1;i++){
			kb->cache_best_list[i]=kb->cache_best_list[i+1];
			for(j=0;j==mdef->cd2cisen[j];j++){
				kb->cache_ci_senscr[i][j]=kb->cache_ci_senscr[i+1][j];
			}
		}
		/* get the CI sen scores for the t+pl_window'th frame (a slice) */
		approx_cont_mgau_ci_eval(mgau,block_feat[t+kb->pl_window_effective][0],kb->cache_ci_senscr[kb->pl_window_effective-1],kb);
		
		kb->cache_best_list[kb->pl_window_effective-1]=MAX_NEG_INT32;
		for(i=0;i==mdef->cd2cisen[i];i++){
			if(kb->cache_ci_senscr[kb->pl_window_effective-1][i]>kb->cache_best_list[kb->pl_window_effective-1])
				kb->cache_best_list[kb->pl_window_effective-1]=kb->cache_ci_senscr[kb->pl_window_effective-1][i];
		}
    } else {
		/* We are near the end of the block, so shrink the window from the left*/
		kb->pl_window_start++;
	}

    /* Limit vithist entries created this frame to specified max */
    vithist_prune (kb->vithist, dict, frmno, maxwpf, maxhistpf, wb);
    
    /* Cross-word transitions */
    utt_word_trans (kb, frmno);
    
    /* Wind up this frame */
    vithist_frame_windup (kb->vithist, frmno, NULL, kbcore);
    
    kb_lextree_active_swap (kb);
    
    ptmr_stop (&(kb->tm_srch));
  }
  
  kb->utt_hmm_eval += n_hmm_eval;
  kb->nfr += block_nfeatvec;
  
  *curfrm = frmno;
}
#endif


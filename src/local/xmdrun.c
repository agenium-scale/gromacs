/*
 *       $Id$
 *
 *       This source code is part of
 *
 *        G   R   O   M   A   C   S
 *
 * GROningen MAchine for Chemical Simulations
 *
 *            VERSION 2.0
 * 
 * Copyright (c) 1991-1997
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 *
 * Also check out our WWW page:
 * http://rugmd0.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 *
 * And Hey:
 * S  C  A  M  O  R  G
 */
static char *SRCID_xmdrun_c = "$Id$";

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "sysstuff.h"
#include "string2.h"
#include "nrnb.h"
#include "network.h"
#include "confio.h"
#include "binio.h"
#include "copyrite.h"
#include "smalloc.h"
#include "main.h"
#include "pbc.h"
#include "force.h"
#include "macros.h"
#include "names.h"
#include "mdrun.h"
#include "fatal.h"
#include "txtdump.h"
#include "typedefs.h"
#include "update.h"
#include "random.h"
#include "vec.h"
#include "filenm.h"
#include "statutil.h"
#include "tgroup.h"
#include "vcm.h"
#include "ebin.h"
#include "mdebin.h"
#include "disre.h"
#include "dummies.h"
#include "init_sh.h"
#include "do_gct.h"
#include "physics.h"
#include "block_tx.h"
#include "rdgroup.h"
#include "glaasje.h"
#include "edsam.h"
#include "calcmu.h"
#include "ionize.h" 

static bool      bMultiSim = FALSE;
static bool      bGlas     = FALSE;
static bool      bIonize   = FALSE;
static t_commrec *cr_msim;

char *par_fn(char *base,int ftp,t_commrec *cr)
{
  static char buf[256];
  
  /* Copy to buf, and strip extension */
  strcpy(buf,base);
  buf[strlen(base)-4] = '\0';
  
  /* Add processor info */
  if (PAR(cr)) 
    sprintf(buf+strlen(buf),"-P%d",cr->pid);
  strcat(buf,".");
  
  /* Add extension again */
  strcat(buf,(ftp == efTPX) ? "tpr" : (ftp == efENX) ? "edr" : ftp2ext(ftp));
    
  return buf;
}

t_commrec *init_msim(t_commrec *cr,int nfile,t_filenm fnm[])
{
  t_commrec *cr_new;
  int  i;
  char *buf;
  
  cr_msim = cr;
  snew(cr_new,1);
  cr_new->pid    = 0;
  cr_new->nprocs = 1;
  
  /* Patch file names (except log which has been done already) */
  for(i=0; (i<nfile); i++) {
    if (fnm[i].ftp != efLOG) {
      buf = par_fn(fnm[i].fn,fnm[i].ftp,cr);
      sfree(fnm[i].fn);
      fnm[i].fn = strdup(buf);
    }
  }
  
  return cr_new;
}

real mol_dipole(int k0,int k1,atom_id ma[],rvec x[],real q[])
{
  int  k,kk,m;
  rvec mu;
  
  clear_rvec(mu);
  for(k=k0; (k<k1); k++) {
    kk = ma[k];
    for(m=0; (m<DIM); m++) 
      mu[m] += q[kk]*x[kk][m];
  }
  return norm(mu);  /* Dipole moment of this molecule in e nm */
}

real calc_mu_aver(t_commrec *cr,t_nsborder *nsb,rvec x[],real q[],rvec mu,
		  t_topology *top,t_mdatoms *md,int gnx,atom_id grpindex[])
{
  int i,start,end,m;
  real    mu_mol,mu_ave;
  t_atom  *atom;
  t_block *mols;
  
  start = START(nsb);
  end   = start + HOMENR(nsb);  
  
  atom = top->atoms.atom;
  mols = &(top->blocks[ebMOLS]);
  /*
  clear_rvec(mu);
  for(i=start; (i<end); i++)
    for(m=0; (m<DIM); m++)
      mu[m] += q[i]*x[i][m];
  if (PAR(cr)) {
    gmx_sum(DIM,mu,cr);
  }
  */
  /* I guess we have to parallelise this one! */

  if (gnx > 0) {
    mu_ave = 0.0;
    for(i=0; (i<gnx); i++) {
      int gi = grpindex[i];
      mu_ave += mol_dipole(mols->index[gi],mols->index[gi+1],mols->a,x,q);
    }
    
    return(mu_ave/gnx);
  }
  else
    return 0;
}

time_t do_md(FILE *log,t_commrec *cr,int nfile,t_filenm fnm[],
	     bool bVerbose,bool bCompact,bool bDummies,int stepout,
	     t_parm *parm,t_groups *grps,
	     t_topology *top,real ener[],
	     rvec x[],rvec vold[],rvec v[],rvec vt[],rvec f[],
	     rvec buf[],t_mdatoms *mdatoms,
	     t_nsborder *nsb,t_nrnb nrnb[],
	     t_graph *graph,t_edsamyn *edyn,
	     t_forcerec *fr,rvec box_size)
{
  t_mdebin   *mdebin;
  int        fp_ene,step,k,n,count;
  double     tcount;
  time_t     start_t;
  real       t,lambda,t0,lam0,SAfactor;
  bool       bNS,bStopCM,bStopRot,bTYZ;
  tensor     force_vir,shake_vir;
  t_nrnb     mynrnb;
  char       strbuf[256];
  char       *traj,*xtc_traj; /* normal & compressed trajectory filename */
  int        nDLB;
  int        i,m;
  rvec       vcm,mu_tot;
  t_coupl_rec *tcr;
  rvec       *xx,*vv,*ff;  
  bool       bTCR;
  real       mu_aver,mu_aver2;
  int        gnx;
  atom_id    *grpindex;
  char       *grpname;

  /* Shell stuff */
  int         nshell;
  t_shell     *shells;

  /* Initial values */
  init_md(cr,&parm->ir,&t,&t0,&lambda,&lam0,&SAfactor,&mynrnb,&bTYZ,top,
	  nfile,fnm,&traj,&xtc_traj,&fp_ene,&mdebin,grps,vcm,force_vir,shake_vir,
	  mdatoms);
  
  /* Check whether we have to do dipole stuff */
  if (ftp2bSet(efNDX,nfile,fnm))
    rd_index(ftp2fn(efNDX,nfile,fnm),1,&gnx,&grpindex,&grpname);
  else 
    gnx = 0;
  
  /* Check whether we have to GCT stuff */
  bTCR = ftp2bSet(efGCT,nfile,fnm);
  if (MASTER(cr) && bTCR)
    fprintf(stderr,"Will do General Coupling Theory!\n");
  
  /* Remove periodicity */  
  do_pbc_first(log,parm,box_size,fr,graph,x);
  
  if (!parm->ir.bUncStart) 
    do_shakefirst(log,bTYZ,lambda,ener,parm,nsb,mdatoms,x,vold,buf,f,v,
		  graph,cr,nrnb,grps,fr,top,edyn);
  
  /* Compute initial EKin for all.. */
  calc_ke_part(TRUE,0,top->atoms.nr,
               vold,v,vt,&(parm->ir.opts),
               mdatoms,grps,&mynrnb,lambda,&ener[F_DVDLKIN]);
  if (PAR(cr)) 
    global_stat(log,cr,ener,force_vir,shake_vir,
		&(parm->ir.opts),grps,&mynrnb,nrnb,vcm,mu_tot);
  clear_rvec(vcm);

  /* Calculate Temperature coupling parameters lambda */
  ener[F_TEMP]=sum_ekin(&(parm->ir.opts),grps,parm->ekin,bTYZ);
  tcoupl(parm->ir.btc,&(parm->ir.opts),grps,parm->ir.delta_t,SAfactor,
	 0,parm->ir.ntcmemory);
  where();
  
  shells = init_shells(log,START(nsb),HOMENR(nsb),&top->idef,mdatoms,&nshell);
  where();
  
  /* Write start time and temperature */
  sprintf(strbuf,"Started %s",Program());
  start_t = print_date_and_time(log,cr->pid,strbuf);
  if (MASTER(cr)) {
    fprintf(log,"Initial temperature: %g K\n",ener[F_TEMP]);
    printf("starting %s '%s'\n%d steps, %8.1f ps.\n\n",strbuf,
	   *(top->name),parm->ir.nsteps,parm->ir.nsteps*parm->ir.delta_t);
  }
  
  tcount       = 0;

  /***********************************************************
   *
   *             Loop over MD steps 
   *
   ************************************************************/
  for (step=0; (step<parm->ir.nsteps); step++) {
    /* Stop Center of Mass motion */
    /*    bStopCM=do_per_step(step,parm->ir.nstcomm); */
    /* Stop Center of Mass motion */
    if (parm->ir.nstcomm == 0) {
      bStopCM=FALSE;
      bStopRot=FALSE;
    } else if (parm->ir.nstcomm > 0) {
      bStopCM=do_per_step(step,parm->ir.nstcomm);
      bStopRot=FALSE;
    } else {
      bStopCM=FALSE;
      bStopRot=do_per_step(step,-parm->ir.nstcomm);
    }
    
    if (bIonize)
      ionize(log,mdatoms,top->atoms.atomname,t,&parm->ir,v);

    /* Determine whether or not to do Neighbour Searching */
    bNS=((parm->ir.nstlist && ((step % parm->ir.nstlist)==0)) || (step==0));

    if (bDummies) {
      /* Construct dummy particles */
      pr_rvecs(log,0,"x b4 gen dummy",x,mdatoms->nr);
      pr_rvecs(log,0,"v b4 gen dummy",v,mdatoms->nr);
      shift_self(graph,fr->shift_vec,x);
      construct_dummies(log,x,&mynrnb,parm->ir.delta_t,(step == 0) ? NULL : v,
			&top->idef);
      unshift_self(graph,fr->shift_vec,x);
      pr_rvecs(log,0,"x after gen dummy",x,mdatoms->nr);
      pr_rvecs(log,0,"v after gen dummy",v,mdatoms->nr);
    }

    /* Set values for invmass etc. */
    init_mdatoms(mdatoms,lambda,FALSE);
    
    /* Now is the time to relax the shells */
    count=relax_shells(log,cr,bVerbose,step,parm,bNS,bStopCM,top,ener,
		       x,vold,v,vt,f,buf,mdatoms,nsb,&mynrnb,graph,grps,force_vir,
		       nshell,shells,fr,traj,t,lambda,nsb->natoms,parm->box,mdebin);
    tcount+=count;
    pr_rvecs(log,0,"x after relax",x,mdatoms->nr);

    /* Calculate total dipole moment if necessary */
    calc_mu(nsb,x,mdatoms->chargeT,mu_tot);

    pr_rvec(log,0,"mu_tot 1",mu_tot,DIM);
    mu_aver=calc_mu_aver(cr,nsb,x,mdatoms->chargeA,mu_tot,top,mdatoms,gnx,
			 grpindex);
    pr_rvec(log,0,"mu_tot 2",mu_tot,DIM);
    
    if (bGlas)
      do_glas(log,START(nsb),HOMENR(nsb),x,f,fr,mdatoms,top->idef.atnr,
	      &parm->ir,ener);
	         
    if (bTCR && MASTER(cr) && (step == 0)) 
      tcr=init_coupling(log,nfile,fnm,cr,fr,mdatoms,&(top->idef));

    /* Now we have the energies and forces corresponding to the 
     * coordinates at time t. 
     * We must output all of this before the update.
     */
    t        = t0   + step*parm->ir.delta_t;
    if (parm->ir.bPert)
      lambda   = lam0 + step*parm->ir.delta_lambda;
    if (parm->ir.bSimAnn) {
      SAfactor = 1.0  - t/parm->ir.zero_temp_time;
      if (SAfactor < 0) 
	SAfactor = 0;
    }

    /* Spread the force on dummy particle to the other particles... */
    if (bDummies)
      spread_dummy_f(log,x,f,&mynrnb,&top->idef);
    pr_rvecs(log,0,"x after spread",x,mdatoms->nr);
    
    if (do_per_step(step,parm->ir.nstxout)) xx=x; else xx=NULL;
    if (do_per_step(step,parm->ir.nstvout)) vv=v; else vv=NULL;
    if (do_per_step(step,parm->ir.nstfout)) ff=f; else ff=NULL;
    write_traj(log,cr,traj,
               nsb,step,t,lambda,&mynrnb,nsb->natoms,xx,vv,ff,parm->box);
    where();
    pr_rvecs(log,0,"x after write traj",x,mdatoms->nr);

    if (do_per_step(step,parm->ir.nstxtcout)) {
      write_xtc_traj(log,cr,xtc_traj,nsb,mdatoms,
                     step,t,x,parm->box,parm->ir.xtcprec);
      where();
    }

    where();

    clear_mat(shake_vir);
    update(nsb->natoms,START(nsb),HOMENR(nsb),
	   step,lambda,&ener[F_DVDL],&(parm->ir),FALSE,
           mdatoms,x,graph,
           fr->shift_vec,f,buf,vold,v,vt,parm->pres,parm->box,
           top,grps,shake_vir,cr,&mynrnb,bTYZ,TRUE,edyn);
    pr_rvecs(log,0,"x after update",x,mdatoms->nr);
    if (PAR(cr)) 
      accumulate_u(cr,&(parm->ir.opts),grps);
 
    /* Calculate partial Kinetic Energy (for this processor) 
     * per group!
     */
    calc_ke_part(FALSE,START(nsb),HOMENR(nsb),
                 vold,v,vt,&(parm->ir.opts),
                 mdatoms,grps,&mynrnb,lambda,&ener[F_DVDLKIN]);
    where();
    if (bStopCM)
      calc_vcm(log,HOMENR(nsb),START(nsb),mdatoms->massT,v,vcm);
    
    /* Copy the partial virial to the global virial (to be summed) */
    if (PAR(cr)) {
      global_stat(log,cr,ener,force_vir,shake_vir,
                  &(parm->ir.opts),grps,&mynrnb,nrnb,vcm,mu_tot);
      if (!bNS)
        for(i=0; (i<grps->estat.nn); i++)
          grps->estat.ee[egLR][i] /= cr->nprocs;
    }
    else 
      cp_nrnb(&(nrnb[0]),&mynrnb);
    
    if (bStopCM) {
      check_cm(log,vcm,mdatoms->tmass);
      do_stopcm(log,HOMENR(nsb),START(nsb),v,vcm,mdatoms->tmass);
      inc_nrnb(&mynrnb,eNR_STOPCM,HOMENR(nsb));
    }
    pr_rvecs(log,0,"x after stopcm",x,mdatoms->nr);
    
    /* Do fit to remove overall rotation */
    if (bStopRot)
      do_stoprot(log,top->atoms.nr,box_size,x,mdatoms->massT);
    
    /* Add force and shake contribution to the virial */
    m_add(force_vir,shake_vir,parm->vir);
    
    /* Sum the potential energy terms from group contributions */
    sum_epot(&(parm->ir.opts),grps,ener); 
    
    /* Sum the kinetic energies of the groups & calc temp */
    ener[F_TEMP]=sum_ekin(&(parm->ir.opts),grps,parm->ekin,bTYZ);
    ener[F_EKIN]=trace(parm->ekin);
    ener[F_ETOT]=ener[F_EPOT]+ener[F_EKIN];
    
    /* Check for excessively large energies */
    if (fabs(ener[F_ETOT]) > 1e18) {
      fprintf(stderr,"Energy too large (%g), giving up\n",ener[F_ETOT]);
      break;
    }
#ifdef DEBUG
    fprintf(stderr,"Ekin 1: %14.10e", ener[F_EKIN]);
#endif
    /* Calculate Temperature coupling parameters lambda */
    tcoupl(parm->ir.btc,&(parm->ir.opts),grps,parm->ir.delta_t,SAfactor,
	   step,parm->ir.ntcmemory);
    
    /* Calculate pressure ! */
    calc_pres(parm->box,parm->ekin,parm->vir,parm->pres,
	      EEL_LR(fr->eeltype) ? ener[F_LR] : 0.0);

    /* Calculate long range corrections to pressure and energy */
    if (bTCR)
      set_avcsix(log,fr,&top->idef,mdatoms);
    calc_ljcorr(log,parm->ir.bLJcorr,
		fr,mdatoms->nr,parm->box,parm->pres,parm->vir,ener);
    
    if (bTCR)
      do_coupling(log,nfile,fnm,tcr,t,step,ener,fr,
		  &(parm->ir),MASTER(cr) || bMultiSim,mdatoms,&(top->idef),mu_aver,
		  top->blocks[ebMOLS].nr,bMultiSim ? cr_msim : cr,
		  parm->box,parm->vir,mu_tot,x,f);
    
    upd_mdebin(mdebin,mdatoms->tmass,step,ener,parm->box,shake_vir,
               force_vir,parm->vir,parm->pres,grps,mu_tot);
               
    where();
    if ((MASTER(cr) && do_per_step(step,parm->ir.nstprint))) {
      print_ebin(fp_ene,log,step,t,lambda,SAfactor,
		 eprNORMAL,bCompact,mdebin,grps,&(top->atoms));
    }
    if (bVerbose)
      fflush(log);
      
    if (MASTER(cr) && bVerbose && ((step % stepout)==0))
      print_time(stderr,start_t,step,&parm->ir);
  }
  
  if (MASTER(cr)) {
    if (parm->ir.nstprint > 1)
      print_ebin(fp_ene,log,step-1,t,lambda,SAfactor,
		 eprNORMAL,bCompact,mdebin,grps,&(top->atoms));
    
    print_ebin(-1,log,step,t,lambda,SAfactor,
	       eprAVER,FALSE,mdebin,grps,&(top->atoms));
    print_ebin(-1,log,step,t,lambda,SAfactor,
	       eprRMS,FALSE,mdebin,grps,&(top->atoms));
  }
  fprintf(log,"Average number of force evaluations per MD step: %.2f\n",
	  tcount/parm->ir.nsteps);

  return start_t;
}

int main(int argc,char *argv[])
{
  static char *desc[] = {
    "xmdrun is the experimental MD program. New features are tested in this",
    "program before being implemented in the default mdrun. Currently under",
    "investigation are: polarizibility, glass simulations, Free energy perturbation",
    "and parallel independent simulations."
  };

  t_commrec    *cr;
  t_filenm fnm[] = {
    { efTPX, NULL,      NULL,       ffREAD },
    { efTRN, "-o",      NULL,       ffWRITE },
    { efXTC, "-x",      NULL,       ffOPTWR },
    { efSTO, "-c",      "confout",  ffWRITE },
    { efHAT, "-hat",    "ghat",     ffOPTRD },
    { efENX, "-e",      "ener",     ffWRITE },
    { efLOG, "-g",      "md",       ffWRITE },
    { efNDX, "-n",      "mols",     ffOPTRD },
    { efGCT, "-j",      "wham",     ffOPTRD },
    { efGCT, "-jo",     "bam",      ffOPTRD },
    { efXVG, "-ffout",  "gct",      ffOPTWR },
    { efXVG, "-devout", "deviatie", ffOPTWR },
    { efXVG, "-runav",  "runaver",  ffOPTWR }
  };
#define NFILE asize(fnm)

  /* Command line options ! */
  static bool bVerbose=FALSE,bCompact=TRUE;
  static int  nprocs=1,nDLB=0,nstepout=10;
  static t_pargs pa[] = {
    { "-np",      FALSE, etINT, &nprocs,
      "Number of processors, must be the same as used for grompp. THIS SHOULD BE THE FIRST ARGUMENT ON THE COMMAND LINE FOR MPI" },
    { "-v",       FALSE, etBOOL,&bVerbose,  "Verbose mode" },
    { "-multi",   FALSE, etBOOL,&bMultiSim, "Do multiple simulations in parallel (only with -np > 1)" },
    { "-compact", FALSE, etBOOL,&bCompact,
      "Write a compact log file, i.e. do not write full virial and energy group matrix (these are also in the energy file, so this is redundant) " },
    { "-dlb",     FALSE, etINT, &nDLB,
      "Use dynamic load balancing every ... step. BUGGY do not use" },
    { "-stepout", FALSE, etINT, &nstepout,
      "Frequency of writing the remaining runtime" },
    { "-glas",    FALSE, etBOOL,&bGlas,
      "Do glass simulation with special long range corrections" },
    { "-ionize",  FALSE, etBOOL,&bIonize,
      "Do a simulation including the effect of an X-Ray bombardment on your system" }
  };

  int          i;

  t_edsamyn edyn;
  
  cr          = init_par(&argc,argv);
  bVerbose    = bVerbose && MASTER(cr);
  edyn.bEdsam = FALSE;
  
  if (MASTER(cr)) 
    CopyRight(stderr,argv[0]);
    
  parse_common_args(&argc,argv,
		    PCA_KEEP_ARGS | PCA_NOEXIT_ON_ARGS | 
		    (MASTER(cr) ? 0 : PCA_QUIET),
		    TRUE,NFILE,fnm,asize(pa),pa,asize(desc),desc,0,NULL);
		    
  open_log(ftp2fn(efLOG,NFILE,fnm),cr);

  if (bMultiSim && PAR(cr)) {
    cr = init_msim(cr,NFILE,fnm);
  }
    
  if (MASTER(cr)) {
    CopyRight(stdlog,argv[0]);
    please_cite(stdlog,"Berendsen95a");
  }

  if (opt2bSet("-ei",NFILE,fnm)) 
    ed_open(NFILE,fnm,&edyn);
    
  mdrunner(cr,NFILE,fnm,bVerbose,bCompact,nDLB,FALSE,nstepout,&edyn);
  
  exit(0);
  
  return 0;
}

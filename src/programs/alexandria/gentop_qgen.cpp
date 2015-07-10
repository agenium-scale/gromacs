/*
 * This source file is part of the Aleandria project.
 *
 * Copyright (C) 2014 David van der Spoel and Paul J. van Maaren
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
/*! \internal \brief
 * Implements part of the alexandria program.
 * \author David van der Spoel <david.vanderspoel@icm.uu.se>
 */
#include "gmxpre.h"
#include <ctype.h>
#include "gromacs/legacyheaders/macros.h"
#include "gromacs/legacyheaders/copyrite.h"
#include "gromacs/listed-forces/bonded.h"
#include "gromacs/utility/cstringutil.h"
#include "gromacs/utility/smalloc.h"
#include "gromacs/fileio/strdb.h"
#include "gromacs/fileio/confio.h"
#include "gromacs/math/units.h"
#include "gromacs/random/random.h"
#include "gromacs/legacyheaders/txtdump.h"
#include "gromacs/legacyheaders/readinp.h"
#include "gromacs/legacyheaders/names.h"
#include "gromacs/math/vec.h"
#include "gromacs/topology/atomprop.h"
#include "gromacs/gmxpreprocess/grompp.h"
#include "gromacs/linearalgebra/matrix.h"
#include "coulombintegrals/coulombintegrals.h"
#include "molprop.h"
#include "poldata.h"
#include "gentop_qgen.h"
#include "gmx_resp.h"

namespace alexandria
{



 void GentopQgen::save_params( Resp * gr)
{
    int i, j;

    if (!this->bAllocSave)
    {
        snew(this->qsave, this->natom);
        snew(this->zetasave, this->natom);
    }
    for (i = 0; (i < this->natom); i++)
    {
        if (!this->bAllocSave)
        {
            snew(this->qsave[i], this->nZeta[i]);
            snew(this->zetasave[i], this->nZeta[i]);
        }
        for (j = 0; (j < this->nZeta[i]); j++)
        {
            if (NULL != gr)
            {
                this->q[i][j]    = gr->get_q( i, j);
                this->zeta[i][j] = gr->get_zeta( i, j);
            }
            this->qsave[i][j]    = this->q[i][j];
            this->zetasave[i][j] = this->zeta[i][j];
        }
    }
    this->bAllocSave = TRUE;
}

 void GentopQgen::get_params( Resp * gr)
{
    int i, j;

    if (this->bAllocSave)
    {
        for (i = 0; (i < this->natom); i++)
        {
            for (j = 0; (j < this->nZeta[i]); j++)
            {
                this->q[i][j]    = this->qsave[i][j];
                this->zeta[i][j] = this->zetasave[i][j];
                if (NULL != gr)
                {
                    gr->set_q( i, j, this->q[i][j]);
                    gr->set_zeta( i, j, this->zeta[i][j]);
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "WARNING: no ESP charges generated.\n");
    }
}

int GentopQgen::get_nzeta( int atom)
{
    if ((0 <= atom) && (atom < this->natom))
    {
        return this->nZeta[atom];
    }
        return NOTSET;
  
}

int GentopQgen::get_row( int atom, int z)
{
    if ((0 <= atom) && (atom < this->natom) &&
        (0 <= z) && (z <= this->nZeta[atom]))
    {
        return this->row[atom][z];
    }
        return NOTSET;
  
}
  double GentopQgen::get_q(int atom, int z)
{
    if ((0 <= atom) && (atom < this->natom) &&
        (0 <= z) && (z <= this->nZeta[atom]))
    {
        return this->q[atom][z];
    }
        return NOTSET;

}


double GentopQgen::get_zeta(int atom, int z)
{
    if ((0 <= atom) && (atom < this->natom) &&
        (0 <= z) && (z <= this->nZeta[atom]))
    {
        return this->zeta[atom][z];
    }
        return NOTSET;
}

 real Coulomb_NN(real r)
{
    return 1/r;
}

 real GentopQgen::calc_jab(ChargeDistributionModel iChargeDistributionModel,
                     rvec xi, rvec xj,
                     int nZi, int nZj,
                     real *zeta_i, real *zeta_j,
                     int *rowi, int *rowj)
{
    int  i, j;
    rvec dx;
    real r;
    real eTot = 0;

    rvec_sub(xi, xj, dx);
    r = norm(dx);
    if (r == 0)
    {
        gmx_fatal(FARGS, "Zero distance between atoms!\n");
    }
    if ((*zeta_i <= 0) || (*zeta_j <= 0))
    {
        iChargeDistributionModel = eqdAXp;
    }
    switch (iChargeDistributionModel)
    {
        case eqdAXp:
            eTot = Coulomb_NN(r);
            break;
        case eqdAXs:
        case eqdRappe:
        case eqdYang:
            eTot = 0;
            for (i = nZi-1; (i < nZi); i++)
            {
                for (j = nZj-1; (j < nZj); j++)
                {
                    eTot += Coulomb_SS(r, rowi[i], rowj[j], zeta_i[i], zeta_j[j]);
                }
            }
            break;
        case eqdAXg:
            eTot = 0;
            for (i = nZi-1; (i < nZi); i++)
            {
                for (j = nZj-1; (j < nZj); j++)
                {
                    eTot += Coulomb_GG(r, zeta_i[i], zeta_j[j]);
                }
            }
            break;
        default:
            gmx_fatal(FARGS, "Unsupported model %d in calc_jab", iChargeDistributionModel);
    }

    return ONE_4PI_EPS0*(eTot)/ELECTRONVOLT;
}

void GentopQgen::solve_q_eem(FILE *fp,  real hardness_factor)
{
    double **a, qtot, q;
    int      i, j, n;

    n = this->natom+1;
    a = alloc_matrix(n, n);
    for (i = 0; (i < n-1); i++)
    {
        for (j = 0; (j < n-1); j++)
        {
            a[i][j] = this->Jab[i][j];
        }
        a[i][i] = hardness_factor*this->Jab[i][i];
    }
    for (j = 0; (j < n-1); j++)
    {
        a[n-1][j] = 1;
    }
    for (i = 0; (i < n-1); i++)
    {
        a[i][n-1] = -1;
    }
    a[n-1][n-1] = 0;

    if (matrix_invert(fp, n, a) == 0)
    {
        for (i = 0; (i < n); i++)
        {
            q = 0;
            for (j = 0; (j < n); j++)
            {
                q += a[i][j]*this->rhs[j];
            }
            this->q[i][this->nZeta[i]-1] = q;
            if (fp)
            {
                fprintf(fp, "%2d RHS = %10g Charge= %10g\n", i, this->rhs[i], q);
            }
        }
    }
    else
    {
        for (i = 0; (i < n); i++)
        {
            this->q[i][this->nZeta[i]] = 1;
        }
    }
    this->chieq = this->q[n-1][this->nZeta[n-1]-1];
    qtot        = 0;
    for (i = 0; (i < n-1); i++)
    {
        for (j = 0; (j < this->nZeta[i]); j++)
        {
            qtot += this->q[i][j];
        }
    }

    if (fp && (fabs(qtot - this->qtotal) > 1e-2))
    {
        fprintf(fp, "qtot = %g, it should be %g\n", qtot, this->qtotal);
    }
    free_matrix(a);
}

void GentopQgen::update_J00()
{
    int    i;
    double j0, qq;
    double zetaH = 1.0698;

    for (i = 0; (i < this->natom); i++)
    {
        j0 = this->j00[i]/this->epsr;
        if (((this->iChargeDistributionModel == eqdYang) ||
             (this->iChargeDistributionModel == eqdRappe)) &&
            (this->atomnr[i] == 1))
        {
            qq = this->q[i][this->nZeta[i]-1];
            j0 = (1+qq/zetaH)*j0;

            if (debug && (j0 < 0) && !this->bWarned)
            {
                fprintf(debug, "WARNING: J00 = %g for atom %d. The equations will be instable.\n", j0, i+1);
                this->bWarned = TRUE;
            }
        }
        this->Jab[i][i] = (j0 > 0) ? j0 : 0;
    }
}

void GentopQgen::debugFun(FILE *fp)
{
    int i, j;

    for (i = 0; (i < this->natom); i++)
    {
        fprintf(fp, "THIS: i: %2d chi0: %8g J0: %8g q:",
                i+1, this->chi0[i], this->Jab[i][i]);
        for (j = 0; (j < this->nZeta[i]); j++)
        {
            fprintf(fp, " %8g", this->q[i][j]);
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "qgen Jab matrix:\n");
    for (i = 0; (i < this->natom); i++)
    {
        for (j = 0; (j <= i); j++)
        {
            fprintf(fp, "  %6.2f", this->Jab[i][j]);
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "\n");
}

real GentopQgen::calc_Sij(int i, int j)
{
    real dist, dism, Sij = 1.0;
    rvec dx;
    int  l, m, tag;

    rvec_sub(this->x[i], this->x[j], dx);
    dist = norm(dx);
    if ((dist < 0.118) && (this->atomnr[i] != 1) && (this->atomnr[j] != 1))
    {
        Sij = Sij*1.64;
    }
    else if ((dist < 0.122) && (this->atomnr[i] != 1) && (this->atomnr[j] != 1))
    {
        if ((this->atomnr[i] != 8) && (this->atomnr[j] != 8))
        {
            Sij = Sij*2.23;
        }
        else
        {
            Sij = Sij*2.23;
        }
    }
    else if (dist < 0.125)
    {
        tag = 0;
        if ((this->atomnr[i] == 6) && (this->atomnr[j] == 8))
        {
            tag = i;
        }
        else if ((this->atomnr[i] == 8) && (this->atomnr[j] == 6))
        {
            tag = j;
        }
        if (tag != 0)
        {
            printf("found CO\n");
            for (l = 0; (l < this->natom); l++)
            {
                if (this->atomnr[l] == 1)
                {
                    printf("found H\n");
                    dism = 0.0;
                    for (m = 0; (m < DIM); m++)
                    {
                        dism = dism+sqr(this->x[tag][m]-this->x[l][m]);
                    }

                    printf("dist: %8.3f\n", sqrt(dism));
                    if (sqrt(dism) < 0.105)
                    {
                        printf("dist %5d %5d %5s  %5s %8.3f\n",
                               i, l, this->elem[tag], this->elem[l], sqrt(dism));
                        Sij = Sij*1.605;
                    }
                }
            }
        }
    }
    else if ((this->atomnr[i] == 6) && (this->atomnr[j] == 8))
    {
        Sij = Sij*1.03;
    }
    else if (((this->atomnr[j] == 6) && (this->atomnr[i] == 7) && (dist < 0.15)) ||
             ((this->atomnr[i] == 6) && (this->atomnr[j] == 7) && (dist < 0.15)))
    {
        if (this->atomnr[i] == 6)
        {
            tag = i;
        }
        else
        {
            tag = j;
        }
        for (l = 0; (l < this->natom); l++)
        {
            if (this->atomnr[l] == 8)
            {
                printf("found Oxy\n");
                dism = 0.0;
                for (m = 0; (m < DIM); m++)
                {
                    dism = dism+sqr(this->x[tag][m]-this->x[l][m]);
                }
                if (sqrt(dism) < 0.130)
                {
                    printf("found peptide bond\n");
                    Sij = Sij*0.66;
                }
                else
                {
                    Sij = Sij*1.1;
                }
            }
        }
    }
    return Sij;
}

void GentopQgen::calc_Jab()
{
    int    i, j;
    double Jab;

    for (i = 0; (i < this->natom); i++)
    {
        for (j = i+1; (j < this->natom); j++)
        {
            Jab = calc_jab(this->iChargeDistributionModel,
                           this->x[i], this->x[j],
                           this->nZeta[i], this->nZeta[j],
                           this->zeta[i], this->zeta[j],
                           this->row[i], this->row[j]);
            if (this->iChargeDistributionModel == eqdYang)
            {
                Jab = Jab*calc_Sij(i, j);
            }
            this->Jab[j][i] = this->Jab[i][j] = Jab/this->epsr;
        }
    }
}

void GentopQgen::calc_rhs()
{
    int    i, j, k, l;
    rvec   dx;
    real   r, j1, j1q, qcore;

    /* This right hand side is for all models */
    for (i = 0; (i < this->natom); i++)
    {
        this->rhs[i] = -this->chi0[i];
    }
    this->rhs[this->natom] = this->qtotal;

    /* In case the charge is split in nuclear charge and electronic charge
     * we need to add some more stuff. See paper for details.
     */
    for (i = 0; (i < this->natom); i++)
    {
        j1q   = 0;
        qcore = 0;
        for (k = 0; (k < this->nZeta[i]-1); k++)
        {
            j1q   += this->j00[i]*this->q[i][k];
            qcore += this->q[i][k];
        }
        j1 = 0;
        /* This assignment for k is superfluous because of the previous loop,
         * but if I take it out it will at some stage break the loop below where
         * exactly this value of k is needed.
         */
        k  = this->nZeta[i]-1;
        for (j = 0; (j < this->natom); j++)
        {
            if (i != j)
            {
                rvec_sub(this->x[i], this->x[j], dx);
                r = norm(dx);
                switch (this->iChargeDistributionModel)
                {
                    case eqdAXs:
                    case eqdRappe:
                    case eqdYang:
                        for (l = 0; (l < this->nZeta[j]-1); l++)
                        {
                            j1 += this->q[j][l]*Coulomb_SS(r, k, l, this->zeta[i][k], this->zeta[j][l]);
                        }
                        break;
                    case eqdAXg:
                        for (l = 0; (l < this->nZeta[j]-1); l++)
                        {
                            j1 += this->q[j][l]*Coulomb_GG(r, this->zeta[i][k], this->zeta[j][l]);
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        this->rhs[i]           -= j1q + ONE_4PI_EPS0*j1/ELECTRONVOLT;
        this->rhs[this->natom] -= qcore;
    }
}

int atomicnumber2rowXX(int elem)
{
    int row;

    /* Compute which row in the periodic table is this element */
    if (elem <= 2)
    {
        row = 1;
    }
    else if (elem <= 10)
    {
        row = 2;
    }
    else if (elem <= 18)
    {
        row = 3;
    }
    else if (elem <= 36)
    {
        row = 4;
    }
    else if (elem <= 54)
    {
        row = 5;
    }
    else if (elem <= 86)
    {
        row = 6;
    }
    else
    {
        row = 7;
    }

    return row;
}

GentopQgen::GentopQgen(Poldata * pd, t_atoms *atoms, gmx_atomprop_t aps,
                 rvec *x,
                 ChargeDistributionModel   iChargeDistributionModel,
                 ChargeGenerationAlgorithm iChargeGenerationAlgorithm,
                 real hfac, int qtotal, real epsr)
{
   
    char        *atp;
    gmx_bool     bSup = TRUE;
    int          i, j, k, atm, nz;

   
    this->iChargeDistributionModel   = iChargeDistributionModel;
    this->iChargeGenerationAlgorithm = iChargeGenerationAlgorithm;
    this->hfac                       = hfac;
    this->qtotal                     = qtotal;
    if (epsr <= 1)
    {
        epsr = 1;
    }
    this->epsr   = epsr;
    for (i = j = 0; (i < atoms->nr); i++)
    {
        if (atoms->atom[i].ptype == eptAtom)
        {
            this->natom++;
        }
    }
    snew(this->chi0, this->natom);
    snew(this->rhs, this->natom+1);
    snew(this->elem, this->natom);
    snew(this->atomnr, this->natom);
    snew(this->row, this->natom);
    snew(this->Jab, this->natom+1);
    snew(this->zeta, this->natom);
    snew(this->j00, this->natom);
    snew(this->q, this->natom+1);
    snew(this->x, this->natom);
    this->bAllocSave = FALSE;
    snew(this->nZeta, this->natom+1);
    /* Special case for chi_eq */
    this->nZeta[this->natom] = 1;
    snew(this->q[this->natom], this->nZeta[this->natom]);
    for (i = j = 0; (i < atoms->nr) && bSup; i++)
    {
        if (atoms->atom[i].ptype == eptAtom)
        {
            snew(this->Jab[j], this->natom+1);
            atm = atoms->atom[i].atomnumber;
            if (atm == NOTSET)
            {
                gmx_fatal(FARGS, "Don't know atomic number for %s %s",
                          *(atoms->resinfo[i].name),
                          *(atoms->atomname[j]));
            }
            atp = *atoms->atomtype[j];
            if (pd->have_eem_support(this->iChargeDistributionModel, atp, TRUE) == 0)
            {
                atp = gmx_atomprop_element(aps, atm);
                if (pd->have_eem_support(this->iChargeDistributionModel, atp, TRUE) == 0)
                {
                    fprintf(stderr, "No charge distribution support for atom %s (element %s), model %s\n",
                            *atoms->atomtype[j], atp, Poldata::get_eemtype_name(this->iChargeDistributionModel));
                    bSup = FALSE;
                }
            }
            if (bSup)
            {
                this->elem[j]   = strdup(atp);
                this->atomnr[j] = atm;
                nz              = pd->get_nzeta(this->iChargeDistributionModel, atp);
                this->nZeta[j]  = nz;
                snew(this->q[j], nz);
                snew(this->zeta[j], nz);
                snew(this->row[j], nz);
                for (k = 0; (k < nz); k++)
                {
                    this->q[j][k]    = pd->get_q(this->iChargeDistributionModel, *atoms->atomtype[j], k);
                    this->zeta[j][k] = pd->get_zeta(this->iChargeDistributionModel, *atoms->atomtype[j], k);
                    this->row[j][k]  = pd->get_row(this->iChargeDistributionModel, *atoms->atomtype[j], k);
                    if (this->row[j][k] > SLATER_MAX)
                    {
                        if (debug)
                        {
                            fprintf(debug, "Can not handle higher slaters than %d for atom %s %s\n",
                                    SLATER_MAX,
                                    *(atoms->resinfo[i].name),
                                    *(atoms->atomname[j]));
                        }
                        this->row[j][k] = SLATER_MAX;
                    }
                }
                this->chi0[j]  = 0;
                this->j00[j]   = 0;
                copy_rvec(x[i], this->x[j]);
                j++;
            }
        }
    }
    if (bSup)
    {
    }
    else
    {
      //    done();
 }
}




  GentopQgen::~GentopQgen()
{
    int  i;

    sfree(this->chi0);
    sfree(this->rhs);
    sfree(this->atomnr);
    sfree(this->j00);
    sfree(this->x);
    for (i = 0; (i < this->natom); i++)
    {
        sfree(this->row[i]);
        sfree(this->q[i]);
        sfree(this->zeta[i]);
        sfree(this->Jab[i]);
        sfree(this->elem[i]);
        if (this->bAllocSave)
        {
            sfree(this->qsave[i]);
            sfree(this->zetasave[i]);
        }
    }
    sfree(this->row);
    sfree(this->zeta);
    sfree(this->elem);
    sfree(this->q);
    sfree(this->Jab);
    sfree(this->nZeta);
    if (this->bAllocSave)
    {
        sfree(this->qsave);
        sfree(this->zetasave);
        this->bAllocSave = FALSE;
    }
}


void GentopQgen::print(FILE *fp, t_atoms *atoms)
{
    int  i, j, k, m;
    rvec mu = { 0, 0, 0 };
    real qq;

    if (this->eQGEN == eQGEN_OK)
    {
        if (fp)
        {
            fprintf(fp, "Res  Atom   Nr       J0     chi0 row        q zeta (1/nm)\n");
        }
        for (i = j = 0; (i < atoms->nr); i++)
        {
            if (atoms->atom[i].ptype == eptAtom)
            {
                qq = 0;
                for (k = 0; (k < this->nZeta[j]); k++)
                {
                    qq += this->q[j][k];
                }

                atoms->atom[i].q = qq;
                for (m = 0; (m < DIM); m++)
                {
                    mu[m] += qq* this->x[i][m] * ENM2DEBYE;
                }
                if (fp)
                {
                    fprintf(fp, "%4s %4s%5d %8g %8g",
                            *(atoms->resinfo[atoms->atom[i].resind].name),
                            *(atoms->atomname[i]), i+1, this->j00[j], this->chi0[j]);
                    for (k = 0; (k < this->nZeta[j]); k++)
                    {
                        fprintf(fp, " %3d %8.5f %8.4f", this->row[j][k], this->q[j][k],
                                this->zeta[j][k]);
                    }
                    fprintf(fp, "\n");
                }
                j++;
            }
        }
        if (fp)
        {
            fprintf(fp, "<chieq> = %10g\n|mu| = %8.3f ( %8.3f  %8.3f  %8.3f )\n",
                    this->chieq, norm(mu), mu[XX], mu[YY], mu[ZZ]);
        }
    }
}

void GentopQgen::message( int len, char buf[], Resp * gr)
{
    switch (this->eQGEN)
    {
        case eQGEN_OK:
            if (NULL != gr)
            {
                gr->calc_pot();
                gr->calc_rms();
                gr->statistics( len, buf);
            }
            else
            {
                sprintf(buf, "Charge generation finished correctly.\n");
            }
            break;
        case eQGEN_NOTCONVERGED:
            sprintf(buf, "Charge generation did not converge.\n");
            break;
        case eQGEN_NOSUPPORT:
            sprintf(buf, "No charge generation support for (some of) the atomtypes.\n");
            break;
        case eQGEN_ERROR:
        default:
            sprintf(buf, "Unknown status %d in charge generation.\n", this->eQGEN);
    }
}

 void GentopQgen::check_support(Poldata * pd, gmx_atomprop_t aps)
{
    int      i;
    gmx_bool bSup = TRUE;

    for (i = 0; (i < this->natom); i++)
    {
        if (pd->have_eem_support(this->iChargeDistributionModel, this->elem[i], TRUE) == 0)
        {
            /*sfree(this->elem[i]);*/
            this->elem[i] = strdup(gmx_atomprop_element(aps, this->atomnr[i]));
            if (pd->have_eem_support(this->iChargeDistributionModel, this->elem[i], TRUE) == 0)
            {
                fprintf(stderr, "No charge generation support for atom %s, model %s\n",
                        this->elem[i], Poldata::get_eemtype_name(this->iChargeDistributionModel));
                bSup = FALSE;
            }
        }
    }
    if (bSup)
    {
        this->eQGEN = eQGEN_OK;
    }
    else
    {
        this->eQGEN = eQGEN_NOSUPPORT;
    }
}

 void GentopQgen::update_pd(t_atoms *atoms, Poldata * pd)
{
    int i, j, n, nz;

    for (i = j = 0; (i < atoms->nr); i++)
    {
        if (atoms->atom[i].ptype == eptAtom)
        {
            this->chi0[j]  = pd->get_chi0(this->iChargeDistributionModel, this->elem[j]);
            this->j00[j]   = pd->get_j00(this->iChargeDistributionModel, this->elem[j]);
            nz             = pd->get_nzeta(this->iChargeDistributionModel, this->elem[j]);
            for (n = 0; (n < nz); n++)
            {
                this->zeta[j][n] = pd->get_zeta(this->iChargeDistributionModel,this->elem[j], n);
                this->q[j][n]    = pd->get_q(this->iChargeDistributionModel,this->elem[j], n);
                this->row[j][n]  = pd->get_row(this->iChargeDistributionModel,this->elem[j], n);
            }
            j++;
        }
    }
}

  int GentopQgen::generate_charges_sm(FILE *fp,
                        Poldata * pd,
                        t_atoms *atoms,
                        real tol, int maxiter, gmx_atomprop_t aps,
                        real *chieq)
{
    real       *qq = NULL;
    int         i, j, iter;
    real        rms;

    check_support(pd, aps);
    if (eQGEN_OK == this->eQGEN)
    {
        update_pd(atoms, pd);

        snew(qq, atoms->nr+1);
        for (i = j = 0; (i < atoms->nr); i++)
        {
            if (atoms->atom[i].ptype != eptShell)
            {
                qq[j] = this->q[j][this->nZeta[j]-1];
                j++;
            }
        }
        iter = 0;
        calc_Jab();
        calc_rhs();
        do
        {
            update_J00();
            if (debug)
            {
                debugFun(debug);
            }
            solve_q_eem(debug, 1.0);
            rms = 0;
            for (i = j = 0; (i < atoms->nr); i++)
            {
                if (atoms->atom[i].ptype != eptShell)
                {
                    rms  += sqr(qq[j] - this->q[j][this->nZeta[j]-1]);
                    qq[j] = this->q[j][this->nZeta[j]-1];
                    j++;
                }
            }
            rms = sqrt(rms/atoms->nr);
            iter++;
        }
        while ((rms > tol) && (iter < maxiter));

        if (iter < maxiter)
        {
            this->eQGEN = eQGEN_OK;
        }
        else
        {
            this->eQGEN = eQGEN_NOTCONVERGED;
        }

        if (fp)
        {
            if (this->eQGEN == eQGEN_OK)
            {
                fprintf(fp, "Converged to tolerance %g after %d iterations\n",
                        tol, iter);
            }
            else
            {
                fprintf(fp, "Did not converge within %d iterations. RMS = %g\n",
                        maxiter, rms);
            }
        }
        *chieq = this->chieq;
        sfree(qq);
    }

    if (eQGEN_OK == this->eQGEN)
    {
        print(fp, atoms);
    }

    return this->eQGEN;
}

  int GentopQgen::generate_charges_bultinck(FILE *fp,
                                     Poldata * pd, t_atoms *atoms,
                                     gmx_atomprop_t aps)
{
    check_support(pd, aps);
    if (eQGEN_OK == this->eQGEN)
    {
        update_pd(atoms, pd);

        calc_Jab();
        calc_rhs();
        update_J00();
        solve_q_eem(debug, 2.0);

        print(fp, atoms);
    }

    return this->eQGEN;
}

int GentopQgen::generate_charges(FILE *fp,
                      Resp * gr,
                     const char *molname, Poldata * pd,
                     t_atoms *atoms,
                     real tol, int maxiter, int maxcycle,
                     gmx_atomprop_t aps)
{
    int  cc, eQGEN_min = eQGEN_NOTCONVERGED;
    real chieq, chi2, chi2min = GMX_REAL_MAX;

    /* Generate charges */
    switch (this->iChargeGenerationAlgorithm)
    {
        case eqgRESP:
            if (NULL == gr)
            {
                gmx_incons("No RESP data structure");
            }
            if (fp)
            {
                fprintf(fp, "Generating %s charges for %s using RESP algorithm\n",
                        Poldata::get_eemtype_name(this->iChargeDistributionModel), molname);
            }
            for (cc = 0; (cc < maxcycle); cc++)
            {
                if (fp)
                {
                    fprintf(fp, "Cycle %d/%d\n", cc+1, maxcycle);
                }
                /* Fit charges to electrostatic potential */
                this->eQGEN = gr->optimize_charges(fp, maxiter, tol, &chi2);
                if (this->eQGEN == eQGEN_OK)
                {
                    eQGEN_min = this->eQGEN;
                    if (chi2 <= chi2min)
                    {
                        save_params(gr);
                        chi2min = chi2;
                    }

                    if (NULL != fp)
                    {
                        fprintf(fp, "chi2 = %g kJ/mol e\n", chi2);
                    }
                    print(fp, atoms);
                }
            }
            if (maxcycle > 1)
            {
                if (fp)
                {
                    fprintf(fp, "---------------------------------\nchi2 at minimum is %g\n", chi2min);
                }
                get_params(gr);
                print(fp, atoms);
            }
            this->eQGEN = eQGEN_min;
            break;
        default:
            /* Use empirical algorithms */
            if (fp)
            {
                fprintf(fp, "Generating charges for %s using %s algorithm\n",
                        molname, Poldata::get_eemtype_name(this->iChargeDistributionModel));
            }
            if (this->iChargeDistributionModel == eqdBultinck)
            {
                (void) generate_charges_bultinck(fp, pd, atoms, aps);
            }
            else
            {
                (void) generate_charges_sm(fp, pd, atoms, tol, maxiter, aps, &chieq);
            }
            save_params(gr);
    }
    return this->eQGEN;
}

}

/*============================================================================
 * Routines to handle the SLES settings
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2021 EDF S.A.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any later
  version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
  Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*----------------------------------------------------------------------------*/

#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include <bft_error.h>
#include <bft_mem.h>

#include "cs_fp_exception.h"
#include "cs_log.h"
#include "cs_multigrid.h"
#include "cs_sles.h"

#if defined(HAVE_MUMPS)
#include "cs_sles_mumps.h"
#endif

#if defined(HAVE_PETSC)
#include "cs_sles_petsc.h"
#endif

/*----------------------------------------------------------------------------
 * Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_param_sles.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*============================================================================
 * Type definitions
 *============================================================================*/

/*============================================================================
 * Local private variables
 *============================================================================*/

/*============================================================================
 * Private function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return true if a solver involving the MUMPS library is requested
 */
/*----------------------------------------------------------------------------*/

static inline bool
_mumps_is_needed(cs_param_itsol_type_t   solver)
{
  if (solver == CS_PARAM_ITSOL_MUMPS ||
      solver == CS_PARAM_ITSOL_MUMPS_LDLT ||
      solver == CS_PARAM_ITSOL_MUMPS_FLOAT ||
      solver == CS_PARAM_ITSOL_MUMPS_FLOAT_LDLT)
    return true;
  else
    return false;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return true if the prescribed solver implies a symmetric linear
 *        system
 */
/*----------------------------------------------------------------------------*/

static inline bool
_system_should_be_sym(cs_param_itsol_type_t   solver)
{
  switch (solver) {

  case CS_PARAM_ITSOL_CG:
  case CS_PARAM_ITSOL_FCG:
  case CS_PARAM_ITSOL_GKB_CG:
  case CS_PARAM_ITSOL_GKB_GMRES:
  case CS_PARAM_ITSOL_MINRES:
  case CS_PARAM_ITSOL_MUMPS_LDLT:
  case CS_PARAM_ITSOL_MUMPS_FLOAT_LDLT:
    return true;

  default:
    return false; /* Assume that the system is not symmetric */
  }
}

#if defined(HAVE_PETSC)
/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set the command line option for PETSc
 *
 * \param[in]      use_prefix    need a prefix
 * \param[in]      prefix        optional prefix
 * \param[in]      keyword       command keyword
 * \param[in]      keyval        command value
 */
/*----------------------------------------------------------------------------*/

static inline void
_petsc_cmd(bool          use_prefix,
           const char   *prefix,
           const char   *keyword,
           const char   *keyval)
{
  char  cmd_line[128];

  if (use_prefix)
    sprintf(cmd_line, "-%s_%s", prefix, keyword);
  else
    sprintf(cmd_line, "-%s", keyword);

#if PETSC_VERSION_GE(3,7,0)
  PetscOptionsSetValue(NULL, cmd_line, keyval);
#else
  PetscOptionsSetValue(cmd_line, keyval);
#endif
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Predefined settings for a block ILU(0) with PETSc
 *
 * \param[in]      prefix        prefix name associated to the current SLES
 */
/*----------------------------------------------------------------------------*/

static inline void
_petsc_bilu0_hook(const char              *prefix)
{
  /* Sanity checks */
  assert(prefix != NULL);

  _petsc_cmd(true, prefix, "pc_type", "bjacobi");
  _petsc_cmd(true, prefix, "pc_jacobi_blocks", "1");
  _petsc_cmd(true, prefix, "sub_ksp_type", "preonly");
  _petsc_cmd(true, prefix, "sub_pc_type", "ilu");
  _petsc_cmd(true, prefix, "sub_pc_factor_level", "0");
  _petsc_cmd(true, prefix, "sub_pc_factor_reuse_ordering", "");
  /* If one wants to optimize the memory consumption */
  /* _petsc_cmd(true, prefix, "sub_pc_factor_in_place", ""); */
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Predefined settings for a block ICC(0) with PETSc
 *
 * \param[in]      prefix        prefix name associated to the current SLES
 */
/*----------------------------------------------------------------------------*/

static inline void
_petsc_bicc0_hook(const char              *prefix)
{
  /* Sanity checks */
  assert(prefix != NULL);

  _petsc_cmd(true, prefix, "pc_type", "bjacobi");
  _petsc_cmd(true, prefix, "pc_jacobi_blocks", "1");
  _petsc_cmd(true, prefix, "sub_ksp_type", "preonly");
  _petsc_cmd(true, prefix, "sub_pc_type", "icc");
  _petsc_cmd(true, prefix, "sub_pc_factor_level", "0");
  _petsc_cmd(true, prefix, "sub_pc_factor_reuse_ordering", "");
  /* If one wants to optimize the memory consumption */
  /* _petsc_cmd(true, prefix, "sub_pc_factor_in_place", ""); */
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Predefined settings for a block SSOR with PETSc
 *
 * \param[in]      prefix        prefix name associated to the current SLES
 */
/*----------------------------------------------------------------------------*/

static inline void
_petsc_bssor_hook(const char              *prefix)
{
  /* Sanity checks */
  assert(prefix != NULL);

  _petsc_cmd(true, prefix, "pc_type", "bjacobi");
  _petsc_cmd(true, prefix, "pc_jacobi_blocks", "1");
  _petsc_cmd(true, prefix, "sub_ksp_type", "preonly");
  _petsc_cmd(true, prefix, "sub_pc_type", "sor");
  _petsc_cmd(true, prefix, "sub_pc_sor_symmetric", "");
  _petsc_cmd(true, prefix, "sub_pc_sor_local_symmetric", "");
  _petsc_cmd(true, prefix, "sub_pc_sor_omega", "1.5");
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Predefined settings for GAMG as a preconditioner even if another
 *        settings have been defined. One assumes that one really wants to use
 *        GAMG (may be HYPRE is not available)
 *
 * \param[in]      prefix        prefix name associated to the current SLES
 * \param[in]      slesp         pointer to a set of SLES parameters
 * \param[in]      is_symm       the linear system to solve is symmetric
 * \param[in, out] pc            pointer to a PETSc preconditioner
 */
/*----------------------------------------------------------------------------*/

static inline void
_petsc_pcgamg_hook(const char              *prefix,
                   const cs_param_sles_t   *slesp,
                   bool                     is_symm,
                   PC                       pc)
{
  /* Sanity checks */
  assert(prefix != NULL);
  assert(slesp != NULL);
  assert(slesp->precond == CS_PARAM_PRECOND_AMG);

  /* Remark: -pc_gamg_reuse_interpolation
   *
   * Reuse prolongation when rebuilding algebraic multigrid
   * preconditioner. This may negatively affect the convergence rate of the
   * method on new matrices if the matrix entries change a great deal, but
   * allows rebuilding the preconditioner quicker. (default=false)
   */

  _petsc_cmd(true, prefix, "pc_gamg_reuse_interpolation", "true");

  /* Remark: -pc_gamg_sym_graph
   * Symmetrize the graph before computing the aggregation. Some algorithms
   * require the graph be symmetric (default=false)
   */

  _petsc_cmd(true, prefix, "pc_gamg_sym_graph", "true");

  /* Set smoothers (general settings, i.e. not depending on the symmetry or not
     of the linear system to solve) */

  _petsc_cmd(true, prefix, "mg_levels_ksp_type", "richardson");
  _petsc_cmd(true, prefix, "mg_levels_ksp_max_it", "1");
  _petsc_cmd(true, prefix, "mg_levels_ksp_norm_type", "none");
  _petsc_cmd(true, prefix, "mg_levels_ksp_richardson_scale", "1.0");

  /* Do not build a coarser level if one reaches the following limit */
  _petsc_cmd(true, prefix, "pc_gamg_coarse_eq_limit", "100");

  /* In parallel computing, migrate data to another rank if the grid has less
     than 200 rows */
  if (cs_glob_n_ranks > 1) {

    _petsc_cmd(true, prefix, "pc_gamg_repartition", "true");
    _petsc_cmd(true, prefix, "pc_gamg_process_eq_limit", "200");

  }
  else {

    _petsc_cmd(true, prefix, "mg_coarse_ksp_type", "preonly");
    _petsc_cmd(true, prefix, "mg_coarse_pc_type", "tfs");

  }

  /* Settings depending on the symmetry or not of the linear system to solve */

  if (is_symm) {

    /* Remark: -pc_gamg_square_graph
     *
     * Squaring the graph increases the rate of coarsening (aggressive
     * coarsening) and thereby reduces the complexity of the coarse grids, and
     * generally results in slower solver converge rates. Reducing coarse grid
     * complexity reduced the complexity of Galerkin coarse grid construction
     * considerably. (default = 1)
     *
     * Remark: -pc_gamg_threshold
     *
     * Increasing the threshold decreases the rate of coarsening. Conversely
     * reducing the threshold increases the rate of coarsening (aggressive
     * coarsening) and thereby reduces the complexity of the coarse grids, and
     * generally results in slower solver converge rates. Reducing coarse grid
     * complexity reduced the complexity of Galerkin coarse grid construction
     * considerably. Before coarsening or aggregating the graph, GAMG removes
     * small values from the graph with this threshold, and thus reducing the
     * coupling in the graph and a different (perhaps better) coarser set of
     * points. (default=0.0) */

    _petsc_cmd(true, prefix, "pc_gamg_agg_nsmooths", "2");
    _petsc_cmd(true, prefix, "pc_gamg_square_graph", "2");
    _petsc_cmd(true, prefix, "pc_gamg_threshold", "0.08");

    if (cs_glob_n_ranks > 1) {

      _petsc_cmd(true, prefix, "mg_levels_pc_type", "bjacobi");
      _petsc_cmd(true, prefix, "mg_levels_pc_jacobi_blocks", "1");
      _petsc_cmd(true, prefix, "mg_levels_sub_ksp_type", "preonly");
      _petsc_cmd(true, prefix, "mg_levels_sub_pc_type", "sor");
      _petsc_cmd(true, prefix, "mg_levels_sub_pc_sor_local_symmetric", "");
      _petsc_cmd(true, prefix, "mg_levels_sub_pc_sor_omega", "1.5");

    }
    else { /* serial run */

      _petsc_cmd(true, prefix, "mg_levels_pc_type", "sor");
      _petsc_cmd(true, prefix, "mg_levels_pc_sor_local_symmetric", "");
      _petsc_cmd(true, prefix, "mg_levels_pc_sor_omega", "1.5");

    }

  }
  else { /* Not a symmetric linear system */

    /* Number of smoothing steps to use with smooth aggregation (default=1) */
    _petsc_cmd(true, prefix, "pc_gamg_agg_nsmooths", "0");
    _petsc_cmd(true, prefix, "pc_gamg_square_graph", "0");
    _petsc_cmd(true, prefix, "pc_gamg_threshold", "0.06");

    _petsc_cmd(true, prefix, "mg_levels_pc_type", "bjacobi");
    _petsc_cmd(true, prefix, "mg_levels_pc_bjacobi_blocks", "1");
    _petsc_cmd(true, prefix, "mg_levels_sub_ksp_type", "preonly");
    _petsc_cmd(true, prefix, "mg_levels_sub_pc_type", "ilu");
    _petsc_cmd(true, prefix, "mg_levels_sub_pc_factor_levels", "0");

  }

  /* After command line options, switch to PETSc setup functions */

  PCSetType(pc, PCGAMG);
  PCGAMGSetType(pc, PCGAMGAGG);
  PCGAMGSetNSmooths(pc, 1);
  PCSetUp(pc);

  switch (slesp->amg_type) {

  case CS_PARAM_AMG_PETSC_GAMG_V:
  case CS_PARAM_AMG_PETSC_PCMG:
  case CS_PARAM_AMG_HYPRE_BOOMER_V:
    PCMGSetCycleType(pc, PC_MG_CYCLE_V);
    break;

  case CS_PARAM_AMG_PETSC_GAMG_W:
  case CS_PARAM_AMG_HYPRE_BOOMER_W:
    PCMGSetCycleType(pc, PC_MG_CYCLE_W);
    break;

  default:
    bft_error(__FILE__, __LINE__, 0, "%s: Invalid type of AMG for SLES %s\n",
              __func__, slesp->name);
  }

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Predefined settings for BoomerAMG in HYPRE as a preconditioner
 *
 * \param[in]      prefix        prefix name associated to the current SLES
 * \param[in]      slesp         pointer to a set of SLES parameters
 * \param[in]      is_symm       the linear system to solve is symmetric
 * \param[in, out] pc            pointer to a PETSc preconditioner
 */
/*----------------------------------------------------------------------------*/

static inline void
_petsc_pchypre_hook(const char              *prefix,
                    const cs_param_sles_t   *slesp,
                    bool                     is_symm,
                    PC                       pc)
{
  /* Sanity checks */
  assert(prefix != NULL);
  assert(slesp != NULL);
  assert(slesp->precond == CS_PARAM_PRECOND_AMG);

  PCSetType(pc, PCHYPRE);
  PCHYPRESetType(pc, "boomeramg");

  switch (slesp->amg_type) {

  case CS_PARAM_AMG_HYPRE_BOOMER_V:
    _petsc_cmd(true, prefix, "pc_hypre_boomeramg_cycle_type", "V");
    break;

  case CS_PARAM_AMG_HYPRE_BOOMER_W:
    _petsc_cmd(true, prefix, "pc_hypre_boomeramg_cycle_type", "W");
    break;

  default:
    bft_error(__FILE__, __LINE__, 0, "%s: Invalid type of AMG for SLES %s\n",
              __func__, slesp->name);
  }

  /* From HYPRE documentation: https://hypre.readthedocs.io/en/lastest
   *
   * for three-dimensional diffusion problems, it is recommended to choose a
   * lower complexity coarsening like HMIS or PMIS (coarsening 10 or 8) and
   * combine it with a distance-two interpolation (interpolation 6 or 7), that
   * is also truncated to 4 or 5 elements per row. Additional reduction in
   * complexity and increased scalability can often be achieved using one or
   * two levels of aggressive coarsening.
   */

  /* _petsc_cmd(true, prefix, "pc_hypre_boomeramg_grid_sweeps_down","2"); */
  /* _petsc_cmd(true, prefix, "pc_hypre_boomeramg_grid_sweeps_up","2"); */
  /* _petsc_cmd(true, prefix, "pc_hypre_boomeramg_smooth_type","Euclid"); */

  /* Remark: fcf-jacobi or l1scaled-jacobi (or chebyshev) as up/down smoothers
     can be a good choice
  */

  /*
    _petsc_cmd(true, prefix, "pc_hypre_boomeramg_relax_type_down","fcf-jacobi");
    _petsc_cmd(true, prefix, "pc_hypre_boomeramg_relax_type_up","fcf-jacobi");
  */

  /* Note that the default coarsening is HMIS in HYPRE */

  _petsc_cmd(true, prefix, "pc_hypre_boomeramg_coarsen_type", "HMIS");

  /* Note that the default interpolation is extended+i interpolation truncated
   * to 4 elements per row. Using 0 means there is no limitation.
   * good choices are: ext+i-cc, ext+i, FF1
   */

  _petsc_cmd(true, prefix, "pc_hypre_boomeramg_interp_type", "ext+i-cc");
  _petsc_cmd(true, prefix, "pc_hypre_boomeramg_P_max","8");

  /* Number of levels (starting from the finest one) on which one applies an
     aggressive coarsening */

  _petsc_cmd(true, prefix, "pc_hypre_boomeramg_agg_nl","2");

  /* Number of paths for aggressive coarsening (default = 1) */

  _petsc_cmd(true, prefix, "pc_hypre_boomeramg_agg_num_paths","2");

  /* For best performance, it might be necessary to set certain parameters,
   * which will affect both coarsening and interpolation. One important
   * parameter is the strong threshold.  The default value is 0.25, which
   * appears to be a good choice for 2-dimensional problems and the low
   * complexity coarsening algorithms. For 3-dimensional problems a better
   * choice appears to be 0.5, when using the default coarsening
   * algorithm. However, the choice of the strength threshold is problem
   * dependent.
   */

  _petsc_cmd(true, prefix, "pc_hypre_boomeramg_strong_threshold","0.5");
  _petsc_cmd(true, prefix, "pc_hypre_boomeramg_no_CF","");
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set command line options for PC according to the kind of
 *        preconditionner
 *
 * \param[in, out] slesp    set of parameters for the linear algebra
 * \param[in, out] ksp      PETSc solver structure
 */
/*----------------------------------------------------------------------------*/

static void
_petsc_set_pc_type(cs_param_sles_t   *slesp,
                   KSP                ksp)
{
  if (_mumps_is_needed(slesp->solver))
    return; /* Direct solver: Nothing to do at this stage */

  PC  pc;
  KSPGetPC(ksp, &pc);

  switch (slesp->precond) {

  case CS_PARAM_PRECOND_NONE:
    PCSetType(pc, PCNONE);
    break;

  case CS_PARAM_PRECOND_DIAG:
    PCSetType(pc, PCJACOBI);
    break;

  case CS_PARAM_PRECOND_BJACOB_ILU0:
    if (slesp->solver_class == CS_PARAM_SLES_CLASS_HYPRE) {
#if defined(PETSC_HAVE_HYPRE)
      PCSetType(pc, PCHYPRE);
      PCHYPRESetType(pc, "euclid");
      _petsc_cmd(true, slesp->name, "pc_euclid_level", "0");
#else
      _petsc_bilu0_hook(slesp->name);
#endif
    }
    else
      _petsc_bilu0_hook(slesp->name);
    break;

  case CS_PARAM_PRECOND_BJACOB_SGS:
    _petsc_bssor_hook(slesp->name);
    break;

  case CS_PARAM_PRECOND_SSOR:
    if (cs_glob_n_ranks > 1) {  /* Switch to a block version */

      slesp->precond = CS_PARAM_PRECOND_BJACOB_SGS;
      cs_base_warn(__FILE__, __LINE__);
      cs_log_printf(CS_LOG_DEFAULT,
                    " %s: System %s: Modify the requested preconditioner to"
                    " enable a parallel computation with PETSC.\n"
                    " Switch to a block jacobi preconditioner.\n",
                    __func__, slesp->name);

      _petsc_bssor_hook(slesp->name);

    }
    else { /* Serial computation */
      PCSetType(pc, PCSOR);
      PCSORSetSymmetric(pc, SOR_SYMMETRIC_SWEEP);
    }
    break;

  case CS_PARAM_PRECOND_ICC0:
    if (cs_glob_n_ranks > 1) { /* Switch to a block version */

      cs_base_warn(__FILE__, __LINE__);
      cs_log_printf(CS_LOG_DEFAULT,
                    " %s: System %s: Modify the requested preconditioner to"
                    " enable a parallel computation with PETSC.\n"
                    " Switch to a block jacobi preconditioner.\n",
                    __func__, slesp->name);

      _petsc_bicc0_hook(slesp->name);

    }
    else {
      PCSetType(pc, PCICC);
      PCFactorSetLevels(pc, 0);
    }
    break;

  case CS_PARAM_PRECOND_ILU0:
    if (slesp->solver_class == CS_PARAM_SLES_CLASS_HYPRE) {
#if defined(PETSC_HAVE_HYPRE)
      /* Euclid is a parallel version of the ILU(0) factorisation */
      PCSetType(pc, PCHYPRE);
      PCHYPRESetType(pc, "euclid");
      _petsc_cmd(true, slesp->name, "pc_euclid_level", "0");
#else
      _petsc_bilu0_hook(slesp->name);
      if (cs_glob_n_ranks > 1)  /* Switch to a block version */
        slesp->precond = CS_PARAM_PRECOND_BJACOB_ILU0;
#endif
    }
    else {
      _petsc_bilu0_hook(slesp->name);
      if (cs_glob_n_ranks > 1) { /* Switch to a block version */

        slesp->precond = CS_PARAM_PRECOND_BJACOB_ILU0;
        cs_base_warn(__FILE__, __LINE__);
        cs_log_printf(CS_LOG_DEFAULT,
                      " %s: System %s: Modify the requested preconditioner to"
                      " enable a parallel computation with PETSC.\n"
                      " Switch to a block jacobi preconditioner.\n",
                      __func__, slesp->name);
      }
    }
    break;

  case CS_PARAM_PRECOND_LU:
#if defined(PETSC_HAVE_MUMPS)
    _petsc_cmd(true, slesp->name, "pc_type", "lu");
    _petsc_cmd(true, slesp->name, "pc_factor_mat_solver_type", "mumps");
#else
    if (cs_glob_n_ranks == 1)
      _petsc_cmd(true, slesp->name, "pc_type", "lu");
    else { /* Switch to a block version (sequential in each block) */
      _petsc_cmd(true, slesp->name, "pc_type", "bjacobi");
      _petsc_cmd(true, slesp->name, "pc_jacobi_blocks", "1");
      _petsc_cmd(true, slesp->name, "sub_ksp_type", "preonly");
      _petsc_cmd(true, slesp->name, "sub_pc_type", "lu");
    }
#endif
    break;

  case CS_PARAM_PRECOND_AMG:
    {
      bool  is_symm = _system_should_be_sym(slesp->solver);

      switch (slesp->amg_type) {

      case CS_PARAM_AMG_PETSC_GAMG_V:
      case CS_PARAM_AMG_PETSC_GAMG_W:
      case CS_PARAM_AMG_PETSC_PCMG:
        _petsc_pcgamg_hook(slesp->name, slesp, is_symm, pc);
        break;

      case CS_PARAM_AMG_HYPRE_BOOMER_V:
      case CS_PARAM_AMG_HYPRE_BOOMER_W:
#if defined(PETSC_HAVE_HYPRE)
        _petsc_pchypre_hook(slesp->name, slesp, is_symm, pc);
#else
        cs_base_warn(__FILE__, __LINE__);
        cs_log_printf(CS_LOG_DEFAULT,
                      "%s: Eq. %s: Switch to GAMG since BoomerAMG is not"
                      " available.\n",
                      __func__, slesp->name);
        _petsc_pcgamg_hook(slesp->name, slesp, is_symm, pc);
#endif
        break;

      default:
        bft_error(__FILE__, __LINE__, 0,
                  " %s: Eq. %s: Invalid AMG type for the PETSc library.",
                  __func__, slesp->name);
        break;

      } /* End of switch on the AMG type */

    } /* AMG as preconditioner */
    break;

  default:
    bft_error(__FILE__, __LINE__, 0,
              " %s: Eq. %s: Preconditioner not interfaced with PETSc.",
              __func__, slesp->name);
  }

  /* Apply modifications to the PC structure given with command lines.
   * This setting stands for a first setting and may be overwritten with
   * parameters stored in the structure cs_param_sles_t
   * To get the last word use cs_user_sles_petsc_hook()  */
  PCSetFromOptions(pc);
  PCSetUp(pc);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set PETSc solver
 *
 * \param[in]      slesp    pointer to SLES parameters
 * \param[in, out] ksp      pointer to PETSc KSP context
 */
/*----------------------------------------------------------------------------*/

static void
_petsc_set_krylov_solver(cs_param_sles_t    *slesp,
                         KSP                 ksp)
{
  /* No choice otherwise PETSc yields an error */
  slesp->resnorm_type = CS_PARAM_RESNORM_NORM2_RHS;
  KSPSetNormType(ksp, KSP_NORM_UNPRECONDITIONED);

  /* 2) Set the krylov solver */
  switch (slesp->solver) {

  case CS_PARAM_ITSOL_NONE:
    KSPSetType(ksp, KSPPREONLY);
    break;

  case CS_PARAM_ITSOL_BICG:      /* Improved Bi-CG stab */
    KSPSetType(ksp, KSPIBCGS);
    break;

  case CS_PARAM_ITSOL_BICGSTAB2: /* Preconditioned BiCGstab2 */
    KSPSetType(ksp, KSPBCGSL);
    break;

  case CS_PARAM_ITSOL_CG:        /* Preconditioned Conjugate Gradient */
    if (slesp->precond == CS_PARAM_PRECOND_AMG)
      KSPSetType(ksp, KSPFCG);
    else
      KSPSetType(ksp, KSPCG);
    break;

  case CS_PARAM_ITSOL_FCG:       /* Flexible Conjuguate Gradient */
    KSPSetType(ksp, KSPFCG);
    break;

  case CS_PARAM_ITSOL_FGMRES:    /* Preconditioned flexible GMRES */
    KSPSetType(ksp, KSPFGMRES);
    break;

  case CS_PARAM_ITSOL_GCR:       /* Generalized Conjuguate Residual */
    KSPSetType(ksp, KSPGCR);
    break;

  case CS_PARAM_ITSOL_GMRES:     /* Preconditioned GMRES */
    KSPSetType(ksp, KSPLGMRES);
    break;

  case CS_PARAM_ITSOL_MINRES:    /* Minimal residual */
    KSPSetType(ksp, KSPMINRES);
    break;

  case CS_PARAM_ITSOL_MUMPS:     /* Direct solver (factorization) */
  case CS_PARAM_ITSOL_MUMPS_LDLT:
#if defined(PETSC_HAVE_MUMPS)
    KSPSetType(ksp, KSPPREONLY);
#else
    bft_error(__FILE__, __LINE__, 0,
              " %s: MUMPS not interfaced with this installation of PETSc.",
              __func__);
#endif
    break;

  default:
    bft_error(__FILE__, __LINE__, 0,
              " %s: Iterative solver not interfaced with PETSc.", __func__);
  }

  /* 3) Additional settings arising from command lines */
  switch (slesp->solver) {

  case CS_PARAM_ITSOL_GMRES: /* Preconditioned GMRES */
    _petsc_cmd(true, slesp->name, "ksp_gmres_modifiedgramschmidt", "1");
    break;

  default:
    break; /* Nothing to do. Settings performed with another mechanism */

  }

  /* Apply modifications to the KSP structure given with command lines.
   * This setting stands for a first setting and may be overwritten with
   * parameters stored in the structure cs_param_sles_t
   *
   * Automatic monitoring
   *  PetscOptionsSetValue(NULL, "-ksp_monitor", "");
   *
   */

  KSPSetFromOptions(ksp);

  /* Apply settings from the cs_param_sles_t structure */
  switch (slesp->solver) {

  case CS_PARAM_ITSOL_GMRES: /* Preconditioned GMRES */
  case CS_PARAM_ITSOL_FGMRES: /* Flexible GMRES */
    KSPGMRESSetRestart(ksp, slesp->restart);
    break;

  case CS_PARAM_ITSOL_GCR: /* Preconditioned GCR */
    KSPGCRSetRestart(ksp, slesp->restart);
    break;

#if defined(PETSC_HAVE_MUMPS)
  case CS_PARAM_ITSOL_MUMPS:
    {
      PC  pc;
      KSPGetPC(ksp, &pc);
      PCSetType(pc, PCLU);
      PCFactorSetMatSolverType(pc, MATSOLVERMUMPS);
    }
    break;

  case CS_PARAM_ITSOL_MUMPS_LDLT:
    {
      PC  pc;
      KSPGetPC(ksp, &pc);

      /* Retrieve the matrices related to this KSP */
      Mat a, pa;
      KSPGetOperators(ksp, &a, &pa);

      MatSetOption(a, MAT_SPD, PETSC_TRUE); /* set MUMPS id%SYM=1 */
      PCSetType(pc, PCCHOLESKY);

      PCFactorSetMatSolverType(pc, MATSOLVERMUMPS);
      PCFactorSetUpMatSolverType(pc); /* call MatGetFactor() to create F */
    }
    break;
#endif

  default:
    break; /* Nothing else to do */
  }

  /* Set KSP tolerances */
  PetscReal rtol, abstol, dtol;
  PetscInt  maxit;
  KSPGetTolerances(ksp, &rtol, &abstol, &dtol, &maxit);
  KSPSetTolerances(ksp,
                   slesp->eps,          /* relative convergence tolerance */
                   abstol,              /* absolute convergence tolerance */
                   dtol,                /* divergence tolerance */
                   slesp->n_max_iter);  /* max number of iterations */

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set PETSc solver and preconditioner
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 */
/*----------------------------------------------------------------------------*/

static void
_petsc_setup_hook(void   *context,
                  KSP     ksp)
{
  cs_param_sles_t  *slesp = (cs_param_sles_t  *)context;

  cs_fp_exception_disable_trap(); /* Avoid trouble with a too restrictive
                                     SIGFPE detection */

  int len = strlen(slesp->name) + 1;
  char  *prefix = NULL;
  BFT_MALLOC(prefix, len + 1, char);
  sprintf(prefix, "%s_", slesp->name);
  prefix[len] = '\0';
  KSPSetOptionsPrefix(ksp, prefix);
  BFT_FREE(prefix);

  /* 1) Set the solver */
  _petsc_set_krylov_solver(slesp, ksp);

  /* 2) Set the preconditioner */
  _petsc_set_pc_type(slesp, ksp);

  /* 3) User function for additional settings */
  cs_user_sles_petsc_hook((void *)slesp, ksp);

  /* Dump the setup related to PETSc in a specific file */
  if (!slesp->setup_done) {
    KSPSetUp(ksp);
    cs_sles_petsc_log_setup(ksp);
    slesp->setup_done = true;
  }

  cs_fp_exception_restore_trap(); /* Avoid trouble with a too restrictive
                                     SIGFPE detection */
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Common settings for block preconditioning (when a system is split
 *         according to the Cartesian components: x,y,z)
 *
 * \param[in]      slesp       pointer to the SLES parameter settings
 * \param[in, out] ksp         pointer to PETSc KSP structure (solver)
 */
/*----------------------------------------------------------------------------*/

static void
_petsc_common_block_hook(const cs_param_sles_t    *slesp,
                         KSP                       ksp)
{
  PC  pc;
  KSPGetPC(ksp, &pc);
  PCSetType(pc, PCFIELDSPLIT);

  switch (slesp->pcd_block_type) {
  case CS_PARAM_PRECOND_BLOCK_UPPER_TRIANGULAR:
  case CS_PARAM_PRECOND_BLOCK_LOWER_TRIANGULAR:
  case CS_PARAM_PRECOND_BLOCK_FULL_UPPER_TRIANGULAR:
  case CS_PARAM_PRECOND_BLOCK_FULL_LOWER_TRIANGULAR:
    PCFieldSplitSetType(pc, PC_COMPOSITE_MULTIPLICATIVE);
    break;

  case CS_PARAM_PRECOND_BLOCK_SYM_GAUSS_SEIDEL:
  case CS_PARAM_PRECOND_BLOCK_FULL_SYM_GAUSS_SEIDEL:
    PCFieldSplitSetType(pc, PC_COMPOSITE_SYMMETRIC_MULTIPLICATIVE);
    break;

  case CS_PARAM_PRECOND_BLOCK_DIAG:
  case CS_PARAM_PRECOND_BLOCK_FULL_DIAG:
  default:
    PCFieldSplitSetType(pc, PC_COMPOSITE_ADDITIVE);
    break;
  }

  /* Apply modifications to the KSP structure */
  PCFieldSplitSetBlockSize(pc, 3);

  PetscInt  id = 0;
  PCFieldSplitSetFields(pc, "x", 1, &id, &id);
  id = 1;
  PCFieldSplitSetFields(pc, "y", 1, &id, &id);
  id = 2;
  PCFieldSplitSetFields(pc, "z", 1, &id, &id);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Function pointer: setup hook for setting PETSc solver and
 *         preconditioner.
 *         Case of multiplicative AMG block preconditioner for a CG with GAMG
 *         as AMG type
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 */
/*----------------------------------------------------------------------------*/

static void
_petsc_amg_block_gamg_hook(void     *context,
                           KSP       ksp)
{
  cs_param_sles_t  *slesp = (cs_param_sles_t *)context;

  cs_fp_exception_disable_trap(); /* Avoid trouble with a too restrictive
                                     SIGFPE detection */

  /* prefix will be extended with the fieldsplit */
  int len = strlen(slesp->name) + 1;
  int _len = len + strlen("_fieldsplit_x_") + 1;
  char  *prefix = NULL;
  BFT_MALLOC(prefix, _len + 1, char);
  sprintf(prefix, "%s_", slesp->name);
  prefix[len] = '\0';
  KSPSetOptionsPrefix(ksp, prefix);

  /* Set the solver */
  _petsc_set_krylov_solver(slesp, ksp);

  /* Common settings to block preconditionner */
  _petsc_common_block_hook(slesp, ksp);

  PC  pc;
  KSPGetPC(ksp, &pc);
  PCSetUp(pc);

  PC  _pc;
  PetscInt  n_split;
  KSP  *xyz_subksp;
  const char xyz[3] = "xyz";
  bool  is_symm = _system_should_be_sym(slesp->solver);

  PCFieldSplitGetSubKSP(pc, &n_split, &xyz_subksp);
  assert(n_split == 3);

  for (PetscInt id = 0; id < n_split; id++) {

    sprintf(prefix, "%s_fieldsplit_%c", slesp->name, xyz[id]);

    _petsc_cmd(true, prefix, "ksp_type","preonly");

    /* Predefined settings when using AMG as a preconditioner */
    KSP  _ksp = xyz_subksp[id];
    KSPGetPC(_ksp, &_pc);

    _petsc_pcgamg_hook(prefix, slesp, is_symm, _pc);

    PCSetFromOptions(_pc);
    KSPSetFromOptions(_ksp);

  } /* Loop on block settings */

  BFT_FREE(prefix);
  PetscFree(xyz_subksp);

  /* User function for additional settings */
  cs_user_sles_petsc_hook(context, ksp);

  PCSetFromOptions(pc);
  KSPSetFromOptions(ksp);

  /* Dump the setup related to PETSc in a specific file */
  if (!slesp->setup_done) {
    KSPSetUp(ksp);
    cs_sles_petsc_log_setup(ksp);
    slesp->setup_done = true;
  }

  cs_fp_exception_restore_trap(); /* Avoid trouble with a too restrictive
                                     SIGFPE detection */
}

#if defined(PETSC_HAVE_HYPRE)
/*----------------------------------------------------------------------------*/
/*!
 * \brief  Function pointer: setup hook for setting PETSc solver and
 *         preconditioner.
 *         Case of multiplicative AMG block preconditioner for a CG with boomer
 *         as AMG type
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 */
/*----------------------------------------------------------------------------*/

static void
_petsc_amg_block_boomer_hook(void     *context,
                             KSP       ksp)
{
  cs_param_sles_t  *slesp = (cs_param_sles_t *)context;

  cs_fp_exception_disable_trap(); /* Avoid trouble with a too restrictive
                                     SIGFPE detection */

  /* prefix will be extended with the fieldsplit */
  int len = strlen(slesp->name) + 1;
  int _len = len + strlen("_fieldsplit_x_") + 1;
  char  *prefix = NULL;
  BFT_MALLOC(prefix, _len + 1, char);
  sprintf(prefix, "%s_", slesp->name);
  prefix[len] = '\0';
  KSPSetOptionsPrefix(ksp, prefix);

  /* Set the solver */
  _petsc_set_krylov_solver(slesp, ksp);

  /* Common settings to block preconditionner */
  _petsc_common_block_hook(slesp, ksp);

  /* Predefined settings when using AMG as a preconditioner */
  PC  pc;
  KSPGetPC(ksp, &pc);
  PCSetUp(pc);

  PC  _pc;
  PetscInt  n_split;
  KSP  *xyz_subksp;
  const char xyz[3] = "xyz";
  bool  is_symm = _system_should_be_sym(slesp->solver);

  PCFieldSplitGetSubKSP(pc, &n_split, &xyz_subksp);
  assert(n_split == 3);

  for (PetscInt id = 0; id < n_split; id++) {

    sprintf(prefix, "%s_fieldsplit_%c", slesp->name, xyz[id]);

    _petsc_cmd(true, prefix, "ksp_type","preonly");

    /* Predefined settings when using AMG as a preconditioner */
    KSP  _ksp = xyz_subksp[id];
    KSPGetPC(_ksp, &_pc);

    _petsc_pchypre_hook(prefix, slesp, is_symm, _pc);

    PCSetFromOptions(_pc);
    KSPSetFromOptions(_ksp);

  } /* Loop on block settings */

  BFT_FREE(prefix);
  PetscFree(xyz_subksp);

  /* User function for additional settings */
  cs_user_sles_petsc_hook(context, ksp);

  PCSetFromOptions(pc);
  KSPSetFromOptions(ksp);

  /* Dump the setup related to PETSc in a specific file */
  if (!slesp->setup_done) {
    KSPSetUp(ksp);
    cs_sles_petsc_log_setup(ksp);
    slesp->setup_done = true;
  }

  cs_fp_exception_restore_trap(); /* Avoid trouble with a too restrictive
                                     SIGFPE detection */
}
#endif

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Function pointer: setup hook for setting PETSc solver and
 *         preconditioner.
 *         Case of block preconditioner
 *
 * \param[in, out] context  pointer to optional (untyped) value or structure
 * \param[in, out] ksp      pointer to PETSc KSP context
 */
/*----------------------------------------------------------------------------*/

static void
_petsc_block_hook(void     *context,
                  KSP       ksp)
{
  cs_param_sles_t  *slesp = (cs_param_sles_t *)context;

  cs_fp_exception_disable_trap(); /* Avoid trouble with a too restrictive
                                     SIGFPE detection */

  /* prefix will be extended with the fieldsplit */
  int len = strlen(slesp->name) + 1;
  int _len = len + strlen("_fieldsplit_x_") + 1;
  char  *prefix = NULL;
  BFT_MALLOC(prefix, _len + 1, char);
  sprintf(prefix, "%s_", slesp->name);
  prefix[len] = '\0';
  KSPSetOptionsPrefix(ksp, prefix);

  /* Set the solver (tolerance and max_it too) */
  _petsc_set_krylov_solver(slesp, ksp);

  /* Common settings to block preconditionner */
  _petsc_common_block_hook(slesp, ksp);

  PC  pc;
  KSPGetPC(ksp, &pc);
  PCSetUp(pc);

  PC  _pc;
  KSP  *xyz_subksp;
  PetscInt  n_split;
  const char  xyz[3] = "xyz";

  PCFieldSplitGetSubKSP(pc, &n_split, &xyz_subksp);
  assert(n_split == 3);

  for (PetscInt id = 0; id < n_split; id++) {

    sprintf(prefix, "%s_fieldsplit_%c", slesp->name, xyz[id]);

    switch (slesp->precond) {

    case CS_PARAM_PRECOND_NONE:
      _petsc_cmd(true, prefix, "ksp_type", "richardson");
      break;

    case CS_PARAM_PRECOND_DIAG:
      _petsc_cmd(true, prefix, "ksp_type", "richardson");
      _petsc_cmd(true, prefix, "pc_type", "jacobi");
      break;

    case CS_PARAM_PRECOND_ILU0:
    case CS_PARAM_PRECOND_BJACOB_ILU0:
      if (slesp->solver_class == CS_PARAM_SLES_CLASS_HYPRE) {
#if defined(PETSC_HAVE_HYPRE)
        _petsc_cmd(true, prefix, "ksp_type", "preonly");
        _petsc_cmd(true, prefix, "pc_type", "hypre");
        _petsc_cmd(true, prefix, "pc_hypre_type", "euclid");
        _petsc_cmd(true, prefix, "pc_hypre_euclid_level", "0");
#else
        bft_error(__FILE__, __LINE__, 0,
                  " %s: Invalid option: HYPRE is not installed.", __func__);
#endif
      }
      else {

          _petsc_cmd(true, prefix, "ksp_type", "richardson");
          _petsc_bilu0_hook(prefix);

      }
      break;

    case CS_PARAM_PRECOND_ICC0:
      _petsc_cmd(true, prefix, "ksp_type", "richardson");
      _petsc_bicc0_hook(prefix);
      break;

    case CS_PARAM_PRECOND_LU:
      _petsc_cmd(true, prefix, "ksp_type", "preonly");
#if defined(PETSC_HAVE_MUMPS)
      _petsc_cmd(true, prefix, "pc_type", "lu");
      _petsc_cmd(true, prefix, "pc_factor_mat_solver_type", "mumps");
#else
      if (cs_glob_n_ranks == 1)
        _petsc_cmd(true, prefix, "pc_type", "lu");
      else { /* Switch to a block version (sequential in each block) */
        _petsc_cmd(true, prefix, "pc_type", "bjacobi");
        _petsc_cmd(true, prefix, "pc_jacobi_blocks", "1");
        _petsc_cmd(true, prefix, "sub_ksp_type", "preonly");
        _petsc_cmd(true, prefix, "sub_pc_type", "lu");
      }
#endif
      break;

    case CS_PARAM_PRECOND_SSOR:
    case CS_PARAM_PRECOND_BJACOB_SGS:
      _petsc_cmd(true, prefix, "ksp_type", "richardson");
      _petsc_bssor_hook(prefix);
      break;

    default:
      bft_error(__FILE__, __LINE__, 0,
                " %s: Eq. %s: Invalid preconditioner.",__func__, slesp->name);
      break;

    } /* Switch on preconditioning type */

    KSP  _ksp = xyz_subksp[id];
    KSPGetPC(_ksp, &_pc);
    PCSetFromOptions(_pc);
    KSPSetUp(_ksp);

  } /* Loop on block settings */

  BFT_FREE(prefix);
  PetscFree(xyz_subksp);

  /* User function for additional settings */
  cs_user_sles_petsc_hook(context, ksp);

  PCSetFromOptions(pc);
  KSPSetFromOptions(ksp);

  /* Dump the setup related to PETSc in a specific file */
  if (!slesp->setup_done) {
    KSPSetUp(ksp);
    cs_sles_petsc_log_setup(ksp);
    slesp->setup_done = true;
  }

  cs_fp_exception_restore_trap(); /* Avoid trouble with a too restrictive
                                     SIGFPE detection */
}
#endif /* defined(HAVE_PETSC) */

/*----------------------------------------------------------------------------*/
/*!
 * \brief Check if the settings are consitent. Can apply minor modifications.
 *
 * \param[in, out]  slesp       pointer to a \ref cs_param_sles_t structure
 */
/*----------------------------------------------------------------------------*/

static void
_check_settings(cs_param_sles_t     *slesp)
{

  /* Checks related to MUMPS */

  if (_mumps_is_needed(slesp->solver)) {

    cs_param_sles_class_t  ret_class =
      cs_param_sles_check_class(CS_PARAM_SLES_CLASS_MUMPS);
    if (ret_class == CS_PARAM_SLES_N_CLASSES)
      bft_error(__FILE__, __LINE__, 0,
                " %s: Error detected while setting the SLES \"%s\"\n"
                " MUMPS is not available with your installation.\n"
                " Please check your installation settings.\n",
                __func__, slesp->name);
    else
      slesp->solver_class = ret_class;

  }
  else {
    if (slesp->solver_class == CS_PARAM_SLES_CLASS_MUMPS)
      bft_error(__FILE__, __LINE__, 0,
                " %s: Error detected while setting the SLES \"%s\"\n"
                " MUMPS class is not consistent with your settings.\n"
                " Please check your installation settings.\n",
                __func__, slesp->name);
  }

  /* Checks related to GCR/GMRES algorithms */

  if (slesp->solver == CS_PARAM_ITSOL_GMRES ||
      slesp->solver == CS_PARAM_ITSOL_GCR)
    if (slesp->restart < 2)
      bft_error(__FILE__, __LINE__, 0,
                " %s: Error detected while setting the SLES \"%s\"\n"
                " The restart interval (=%d) is not big enough.\n"
                " Please check your installation settings.\n",
                __func__, slesp->name, slesp->restart);

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set parameters for initializing SLES structures used for the
 *        resolution of the linear system.
 *        Case of saturne's own solvers.
 *
 * \param[in]       use_field_id  if false use system name
 * \param[in, out]  slesp         pointer to a \ref cs_param_sles_t structure
 */
/*----------------------------------------------------------------------------*/

static void
_set_saturne_sles(bool                 use_field_id,
                  cs_param_sles_t     *slesp)
{
  assert(slesp != NULL);  /* Sanity checks */

  const  char  *sles_name = use_field_id ? NULL : slesp->name;
  assert(slesp->field_id > -1 || sles_name != NULL);

  /* 1- Define the preconditioner */
  /*    ========================= */

  int  poly_degree;
  cs_sles_pc_t *pc = NULL;

  switch (slesp->precond) {

  case CS_PARAM_PRECOND_DIAG:
    poly_degree = 0;
    break;

  case CS_PARAM_PRECOND_POLY1:
    poly_degree = 1;
    break;

  case CS_PARAM_PRECOND_POLY2:
    poly_degree = 2;
    break;

  case CS_PARAM_PRECOND_AMG:
    poly_degree = -1;
    switch (slesp->amg_type) {

    case CS_PARAM_AMG_HOUSE_V:
      pc = cs_multigrid_pc_create(CS_MULTIGRID_V_CYCLE);
      break;
    case CS_PARAM_AMG_HOUSE_K:
      if (slesp->solver == CS_PARAM_ITSOL_CG)
        slesp->solver = CS_PARAM_ITSOL_FCG;
      pc = cs_multigrid_pc_create(CS_MULTIGRID_K_CYCLE);
      break;

    default:
      bft_error(__FILE__, __LINE__, 0,
                " %s: System: %s; Invalid AMG type with Code_Saturne solvers.",
                __func__, slesp->name);
      break;

    } /* Switch on the type of AMG */
    break; /* AMG as preconditioner */

  case CS_PARAM_PRECOND_GKB_CG:
  case CS_PARAM_PRECOND_GKB_GMRES:
    poly_degree = -1;
    break;

  case CS_PARAM_PRECOND_NONE:
  default:
    poly_degree = -1;       /* None or other */

  } /* Switch on the preconditioner type */

  /* 2- Define the iterative solver */
  /*    =========================== */

  cs_sles_it_t  *it = NULL;
  cs_multigrid_t  *mg = NULL;

  switch (slesp->solver) {

  case CS_PARAM_ITSOL_AMG:
    {
      switch (slesp->amg_type) {

      case CS_PARAM_AMG_HOUSE_V:
        mg = cs_multigrid_define(slesp->field_id,
                                 sles_name,
                                 CS_MULTIGRID_V_CYCLE);

        /* Advanced setup (default is specified inside the brackets)
         * for AMG as solver */
        cs_multigrid_set_solver_options
          (mg,
           CS_SLES_JACOBI,   /* descent smoother type (CS_SLES_PCG) */
           CS_SLES_JACOBI,   /* ascent smoother type (CS_SLES_PCG) */
           CS_SLES_PCG,      /* coarse solver type (CS_SLES_PCG) */
           slesp->n_max_iter, /* n max cycles (100) */
           5,                /* n max iter for descent (10) */
           5,                /* n max iter for ascent (10) */
           1000,             /* n max iter coarse solver (10000) */
           0,                /* polynomial precond. degree descent (0) */
           0,                /* polynomial precond. degree ascent (0) */
           -1,               /* polynomial precond. degree coarse (0) */
           1.0,    /* precision multiplier descent (< 0 forces max iters) */
           1.0,    /* precision multiplier ascent (< 0 forces max iters) */
           1);     /* requested precision multiplier coarse (default 1) */
        break;

      case CS_PARAM_AMG_HOUSE_K:
        mg = cs_multigrid_define(slesp->field_id,
                                 sles_name,
                                 CS_MULTIGRID_K_CYCLE);

        cs_multigrid_set_solver_options
          (mg,
           CS_SLES_P_SYM_GAUSS_SEIDEL, /* descent smoother */
           CS_SLES_P_SYM_GAUSS_SEIDEL, /* ascent smoother */
           CS_SLES_PCG,                /* coarse smoother */
           slesp->n_max_iter,           /* n_max_cycles */
           1,                          /* n_max_iter_descent, */
           1,                          /* n_max_iter_ascent */
           100,                        /* n_max_iter_coarse */
           0,                          /* poly_degree_descent */
           0,                          /* poly_degree_ascent */
           0,                          /* poly_degree_coarse */
           -1.0,                       /* precision_mult_descent */
           -1.0,                       /* precision_mult_ascent */
           1);                         /* precision_mult_coarse */
        break;

      default:
        bft_error(__FILE__, __LINE__, 0,
                  " %s; System: %s -- Invalid AMG type with Code_Saturne"
                  " solvers.", __func__, slesp->name);
        break;

      } /* End of switch on the AMG type */

    }
    break; /* AMG as solver */

  case CS_PARAM_ITSOL_BICG:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_BICGSTAB,
                           poly_degree,
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_BICGSTAB2:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_BICGSTAB2,
                           poly_degree,
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_CG:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_PCG,
                           poly_degree,
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_CR3:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_PCR3,
                           poly_degree,
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_FCG:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_IPCG,
                           poly_degree,
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_GAUSS_SEIDEL:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_P_GAUSS_SEIDEL,
                           -1, /* Not useful to apply a preconditioner */
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_GCR:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_GCR,
                           poly_degree,
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_GKB_CG:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_IPCG, /* Flexible CG */
                           poly_degree,
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_GKB_GMRES:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_GMRES, /* Should be a flexible GMRES */
                           poly_degree,
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_GMRES:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_GMRES,
                           poly_degree,
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_JACOBI:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_JACOBI,
                           -1, /* Not useful to apply a preconditioner */
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_SYM_GAUSS_SEIDEL:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_P_SYM_GAUSS_SEIDEL,
                           -1, /* Not useful to apply a preconditioner */
                           slesp->n_max_iter);
    break;

  case CS_PARAM_ITSOL_USER_DEFINED:
    it = cs_sles_it_define(slesp->field_id,
                           sles_name,
                           CS_SLES_USER_DEFINED,
                           poly_degree,
                           slesp->n_max_iter);
    break;

  default:
    bft_error(__FILE__, __LINE__, 0,
              " %s: Invalid iterative solver for solving equation %s.\n"
              " Please modify your settings.", __func__, slesp->name);
    break;

  } /* end of switch */

  /* Update the preconditioner settings if needed */
  if (slesp->precond == CS_PARAM_PRECOND_AMG) {

    assert(pc != NULL && it != NULL);

    mg = cs_sles_pc_get_context(pc);
    cs_sles_it_transfer_pc(it, &pc);

    /* If this is a K-cycle multigrid. Change the default settings when used as
       preconditioner */
    if (slesp->amg_type == CS_PARAM_AMG_HOUSE_K) {

      cs_multigrid_set_solver_options
        (mg,
         CS_SLES_PCG,       /* descent smoother */
         CS_SLES_PCG,       /* ascent smoother */
         CS_SLES_PCG,       /* coarse solver */
         slesp->n_max_iter, /* n_max_cycles */
         2,                 /* n_max_iter_descent, */
         2,                 /* n_max_iter_ascent */
         500,               /* n_max_iter_coarse */
         0,                 /* poly_degree_descent */
         0,                 /* poly_degree_ascent */
         0,                 /* poly_degree_coarse */
         -1.0,              /* precision_mult_descent */
         -1.0,              /* precision_mult_ascent */
         1.0);              /* precision_mult_coarse */

      cs_multigrid_set_coarsening_options(mg,
                                          8,   /* aggregation_limit*/
                                          CS_GRID_COARSENING_SPD_PW,
                                          10,  /* n_max_levels */
                                          50,  /* min_g_cells */
                                          0.,  /* P0P1 relaxation */
                                          0);  /* postprocess */

    } /* AMG K-cycle as preconditioner */

  } /* AMG preconditioner */

  /* Define the level of verbosity for SLES structure */
  if (slesp->verbosity > 3) {

    cs_sles_t  *sles = cs_sles_find_or_add(slesp->field_id, sles_name);
    cs_sles_it_t  *sles_it = (cs_sles_it_t *)cs_sles_get_context(sles);

    /* true = use_iteration instead of wall clock time */
    cs_sles_it_set_plot_options(sles_it, slesp->name, true);

  }

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set parameters for initializing SLES structures used for the
 *        resolution of the linear system.
 *        Case of MUMPS's own solvers.
 *
 * \param[in]       use_field_id  if false use system name
 * \param[in, out]  slesp         pointer to a \ref cs_param_sles_t structure
 */
/*----------------------------------------------------------------------------*/

static void
_set_mumps_sles(bool                 use_field_id,
                cs_param_sles_t     *slesp)
{
  assert(slesp != NULL);  /* Sanity checks */

  const  char  *sles_name = use_field_id ? NULL : slesp->name;
  assert(slesp->field_id > -1 || sles_name != NULL);

#if defined(HAVE_MUMPS)
  cs_sles_mumps_define(slesp->field_id,
                       sles_name,
                       slesp,
                       cs_user_sles_mumps_hook,
                       NULL);
#else
  bft_error(__FILE__, __LINE__, 0,
            "%s: System: %s\n"
            " MUMPS is not supported directly.\n"
            " Please check your settings or your code_saturne installation.",
            __func__, slesp->name);
#endif
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set parameters for initializing SLES structures used for the
 *        resolution of the linear system.
 *        Case of PETSc and Hypre families of solvers.
 *
 * \param[in]       use_field_id  if false use system name
 * \param[in, out]  slesp         pointer to a \ref cs_param_sles_t structure
 */
/*----------------------------------------------------------------------------*/

static void
_set_petsc_hypre_sles(bool                 use_field_id,
                      cs_param_sles_t     *slesp)
{
  assert(slesp != NULL);  /* Sanity checks */

  const  char  *sles_name = use_field_id ? NULL : slesp->name;
  assert(slesp->field_id > -1 || sles_name != NULL);

#if defined(HAVE_PETSC)
  cs_sles_petsc_init();

  if (slesp->pcd_block_type != CS_PARAM_PRECOND_BLOCK_NONE) {

    if (slesp->precond == CS_PARAM_PRECOND_AMG) {

      if (slesp->amg_type == CS_PARAM_AMG_PETSC_GAMG_V ||
          slesp->amg_type == CS_PARAM_AMG_PETSC_GAMG_W)
        cs_sles_petsc_define(slesp->field_id,
                             sles_name,
                             MATMPIAIJ,
                             _petsc_amg_block_gamg_hook,
                             (void *)slesp);

      else if (slesp->amg_type == CS_PARAM_AMG_HYPRE_BOOMER_V ||
               slesp->amg_type == CS_PARAM_AMG_HYPRE_BOOMER_W) {
#if defined(PETSC_HAVE_HYPRE)
        cs_sles_petsc_define(slesp->field_id,
                             sles_name,
                             MATMPIAIJ,
                             _petsc_amg_block_boomer_hook,
                             (void *)slesp);
#else
        cs_base_warn(__FILE__, __LINE__);
        cs_log_printf(CS_LOG_DEFAULT,
                      " %s: System: %s.\n"
                      " Boomer is not available. Switch to GAMG solver.",
                      __func__, slesp->name);
        cs_sles_petsc_define(slesp->field_id,
                             sles_name,
                             MATMPIAIJ,
                             _petsc_amg_block_gamg_hook,
                             (void *)slesp);
#endif
      }
      else
        bft_error(__FILE__, __LINE__, 0,
                  " %s: System: %s\n"
                  " No AMG solver available for a block-AMG.",
                  __func__, slesp->name);
    }
    else {

      cs_sles_petsc_define(slesp->field_id,
                           sles_name,
                           MATMPIAIJ,
                           _petsc_block_hook,
                           (void *)slesp);

    }

  }
  else { /* No block preconditioner */

    cs_sles_petsc_define(slesp->field_id,
                         sles_name,
                         MATMPIAIJ,
                         _petsc_setup_hook,
                         (void *)slesp);

  }
#else
  bft_error(__FILE__, __LINE__, 0,
            _(" %s: PETSC algorithms used to solve %s are not linked.\n"
              " Please install Code_Saturne with PETSc."),
            __func__, slesp->name);
#endif /* HAVE_PETSC */
}

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Create a \ref cs_param_sles_t structure and assign a default
 *         settings
 *
 * \param[in]  field_id      id related to to the variable field or -1
 * \param[in]  system_name   name of the system to solve or NULL
 *
 * \return a pointer to a cs_param_sles_t stucture
 */
/*----------------------------------------------------------------------------*/

cs_param_sles_t *
cs_param_sles_create(int          field_id,
                     const char  *system_name)
{
  cs_param_sles_t  *slesp = NULL;

  BFT_MALLOC(slesp, 1, cs_param_sles_t);

  slesp->verbosity = 0;                         /* SLES verbosity */

  slesp->field_id = field_id;                   /* associated field id */

  slesp->solver_class = CS_PARAM_SLES_CLASS_CS; /* solver family */

  slesp->precond = CS_PARAM_PRECOND_DIAG;       /* preconditioner */
  slesp->solver = CS_PARAM_ITSOL_GMRES;         /* iterative solver */
  slesp->amg_type = CS_PARAM_AMG_NONE;          /* no predefined AMG type */
  slesp->pcd_block_type = CS_PARAM_PRECOND_BLOCK_NONE; /* no block by default */

  slesp->restart = 15;                       /* max. iter. before restarting */
  slesp->n_max_iter = 10000;                 /* max. number of iterations */
  slesp->eps = 1e-8;                         /* relative tolerance to stop
                                                an iterative solver */

  slesp->resnorm_type = CS_PARAM_RESNORM_NONE;
  slesp->setup_done = false;

  slesp->name = NULL;
  if (system_name != NULL) {
    size_t  len = strlen(system_name);
    BFT_MALLOC(slesp->name, len + 1, char);
    strncpy(slesp->name, system_name, len);
    slesp->name[len] = '\0';
  }

  return slesp;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Free a \ref cs_param_sles_t structure
 *
 * \param[in, out]  slesp    pointer to a \cs_param_sles_t structure to free
 */
/*----------------------------------------------------------------------------*/

void
cs_param_sles_free(cs_param_sles_t   **p_slesp)
{
  if (p_slesp == NULL)
    return;

  cs_param_sles_t  *slesp = *p_slesp;

  if (slesp == NULL)
    return;

  BFT_FREE(slesp->name);
  BFT_FREE(slesp);
  slesp = NULL;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Log information related to the linear settings stored in the
 *         structure
 *
 * \param[in] slesp    pointer to a \ref cs_param_sles_log
 */
/*----------------------------------------------------------------------------*/

void
cs_param_sles_log(cs_param_sles_t   *slesp)
{
  if (slesp == NULL)
    return;

  cs_log_printf(CS_LOG_SETUP, "\n### %s | Linear algebra settings\n",
                slesp->name);
  cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Family:", slesp->name);
  if (slesp->solver_class == CS_PARAM_SLES_CLASS_CS)
    cs_log_printf(CS_LOG_SETUP, "             Code_Saturne\n");
  else if (slesp->solver_class == CS_PARAM_SLES_CLASS_MUMPS)
    cs_log_printf(CS_LOG_SETUP, "             MUMPS\n");
  else if (slesp->solver_class == CS_PARAM_SLES_CLASS_HYPRE)
    cs_log_printf(CS_LOG_SETUP, "             HYPRE\n");
  else if (slesp->solver_class == CS_PARAM_SLES_CLASS_PETSC)
    cs_log_printf(CS_LOG_SETUP, "             PETSc\n");

  cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Verbosity:          %d\n",
                slesp->name, slesp->verbosity);
  cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Field id:           %d\n",
                slesp->name, slesp->field_id);

  cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Solver.Name:        %s\n",
                slesp->name, cs_param_get_solver_name(slesp->solver));
  if (slesp->solver == CS_PARAM_ITSOL_AMG)
    cs_log_printf(CS_LOG_SETUP, "  * %s | SLES AMG.Type:           %s\n",
                  slesp->name, cs_param_get_amg_type_name(slesp->amg_type));

  cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Solver.Precond:     %s\n",
                slesp->name, cs_param_get_precond_name(slesp->precond));
  if (slesp->precond == CS_PARAM_PRECOND_AMG)
    cs_log_printf(CS_LOG_SETUP, "  * %s | SLES AMG.Type:           %s\n",
                  slesp->name, cs_param_get_amg_type_name(slesp->amg_type));
  cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Block.Precond:      %s\n",
                slesp->name,
                cs_param_get_precond_block_name(slesp->pcd_block_type));

  cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Solver.MaxIter:     %d\n",
                slesp->name, slesp->n_max_iter);
  if (slesp->solver == CS_PARAM_ITSOL_GMRES ||
      slesp->solver == CS_PARAM_ITSOL_FGMRES ||
      slesp->solver == CS_PARAM_ITSOL_GCR)
    cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Solver.Restart:     %d\n",
                  slesp->name, slesp->restart);

  cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Solver.Eps:        % -10.6e\n",
                slesp->name, slesp->eps);

  cs_log_printf(CS_LOG_SETUP, "  * %s | SLES Normalization:      ",
                slesp->name);
  switch (slesp->resnorm_type) {
  case CS_PARAM_RESNORM_NORM2_RHS:
    cs_log_printf(CS_LOG_SETUP, "Euclidean norm of the RHS\n");
    break;
  case CS_PARAM_RESNORM_WEIGHTED_RHS:
    cs_log_printf(CS_LOG_SETUP, "Weighted Euclidean norm of the RHS\n");
    break;
  case CS_PARAM_RESNORM_FILTERED_RHS:
    cs_log_printf(CS_LOG_SETUP, "Filtered Euclidean norm of the RHS\n");
    break;
  case CS_PARAM_RESNORM_NONE:
  default:
    cs_log_printf(CS_LOG_SETUP, "None\n");
    break;
  }
  cs_log_printf(CS_LOG_SETUP, "\n");
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief   Copy a cs_param_sles_t structure from src to dst
 *
 * \param[in]       src    reference cs_param_sles_t structure to copy
 * \param[in, out]  dst    copy of the reference at exit
 */
/*----------------------------------------------------------------------------*/

void
cs_param_sles_copy_from(cs_param_sles_t   *src,
                        cs_param_sles_t   *dst)
{
  if (dst == NULL)
    return;

  /* Remark: name is managed at the creation of the structure */

  dst->setup_done = src->setup_done;
  dst->verbosity = src->verbosity;
  dst->field_id = src->field_id;

  dst->solver_class = src->solver_class;
  dst->precond = src->precond;
  dst->solver = src->solver;
  dst->amg_type = src->amg_type;
  dst->pcd_block_type = src->pcd_block_type;

  dst->resnorm_type = src->resnorm_type;
  dst->n_max_iter = src->n_max_iter;
  dst->eps = src->eps;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Define cs_sles_t structure in accordance with the settings of a
 *        cs_param_sles_t structure (SLES = Sparse Linear Equation Solver)
 *
 * \param[in]       use_field_id  if false use system name to define a SLES
 * \param[in, out]  slesp         pointer to a cs_param_sles_t structure
 *
 * \return an error code (-1 if a problem is encountered, 0 otherwise)
 */
/*----------------------------------------------------------------------------*/

int
cs_param_sles_set(bool                 use_field_id,
                  cs_param_sles_t     *slesp)
{
  if (slesp == NULL)
    return 0;

  _check_settings(slesp);

  switch (slesp->solver_class) {

  case CS_PARAM_SLES_CLASS_CS: /* Code_Saturne's own solvers */
    /* true = use field_id instead of slesp->name to set the sles */
    _set_saturne_sles(use_field_id, slesp);
    break;

  case CS_PARAM_SLES_CLASS_MUMPS: /* MUMPS sparse direct solvers */
    /* true = use field_id instead of slesp->name to set the sles */
    _set_mumps_sles(use_field_id, slesp);
    break;

  case CS_PARAM_SLES_CLASS_PETSC: /* PETSc solvers */
  case CS_PARAM_SLES_CLASS_HYPRE: /* HYPRE solvers through PETSc */
    /* true = use field_id instead of slesp->name to set the sles */
    _set_petsc_hypre_sles(use_field_id, slesp);
    break;

  default:
    return -1;

  } /* End of switch on class of solvers */

  /* Define the level of verbosity for the SLES structure */
  if (slesp->verbosity > 1) {

    /* All the previous SLES are defined thanks to the field_id */
    cs_sles_t  *sles = NULL;
    if (use_field_id)
      sles = cs_sles_find_or_add(slesp->field_id, NULL);
    else
      sles = cs_sles_find_or_add(slesp->field_id, slesp->name);

    /* Set verbosity */
    cs_sles_set_verbosity(sles, slesp->verbosity);

  }

  return 0;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Check the availability of a solver library and return the requested
 *        one if this is possible or an alternative or CS_PARAM_SLES_N_CLASSES
 *        if no alternative is available.
 *
 * \param[in]       wanted_class  requested class of solvers
 *
 * \return the available solver class related to the requested class
 */
/*----------------------------------------------------------------------------*/

cs_param_sles_class_t
cs_param_sles_check_class(cs_param_sles_class_t   wanted_class)
{
  switch (wanted_class) {

  case CS_PARAM_SLES_CLASS_CS:  /* No issue */
    return CS_PARAM_SLES_CLASS_CS;

  case CS_PARAM_SLES_CLASS_HYPRE:
#if defined(HAVE_PETSC)
#if defined(PETSC_HAVE_HYPRE)
    return CS_PARAM_SLES_CLASS_HYPRE;
#else
    cs_base_warn(__FILE__, __LINE__);
    bft_printf(" Switch to PETSc library since Hypre is not available");
    return CS_PARAM_SLES_CLASS_PETSC; /* Switch to petsc */
#endif
#else
    return CS_PARAM_SLES_N_CLASSES;
#endif

  case CS_PARAM_SLES_CLASS_PETSC:
#if defined(HAVE_PETSC)
    return CS_PARAM_SLES_CLASS_PETSC;
#else
    return CS_PARAM_SLES_N_CLASSES;
#endif

  case CS_PARAM_SLES_CLASS_MUMPS:
#if defined(HAVE_MUMPS)
    return CS_PARAM_SLES_CLASS_MUMPS;
#else
#if defined(HAVE_PETSC)
#if defined(PETSC_HAVE_MUMPS)
    cs_base_warn(__FILE__, __LINE__);
    bft_printf(" Switch to PETSc library since MUMPS is not available as"
               " a stand-alone library\n");
    return CS_PARAM_SLES_CLASS_PETSC;
#else
    return CS_PARAM_SLES_N_CLASSES;
#endif  /* PETSC_HAVE_MUMPS */
#else
    return CS_PARAM_SLES_N_CLASSES; /* PETSc without MUMPS  */
#endif  /* HAVE_PETSC */
    return CS_PARAM_SLES_N_CLASSES; /* Neither MUMPS nor PETSc */
#endif

  default:
    return CS_PARAM_SLES_N_CLASSES;
  }
}

#if defined(HAVE_PETSC)
/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set the command line option for PETSc
 *
 * \param[in]      use_prefix    need a prefix
 * \param[in]      prefix        optional prefix
 * \param[in]      keyword       command keyword
 * \param[in]      keyval        command value
 */
/*----------------------------------------------------------------------------*/

void
cs_param_sles_petsc_cmd(bool          use_prefix,
                        const char   *prefix,
                        const char   *keyword,
                        const char   *keyval)
{
  _petsc_cmd(use_prefix, prefix, keyword, keyval);
}
#endif

/*----------------------------------------------------------------------------*/

END_C_DECLS

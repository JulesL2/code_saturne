#ifndef __CS_ITER_ALGO_H__
#define __CS_ITER_ALGO_H__

/*============================================================================
 * Set of functions to manage high-level iterative algorithms
 *============================================================================*/

/*
  This file is part of code_saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2023 EDF S.A.

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

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include "cs_cdo_blas.h"
#include "cs_math.h"
#include "cs_param_sles.h"
#include "cs_param_types.h"
#include "cs_sles.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*============================================================================
 * Macro definitions
 *============================================================================*/

/*============================================================================
 * Type definitions
 *============================================================================*/

/*! \struct cs_iter_algo_t
 *  \brief Structure to handle the convergence of an iterative algorithm
 *
 *  Metadata to manage an iterative algorithm such as Picard or Uzawa for
 *  instance. This structure can handle embedded iterative algorithm since the
 *  notion of inner and outer iterations is defined. Nevertheless, only the
 *  outer iterative algorithm is managed (information about inner iterations
 *  are only for monitoring purposes).
 */

typedef struct {

/*!
 * @name Generic parameters
 * @{
 *
 * \var verbosity
 * Level of printed information
 *
 * \var cvg_param
 * structure storing the main settings
 *
 * \var context
 * pointer to structure cast on the fly
 *
 * @}
 * @name Convergence monitoring
 * @{
 *
 * \var cvg_status
 * Converged, iterating or diverged status
 *
 * \var normalization
 * Value of the normalization for the relative tolerance.
 *
 * The stopping criterion is such that res < rtol * normalization. By default,
 * the normalization is set to 1.
 *
 * \var tol
 * Tolerance computed as tol = max(atol, normalization*rtol) where
 * atol and rtol are respectively the absolute and relative tolerance associated
 * to the algorithm
 *
 * \var prev_res
 * Value of the previous residual achieved during the iterative process
 *
 * \var res
 * Value of the residual for the iterative algorithm
 *
 * \var res0
 * Value of the first residual of the iterative process. This is used for
 * detecting the divergence of the algorithm.
 *
 * \var n_algo_iter
 * Current number of iterations for the algorithm (outer iterations)
 *
 * \var n_inner_iter
 * Curent cumulated number of inner iterations (sum over the outer iterations)
 *
 * \var last_inner_iter
 * Last number of iterations for the inner solver
 *
 * @}
 */

  int                              verbosity;
  cs_param_sles_cvg_t              cvg_param;

  void                            *context;

  cs_sles_convergence_state_t      cvg_status;
  double                           normalization;
  double                           tol;

  double                           prev_res;
  double                           res;
  double                           res0;

  int                              n_algo_iter;
  int                              n_inner_iter;
  int                              last_inner_iter;

} cs_iter_algo_t;

/*! \struct cs_iter_algo_param_aa_t

 *  \brief Structure storing all the parameters to drive the algorithm called
 *  Anderson acceleration
 */

typedef struct {

/*!
 * \var n_max_dir
 * Maximum number of directions
 *
 * \var starting_iter
 * Anderson acceleration starts at this iteration number
 *
 * \var max_cond
 * Tolerance under which terms are dropped in order to improve the
 * conditionning number of the QR factorization
 *
 * \var beta
 * Value of the relaxation coefficient (if this is equal to zero then there is
 * non relaxation to perform)
 *
 * \var dp_type
 * Type of dot product to apply (usual Euclidean 2-norm or CDO-based one)
 */

  int                        n_max_dir;
  int                        starting_iter;
  double                     max_cond;
  double                     beta;
  cs_param_dotprod_type_t    dp_type;

} cs_iter_algo_param_aa_t;

/*! \struct cs_iter_algo_aa_t
 *  \brief Context structure for the algorithm called Anderson acceleration
 *
 *  Set of parameters and arrays to manage the Anderson acceleration
 */

typedef struct _cs_iter_algo_aa_t  cs_iter_algo_aa_t;

/*============================================================================
 * Inline static public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief Reset a cs_iter_algo_t structure
 *
 * \param[in, out] algo    pointer to a cs_iter_algo_t
 */
/*----------------------------------------------------------------------------*/

static inline void
cs_iter_algo_reset(cs_iter_algo_t    *algo)
{
  if (algo == NULL)
    return;

  algo->cvg_status = CS_SLES_ITERATING;
  algo->res0 = cs_math_big_r;
  algo->prev_res = cs_math_big_r;
  algo->res = cs_math_big_r;
  algo->n_algo_iter = 0;
  algo->n_inner_iter = 0;
  algo->last_inner_iter = 0;
}

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief Create and initialize a new cs_iter_algo_t structure
 *
 * \param[in] verbosity  level of information to print
 * \param[in] param      set of parameters driving the convergence of the
 *                       iterative algorithm
 *
 * \return a pointer to the new allocated structure
 */
/*----------------------------------------------------------------------------*/

cs_iter_algo_t *
cs_iter_algo_create(int                    verbosity,
                    cs_param_sles_cvg_t    cvg_param);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Check if something wrong happens during the iterative process after
 *        one new iteration
 *
 * \param[in] func_name    name of the calling function
 * \param[in] eq_name      name of the equation being solved
 * \param[in] algo_name    name of the iterative algo. used
 * \param[in] algo         pointer to the iterative algo. structure
 */
/*----------------------------------------------------------------------------*/

void
cs_iter_algo_post_check(const char            *func_name,
                        const char            *eq_name,
                        const char            *algo_name,
                        cs_iter_algo_t        *algo);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Update the convergence status and the number of iterations. The
 *        tolerance threshold has to be computed outside the function and
 *        before calling this function.
 *
 * \param[in, out] algo      pointer to a cs_iter_algo_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_iter_algo_update_cvg(cs_iter_algo_t         *algo);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Update the convergence status and the number of iterations. The
 *        tolerance threshold is computed by a default formula relying on the
 *        relative tolerance scaled by the normalization factor and the
 *        absolute tolerance.
 *
 * \param[in, out] algo      pointer to a cs_iter_algo_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_iter_algo_update_cvg_default(cs_iter_algo_t         *algo);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Reset a cs_iter_algo_t structure in case of a non-linear algorothm
 *
 * \param[in]      nl_algo_type   type of non-linear algorithm
 * \param[in, out] algo           pointer to a cs_iter_algo_t
 */
/*----------------------------------------------------------------------------*/

void
cs_iter_algo_reset_nl(cs_param_nl_algo_t   nl_algo_type,
                      cs_iter_algo_t      *algo);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Create a new cs_iter_algo_aa_t structure for Anderson acceleration
 *
 * \param[in] aap          set of parameters for the Anderson acceleration
 * \param[in] n_elts       number of elements by direction
 *
 * \return a pointer to the new allocated structure
 */
/*----------------------------------------------------------------------------*/

cs_iter_algo_aa_t *
cs_iter_algo_aa_create(cs_iter_algo_param_aa_t    aap,
                       cs_lnum_t                  n_elts);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Retrieve the set of parameters for an Anderson algorithm
 *
 * \param[in, out] algo      pointer to a cs_iter_algo_t structure
 *
 * \return a cs_iter_algo_param_aa_t structure
 */
/*----------------------------------------------------------------------------*/

cs_iter_algo_param_aa_t
cs_iter_algo_get_anderson_param(cs_iter_algo_t         *algo);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Allocate arrays needed by the "Anderson acceleration" algorithm
 *
 * \param[in, out] aa    pointer to the structure managing the Anderson algo.
 */
/*----------------------------------------------------------------------------*/

void
cs_iter_algo_aa_allocate_arrays(cs_iter_algo_aa_t     *aa);

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Free arrays used during the Anderson acceleration
 *
 * \param[in, out] aa     pointer to the structure managing the Anderson algo.
 */
/*----------------------------------------------------------------------------*/

void
cs_iter_algo_aa_free_arrays(cs_iter_algo_aa_t     *aa);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Free a cs_iter_algo_aa_t structure inside a cs_iter_algo_t structure
 *        This structure is used to manage the Anderson acceleration
 *
 * \param[in, out] algo    pointer the main structure. Free the context part.
 */
/*----------------------------------------------------------------------------*/

void
cs_iter_algo_aa_free(cs_iter_algo_t  *algo);

/*----------------------------------------------------------------------------*/
/*!
 * \brief Apply one more iteration of the Anderson acceleration
 *
 * \param[in, out] algo           pointer to a cs_iter_algo_t structure
 * \param[in, out] cur_iterate    current iterate
 * \param[in]      pre_iterate    previous iterate
 * \param[in]      dotprod        function to compute a dot product
 * \param[in]      sqnorm         function to compute a square norm
 */
/*----------------------------------------------------------------------------*/

void
cs_iter_algo_aa_update(cs_iter_algo_t               *algo,
                       cs_real_t                    *cur_iterate,
                       const cs_real_t              *pre_iterate,
                       cs_cdo_blas_dotprod_t        *dotprod,
                       cs_cdo_blas_square_norm_t    *sqnorm);

/*----------------------------------------------------------------------------*/

END_C_DECLS

#endif /* __CS_ITER_ALGO_H__ */

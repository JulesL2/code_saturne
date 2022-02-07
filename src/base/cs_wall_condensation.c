/*============================================================================
 * Base wall condensation model data.
 *============================================================================*/

/*
  This file is part of code_saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2022 EDF S.A.

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

/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*----------------------------------------------------------------------------
 * Local headers
 *----------------------------------------------------------------------------*/

#include "bft_mem.h"
#include "bft_error.h"
#include "bft_printf.h"

#include "cs_defs.h"
#include "cs_field.h"
#include "cs_field_pointer.h"
#include "cs_log.h"
#include "cs_map.h"
#include "cs_parall.h"
#include "cs_parameters.h"
#include "cs_mesh_location.h"
#include "cs_time_step.h"

#include "cs_wall_functions.h"
#include "cs_thermal_model.h"
#include "cs_restart.h"
#include "cs_math.h"
#include "cs_physical_constants.h"
#include "cs_log_iteration.h"
#include "cs_gas_mix.h"
#include "cs_velocity_pressure.h"

/*----------------------------------------------------------------------------
 * Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_wall_condensation.h"
#include "cs_wall_condensation_priv.h"
#include "cs_wall_condensation_1d_thermal.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*=============================================================================
 * Macro definitions
 *============================================================================*/

/*============================================================================
 * Local type definitions
 *============================================================================*/

/*============================================================================
 * Global variables
 *============================================================================*/

//> Constants for the correlation of steam saturated pressure
static const cs_real_t pr_c = 221.2e+5;
static const cs_real_t t_c  = 647.3e0;
static const cs_real_t patm = 101320.0e0;
static const cs_real_t C_k1 = -  7.691234564e0;
static const cs_real_t C_k2 = - 26.08023696e0;
static const cs_real_t C_k3 = -168.1706546e0;
static const cs_real_t C_k4 =   64.23285504e0;
static const cs_real_t C_k5 = -118.9646225e0;
static const cs_real_t C_k6 =    4.16711732e0;
static const cs_real_t C_k7 =   20.9750676e0;
static const cs_real_t C_k8 = -  1.e+9;
static const cs_real_t C_k9 =    6.e0;

//> Caracteristic length
static const cs_real_t lcar = 1.0;
//> Turbulent Prandtl
static const cs_real_t Pr_tur = 0.9;
//> Latent heat of condensation (water)
static const cs_real_t lcond = 2278.0e+3;

static cs_wall_cond_t _wall_cond =
{
  .icondb     = -1,
  .natural_conv_model      = CS_WALL_COND_MODEL_COPAIN,
  .forced_conv_model       = CS_WALL_COND_MODEL_WALL_LAW, // fixed for now
  .mixed_conv_model        = CS_WALL_COND_MIXED_MAX , // fixed for now

   // Mesh related quantities
   // TODO : clean unnecessary quantities
  .nfbpcd     = 0,
  .ifbpcd     = NULL,
  .itypcd     = NULL,
  .izzftcd    = NULL,
  .spcond     = NULL,
  .hpcond     = NULL,
  .twall_cond = NULL,
  .thermal_condensation_flux = NULL,
  .convective_htc = NULL,
  .condensation_htc = NULL,
  .flthr      = NULL,
  .dflthr     = NULL,

  // Zone related quantities
  .nzones     = -1,
  .izcophc    = NULL,
  .izcophg    = NULL,
  .iztag1d    = NULL,
  .ztpar      = NULL,
  .zxrefcond  = NULL,
  .zprojcond  = NULL
};

const cs_wall_cond_t *cs_glob_wall_cond = &_wall_cond;

/*============================================================================
 * Fortran function prototypes
 *============================================================================*/

/*============================================================================
 * Prototypes for functions intended for use only by Fortran wrappers.
 * (descriptions follow, with function bodies).
 *============================================================================*/

void
cs_f_wall_condensation_get_model_pointers(int **icondb,
                                          cs_wall_cond_natural_conv_model_t **icondb_model);

void
cs_f_wall_condensation_get_size_pointers(cs_lnum_t **nfbpcd, cs_lnum_t **nzones);

void
cs_f_wall_condensation_get_pointers(cs_lnum_t **ifbpcd, cs_lnum_t **itypcd,
                                    cs_lnum_t **izzftcd, cs_real_t **spcond,
                                    cs_real_t **hpcond, cs_real_t **twall_cond,
                                    cs_real_t **thermflux, cs_real_t **flthr,
                                    cs_real_t **dflthr, cs_lnum_t **izcophc,
                                    cs_lnum_t **izcophg, cs_lnum_t **iztag1d,
                                    cs_real_t **ztpar, cs_real_t **xrefcond,
                                    cs_real_t **projcond);


void
cs_wall_condensation_set_model(cs_wall_cond_natural_conv_model_t model);

void
cs_wall_condensation_set_onoff_state(int icondb);

/*============================================================================
 * Private function definitions
 *============================================================================*/

/* Compute saturation pressure at given temperature (in Kelvins) */
cs_real_t
_compute_psat(cs_real_t temperature) {

  cs_real_t dtheta = temperature / t_c;
  cs_real_t dtheta_comp = 1.0 - dtheta;
  cs_real_t psat = pr_c * exp( (1.0/dtheta)
     * ( C_k1 * dtheta_comp
       + C_k2 * cs_math_pow2(dtheta_comp)
       + C_k3 * cs_math_pow3(dtheta_comp)
       + C_k4 * cs_math_pow4(dtheta_comp)
       + C_k5 * pow(dtheta_comp, 5.0))
     / (1.0 + C_k6 * dtheta_comp + C_k7 * cs_math_pow2(dtheta_comp))
     - dtheta_comp / (C_k8 * cs_math_pow2(dtheta_comp) + C_k9));
  return psat;
}

/* Compute mole fraction from mass fraction */
cs_real_t
_compute_mole_fraction(cs_real_t mass_fraction,
                     cs_real_t mix_mol_mas,
		     cs_real_t mol_mas) {
  return mass_fraction * mix_mol_mas / mol_mas;
}

/* Get wall temperature */
cs_real_t
_get_wall_temperature(int izone_fortran,
                      cs_lnum_t ieltcd_fortran) {

  cs_real_t temperature;
  if (_wall_cond.iztag1d[izone_fortran-1] == 1) {
    if ((cs_glob_time_step->nt_cur == 1) && !cs_restart_present()) {
      temperature = cs_glob_wall_cond_1d_thermal->ztpar0[izone_fortran-1];
    }
    else {
      temperature = cs_glob_wall_cond_1d_thermal->ztmur[ieltcd_fortran -1];
    }
  }
  else {
    temperature = _wall_cond.ztpar[izone_fortran-1];
  }

  temperature += cs_physical_constants_celsius_to_kelvin;
  return temperature;
}

/* Compute Mac Adams natural convection correlation for mass or
 * heat transfer exchange coefficient */
cs_real_t
_compute_mac_adams(cs_real_t theta,
                   cs_real_t grashof,
		   cs_real_t schmidt_or_prandtl) {
  return theta * 0.13 * pow(grashof*schmidt_or_prandtl, cs_math_1ov3);
}

cs_real_t
_compute_schlichting(cs_real_t theta,
    cs_real_t reynolds,
    cs_real_t schmidt_or_prandtl) {
  return theta * 0.0296 * pow(reynolds,0.8) * pow(schmidt_or_prandtl,cs_math_1ov3);
}

/* COmpute Grashof number */
cs_real_t
_compute_grashof(cs_real_t gravity,
                 cs_real_t drho,
		 cs_real_t length,
                 cs_real_t kin_viscosity) {
  return gravity * fabs(drho) * cs_math_pow3(length)
    / cs_math_pow2(kin_viscosity);
}

/* Compute characteristic length for schlichting model */
cs_real_t
_compute_characteristic_length(cs_real_3_t point,
    cs_real_3_t ref_point,
    cs_real_3_t ref_direction) {
  cs_real_t lcar = 0.0;
  for (int idir = 0; idir < 3; idir++) {
    lcar += (point[idir] - ref_point[idir]) * ref_direction[idir];
  }
  return lcar;
}

/* Compute tangential velocity (for schlichting model) */
cs_real_t
_compute_tangential_velocity(cs_real_3_t velocity,
    cs_real_3_t normal_vector,
    cs_real_t coeff) {
  cs_real_t u_square = 0.0;
  cs_real_t u_normal = 0.0;
  for (int idir = 0; idir < 3; idir++) {
    u_normal += velocity[idir] * normal_vector[idir] * coeff;
    u_square += velocity[idir] * velocity[idir];
  }
  return sqrt(u_square - u_normal * u_normal);
}

/* Compute convective exchange coefficient for forced regime */
cs_real_t
_compute_hconv_forced(const cs_lnum_t ieltcd) {

  // Mesh related indices
  const cs_lnum_t *ifabor = cs_glob_mesh->b_face_cells;
  const cs_lnum_t ifac = _wall_cond.ifbpcd[ieltcd]-1;
  const cs_lnum_t iel = ifabor[ifac];
  const int       iz = _wall_cond.izzftcd[ieltcd]-1;

  // Physical properties at ieltcd
  const cs_real_t rho = cs_field_by_name("density")->val[iel];
  const cs_real_t dyn_visc = cs_field_by_name("molecular_viscosity")->val[iel];
  const cs_real_t lambda_over_cp = cs_field_by_name("thermal_diffusivity")->val[iel];
  const cs_real_t kin_visc = dyn_visc / rho;
  const cs_real_t Pr_lam = dyn_visc / lambda_over_cp;
  const cs_real_t pressure =
     (cs_glob_velocity_pressure_model->idilat == 3) ? cs_glob_fluid_properties->pther : cs_glob_fluid_properties->p0;

  const cs_real_t t_wall =_get_wall_temperature(iz+1, ieltcd+1);
  cs_real_t mix_mol_mas = cs_field_by_name("mix_mol_mas")->val[iel];

  cs_field_t *f_vap = cs_field_by_name("y_h2o_g");
  cs_gas_mix_species_prop_t s_vap;
  const int k_id = cs_gas_mix_get_field_key();
  cs_field_get_key_struct(f_vap, k_id, &s_vap);

  cs_real_t y_vap_core = f_vap->val[iel];
  cs_real_t mol_mas_vap = s_vap.mol_mas;
  cs_real_t x_vap_core =_compute_mole_fraction(y_vap_core, mix_mol_mas, mol_mas_vap);

  cs_real_t psat = _compute_psat(t_wall);
  cs_real_t x_vap_int = psat / pressure;

  cs_real_t hconv;

  switch (_wall_cond.forced_conv_model) {
    case CS_WALL_COND_MODEL_WALL_LAW: ;
      const cs_real_t rough_t = 0.0;
      cs_real_t uk = 0.0;
      const cs_field_t *f_ustar = cs_field_by_name_try("ustar");
      if (f_ustar != NULL) {
    uk = f_ustar->val[ifac];
      }
      const cs_real_t dplus = 0.0;
      const cs_real_t yplus = cs_field_by_name("yplus")->val[ifac];
      const cs_wall_f_s_type_t iwalfs = cs_glob_wall_functions->iwalfs;
      cs_real_t hpflui, ypth;
      cs_wall_functions_scalar(iwalfs,
                       kin_visc,
                       Pr_lam,
                       Pr_tur,
                       rough_t,
                       uk,
                       yplus,
                       dplus,
                       &hpflui,
                       &ypth);

      const cs_real_t distbf = cs_glob_mesh_quantities->b_dist[ifac];
      hconv = lambda_over_cp * hpflui / distbf;
      break;
    case CS_WALL_COND_MODEL_SCHLICHTING:;
      cs_real_3_t *cdgfbo = (cs_real_3_t *)cs_glob_mesh_quantities->b_face_cog;
      const cs_real_3_t *surfbo
        = (const cs_real_3_t *)cs_glob_mesh_quantities->b_face_normal;
      const cs_real_t *surfbn = cs_glob_mesh_quantities->b_face_surf;
      const cs_real_3_t *velocity = (cs_real_3_t *)cs_field_by_name("velocity")->val;
      const cs_real_3_t *n_ref = (cs_real_3_t *)_wall_cond.zprojcond;
      const cs_real_3_t *x_ref = (cs_real_3_t *)_wall_cond.zxrefcond;
      cs_real_3_t n_ref_norm;
      cs_math_3_normalize(n_ref[iz], n_ref_norm);
      const cs_real_t lcar = _compute_characteristic_length(cdgfbo[ifac],
	  x_ref[iz], n_ref_norm);
      const cs_real_t u_ref = _compute_tangential_velocity(velocity[iel],
	  surfbo[ifac], 1.0/surfbn[ifac]);
      // Reynolds number
      const cs_real_t Re = rho * u_ref * lcar / dyn_visc;
      cs_real_t theta = 1.0;
      if (x_vap_int < x_vap_core) {
	// here a choice between the different suction coefficients must be made
	// => we choose to use the updated value of Benteboula and Dabbene
        theta = 0.8254 + 0.616 * (x_vap_core-x_vap_int) / (1.0 - x_vap_int);
      }
      const cs_real_t Nu = _compute_schlichting(theta, Re, Pr_lam);
      hconv = lambda_over_cp * Nu / lcar;
      break;
  }

  return hconv;
}

/* Compute convective exchange coefficient for natural regime */
cs_real_t
_compute_hconv_natural(const cs_lnum_t ieltcd) {

  // Mesh related indices
  const cs_lnum_t *ifabor = cs_glob_mesh->b_face_cells;
  const cs_lnum_t ifac = _wall_cond.ifbpcd[ieltcd]-1;
  const cs_lnum_t iel = ifabor[ifac];
  const int       iz = _wall_cond.izzftcd[ieltcd]-1;

  const cs_real_t gravity = cs_math_3_norm(cs_glob_physical_constants->gravity);
  const cs_real_t pressure =
     (cs_glob_velocity_pressure_model->idilat == 3) ? cs_glob_fluid_properties->pther : cs_glob_fluid_properties->p0;
  const int nscasp = cs_glob_gas_mix->n_species;

  // Physical properties at ieltcd
  const cs_real_t rho = cs_field_by_name("density")->val[iel];
  const cs_real_t dyn_visc = cs_field_by_name("molecular_viscosity")->val[iel];
  const cs_real_t lambda_over_cp = cs_field_by_name("thermal_diffusivity")->val[iel];
  const cs_real_t kin_visc = dyn_visc / rho;
  const cs_real_t Pr_lam = dyn_visc / lambda_over_cp;

  const cs_real_t t_wall =_get_wall_temperature(iz+1, ieltcd+1);
  const cs_real_t t_inf = cs_field_by_name("tempk")->val[iel];

  cs_real_t mix_mol_mas = cs_field_by_name("mix_mol_mas")->val[iel];
  cs_real_t mol_mas_ncond = cs_field_by_name("mol_mas_ncond")->val[iel];

  cs_field_t *f_vap = cs_field_by_name("y_h2o_g");
  cs_gas_mix_species_prop_t s_vap;
  const int k_id = cs_gas_mix_get_field_key();
  cs_field_get_key_struct(f_vap, k_id, &s_vap);

  cs_real_t y_vap_core = f_vap->val[iel];
  cs_real_t mol_mas_vap = s_vap.mol_mas;
  cs_real_t x_vap_core =_compute_mole_fraction(y_vap_core, mix_mol_mas, mol_mas_vap);

  cs_real_t psat = _compute_psat(t_wall);
  cs_real_t x_vap_int = psat / pressure;

  cs_real_t theta = 1.0;
  cs_real_t Gr, Nu, drho, hpcond;

  switch (_wall_cond.natural_conv_model) {
    case CS_WALL_COND_MODEL_COPAIN: {
      drho = fabs((t_inf - t_wall)/t_inf);

      if (x_vap_int < x_vap_core) // condensation
        theta = 1.0 + 0.625 * (x_vap_core-x_vap_int) / (1.0 - x_vap_int);
      break;				    }

    case CS_WALL_COND_MODEL_COPAIN_BD: {

      cs_real_t y_nc_core = 1.0 - y_vap_core;
      cs_real_t mix_mol_mas_int = x_vap_int*mol_mas_vap + (1.0 - x_vap_int)*mol_mas_ncond;
      cs_real_t y_nc_int  = (1.0 - x_vap_int) * mol_mas_ncond / mix_mol_mas_int;

      drho = fabs(1.0 - t_wall/t_inf +
	  (y_nc_int - y_nc_core) / (mol_mas_ncond / (mol_mas_ncond - mol_mas_vap) - y_nc_int));

      if (x_vap_int < x_vap_core) // condensation
        theta = 0.8254 + 0.616 * (x_vap_core-x_vap_int) / (1.0 - x_vap_int);
      break;
				       }
    case CS_WALL_COND_MODEL_UCHIDA: {
      cs_real_t mol_mas_int = x_vap_int*mol_mas_vap + (1.0 - x_vap_int)*mol_mas_ncond;
      cs_real_t rho_wall = pressure * mol_mas_int / (cs_physical_constants_r * t_wall);
      drho = fabs(rho_wall - rho) / rho; // different from Fortran

      if (x_vap_int < x_vap_core) // condensation
        theta = 1.0 + 0.625 * (x_vap_core-x_vap_int) / (1.0 - x_vap_int);
      break;				    }

    case CS_WALL_COND_MODEL_DEHBI: {
      cs_real_t mol_mas_int = x_vap_int*mol_mas_vap + (1.0 - x_vap_int)*mol_mas_ncond;
      cs_real_t rho_wall = pressure * mol_mas_int / (cs_physical_constants_r * t_wall);
      drho = fabs(rho_wall - rho) / rho; // different from Fortran

      if (x_vap_int < x_vap_core) { // condensation
	cs_real_t y_vap_int = x_vap_int * mol_mas_vap / mol_mas_int;
	cs_real_t B = (y_vap_int - y_vap_core) / (1.0 - y_vap_int);
        theta = 1.33 * log(1.0 + B) / B;
      }
      break;				    }
    default:
      bft_error(__FILE__, __LINE__, 0,
	  "Requested model is unknown or not yet implemented: %d\n",
	 _wall_cond.natural_conv_model);

  }

  Gr = _compute_grashof(gravity, drho, lcar, kin_visc);
  Nu = _compute_mac_adams(theta, Gr, Pr_lam);
  hpcond = lambda_over_cp * Nu / lcar ;
  return hpcond;
}

/* Compute convective exchange coefficient for mixed regime */
cs_real_t
_compute_hconv_mixed(const cs_lnum_t ieltcd) {
  const int       iz = _wall_cond.izzftcd[ieltcd]-1;
  cs_real_t hconv_forced = _compute_hconv_forced(ieltcd);
  cs_real_t hconv_natural = _compute_hconv_natural(ieltcd);
  cs_real_t hconv;
  switch (_wall_cond.mixed_conv_model) {
    case CS_WALL_COND_MIXED_MAX:
      hconv = cs_math_fmax(hconv_forced, hconv_natural);
      break;
    case CS_WALL_COND_MIXED_INCROPERA: ;
      const cs_lnum_t *ifabor = cs_glob_mesh->b_face_cells;
      const cs_lnum_t ifac = _wall_cond.ifbpcd[ieltcd]-1;
      const cs_lnum_t iel = ifabor[ifac];
      const cs_real_3_t *velocity = (cs_real_3_t *)cs_field_by_name("velocity")->val;
      const cs_real_t g_dot_u =
	cs_math_3_dot_product(cs_glob_physical_constants->gravity, velocity);
      if (g_dot_u > 0) { // buoyancy-aided flow
	hconv = fabs(hconv_forced - hconv_natural);
      }
      else { // buoyance-opposed flow
	hconv = fabs(hconv_forced + hconv_natural);
      }
      break;
  }
  return hconv;
}

cs_real_t
_compute_exchange_coefficient_convection(const cs_lnum_t ieltcd) {

  cs_real_t hconv;
  const int       iz = _wall_cond.izzftcd[ieltcd]-1;
  switch (_wall_cond.izcophg[iz]) {
    case 1: // forced convection
      hconv = _compute_hconv_forced(ieltcd);
      break;
    case 2: // natural convection
      hconv = _compute_hconv_natural(ieltcd);
      break;
    case 3 : // mixed convection
      hconv = _compute_hconv_mixed(ieltcd);
  }

  return hconv;
}

/* Compute condensation exchange coefficient for forced regime */
cs_real_t
_compute_hcond_forced(const cs_lnum_t ieltcd) {

  // Mesh related indices
  const cs_lnum_t *ifabor = cs_glob_mesh->b_face_cells;
  const cs_lnum_t ifac = _wall_cond.ifbpcd[ieltcd]-1;
  const cs_lnum_t iel = ifabor[ifac];
  const int       iz = _wall_cond.izzftcd[ieltcd]-1;

  // Physical properties at ieltcd
  const cs_real_t rho = cs_field_by_name("density")->val[iel];
  const cs_real_t dyn_visc = cs_field_by_name("molecular_viscosity")->val[iel];
  const cs_real_t mass_diffusion = cs_field_by_name("steam_binary_diffusion")->val[iel];
  const cs_real_t kin_visc = dyn_visc / rho;
  const cs_real_t Sch = kin_visc / mass_diffusion;

  cs_field_t *f_vap = cs_field_by_name("y_h2o_g");
  cs_gas_mix_species_prop_t s_vap;
  const int k_id = cs_gas_mix_get_field_key();
  cs_field_get_key_struct(f_vap, k_id, &s_vap);

  cs_real_t y_vap_core = f_vap->val[iel];
  cs_real_t mol_mas_vap = s_vap.mol_mas;

  const cs_real_t t_wall =_get_wall_temperature(iz+1, ieltcd+1);
  cs_real_t psat = _compute_psat(t_wall);
  const cs_real_t pressure =
     (cs_glob_velocity_pressure_model->idilat == 3) ? cs_glob_fluid_properties->pther : cs_glob_fluid_properties->p0;

  const cs_real_t mol_mas_ncond = cs_field_by_name("mol_mas_ncond")->val[iel];
  cs_real_t x_vap_int = psat / pressure;
  cs_real_t mol_mas_int = x_vap_int*mol_mas_vap + (1.0 - x_vap_int)*mol_mas_ncond;
  cs_real_t y_vap_int  = x_vap_int * mol_mas_vap / mol_mas_int;

  cs_real_t kcond;

  switch (_wall_cond.forced_conv_model) {
    case CS_WALL_COND_MODEL_WALL_LAW: ;
      const cs_real_t rough_t = 0.0;
      cs_real_t uk = 0.0;
      const cs_field_t *f_ustar = cs_field_by_name_try("ustar");
      if (f_ustar != NULL) {
    uk = f_ustar->val[ifac];
      }
      const cs_real_t dplus = 0.0;
      const cs_real_t yplus = cs_field_by_name("yplus")->val[ifac];
      const cs_wall_f_s_type_t iwalfs = cs_glob_wall_functions->iwalfs;
      cs_real_t hpflui, ypth;
      cs_wall_functions_scalar(iwalfs,
                       kin_visc,
                       Sch,
                       Pr_tur,
                       rough_t,
                       uk,
                       yplus,
                       dplus,
                       &hpflui,
                       &ypth);

      const cs_real_t distbf = cs_glob_mesh_quantities->b_dist[ifac];
      kcond = mass_diffusion * hpflui / distbf;
      break;
    case CS_WALL_COND_MODEL_SCHLICHTING: ;
      cs_real_3_t *cdgfbo = (cs_real_3_t *)cs_glob_mesh_quantities->b_face_cog;
      const cs_real_3_t *surfbo
        = (const cs_real_3_t *)cs_glob_mesh_quantities->b_face_normal;
      const cs_real_t *surfbn = cs_glob_mesh_quantities->b_face_surf;
      const cs_real_3_t *velocity = (cs_real_3_t *)cs_field_by_name("velocity")->val;
      const cs_real_3_t *n_ref = (cs_real_3_t *)_wall_cond.zprojcond;
      const cs_real_3_t *x_ref = (cs_real_3_t *)_wall_cond.zxrefcond;
      cs_real_3_t n_ref_norm;
      cs_math_3_normalize(n_ref[iz], n_ref_norm);
      const cs_real_t lcar = _compute_characteristic_length(cdgfbo[ifac],
	  x_ref[iz], n_ref_norm);
      const cs_real_t u_ref = _compute_tangential_velocity(velocity[iel],
	  surfbo[ifac], 1.0/surfbn[ifac]);
      // Reynolds number
      const cs_real_t Re = rho * u_ref * lcar / dyn_visc;
      const cs_real_t mix_mol_mas = cs_field_by_name("mix_mol_mas")->val[iel];
      cs_real_t x_vap_core =_compute_mole_fraction(y_vap_core, mix_mol_mas, mol_mas_vap);
      // => we choose to use the updated values of Benteboula and Dabbene for theta
      cs_real_t theta = 0.8254 + 0.616 * (x_vap_core-x_vap_int) / (1.0 - x_vap_int);
      const cs_real_t She = _compute_schlichting(theta, Re, Sch);
      kcond = mass_diffusion * She / lcar;
      break;
  }

  const cs_real_t t_inf = cs_field_by_name("tempk")->val[iel];
  cs_real_t hcond = kcond * rho * (y_vap_core - y_vap_int) / (1 - y_vap_int) * lcond / (t_inf - t_wall);
  return ;
}

/* Compute condensation exchange coefficient for natural regime */
cs_real_t
_compute_hcond_natural(const cs_lnum_t ieltcd) {

  // Mesh related indices
  const cs_lnum_t *ifabor = cs_glob_mesh->b_face_cells;
  const cs_lnum_t ifac = _wall_cond.ifbpcd[ieltcd]-1;
  const cs_lnum_t iel = ifabor[ifac];
  const int       iz = _wall_cond.izzftcd[ieltcd]-1;

  const cs_real_t gravity = cs_math_3_norm(cs_glob_physical_constants->gravity);
  const cs_real_t pressure =
     (cs_glob_velocity_pressure_model->idilat == 3) ? cs_glob_fluid_properties->pther : cs_glob_fluid_properties->p0;
  const int nscasp = cs_glob_gas_mix->n_species;

  // Physical properties at ieltcd
  const cs_real_t rho = cs_field_by_name("density")->val[iel];
  const cs_real_t dyn_visc = cs_field_by_name("molecular_viscosity")->val[iel];
  const cs_real_t lambda_over_cp = cs_field_by_name("thermal_diffusivity")->val[iel];
  const cs_real_t mass_diffusion = cs_field_by_name("steam_binary_diffusion")->val[iel];
  const cs_real_t kin_visc = dyn_visc / rho;
  const cs_real_t Sch = kin_visc / mass_diffusion;

  const cs_real_t t_wall =_get_wall_temperature(iz+1, ieltcd+1);
  const cs_real_t t_inf = cs_field_by_name("tempk")->val[iel];

  cs_real_t mix_mol_mas = cs_field_by_name("mix_mol_mas")->val[iel];
  cs_real_t mol_mas_ncond = cs_field_by_name("mol_mas_ncond")->val[iel];

  cs_field_t *f_vap = cs_field_by_name("y_h2o_g");
  cs_gas_mix_species_prop_t s_vap;
  const int k_id = cs_gas_mix_get_field_key();
  cs_field_get_key_struct(f_vap, k_id, &s_vap);

  cs_real_t y_vap_core = f_vap->val[iel];
  cs_real_t mol_mas_vap = s_vap.mol_mas;
  cs_real_t x_vap_core = _compute_mole_fraction(y_vap_core, mix_mol_mas, mol_mas_vap);

  cs_real_t psat = _compute_psat(t_wall);
  cs_real_t x_vap_int = psat / pressure;
  cs_real_t mol_mas_int = x_vap_int*mol_mas_vap + (1.0 - x_vap_int)*mol_mas_ncond;
  cs_real_t y_vap_int  = x_vap_int * mol_mas_vap / mol_mas_int;

  if (x_vap_int >= x_vap_core) // no condensation
    return 0.0;

  cs_real_t theta = 1.0;
  cs_real_t Gr, She, drho, h_cond;

  switch (_wall_cond.natural_conv_model) {
    case CS_WALL_COND_MODEL_COPAIN: {
      drho = fabs((t_inf - t_wall)/t_inf);
      theta = 1.0 + 0.625 * (x_vap_core-x_vap_int) / (1.0 - x_vap_int);
      Gr = _compute_grashof(gravity, drho, lcar, kin_visc);
      She = _compute_mac_adams(theta, Gr, Sch);
      h_cond = mass_diffusion * She / lcar * rho * (y_vap_core - y_vap_int) /
	(1.0 - y_vap_int) * lcond / (t_inf - t_wall) ;
      break;				    }

    case CS_WALL_COND_MODEL_COPAIN_BD: {

      drho = fabs(1.0 - t_wall/t_inf +
	  (y_vap_core - y_vap_int) / (mol_mas_ncond / (mol_mas_ncond - mol_mas_vap) - 1.0 + y_vap_int));
      theta = 0.8254 + 0.616 * (x_vap_core-x_vap_int) / (1.0 - x_vap_int);
      Gr = _compute_grashof(gravity, drho, lcar, kin_visc);
      She = _compute_mac_adams(theta, Gr, Sch);
      h_cond = mass_diffusion * She / lcar * rho * (y_vap_core - y_vap_int) /
	(1.0 - y_vap_int) * lcond / (t_inf - t_wall);
      break;
				       }
    case CS_WALL_COND_MODEL_UCHIDA:;
      cs_real_t h_uchida = 380.0 * pow((1.0 - y_vap_core) / y_vap_core, -0.7);
      cs_real_t cp = cs_field_by_name("specific_heat")->val[iel];
      h_cond = h_uchida - _wall_cond.hpcond[ieltcd] * cp;
      break;

    case CS_WALL_COND_MODEL_DEHBI:;
      cs_real_t rho_wall = pressure * mol_mas_int / (cs_physical_constants_r * t_wall);
      drho = fabs(rho_wall - rho) / rho; // different from Fortran

      cs_real_t B = (y_vap_int - y_vap_core) / (1.0 - y_vap_int);
      theta = 1.33 * log(1.0 + B) / B;
      Gr = _compute_grashof(gravity, drho, lcar, kin_visc);
      She = _compute_mac_adams(theta, Gr, Sch);
      cs_real_t h_dehbi = mass_diffusion * She / lcar * rho * (y_vap_core - y_vap_int)
       / (1.0 - y_vap_int) * lcond / (t_inf - t_wall);
      h_cond = h_dehbi - _wall_cond.hpcond[ieltcd] * cp;
      break;
    default:
      bft_error(__FILE__, __LINE__, 0,
	  "Requested model is unknown or not yet implemented: %d\n",
	 _wall_cond.natural_conv_model);

  }

  return h_cond;
}

/* Compute condensation exchange coefficient for mixed regime */
cs_real_t
_compute_hcond_mixed(const cs_lnum_t ieltcd) {
  cs_real_t hcond_forced = _compute_hcond_forced(ieltcd);
  cs_real_t hcond_natural = _compute_hcond_natural(ieltcd);
  cs_real_t hcond;
  switch (_wall_cond.mixed_conv_model) {
    case CS_WALL_COND_MIXED_MAX:
      hcond = cs_math_fmax(hcond_forced, hcond_natural);
      break;
    case CS_WALL_COND_MIXED_INCROPERA:;
      const cs_lnum_t *ifabor = cs_glob_mesh->b_face_cells;
      const cs_lnum_t ifac = _wall_cond.ifbpcd[ieltcd]-1;
      const cs_lnum_t iel = ifabor[ifac];
      const cs_real_3_t *velocity = (cs_real_3_t *)cs_field_by_name("velocity")->val;
      const cs_real_t g_dot_u =
	cs_math_3_dot_product(cs_glob_physical_constants->gravity, velocity);
      if (g_dot_u > 0) { // buoyancy-aided flow
	hcond = fabs(hcond_forced - hcond_natural);
      }
      else { // buoyance-opposed flow
	hcond = fabs(hcond_forced + hcond_natural);
      }
      break;
  }
  return hcond;
}

/* Compute condensation exchange coefficient*/
cs_real_t
_compute_exchange_coefficient_condensation(const cs_lnum_t ieltcd) {
  cs_real_t hcond;
  const int iz = _wall_cond.izzftcd[ieltcd]-1;
  switch (_wall_cond.izcophc[iz]) {
    case 1: // forced convection
      hcond = _compute_hcond_forced(ieltcd);
      break;
    case 2: // natural convection
      hcond = _compute_hcond_natural(ieltcd);
      break;
    case 3 : // mixed convection
      hcond = _compute_hcond_mixed(ieltcd);
  }
  return hcond;
}

/*============================================================================
 * Fortran wrapper function definitions
 *============================================================================*/

void
cs_f_wall_condensation_get_model_pointers(int **icondb,
                                          cs_wall_cond_natural_conv_model_t **icondb_model)
{
  *icondb        = &(_wall_cond.icondb);
  *icondb_model  = &(_wall_cond.natural_conv_model);
}

void
cs_f_wall_condensation_get_size_pointers(cs_lnum_t **nfbpcd, cs_lnum_t **nzones)
{
  *nfbpcd = &(_wall_cond.nfbpcd);
  *nzones = &(_wall_cond.nzones);
}

void
cs_f_wall_condensation_get_pointers(cs_lnum_t **ifbpcd, cs_lnum_t **itypcd,
                                    cs_lnum_t **izzftcd, cs_real_t **spcond,
                                    cs_real_t **hpcond, cs_real_t **twall_cond,
                                    cs_real_t **thermflux, cs_real_t **flthr,
                                    cs_real_t **dflthr, cs_lnum_t **izcophc,
                                    cs_lnum_t **izcophg, cs_lnum_t **iztag1d,
                                    cs_real_t **ztpar, cs_real_t **zxrefcond,
                                    cs_real_t **zprojcond)
{
  *ifbpcd = _wall_cond.ifbpcd ;
  *itypcd = _wall_cond.itypcd;
  *izzftcd = _wall_cond.izzftcd;
  *spcond = _wall_cond.spcond ;
  *hpcond = _wall_cond.hpcond ;
  *twall_cond = _wall_cond.twall_cond;
  *thermflux = _wall_cond.thermal_condensation_flux;
  *flthr = _wall_cond.flthr;
  *dflthr = _wall_cond.dflthr;
  *izcophc = _wall_cond.izcophc;
  *izcophg = _wall_cond.izcophg;
  *iztag1d = _wall_cond.iztag1d;
  *ztpar = _wall_cond.ztpar;
  *zxrefcond = _wall_cond.zxrefcond;
  *zprojcond = _wall_cond.zprojcond;
}

/*! \cond DOXYGEN_SHOULD_SKIP_THIS */

/*============================================================================
 * Public function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set the wall condensation model
 *
 * \param[in] model    integer corresponding to the desired model
 */
/*----------------------------------------------------------------------------*/

void
cs_wall_condensation_set_model(cs_wall_cond_natural_conv_model_t  model)
{
  _wall_cond.natural_conv_model = model;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set the onoff state of wall condensation modeling
 *
 * \param[in] icondb integer corresponding to the onoff state (-1 : off, 0: on)
 */
/*----------------------------------------------------------------------------*/

void
cs_wall_condensation_set_onoff_state(int  icondb)
{
  _wall_cond.icondb = icondb;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Create the context for wall condensation models.
 *
 * \param[in] nfbpcd   number of faces with wall condensation
 * \param[in] nzones   number of zones with wall condensation
 * \param[in] nvar     number of variables (?)
 */
/*----------------------------------------------------------------------------*/

void
cs_wall_condensation_create(cs_lnum_t  nfbpcd,
                            cs_lnum_t  nzones,
                            cs_lnum_t  nvar)
{
  _wall_cond.nfbpcd = nfbpcd;
  if (nzones < 1) {
    _wall_cond.nzones = 1;
  }
  else {
    _wall_cond.nzones = nzones;
  }

  // Mesh related quantities
  BFT_MALLOC(_wall_cond.ifbpcd, nfbpcd, cs_lnum_t);
  BFT_MALLOC(_wall_cond.itypcd, nfbpcd*nvar, cs_lnum_t);
  BFT_MALLOC(_wall_cond.izzftcd, nfbpcd, cs_lnum_t);
  BFT_MALLOC(_wall_cond.spcond, nfbpcd*nvar, cs_real_t);
  BFT_MALLOC(_wall_cond.hpcond, nfbpcd, cs_real_t);
  BFT_MALLOC(_wall_cond.twall_cond, nfbpcd, cs_real_t);
  BFT_MALLOC(_wall_cond.thermal_condensation_flux, nfbpcd, cs_real_t);
  BFT_MALLOC(_wall_cond.convective_htc, nfbpcd, cs_real_t);
  BFT_MALLOC(_wall_cond.condensation_htc, nfbpcd, cs_real_t);
  BFT_MALLOC(_wall_cond.flthr, nfbpcd, cs_real_t);
  BFT_MALLOC(_wall_cond.dflthr, nfbpcd, cs_real_t);

  // Zone related quantities
  BFT_MALLOC(_wall_cond.izcophc, nzones, cs_lnum_t);
  BFT_MALLOC(_wall_cond.izcophg, nzones, cs_lnum_t);
  BFT_MALLOC(_wall_cond.iztag1d, nzones, cs_lnum_t);
  BFT_MALLOC(_wall_cond.ztpar, nzones, cs_real_t);
  BFT_MALLOC(_wall_cond.zxrefcond, 3*nzones, cs_real_t);
  BFT_MALLOC(_wall_cond.zprojcond, 3*nzones, cs_real_t);

  for (cs_lnum_t i= 0 ; i< nfbpcd; i++) {
    _wall_cond.ifbpcd[i] = 0;
    _wall_cond.hpcond[i] = 0.0;
    _wall_cond.twall_cond[i] = 0.0;
    _wall_cond.thermal_condensation_flux[i] = 0.0;
    _wall_cond.convective_htc[i] = 0.0;
    _wall_cond.condensation_htc[i] = 0.0;
    _wall_cond.flthr[i] = 0.0;
    _wall_cond.dflthr[i] = 0.0;
    if (_wall_cond.nzones <= 1) {
      _wall_cond.izzftcd[i] = 1;
    }
    else {
      _wall_cond.izzftcd[i] = 0;
    }
    for (cs_lnum_t j=0; j<nvar; j++) {
      _wall_cond.itypcd[nvar*i+j] = 0;
      _wall_cond.spcond[nvar*i+j] = 0.0;
    }
  }

  for (cs_lnum_t i=0; i<nzones; i++) {
    _wall_cond.izcophc[i] = 0;
    _wall_cond.izcophg[i] = 0;
    _wall_cond.iztag1d[i] = 0;
    _wall_cond.ztpar[i] = -1.0;
    _wall_cond.zxrefcond[3*i] = 0.0;
    _wall_cond.zxrefcond[3*i+1] = 0.0;
    _wall_cond.zxrefcond[3*i+2] = 0.0;
    _wall_cond.zprojcond[3*i] = 0.0;
    _wall_cond.zprojcond[3*i+1] = 0.0;
    _wall_cond.zprojcond[3*i+2] = 0.0;
  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Free all structures related to wall condensation models
 */
/*----------------------------------------------------------------------------*/

void
cs_wall_condensation_free(void)
{
  BFT_FREE(_wall_cond.ifbpcd);
  BFT_FREE(_wall_cond.itypcd);
  BFT_FREE(_wall_cond.izzftcd);
  BFT_FREE(_wall_cond.spcond);
  BFT_FREE(_wall_cond.hpcond);
  BFT_FREE(_wall_cond.twall_cond);
  BFT_FREE(_wall_cond.thermal_condensation_flux);
  BFT_FREE(_wall_cond.convective_htc);
  BFT_FREE(_wall_cond.condensation_htc);
  BFT_FREE(_wall_cond.flthr);
  BFT_FREE(_wall_cond.dflthr);

  BFT_FREE(_wall_cond.izcophc);
  BFT_FREE(_wall_cond.izcophg);
  BFT_FREE(_wall_cond.iztag1d);
  BFT_FREE(_wall_cond.ztpar);
  BFT_FREE(_wall_cond.zxrefcond);
  BFT_FREE(_wall_cond.zprojcond);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Compute the wall condensation source terms.
 *
 * \param[in] nvar     number of variables (?)
 * \param[in] izzftcd  pointer to the table connecting faces to their
 *                     condensation zone
 *
 * \return
 */
/*----------------------------------------------------------------------------*/

void
cs_wall_condensation_compute()
{
  cs_field_t *f = cs_field_by_name("pressure");
  const int var_id_key = cs_field_key_id("variable_id");
  const int ipr = cs_field_get_key_int(f, var_id_key)-1;
  cs_lnum_t *ifabor = cs_glob_mesh->b_face_cells;
  cs_field_t *f_enth = cs_thermal_model_field();
  if (CS_F_(cp) == NULL)
    bft_error(__FILE__, __LINE__, 0,_("error lambda not variable\n"));
  cs_real_t *cpro_cp = CS_F_(cp)->val;

  for (int ii=0; ii<_wall_cond.nfbpcd; ii++) {

    cs_lnum_t ifac_fort = _wall_cond.ifbpcd[ii]; // TODO update when switching ifbpcd to C numbering
    cs_lnum_t ifac_c = ifac_fort - 1;
    cs_lnum_t iel_c  = ifabor[ifac_c];
    int iz_fort   = _wall_cond.izzftcd[ii];
    int iz_c = iz_fort - 1;

    const cs_real_t t_wall =_get_wall_temperature(iz_fort, ii+1);
    const cs_real_t t_inf = cs_field_by_name("tempk")->val[iel_c];
    cs_real_t h_conv = _compute_exchange_coefficient_convection(ii);
    cs_real_t h_cond = _compute_exchange_coefficient_condensation(ii);
    cs_real_t flux = h_cond * (t_inf - t_wall) + h_conv * cpro_cp[iel_c] * (t_inf - t_wall);
    _wall_cond.convective_htc[ii] = h_conv * cpro_cp[iel_c];
    _wall_cond.condensation_htc[ii] = h_cond;
    _wall_cond.hpcond[ii] = h_conv;
    _wall_cond.thermal_condensation_flux[ii] = flux;
    _wall_cond.spcond[ipr * _wall_cond.nfbpcd + ii] -= h_cond * (t_inf - t_wall) / lcond;
    if (_wall_cond.iztag1d[iz_c] == 1) {
      _wall_cond.flthr[ii] = flux;
      _wall_cond.dflthr[ii] = 0.0;
    }
  }

  if (cs_glob_time_step->nt_cur % cs_glob_log_frequency == 0)
    cs_wall_condensation_log();
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Print information about min/max values of condensation exchange
 */
/*----------------------------------------------------------------------------*/

void
cs_wall_condensation_log(void) {

  const cs_field_t *f = cs_field_by_name("pressure");
  const int var_id_key = cs_field_key_id("variable_id");
  const int ipr = cs_field_get_key_int(f, var_id_key)-1;
  const cs_real_t *cpro_cp = cs_field_by_name("specific_heat")->val;
  cs_lnum_t *ifabor = cs_glob_mesh->b_face_cells;

  cs_real_t gamma_cond = 0.0;
  cs_real_t h_conv_min =  cs_math_infinite_r;
  cs_real_t h_cond_min =  cs_math_infinite_r;
  cs_real_t flux_min   =  cs_math_infinite_r;
  cs_real_t h_conv_max = -cs_math_infinite_r;
  cs_real_t h_cond_max = -cs_math_infinite_r;
  cs_real_t flux_max   = -cs_math_infinite_r;

  for (cs_lnum_t ii = 0; ii < _wall_cond.nfbpcd; ii++) {
    cs_lnum_t ifac = _wall_cond.ifbpcd[ii]-1;
    cs_lnum_t iel = ifabor[ifac];
    gamma_cond += _wall_cond.spcond[ipr * _wall_cond.nfbpcd + ii];
    h_conv_min = cs_math_fmin(h_conv_min, _wall_cond.convective_htc[ii]);
    h_conv_max = cs_math_fmax(h_conv_max, _wall_cond.convective_htc[ii]);
    h_cond_min = cs_math_fmin(h_cond_min, _wall_cond.condensation_htc[ii]);
    h_cond_max = cs_math_fmax(h_cond_max, _wall_cond.condensation_htc[ii]);
    flux_min = cs_math_fmin(flux_min, _wall_cond.thermal_condensation_flux[ii]);
    flux_max = cs_math_fmax(flux_max, _wall_cond.thermal_condensation_flux[ii]);
  }

  if (cs_glob_rank_id >= 0) {
    cs_parall_min(1, CS_DOUBLE, &h_conv_min);
    cs_parall_max(1, CS_DOUBLE, &h_conv_max);
    cs_parall_min(1, CS_DOUBLE, &h_cond_min);
    cs_parall_max(1, CS_DOUBLE, &h_cond_max);
    cs_parall_min(1, CS_DOUBLE, &flux_min);
    cs_parall_max(1, CS_DOUBLE, &flux_max);
    cs_parall_sum(1, CS_DOUBLE, &gamma_cond);
  }

  bft_printf(" Minmax values of convective HTC [W/m2/K]   : %15.12e    %15.12e\n",
      h_conv_min, h_conv_max);
  bft_printf(" Minmax values of condensation HTC [W/m2/K] : %15.12e    %15.12e\n",
      h_cond_min, h_cond_max);
  bft_printf(" Minmax values of thermal flux [W/m2]       : %15.12e    %15.12e\n",
      flux_min, flux_max);
  bft_printf(" Total mass sink term [kg/s/m2]             : %15.12e\n", gamma_cond);

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Provide writable access to _wall_cond structure.
 *
 * \return pointer to global wall_cond structure
 */
/*----------------------------------------------------------------------------*/

cs_wall_cond_t *
cs_get_glob_wall_cond(void)
{
  return &_wall_cond;
}

/*----------------------------------------------------------------------------*/

END_C_DECLS

!-------------------------------------------------------------------------------

! This file is part of code_saturne, a general-purpose CFD tool.
!
! Copyright (C) 1998-2023 EDF S.A.
!
! This program is free software; you can redistribute it and/or modify it under
! the terms of the GNU General Public License as published by the Free Software
! Foundation; either version 2 of the License, or (at your option) any later
! version.
!
! This program is distributed in the hope that it will be useful, but WITHOUT
! ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
! FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
! details.
!
! You should have received a copy of the GNU General Public License along with
! this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
! Street, Fifth Floor, Boston, MA 02110-1301, USA.

!-------------------------------------------------------------------------------

!===============================================================================
! Function:
! ---------

!> \file resvoi.f90
!>
!> \brief Solve the void fraction \f$ \alpha \f$ for the Volume of Fluid
!>        method (and hence for cavitating flows).
!>
!> This function solves:
!> \f[
!> \dfrac{\alpha^n - \alpha^{n-1}}{\Delta t}
!>     + \divs \left( \alpha^n \vect{u}^n \right)
!>     + \divs \left( \left[ \alpha^n
!>                           \left( 1 - \alpha^{n} \right)
!>                    \right] \vect{u^r}^n \right)
!>     = \dfrac{\Gamma_V \left( \alpha^{n-1}, p^n \right)}{\rho_v}
!> \f]
!> with \f$ \Gamma_V \f$ the eventual vaporization source term (Merkle model) in
!> case the cavitation model is enabled, \f$ \rho_v \f$ the reference gas
!> density and \f$ \vect{u^r} \f$ the drift velocity for the compressed
!> interface.
!>
!-------------------------------------------------------------------------------

!-------------------------------------------------------------------------------
! Arguments
!______________________________________________________________________________.
!  mode           name          role                                           !
!______________________________________________________________________________!
!> \param[in]     dt            time step (per cell)
!> \param[in]     iterns        Navier-Stokes iteration number
!_______________________________________________________________________________

subroutine resvoi &
 ( dt     , iterns ) &
 bind(C, name='cs_solve_void_fraction')

!===============================================================================

!===============================================================================
! Module files
!===============================================================================

use, intrinsic :: iso_c_binding

use dimens
use paramx
use numvar
use entsor
use optcal
use pointe, only: gamcav, dgdpca
use mesh
use field
use cavitation
use vof
use parall
use cs_c_bindings
use cs_cf_bindings

!===============================================================================

implicit none

! Arguments

double precision dt(ncelet)
integer          iterns

! Local variables

integer          ivar  , iel, ifac, f_id
integer          init
integer          nswrgp, imligp
integer          imrgrp, iescap
integer          iflmas, iflmab
integer          iwarnp, i_mass_transfer
integer          imucpp

integer          icvflb, kiflux, kbflux, icflux_id, bcflux_id
integer          ivoid(1)
integer          kscmin, kscmax, iclmin(1), iclmax(1)
integer          kclipp, clip_voidf_id

double precision epsrgp, climgp

double precision rvoid(1)
double precision vmin(1), vmax(1)
double precision dtmaxl, dtmaxg
double precision scmaxp, scminp
double precision thets, thetv, tsexp
double precision normp

double precision, allocatable, dimension(:) :: viscf, viscb
double precision, allocatable, dimension(:) :: smbrs, rovsdt
double precision, allocatable, dimension(:) :: dpvar
double precision, allocatable, dimension(:), target :: t_divu
double precision, dimension(:), pointer :: divu
double precision, dimension(:), pointer :: ivolfl, bvolfl
double precision, dimension(:), pointer :: coefap, coefbp, cofafp, cofbfp
double precision, dimension(:), pointer :: c_st_voidf
double precision, dimension(:), pointer :: cvar_pr, cvara_pr, icflux, bcflux
double precision, dimension(:), pointer :: cvar_voidf, cvara_voidf
double precision, dimension(:), pointer :: cpro_voidf_clipped

type(var_cal_opt) :: vcopt
type(var_cal_opt), target :: vcopt_loc
type(var_cal_opt), pointer :: p_k_value
type(c_ptr)       :: c_k_value

!===============================================================================

!===============================================================================
! 1. Initialization
!===============================================================================

i_mass_transfer = iand(ivofmt,VOF_MERKLE_MASS_TRANSFER)

ivar = ivolf2

call field_get_key_struct_var_cal_opt(ivarfl(ivar), vcopt)

call field_get_val_s(ivarfl(ivolf2), cvar_voidf)
call field_get_val_prev_s(ivarfl(ivolf2), cvara_voidf)

! implicitation in pressure of the vaporization/condensation model (cavitation)
if (i_mass_transfer.ne.0.and.itscvi.eq.1) then
  call field_get_val_s(ivarfl(ipr), cvar_pr)
  call field_get_val_prev_s(ivarfl(ipr), cvara_pr)
endif

! Allocate temporary arrays

allocate(viscf(nfac), viscb(nfabor))
allocate(smbrs(ncelet),rovsdt(ncelet))

! Allocate work arrays
allocate(dpvar(ncelet))

! --- Boundary conditions

call field_get_coefa_s(ivarfl(ivar), coefap)
call field_get_coefb_s(ivarfl(ivar), coefbp)
call field_get_coefaf_s(ivarfl(ivar), cofafp)
call field_get_coefbf_s(ivarfl(ivar), cofbfp)

! --- Physical quantities

call field_get_key_int(ivarfl(ivar), kimasf, iflmas)
call field_get_key_int(ivarfl(ivar), kbmasf, iflmab)
call field_get_val_s(iflmas, ivolfl)
call field_get_val_s(iflmab, bvolfl)

! Key id for clipping
call field_get_key_id("min_scalar_clipping", kscmin)
call field_get_key_id("max_scalar_clipping", kscmax)

! Theta related to explicit source terms
thets  = thetsn
if (isno2t.gt.0) then
  call field_get_key_int(ivarfl(ivolf2), kstprv, f_id)
  call field_get_val_s(f_id, c_st_voidf)
else
  c_st_voidf => null()
endif

! Theta related to void fraction
thetv = vcopt%thetav

! --- Initialization

do iel = 1, ncel
  smbrs(iel) = 0.d0
enddo

! Arbitrary initialization (no diffusion for void fraction)
do ifac = 1, nfac
  viscf(ifac) = 1.d0
enddo
do ifac = 1, nfabor
  viscb(ifac) = 1.d0
enddo

! Initialize void fraction convection flux
call field_get_key_id("inner_flux_id", kiflux)
call field_get_key_id("boundary_flux_id", kbflux)

call field_get_key_int(ivarfl(ivar), kiflux, icflux_id)
call field_get_key_int(ivarfl(ivar), kbflux, bcflux_id)

call field_get_val_s(icflux_id, icflux)
do ifac = 1, nfac
  icflux(ifac) = 0.d0
enddo

call field_get_val_s(bcflux_id, bcflux)
do ifac = 1, nfabor
  bcflux(ifac) = 0.d0
enddo

!===============================================================================
! 2. Preliminary computations
!===============================================================================

! Update the cavitation source term with pressure increment
!   if it has been implicited in pressure at correction step,
!   in order to ensure mass conservation.

if (i_mass_transfer.ne.0.and.itscvi.eq.1) then
  do iel = 1, ncel
    gamcav(iel) = gamcav(iel) + dgdpca(iel)*(cvar_pr(iel)-cvara_pr(iel))
  enddo
endif

! Compute the limiting time step to satisfy min/max principle.
!   Only if a source term is accounted for.

dtmaxl = 1.d15
dtmaxg = 1.d15

if (i_mass_transfer.ne.0) then
  do iel = 1, ncel
    if (gamcav(iel).lt.0.d0) then
      dtmaxl = -rho2*cvara_voidf(iel)/gamcav(iel)
    else
      dtmaxl = rho1*(1.d0-cvara_voidf(iel))/gamcav(iel)
    endif
    dtmaxg = min(dtmaxl,dtmaxg)
  enddo
  if (irangp.ge.0) call parmin(dtmaxg)

  if (dt(1).gt.dtmaxg)  write(nfecra,1000) dt(1), dtmaxg
endif

! Visualization of divu
!   (only for advanced using purposed, not used in the source terms hereafter)

init = 1
call field_get_id_try("velocity_divergence", f_id)
if (f_id.ge.0) then
  call field_get_val_s(f_id, divu)
else
  !Allocation
  allocate(t_divu(ncelet))
  divu => t_divu
endif

call divmas (init, ivolfl, bvolfl, divu)

if (allocated(t_divu)) deallocate(t_divu)

!===============================================================================
! 3. Construct the system to solve
!===============================================================================

! Source terms
!-------------

! Cavitation source term (explicit)
if (i_mass_transfer.ne.0) then
  do iel = 1, ncel
    smbrs(iel) = smbrs(iel) + cell_f_vol(iel)*gamcav(iel)/rho2
  enddo
endif

! Source terms assembly for cs_equation_iterative_solve_scalar

! If source terms are extrapolated over time
if (isno2t.gt.0) then
  do iel = 1, ncel
    tsexp = c_st_voidf(iel)
    c_st_voidf(iel) = smbrs(iel)
    smbrs(iel) = -thets*tsexp + (1.d0+thets)*c_st_voidf(iel)
  enddo
endif

! Source term linked with the non-conservative form of convection term
! in cs_equation_iterative_solve_scalar (always implicited)
! FIXME set imasac per variable? Here it could be set to 0
! (or Gamma(1/rho2 - 1/rho1) for cavitation) and divu not used.
! Note: we prefer to be not perfectly conservative (up to the precision
! of the pressure solver, but that allows to fullfill the min/max principle
! on alpha

if (i_mass_transfer.ne.0) then
  do iel = 1, ncel
    ! Should be for the conservative form:
    smbrs(iel) = smbrs(iel) - cell_f_vol(iel)*gamcav(iel)*(1.d0/rho2 - 1.d0/rho1)*cvara_voidf(iel)
    rovsdt(iel) = cell_f_vol(iel)*gamcav(iel)*(1.d0/rho2 - 1.d0/rho1)
  enddo
else
  do iel = 1, ncel
    rovsdt(iel) = 0.d0
  enddo
endif

if (idrift.gt.0) then
  imrgrp = vcopt%imrgra
  nswrgp = vcopt%nswrgr
  imligp = vcopt%imligr
  iwarnp = vcopt%iwarni
  epsrgp = vcopt%epsrgr
  climgp = vcopt%climgr

  call vof_drift_term &
( imrgrp , nswrgp , imligp , iwarnp , epsrgp , climgp ,          &
  cvar_voidf      , cvara_voidf     , smbrs  )
endif

! Unteady term
!-------------

do iel = 1, ncel
  rovsdt(iel) = rovsdt(iel) + vcopt%istat*cell_f_vol(iel)/dt(iel)
enddo

!===============================================================================
! 3. Solving
!===============================================================================

! Solving void fraction
iescap = 0
imucpp = 0
! all boundary convective flux with upwind
icvflb = 0
normp = -1.d0

vcopt_loc = vcopt

vcopt_loc%icoupl = -1
vcopt_loc%idifft = -1
vcopt_loc%iwgrec = 0 ! Warning, may be overwritten if a field
vcopt_loc%blend_st = 0 ! Warning, may be overwritten if a field

p_k_value => vcopt_loc
c_k_value = equation_param_from_vcopt(c_loc(p_k_value))

call cs_equation_iterative_solve_scalar          &
 ( idtvar , iterns ,                             &
   ivarfl(ivar)    , c_null_char ,               &
   iescap , imucpp , normp  , c_k_value       ,  &
   cvara_voidf     , cvara_voidf     ,           &
   coefap , coefbp , cofafp , cofbfp ,           &
   ivolfl , bvolfl ,                             &
   viscf  , viscb  , viscf  , viscb  ,           &
   rvoid  , rvoid  , rvoid  ,                    &
   icvflb , ivoid  ,                             &
   rovsdt , smbrs  , cvar_voidf      , dpvar  ,  &
   rvoid  , rvoid  )

!===============================================================================
! 3. Clipping: only if min/max principle is not satisfied for cavitation
!===============================================================================

iclmax(1) = 0
iclmin(1) = 0

if ((i_mass_transfer.ne.0.and.dt(1).gt.dtmaxg).or.i_mass_transfer.eq.0) then

  ! --- Calcul du min et max
  vmin(1) = cvar_voidf(1)
  vmax(1) = cvar_voidf(1)
  do iel = 1, ncel
    vmin(1) = min(vmin(1),cvar_voidf(iel))
    vmax(1) = max(vmax(1),cvar_voidf(iel))
  enddo

  ! Get the min and max clipping
  call field_get_key_double(ivarfl(ivar), kscmin, scminp)
  call field_get_key_double(ivarfl(ivar), kscmax, scmaxp)

  call field_get_key_id("clipping_id", kclipp)

  ! Postprocess clippings?
  call field_get_key_int(ivarfl(ivar), kclipp, clip_voidf_id)
  if (clip_voidf_id.ge.0) then
    call field_get_val_s(clip_voidf_id, cpro_voidf_clipped)
    do iel = 1, ncel
      cpro_voidf_clipped(iel) = 0.d0
    enddo
  endif

  if (scmaxp.gt.scminp) then
    do iel = 1, ncel
      if(cvar_voidf(iel).gt.scmaxp)then
        iclmax(1) = iclmax(1) + 1
        if (clip_voidf_id.ge.0) then
          cpro_voidf_clipped(iel) = cvar_voidf(iel) - scmaxp
        endif
        cvar_voidf(iel) = scmaxp
      endif
      if(cvar_voidf(iel).lt.scminp)then
        iclmin(1) = iclmin(1) + 1
        if (clip_voidf_id.ge.0) then
          cpro_voidf_clipped(iel) = cvar_voidf(iel) - scminp
        endif
        cvar_voidf(iel) = scminp
      endif
    enddo
  endif

endif

call log_iteration_clipping_field(ivarfl(ivar), iclmin(1), iclmax(1), &
                                  vmin, vmax, iclmin(1), iclmax(1))

! Free memory
deallocate(viscf, viscb)
deallocate(smbrs, rovsdt)
deallocate(dpvar)

!--------
! Formats
!--------

 1000   format(                                                 /,&
'@',                                                            /,&
'@ @@ WARNING: Void fraction resolution',                       /,&
'@    ========',                                                /,&
'@  The current time step is too large to ensure the min/max',  /,&
'@     principle on void fraction.',                            /,&
'@'                                                             /,&
'@  The current time step is', E13.5,' while',                  /,&
'@     the maximum admissible value is', E13.5,                 /,&
'@'                                                             /,&
'@  Clipping on void fraction should occur and',                /,&
'@     mass conservation is lost.',                             /,&
'@ ',                                                           /)

!----
! End
!----

return

end subroutine

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

subroutine fldtri() &
  bind(C, name='cs_boundary_conditions_init_bc_coeffs')

!===============================================================================

!> \brief Initialize all field BC coefficients.

!===============================================================================
! Module files
!===============================================================================

use paramx
use optcal
use cstphy
use numvar
use dimens, only: nvar
use entsor
use pointe
use albase
use period
use ppppar
use ppthch
use ppincl
use cfpoin
use pointe, only:compute_porosity_from_scan, ibm_porosity_mode
use lagran
use cplsat
use mesh
use field
use cs_c_bindings

!===============================================================================

implicit none

! Arguments

integer nscal

! Local variables

integer          ii, ivar
integer          nfld
integer          f_id
integer          kturt, turb_flux_model, turb_flux_model_type

integer          ifvar(nvarmx)

character(len=80) :: fname

integer, save :: ipass = 0

logical :: has_exch_bc

type(var_cal_opt) :: vcopt

!===============================================================================

!===============================================================================
! Interfaces
!===============================================================================

interface

  subroutine cs_turbulence_bc_init_pointers()  &
    bind(C, name='cs_turbulence_bc_init_pointers')
    use, intrinsic :: iso_c_binding
    implicit none
  end subroutine cs_turbulence_bc_init_pointers

end interface

!===============================================================================
! Initialisation
!===============================================================================

nfld = 0

ipass = ipass + 1

!===============================================================================
! Mapping
!===============================================================================

! Velocity and pressure
!----------------------

ivar = ipr

if (ipass .eq. 1) then
  call field_allocate_bc_coeffs(ivarfl(ivar), .true., .false., .false., .false.)
  call field_init_bc_coeffs(ivarfl(ivar))
endif

call field_get_id_try('pressure_increment', f_id)

if (f_id.ge.0) then
  if (ipass .eq. 1) then
    call field_allocate_bc_coeffs(f_id, .true., .false., .false., .false.)
    call field_init_bc_coeffs(f_id)
  endif
endif

ivar = iu

if (ipass.eq.1) then
  if (ippmod(icompf).ge.0) then
    call field_allocate_bc_coeffs(ivarfl(ivar), .true., .false., .true., .false.)
  else
    call field_allocate_bc_coeffs(ivarfl(ivar), .true., .false., .false., .false.)
  endif
  call field_init_bc_coeffs(ivarfl(ivar))
endif

! Void fraction for VOF algo.
!----------------------------

if (ivofmt.gt.0) then
  ivar = ivolf2
  if (ipass .eq. 1) then
    call field_allocate_bc_coeffs(ivarfl(ivar), .true., .false., .false., .false.)
    call field_init_bc_coeffs(ivarfl(ivar))
  endif
endif

! Turbulence
!-----------

if (itytur.eq.2) then
  nfld = nfld + 1
  ifvar(nfld) = ik
  nfld = nfld + 1
  ifvar(nfld) = iep
elseif (itytur.eq.3) then
  nfld = nfld + 1
  ifvar(nfld) = irij
  nfld = nfld + 1
  ifvar(nfld) = irij + 1
  nfld = nfld + 1
  ifvar(nfld) = irij + 2
  nfld = nfld + 1
  ifvar(nfld) = irij + 3
  nfld = nfld + 1
  ifvar(nfld) = irij + 4
  nfld = nfld + 1
  ifvar(nfld) = irij + 5
  nfld = nfld + 1
  ifvar(nfld) = iep
  if (iturb.eq.32) then
    nfld = nfld + 1
    ifvar(nfld) = ial
  endif
elseif (itytur.eq.5) then
  nfld = nfld + 1
  ifvar(nfld) = ik
  nfld = nfld + 1
  ifvar(nfld) = iep
  nfld = nfld + 1
  ifvar(nfld) = iphi
  if (iturb.eq.50) then
    nfld = nfld + 1
    ifvar(nfld) = ifb
  elseif (iturb.eq.51) then
    nfld = nfld + 1
    ifvar(nfld) = ial
  endif
elseif (iturb.eq.60) then
  nfld = nfld + 1
  ifvar(nfld) = ik
  nfld = nfld + 1
  ifvar(nfld) = iomg
elseif (iturb.eq.70) then
  nfld = nfld + 1
  ifvar(nfld) = inusa
endif

! Map fields

do ii = 1, nfld
  ivar = ifvar(ii)
  if (ipass .eq. 1) then
    if (itytur.eq.3 ) then
      if (ivar.eq.irij) then
        call field_allocate_bc_coeffs(ivarfl(ivar), .true., .true., .false., .false.)
      else if (ivar.gt.ir13) then
        call field_allocate_bc_coeffs(ivarfl(ivar), .true., .false., .false., .false.)
      endif
    else
      call field_allocate_bc_coeffs(ivarfl(ivar), .true., .false., .false., .false.)
    endif
    call field_init_bc_coeffs(ivarfl(ivar))
  endif
enddo

nfld = 0

! Mesh velocity
!--------------

! ALE legacy solver
if (iale.eq.1) then
  ivar = iuma
  if (ipass .eq. 1) then
    call field_allocate_bc_coeffs(ivarfl(ivar), .true., .false., .false., .false.)
    call field_init_bc_coeffs(ivarfl(ivar))
  endif
endif

! Wall distance
!--------------

call field_get_id_try("wall_distance", f_id)

if (f_id.ne.-1) then
  if (ipass .eq. 1) then
    call field_allocate_bc_coeffs(f_id, .true., .false., .false., .false.)
    call field_init_bc_coeffs(f_id)
  endif
endif

call field_get_id_try("wall_yplus", f_id)

if (f_id.ne.-1) then
  if (ipass .eq. 1) then
    call field_allocate_bc_coeffs(f_id, .true., .false., .false., .false.)
    call field_init_bc_coeffs(f_id)
  endif
endif

call field_get_id_try("z_ground", f_id)

if (f_id.ne.-1) then
  if (ipass .eq. 1) then
    call field_allocate_bc_coeffs(f_id, .true., .false., .false., .false.)
    call field_init_bc_coeffs(f_id)
  endif
endif

call field_get_id_try("porosity", f_id)

if (f_id.ne.-1) then
  if (ipass .eq. 1 .and. (compute_porosity_from_scan .or. ibm_porosity_mode.gt.0)) then
    call field_allocate_bc_coeffs(f_id, .true., .false., .false., .false.)
    call field_init_bc_coeffs(f_id)
  endif
endif

call field_get_id_try("hydrostatic_pressure", f_id)

if (f_id.ne.-1) then
  if (ipass .eq. 1) then
    call field_allocate_bc_coeffs(f_id, .true., .false., .false., .false.)
    call field_init_bc_coeffs(f_id)
  endif
endif

call field_get_id_try("meteo_pressure", f_id)

if (f_id.ne.-1) then
  if (ipass .eq. 1) then
    call field_allocate_bc_coeffs(f_id, .true., .false., .false., .false.)
    call field_init_bc_coeffs(f_id)
  endif
endif

call field_get_id_try("lagr_time", f_id)

if (f_id.ne.-1) then
  if (ipass .eq. 1) then
    call field_allocate_bc_coeffs(f_id, .true., .false., .false., .false.)
    call field_init_bc_coeffs(f_id)
  endif
endif

! User variables
!---------------

nscal = nscaus + nscapp

! Get the turbulent flux model
call field_get_key_id('turbulent_flux_model', kturt)

do ii = 1, nscal
  if (isca(ii) .gt. 0) then
    ivar = isca(ii)
    has_exch_bc = .false.
    call field_get_key_struct_var_cal_opt(ivarfl(ivar), vcopt)
    if (vcopt%icoupl.gt.0) has_exch_bc = .true.

    call field_get_key_int(ivarfl(isca(ii)), kturt, turb_flux_model)
    turb_flux_model_type = turb_flux_model / 10

    if (ipass .eq. 1) then
      if (ippmod(icompf).ge.0 .and. ii.eq.ienerg) then
        call field_allocate_bc_coeffs(ivarfl(ivar), .true., .false., .true., has_exch_bc)
      else
        call field_allocate_bc_coeffs(ivarfl(ivar), .true., .false., .false., has_exch_bc)
      endif
      call field_init_bc_coeffs(ivarfl(ivar))
      ! Boundary conditions of the turbulent fluxes T'u'
      if (turb_flux_model_type.eq.3) then
        call field_get_name(ivarfl(ivar), fname)
        ! Index of the corresponding turbulent flux
        call field_get_id(trim(fname)//'_turbulent_flux', f_id)
        call field_allocate_bc_coeffs(f_id, .true., .true., .false., .false.)
        call field_init_bc_coeffs(f_id)
      endif
      ! Elliptic Blending (AFM or DFM)
      if (turb_flux_model.eq.11 .or. turb_flux_model.eq.21 .or. turb_flux_model.eq.31) then
        call field_get_name(ivarfl(ivar), fname)
        call field_get_id(trim(fname)//'_alpha', f_id)
        call field_allocate_bc_coeffs(f_id, .true., .false., .false., .false.)
        call field_init_bc_coeffs(f_id)
      endif
    endif
  endif
enddo

! Reserved fields whose ids are not saved (may be queried by name)
!-----------------------------------------------------------------

! Local time step

! Set previous values for backward n order in time
do ivar = 1, nvar
  ! Here there is no problem if there are multiple
  ! set on non scalar fields.
  call field_get_key_struct_var_cal_opt(ivarfl(ivar), vcopt)
  if (vcopt%ibdtso.gt.1) then
    call field_set_n_previous(ivarfl(ivar), vcopt%ibdtso)
  endif
enddo

! Map pointers to BC's now that BC coefficient pointers are allocated
!--------------------------------------------------------------------

call cs_turbulence_bc_init_pointers

return
end subroutine fldtri

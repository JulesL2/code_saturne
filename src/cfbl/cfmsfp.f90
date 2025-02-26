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

subroutine cfmsfp &
 ( iterns ,                                              &
   dt     , vela   ,                                     &
   flumas , flumab )

!===============================================================================
! FONCTION :
! ----------

!  "MASS FLUX" AT THE FACES CALCULATION FOR THE CFL RESTRICTION CALCULATION
!   AND THE SOLVING OF THE PRESSURE

!-------------------------------------------------------------------------------
!ARGU                             ARGUMENTS
!__________________.____._____.________________________________________________.
! name             !type!mode ! role                                           !
!__________________!____!_____!________________________________________________!
! iterns           ! i  ! <-- ! Navier-Stokes iteration number                 !
! ncepdp           ! i  ! <-- ! number of cells with head loss                 !
! dt(ncelet)       ! ra ! <-- ! time step (per cell)                           !
! vela             ! ra ! <-- ! variable value at time step beginning          !
! flumas(nfac)     ! tr ! --> ! flux de masse aux faces internes               !
! flumab(nfabor)   ! tr ! --> ! flux de masse aux faces de bord                !
!__________________!____!_____!________________________________________________!

!     Type: i (integer), r (real), s (string), a (array), l (logical),
!           and composite types (ex: ra real array)
!     mode: <-- input, --> output, <-> modifies data, --- work array
!===============================================================================

!===============================================================================
! Module files
!===============================================================================

use cfpoin
use paramx
use numvar
use entsor
use optcal
use cstphy
use cstnum
use parall
use period
use ppppar
use ppthch
use ppincl
use mesh
use field
use pointe, only: ncetsm, icetsm, itypsm, smacel
use cs_f_interfaces
use cs_c_bindings

!===============================================================================

implicit none

! Arguments

integer          iterns

double precision dt(ncelet)
double precision flumas(nfac), flumab(nfabor)
double precision vela(3, ncelet)

! Local variables

integer          ifac  , iel, ischcp, idftnp, ircflp
integer          init  , inc   , isstpp
integer          imrgrp, nswrgp, imligp, iwarnp, iconvp, idiffp, imvisp
integer          icvflb, f_id0
integer          isou  , jsou
integer          iflmb0, itypfl
integer          itsqdm, imasac

double precision epsrgp, climgp, thetap, blencp, relaxp
double precision rom

double precision, allocatable, dimension(:) :: w1
double precision, dimension(:), pointer :: crom, brom
double precision, dimension(:), pointer :: viscl, visct
double precision, dimension(:,:), allocatable :: tsexp, gavinj
double precision, allocatable, dimension(:,:) :: vel0
double precision, dimension(:,:,:), allocatable :: tsimp
double precision, allocatable, dimension(:,:,:), target :: viscf
double precision, allocatable, dimension(:,:) :: viscce
double precision, allocatable, dimension(:) :: viscb
double precision, allocatable, dimension(:) :: secvif, secvib
double precision, allocatable, dimension(:,:,:) :: coefbv

double precision, dimension(:,:), pointer :: coefau, cofafu
double precision, dimension(:,:,:), pointer :: coefbu, cofbfu

type(var_cal_opt) :: vcopt_u, vcopt_p
type(var_cal_opt), target :: vcopt_loc
type(var_cal_opt), pointer :: p_k_value
type(c_ptr) :: c_k_value

double precision rvoid(1)

!===============================================================================

!===============================================================================
! 1. INITIALISATION
!===============================================================================

call field_get_coefa_v(ivarfl(iu), coefau)
call field_get_coefb_v(ivarfl(iu), coefbu)
call field_get_coefaf_v(ivarfl(iu), cofafu)
call field_get_coefbf_v(ivarfl(iu), cofbfu)

! Allocate work arrays
allocate(w1(ncelet))
allocate(tsexp(3,ncelet))
allocate(gavinj(3,ncelet))
allocate(tsimp(3,3,ncelet))
allocate(coefbv(3,3,nfabor))
allocate(vel0(3,ncelet))

call field_get_key_struct_var_cal_opt(ivarfl(iu), vcopt_u)
call field_get_key_struct_var_cal_opt(ivarfl(ipr), vcopt_p)

if (iand(vcopt_u%idften, ISOTROPIC_DIFFUSION).ne.0) then
  allocate(viscf(1, 1, nfac), viscb(nfabor))
else if (iand(vcopt_u%idften, ANISOTROPIC_LEFT_DIFFUSION).ne.0) then
  allocate(viscf(3, 3, nfac), viscb(nfabor))
  allocate(viscce(6,ncelet))
endif

if (ivisse.eq.1) then
  allocate(secvif(nfac),secvib(nfabor))
endif

f_id0 = -1

! Density

call field_get_val_s(icrom, crom)
call field_get_val_s(ibrom, brom)

!===============================================================================
! 2. MASS FLUX AT THE FACES
!===============================================================================

!     2.1 SOURCE TERMS OF THE MOMENTUM EQUATIONS
!     ==========================================

!     Some first tests (double expansion waves in a shock tube)
!       has shown that taking into account all the
!       momentum equation terms in the mass equation seems to be a
!       bad idea (in particular the convective term, but the diffusive
!       term, the transposed gradient, the mass and user source terms
!       were all null in the considered tests).
!       However, it may be due to a bug at that early stage of implementation
!       of the algorithm (but we didn't find it).
!     We thus recommand not to take into account the momentum source terms,
!       except the gravity term (because it is in balance with the pressure
!       gradient and because its effect is visible at equilibrium).
!     However, we keep here the implementation of the preliminary tests
!       (1.1.0.h version) with an overall test so that the correction is not
!       active (thus, there is no user question and there is always the
!       possibility to perform other tests in the future).
!     Note that, with these terms, the thoeretical analysis is harder
!     (Without these terms we are in the configuration Euler + gravity)

! --- Initialization
do iel = 1, ncel
  do isou = 1, 3
    tsexp(isou,iel) = 0.d0
    do jsou = 1, 3
      tsimp(isou,jsou,iel) = 0.d0
    enddo
  enddo
enddo

!     Test on momentum source terms
itsqdm = 0
if (itsqdm.ne.0) then

  if (ivisse.eq.1) then
    call visecv(secvif, secvib)
  endif

  ! C version
  call user_source_terms(ivarfl(iu), tsexp, tsimp)

  ! Convective of the momentum equation
  ! in upwind and without reconstruction
  iconvp = vcopt_u%iconv

  init   = 1
  inc    = 1
  iflmb0 = 1
  imrgrp = vcopt_u%imrgra
  nswrgp = vcopt_u%nswrgr
  imligp = vcopt_u%imligr
  iwarnp = vcopt_u%iwarni
  epsrgp = vcopt_u%epsrgr
  climgp = vcopt_u%climgr
  relaxp = vcopt_u%relaxv
  thetap = vcopt_u%thetav

  itypfl = 1

  ! Mass flux calculation
  call inimav                                                   &
  !==========
( ivarfl(iu)      , itypfl ,                                     &
  iflmb0 , init   , inc    , imrgrp , nswrgp , imligp ,          &
  iwarnp ,                                                       &
  epsrgp , climgp ,                                              &
  crom, brom,                                                    &
  vela,                                                          &
  coefau , coefbu ,                                              &
  flumas , flumab )

  ! ---> Face diffusivity for the velocity
  idiffp = vcopt_u%idiff
  imvisp = vcopt_u%imvisf
  if (idiffp.ge. 1) then

     call field_get_val_s(iviscl, viscl)
     call field_get_val_s(ivisct, visct)

     if (itytur.eq.3) then
        do iel = 1, ncel
           w1(iel) = viscl(iel)
        enddo
     else
        do iel = 1, ncel
           w1(iel) = viscl(iel) + vcopt_u%idifft*visct(iel)
        enddo
     endif

     ! Scalar diffusivity (Default)
     if (iand(vcopt_u%idften, ISOTROPIC_DIFFUSION).ne.0) then

        call viscfa(imvisf, w1, viscf, viscb)

     ! Tensorial diffusion of the velocity (in case of tensorial porosity)
     else if (iand(vcopt_u%idften, ANISOTROPIC_LEFT_DIFFUSION).ne.0) then

        do iel = 1, ncel
          do isou = 1, 3
            viscce(isou, iel) = w1(iel)
          enddo
          do isou = 4, 6
            viscce(isou, iel) = 0.d0
          enddo
        enddo

        call vistnv (imvisf, viscce, viscf, viscb)

     endif

  ! --- If no diffusion, viscosity is set to 0.
  else

     do ifac = 1, nfac
       viscf(1,1,ifac) = 0.d0
     enddo
     do ifac = 1, nfabor
       viscb(ifac) = 0.d0
     enddo

  endif

  idftnp = vcopt_u%idften

  ! no recontruction
  ircflp = 0;

  ! upwind
  ischcp = 0;
  blencp = 0;
  isstpp = 0;

  inc = 1;

  icvflb = 0;

  ! The added convective scalar mass flux is:
  !      (thetap*Y_\face-imasac*Y_\celli)*mf.
  ! When building the implicit part of the rhs, one
  ! has to impose 1 on mass accumulation.
  imasac = 1

  vcopt_loc = vcopt_u

  vcopt_loc%istat  = -1
  vcopt_loc%idifft = -1
  vcopt_loc%iswdyn = -1
  vcopt_loc%nswrsm = -1
  vcopt_loc%iwgrec = 0
  vcopt_loc%blend_st = 0 ! Warning, may be overwritten if a field
  vcopt_loc%epsilo = -1
  vcopt_loc%epsrsm = -1

  p_k_value => vcopt_loc
  c_k_value = equation_param_from_vcopt(c_loc(p_k_value))

  call cs_balance_vector                                      &
    (idtvar, ivarfl(iu), imasac, inc, ivisse,                 &
     c_k_value, vela, vela, coefau, coefbu, cofafu, cofbfu,   &
     flumas, flumab, viscf, viscb, secvif, secvib,            &
     rvoid, rvoid, rvoid,                                     &
     icvflb, icvfli, c_null_ptr, c_null_ptr, tsexp)

endif

! End of the test on momentum source terms

! Mass source term
if (ncetsm.gt.0) then

  ! The momentum balance is used in its conservative form here
  ! so the mass source term is only composed of gamma*uinj
  ! => array of previous velocity has to be set to zero

  do iel = 1, ncel
    do isou = 1,3
      vel0(isou,iel) = 0.d0
    enddo
  enddo

  call catsmv(ncetsm, iterns, icetsm, itypsm(:,iu),                      &
              cell_f_vol, vel0, smacel(:,iu), smacel(:,ipr),             &
              tsexp, tsimp, gavinj)

  do iel = 1, ncel
    do isou = 1, 3
      tsexp(isou,iel) = tsexp(isou,iel) + gavinj(isou,iel)
    enddo
  enddo

endif

! --- Volumic forces term (gravity)
do iel = 1, ncel
  rom = crom(iel)
  tsexp(1,iel) = gx + tsexp(1,iel)/rom
  tsexp(2,iel) = gy + tsexp(2,iel)/rom
  tsexp(3,iel) = gz + tsexp(3,iel)/rom
enddo

! --- Calculation of the convective "velocities at the cell centers
!     (Calculation of u^n+dt*f^n)

do iel = 1, ncel
  do isou = 1, 3
    tsexp(isou,iel) = dt(iel)*tsexp(isou,iel)
  enddo
enddo

! Computation of the flux

! volumic flux part based on dt*f^n

! Initialization of the mass flux
init   = 1
! As mentioned above, for homogeneous Neumann
inc    = 0
iflmb0 = 1
! Reconstruction is useless here
nswrgp = 0 ! FIXME
imrgrp = vcopt_p%imrgra
imligp = vcopt_p%imligr
iwarnp = vcopt_p%iwarni
epsrgp = vcopt_p%epsrgr
climgp = vcopt_p%climgr

! Velocity flux (crom, brom not used)
itypfl = 0

! No contribution of f to the boundary mass flux
do ifac = 1, nfabor
  do isou = 1, 3
    do jsou = 1, 3
      coefbv(isou,jsou,ifac) = 0.d0
    enddo
  enddo
enddo

call inimav                                                      &
!==========
( f_id0  , itypfl ,                                              &
  iflmb0 , init   , inc    , imrgrp , nswrgp , imligp ,          &
  iwarnp ,                                                       &
  epsrgp , climgp ,                                              &
  crom, brom,                                                    &
  tsexp,                                                         &
  coefau , coefbv ,                                              &
  flumas , flumab )

! volumic flux part based on velocity u^n
init   = 0
! take into account Dirichlet velocity boundary conditions
inc    = 1

call inimav                                                      &
!==========
( ivarfl(iu)      , itypfl ,                                     &
  iflmb0 , init   , inc    , imrgrp , nswrgp , imligp ,          &
  iwarnp ,                                                       &
  epsrgp , climgp ,                                              &
  crom, brom,                                                    &
  vela,                                                          &
  coefau , coefbu ,                                              &
  flumas , flumab )

! Free memory
deallocate(w1)
deallocate(tsexp)
deallocate(gavinj)
deallocate(tsimp)
deallocate(viscf, viscb)
if (allocated(secvif)) deallocate(secvif, secvib)
if (allocated(viscce)) deallocate(viscce)
deallocate(coefbv)
deallocate(vel0)

!--------
! FORMATS
!--------


!----
! FIN
!----

return

end subroutine

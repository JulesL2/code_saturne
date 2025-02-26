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
!> \file solvar.f90
!> \brief Atmospheric soil module - Compute ground level variables
!
!> \brief   Soil / atmosphere interface routine
!>   Compute the following surface variables :
!>-     temperature with a prognostic equation of the "force-restore" type
!>                       (Deardorff 1978)
!>-     humidity with a two reservoirs method
!
!-------------------------------------------------------------------------------
! Arguments
!______________________________________________________________________________.
!  mode           name          role
!______________________________________________________________________________!
!> \param[in]   temp    temperature
!> \param[in]   qv      specific humidity
!> \param[in]   rom     Density
!> \param[in]   dt      (local) time step
!> \param[in]   rcodcl  Boundary conditions type
!-------------------------------------------------------------------------------

subroutine solvar ( temp , qv ,rom , dt, rcodcl )

!===============================================================================
! Module files
!===============================================================================

use paramx
use dimens
use numvar
use optcal
use cstphy
use cstnum
use entsor
use parall
use period
use ppppar
use ppthch
use ppincl
use atincl
use ctincl, only: cp_a, cp_v
use atsoil
use mesh
use field
use cs_c_bindings

implicit none

!==============================================================================

! Arguments

double precision rcodcl(nfabor,nvar,3)

double precision temp(ncelet)
double precision qv(ncelet)
double precision rom(ncelet),dt(ncelet)

! Local variables

integer ifac,iel,isol
double precision rcodcx, rcodcy, rcodcz, rcodsn
double precision rnx, rny, rnz
double precision tx, ty, tz
double precision vtmod, upx, upy, upz, usn
integer ichal
integer iseuil
double precision zreel
double precision b,c,d,tau1,precip,evapor,albedo
double precision emis,csol,veg,c1w,c2w,z0t,tprof
double precision act,rib,fh,fhden,rscp1,rscp2
double precision pres1,tpot1,tpot2,tpotv1,tpotv2
double precision cphum,cht,chq,tsplus,qvsplu,w1plus,w2plus
double precision w1num,w1den,w2num,w2den
double precision hu,esat,cstder,qsat,dqsat,rapsat
double precision tssol,qvsol,w1,w2,foir,fos
double precision ray1,chas1,chal1,rapp1,premem
double precision ray2,chas2,chal2,rapp2,secmem
double precision w1min,w1max,w2min,w2max
double precision r1,r2,tseuil,dum
double precision cpvcpa

double precision, dimension(:,:), pointer :: vel
double precision, dimension(:), pointer :: cpro_met_p

integer, dimension(:), pointer :: elt_ids

double precision, pointer, dimension(:)   :: bpro_rugdyn ! all boundary faces
double precision, pointer, dimension(:)   :: bpro_rugthe ! all boundary faces
double precision, pointer, dimension(:)   :: bpro_albedo ! all boundary faces
double precision, pointer, dimension(:)   :: bpro_emissi ! all boundary faces
double precision, pointer, dimension(:)   :: bpro_vegeta ! only zone faces
double precision, pointer, dimension(:)   :: bpro_c1w    ! only zone faces
double precision, pointer, dimension(:)   :: bpro_c2w    ! only zone faces
double precision, pointer, dimension(:)   :: bpro_csol   ! only zone faces
double precision, pointer, dimension(:)   :: bpro_r1     ! only zone faces
double precision, pointer, dimension(:)   :: bpro_r2     ! only zone faces
double precision, pointer, dimension(:)   :: bpro_tprof  ! only zone faces
double precision, pointer, dimension(:,:) :: bpro_pourcent_sol ! only zone faces

double precision, pointer, dimension(:)   :: bvar_temp_sol
double precision, pointer, dimension(:)   :: bvar_tempp
double precision, pointer, dimension(:)   :: bvara_tempp
double precision, pointer, dimension(:)   :: bvar_total_water
double precision, pointer, dimension(:)   :: bvara_total_water
double precision, pointer, dimension(:)   :: bvar_w1
double precision, pointer, dimension(:)   :: bvar_w2

!===============================================================================

! Map field arrays
call field_get_val_v(ivarfl(iu), vel)

call field_get_val_s_by_name("boundary_roughness", bpro_rugdyn)
call field_get_val_s_by_name("boundary_thermal_roughness", bpro_rugthe)
call field_get_val_s_by_name("boundary_albedo", bpro_albedo)
call field_get_val_s_by_name("emissivity", bpro_emissi)
call field_get_val_s_by_name("boundary_vegetation", bpro_vegeta)
call field_get_val_s_by_name("soil_water_capacity", bpro_c1w   )
call field_get_val_s_by_name("soil_water_ratio", bpro_c2w   )
call field_get_val_s_by_name("soil_thermal_capacity", bpro_csol  )
call field_get_val_s_by_name("soil_r1", bpro_r1    )
call field_get_val_s_by_name("soil_r2", bpro_r2    )
call field_get_val_s_by_name("soil_temperature_deep", bpro_tprof )

call field_get_val_v_by_name("atmo_soil_percentages", bpro_pourcent_sol )

call field_get_val_s_by_name("soil_temperature", bvar_temp_sol)
call field_get_val_s_by_name("soil_pot_temperature", bvar_tempp)
call field_get_val_prev_s_by_name("soil_pot_temperature", bvara_tempp)
call field_get_val_s_by_name("soil_total_water", bvar_total_water)
call field_get_val_prev_s_by_name("soil_total_water", bvara_total_water)
call field_get_val_s_by_name("soil_w1", bvar_w1)
call field_get_val_s_by_name("soil_w2", bvar_w2)

call atmo_get_soil_zone(nfmodsol, nbrsol, elt_ids)

if (imeteo.eq.2) then
  call field_get_val_s_by_name('meteo_pressure', cpro_met_p)
endif

!     ==========================
!     1) initialisations locales
!     ==========================

cpvcpa = cp_v / cp_a

b = 5.d0
c = 5.d0
d = 5.d0

tau1 = 86400.d0

w1min = 0.d0
w1max = 1.d0
w2min = 0.d0
w2max = 1.d0

!  pas de precipitations pour l'instant
precip = 0.d0

!  tseuil pour le flux anthropogenique (m.a. atwater 1975)
tseuil = 16.d0 + tkelvi

do isol = 1, nfmodsol

  tssol = bvar_temp_sol   (isol) + tkelvi
  qvsol = bvar_total_water(isol)
  w1    = bvar_w1         (isol)
  w2    = bvar_w2         (isol)

  ifac = elt_ids(isol) + 1 ! C > Fortran
  z0t    = bpro_rugthe(ifac)
  emis   = bpro_emissi(ifac)
  albedo = bpro_albedo(ifac)

  csol   = bpro_csol  (isol)
  veg    = bpro_vegeta(isol)
  c1w    = bpro_c1w   (isol)
  c2w    = bpro_c2w   (isol)
  r1     = bpro_r1    (isol)
  r2     = bpro_r2    (isol)
  tprof  = bpro_tprof (isol)

  if (iatra1.eq.1) then
    foir = soilvert(1)%foir !FIXME not correct for many verticals
    fos  = soilvert(1)%fos
  else
    foir = 0.d0
    fos  = 0.d0
  endif

  !     ==================================================================
  !     2) calcul du vecteur vitesse tangent (identique a celui fait ds fr
  !     ==================================================================

  ifac = elt_ids(isol) + 1 ! C > Fortran

  ! Unite normal
  rnx = surfbo(1,ifac)/surfbn(ifac)
  rny = surfbo(2,ifac)/surfbn(ifac)
  rnz = surfbo(3,ifac)/surfbn(ifac)

  ! ---> PROJECTION DE LA VITESSE DE DEFILEMENT
  !           (utilisee plusieurs fois ensuite)

  rcodcx = rcodcl(ifac,iu,1)
  rcodcy = rcodcl(ifac,iv,1)
  rcodcz = rcodcl(ifac,iw,1)

  rcodsn = rcodcx*rnx + rcodcy*rny + rcodcz*rnz
  rcodcl(ifac,iu,1) = rcodcx - rcodsn*rnx
  rcodcl(ifac,iv,1) = rcodcy - rcodsn*rny
  rcodcl(ifac,iw,1) = rcodcz - rcodsn*rnz

  ! ---> VITESSE TANGENTIELLE RELATIVE

  upx = vel(1,ifabor(ifac))
  upy = vel(2,ifabor(ifac))
  upz = vel(3,ifabor(ifac))

  usn = upx*rnx + upy*rny + upz*rnz
  tx  = upx - usn*rnx
  ty  = upy - usn*rny
  tz  = upz - usn*rnz
  tx  = tx - rcodcl(ifac,iu,1)
  ty  = ty - rcodcl(ifac,iv,1)
  tz  = tz - rcodcl(ifac,iw,1)
  vtmod = sqrt(tx**2 + ty**2 + tz**2)

  iel = ifabor(ifac)
  zreel = xyzcen(3,iel)

  if (bpro_pourcent_sol(1+1, isol) .gt. 50.d0) then

    !     ====================================
    !     3) cas particulier des points de mer
    !     ====================================

    ! on impose t = tmer et hr = 100 %
    esat  = cs_air_pwv_sat(tmer)

    if (imeteo.eq.0) then
      call atmstd(zreel,pres1,dum,dum)


    else if (imeteo.eq.1) then
      call intprf                                                       &
        (nbmett, nbmetm,                                               &
        ztmet, tmmet, phmet, zreel, ttcabs, pres1)
    else
      pres1 = cpro_met_p(iel)
    endif

    tsplus = tmer + tkelvi
    qvsplu = esat/(rvsra*pres1 + esat*(1.d0 - rvsra))

  else

    !     ============================================
    !     4) compute Richardson number and function fh
    !     ============================================

    act = xkappa/log((distb(ifac) + z0t)/z0t)

    rscp1 = (rair/cp0)*(1.d0 + (rvsra - cpvcpa)*qvsol)
    rscp2 = (rair/cp0)*(1.d0 + (rvsra - cpvcpa)*qv(iel))

    if (imeteo.eq.0) then
      call atmstd(zreel,pres1,dum,dum)
    else if (imeteo.eq.1) then
      call intprf &
        (nbmett, nbmetm,                                               &
        ztmet, tmmet, phmet, zreel, ttcabs, pres1)
    else
      pres1 = cpro_met_p(iel)
    endif

    tpot1 = bvar_tempp(isol)
    tpot2 = temp(iel)

    tpotv1 = tpot1*(1.d0 + (rvsra - 1.d0)*qvsol)
    tpotv2 = tpot2*(1.d0 + (rvsra - 1.d0)*qv(iel))

    if (vtmod.ge.epzero) then
      rib = 2.d0*abs(gz)*distb(ifac)*(tpotv2 - tpotv1)/(tpotv1 + tpotv2)      &
        /vtmod/vtmod
    else
      rib = 0.d0
    endif
    if(rib.ge.0.d0) then
      fh = 1.d0/(1.d0 + 3.d0*b*rib*sqrt(1.d0 + d*rib))
    else
      fhden = 3.d0*b*c*act*act*sqrt((distb(ifac) + z0t)/z0t)
      fh = 1.d0 - (3.d0*b*rib)/(1.d0 + fhden*sqrt(abs(rib)))
    endif

    !     ==================================================================
    !     5) calcul des coefficients pour flux de chaleur sensible et latent
    !     ==================================================================

    cphum = cp0*(1.d0 + (cpvcpa-1.d0)*qvsol)
    cht = rom(iel)*cphum*act*act*fh*vtmod*((ps/pres1)**rscp1)
    chq = rom(iel)*act*act*fh*vtmod*(clatev - 2370.d0*(tssol - tkelvi))

    !     ==================================================================
    !     6) calcul des teneurs en eau des reservoirs superficiel et profond
    !     ==================================================================

    evapor = -rom(iel)*act*act*fh*vtmod*(qv(iel) - qvsol)

    w1num = w1 + dt(iel)*(precip - evapor)/c1w +                        &
            w2*dt(iel)/(tau1 + c2w*dt(iel))

    w1den = 1.d0 + 1.d0/(tau1/dt(iel) + c2w)
    w1plus = w1num/w1den
    w1plus = max(w1plus,w1min)
    w1plus = min(w1plus,w1max)
    w2num = w2*tau1 + w1plus*dt(iel)*c2w
    w2den = tau1 + dt(iel)*c2w
    w2plus = w2num/w2den
    w2plus = max(w2plus,w2min)
    w2plus = min(w2plus,w2max)
    bvar_w1(isol) = w1plus
    bvar_w2(isol) = w2plus
    hu = 0.5d0*(1.d0-cos(pi*w1plus))

    !     ==================================================================
    !     7) calcul de la pression de vapeur saturante esat et de d(qsat)/dt
    !     ==================================================================

    esat  = cs_air_pwv_sat(tssol-tkelvi)
    rapsat = rvsra*pres1+esat*(1.d0-rvsra)
    qsat = esat/rapsat
    cstder = 17.438d0*(239.78d0)!TODO code it in  cs_air_prop.c
    dqsat = pres1*rvsra/rapsat/rapsat*cstder*esat                     &
         /(tssol -tkelvi+ 239.78d0)/(tssol -tkelvi+ 239.78d0)!TODO code it in  cs_air_prop.c

    !     ===========================================================
    !     8) calcul du premier membre de l'equation d'evolution de tssol
    !     ===========================================================

    !  prise en compte du flux anthropogenique
    !  en-dessous de tseuil, ce flux depend de la temperature
    iseuil = 0
    if (tssol.lt.tseuil) iseuil = 1

    ichal = 1
    !  si e0 < 0 pas de contribution du flux de chaleur latente dans le bilan
    !     if(evapor    .lt.0.) ichal=0

    ray1 = 4.d0*emis*stephn*(tssol**3)
    chas1 = cht
    chal1 = chq*hu*dqsat
    rapp1 = 2.d0*pi/tau1

    premem = csol*(ray1 + chas1 + chal1*ichal + r2*iseuil) + rapp1

    !     ==========================================================
    !     9) calcul du second membre de l'equation d'evolution de tssol
    !     ==========================================================

    ! Warning: fos already contains (1-albedo) factor!
    ray2 = fos + emis*foir + 3.d0*emis*stephn*(tssol**4)
    chas2 = cht*tpot2*((pres1/ps)**rscp1)
    chal2 = chq*(qv(iel)*(1.d0 - veg*(1.d0 - hu)) - hu*(qsat - tssol*dqsat))
    rapp2 = 2.d0*pi*(tprof + tkelvi)/tau1

    secmem = csol*(ray2 + chas2+chal2*ichal + r1 + tseuil*r2*iseuil)            &
           + rapp2
    !     ===========================================
    !     10) calcul des nouveaux parametres du sol
    !     ===========================================

    tsplus = (tssol + dt(iel)*secmem) /                                 &
         (1.d0 + dt(iel)*premem)

    qvsplu = hu*(qsat+dqsat*(tsplus - tssol))                           &
         + veg*qv(iel)*(1.d0 - hu)

    !  cas d'un flux de rosee
    !      if(qv(i,j,k+1).gt.qsat.and.hu.ne.1.d0) then
    !        hu=1.d0
    !        qvsplu=qsat
    !      endif

  endif

  !     (indicateur terre/mer)

  !     ================================
  !     11) mise a jour du tableau solva
  !     ================================

  ! Current to previous
  bvara_tempp(isol) = bvar_tempp(isol)
  bvara_total_water(isol) = bvar_total_water(isol)

  bvar_temp_sol   (isol) = tsplus - tkelvi
  bvar_tempp      (isol) = tsplus*((ps/pres1)**((rair/cp0) * &
                           (1.d0+(rvsra-cpvcpa)*qvsplu)))
  bvar_total_water(isol) = qvsplu

enddo

call cs_user_atmo_soil(temp, qv, rom, dt, rcodcl)

return

end subroutine solvar

!-------------------------------------------------------------------------------

! This file is part of Code_Saturne, a general-purpose CFD tool.
!
! Copyright (C) 1998-2012 EDF S.A.
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

subroutine matrix &
!================

 ( ncelet , ncel   , nfac   , nfabor ,                            &
   iconvp , idiffp , ndircp , isym   , nfecra ,                   &
   thetap ,                                                       &
   ifacel , ifabor ,                                              &
   coefbp , rovsdt , flumas , flumab , viscf  , viscb  ,          &
   da     , xa     )

!===============================================================================
! FONCTION :
! ----------

! CONSTRUCTION DE LA MATRICE DE CONVECTION UPWIND/DIFFUSION/TS

!     IL EST INTERDIT DE MODIFIER ROVSDT ICI


!-------------------------------------------------------------------------------
!ARGU                             ARGUMENTS
!__________________.____._____.________________________________________________.
! name             !type!mode ! role                                           !
!__________________!____!_____!________________________________________________!
! ncelet           ! i  ! <-- ! number of extended (real + ghost) cells        !
! ncel             ! i  ! <-- ! number of cells                                !
! nfac             ! i  ! <-- ! number of interior faces                       !
! nfabor           ! i  ! <-- ! number of boundary faces                       !
! iconvp           ! e  ! <-- ! indicateur = 1 convection, 0 sinon             !
! idiffp           ! e  ! <-- ! indicateur = 1 diffusion , 0 sinon             !
! ndircp           ! e  ! <-- ! indicateur = 0 si decalage diagonale           !
! isym             ! e  ! <-- ! indicateur = 1 matrice symetrique              !
!                  !    !     !              2 matrice non symetrique          !
! thetap           ! r  ! <-- ! coefficient de ponderation pour le             !
!                  !    !     ! theta-schema (on ne l'utilise pour le          !
!                  !    !     ! moment que pour u,v,w et les scalaire          !
!                  !    !     ! - thetap = 0.5 correspond a un schema          !
!                  !    !     !   totalement centre en temps (mixage           !
!                  !    !     !   entre crank-nicolson et adams-               !
!                  !    !     !   bashforth)                                   !
! ifacel(2,nfac    ! te ! <-- ! no des elts voisins d'une face intern          !
! ifabor(nfabor    ! te ! <-- ! no de l'elt voisin d'une face de bord          !
! coefbp(nfabor    ! tr ! <-- ! tab b des cl pour la var consideree            !
! flumas(nfac)     ! tr ! <-- ! flux de masse aux faces internes               !
! flumab(nfabor    ! tr ! <-- ! flux de masse aux faces de bord                !
! viscf(nfac)      ! tr ! <-- ! visc*surface/dist aux faces internes           !
! viscb(nfabor     ! tr ! <-- ! visc*surface/dist aux faces de bord            !
! da (ncelet       ! tr ! --> ! partie diagonale de la matrice                 !
! xa (nfac,*)      ! tr ! --> ! extra  diagonale de la matrice                 !
!__________________!____!_____!________________________________________________!

!     Type: i (integer), r (real), s (string), a (array), l (logical),
!           and composite types (ex: ra real array)
!     mode: <-- input, --> output, <-> modifies data, --- work array
!===============================================================================

!===============================================================================
! Module files
!===============================================================================

use parall

!===============================================================================

implicit none

! Arguments

integer          ncelet , ncel   , nfac   , nfabor
integer          iconvp , idiffp , ndircp , isym
integer          nfecra
double precision thetap

integer          ifacel(2,nfac), ifabor(nfabor)
double precision coefbp(nfabor), rovsdt(ncelet)
double precision flumas(nfac), flumab(nfabor)
double precision viscf(nfac), viscb(nfabor)
double precision da(ncelet ), xa(nfac ,isym)

! Local variables

integer          ifac, ii, jj, iel, ig, it
double precision flui, fluj, epsi

!===============================================================================

!===============================================================================
! 1. INITIALISATION
!===============================================================================

if (isym.ne.1.and.isym.ne.2) then
  write(nfecra,1000) isym
  call csexit (1)
endif

epsi = 1.d-7

!omp parallel do
do iel = 1, ncel
  da(iel) = rovsdt(iel)
enddo
if (ncelet.gt.ncel) then
  !omp parallel do if (ncelet - ncel > thr_n_min)
  do iel = ncel+1, ncelet
    da(iel) = 0.d0
  enddo
endif

if (isym.eq.2) then
  !omp parallel do
  do ifac = 1, nfac
    xa(ifac,1) = 0.d0
    xa(ifac,2) = 0.d0
  enddo
else
  !omp parallel do
  do ifac = 1, nfac
    xa(ifac,1) = 0.d0
  enddo
endif

!===============================================================================
! 2.    CALCUL DES TERMES EXTRADIAGONAUX
!===============================================================================

if (isym.eq.2) then

  !$omp parallel do firstprivate(thetap, iconvp, idiffp) private(flui, fluj)
  do ifac = 1, nfac
    flui = 0.5d0*(flumas(ifac) -abs(flumas(ifac)))
    fluj =-0.5d0*(flumas(ifac) +abs(flumas(ifac)))
    xa(ifac,1) = thetap*(iconvp*flui -idiffp*viscf(ifac))
    xa(ifac,2) = thetap*(iconvp*fluj -idiffp*viscf(ifac))
  enddo

else

  !$omp parallel do firstprivate(thetap, iconvp, idiffp) private(flui)
  do ifac = 1, nfac
    flui = 0.5d0*( flumas(ifac) -abs(flumas(ifac)) )
    xa(ifac,1) = thetap*(iconvp*flui -idiffp*viscf(ifac))
  enddo

endif

!===============================================================================
! 3.     CONTRIBUTION DES TERMES X-TRADIAGONAUX A LA DIAGONALE
!===============================================================================

if (isym.eq.2) then

  do ig = 1, ngrpi
    !$omp parallel do private(ifac, ii, jj)
    do it = 1, nthrdi
      do ifac = iompli(1,ig,it), iompli(2,ig,it)
        ii = ifacel(1,ifac)
        jj = ifacel(2,ifac)
        da(ii) = da(ii) - xa(ifac,2)
        da(jj) = da(jj) - xa(ifac,1)
      enddo
    enddo
  enddo

else

  do ig = 1, ngrpi
    !$omp parallel do private(ifac, ii, jj)
    do it = 1, nthrdi
      do ifac = iompli(1,ig,it), iompli(2,ig,it)
        ii = ifacel(1,ifac)
        jj = ifacel(2,ifac)
        da(ii) = da(ii) - xa(ifac,1)
        da(jj) = da(jj) - xa(ifac,1)
      enddo
    enddo
  enddo

endif

!===============================================================================
! 4.     CONTRIBUTION DES FACETTES DE BORDS A LA DIAGONALE
!===============================================================================

do ig = 1, ngrpb
  !$omp parallel do firstprivate(thetap, iconvp, idiffp) &
  !$omp          private(ifac, ii, flui, fluj) if(nfabor > thr_n_min)
  do it = 1, nthrdb
    do ifac = iomplb(1,ig,it), iomplb(2,ig,it)
      ii = ifabor(ifac)
      flui = 0.5d0*(flumab(ifac) - abs(flumab(ifac)))
      fluj =-0.5d0*(flumab(ifac) + abs(flumab(ifac)))
      da(ii) = da(ii) + thetap*(                                          &
                                  iconvp*(-fluj + flui*coefbp(ifac))      &
                                + idiffp*viscb(ifac)*(1.d0-coefbp(ifac)))
    enddo
  enddo
enddo

!===============================================================================
! 5.  NON PRESENCE DE PTS DIRICHLET --> LEGER RENFORCEMENT DE LA
!     DIAGONALE POUR DECALER LE SPECTRE DES VALEURS PROPRES
!===============================================================================
!     (si IDIRCL=0, on a force NDIRCP a valoir au moins 1 pour ne pas
!      decaler la diagonale)

if (ndircp.le.0) then
  !omp parallel do firstprivate(epsi)
  do iel=1,ncel
    da(iel) = (1.d0+epsi)*da(iel)
  enddo
endif

!--------
! FORMATS
!--------

#if defined(_CS_LANG_FR)

 1000 format(                                                           &
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/,&
'@ @@ ATTENTION : ARRET DANS matrix                           ',/,&
'@    =========                                               ',/,&
'@     APPEL DE matrix              AVEC ISYM   = ',I10        ,/,&
'@                                                            ',/,&
'@  Le calcul ne peut pas etre execute.                       ',/,&
'@                                                            ',/,&
'@  Contacter l''assistance.                                  ',/,&
'@                                                            ',/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@                                                            ',/)

#else

 1000 format(                                                           &
'@'                                                            ,/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@'                                                            ,/,&
'@ @@ WARNING: ABORT IN matrix'                                ,/,&
'@    ========'                                                ,/,&
'@     matrix CALLED                WITH ISYM   = ',I10        ,/,&
'@'                                                            ,/,&
'@  The calculation will not be run.'                          ,/,&
'@'                                                            ,/,&
'@  Contact support.'                                          ,/,&
'@'                                                            ,/,&
'@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@',/,&
'@'                                                            ,/)

#endif

!----
! FIN
!----

return

end subroutine

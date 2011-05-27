!-------------------------------------------------------------------------------

!     This file is part of the Code_Saturne Kernel, element of the
!     Code_Saturne CFD tool.

!     Copyright (C) 1998-2010 EDF S.A., France

!     contact: saturne-support@edf.fr

!     The Code_Saturne Kernel is free software; you can redistribute it
!     and/or modify it under the terms of the GNU General Public License
!     as published by the Free Software Foundation; either version 2 of
!     the License, or (at your option) any later version.

!     The Code_Saturne Kernel is distributed in the hope that it will be
!     useful, but WITHOUT ANY WARRANTY; without even the implied warranty
!     of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
!     GNU General Public License for more details.

!     You should have received a copy of the GNU General Public License
!     along with the Code_Saturne Kernel; if not, write to the
!     Free Software Foundation, Inc.,
!     51 Franklin St, Fifth Floor,
!     Boston, MA  02110-1301  USA

!-------------------------------------------------------------------------------

subroutine laggra &
!================

 ( idbia0 , idbra0 ,                                              &
   nvar   , nscal  ,                                              &
   ia     ,                                                       &
   rtp    , propce , coefa  , coefb  ,                            &
   gradpr , gradvf ,                                              &
   ra     )

!===============================================================================
! FONCTION :
! ----------

!   SOUS-PROGRAMME DU MODULE LAGRANGIEN :
!   -------------------------------------

!    1)   Calcul de (- (GRADIENT DE PRESSION) / ROM )

!    2)   Calcul du gradient de Vitesse

!-------------------------------------------------------------------------------
! Arguments
!__________________.____._____.________________________________________________.
! name             !type!mode ! role                                           !
!__________________!____!_____!________________________________________________!
! idbia0           ! i  ! <-- ! number of first free position in ia            !
! idbra0           ! i  ! <-- ! number of first free position in ra            !
! nvar             ! i  ! <-- ! total number of variables                      !
! nscal            ! i  ! <-- ! total number of scalars                        !
! ia(*)            ! ia ! --- ! main integer work array                        !
! rtp              ! tr ! <-- ! variables de calcul au centre des              !
! (ncelet,*)       !    !     !    cellules (instant courant ou prec)          !
! propce(ncelet, *)! ra ! <-- ! physical properties at cell centers            !
! coefa,coefb      ! tr ! <-- ! tableaux des cond lim pour pvar                !
!   (nfabor)       !    !     !  sur la normale a la face de bord              !
! gradpr(ncel,3    ! tr ! --> ! gradient de pression                           !
! gradvf(ncel,9    ! tr ! --> ! gradient de vitesse fluide                     !
! ra(*)            ! ra ! --- ! main real work array                           !
!__________________!____!_____!________________________________________________!

!     TYPE : E (ENTIER), R (REEL), A (ALPHANUMERIQUE), T (TABLEAU)
!            L (LOGIQUE)   .. ET TYPES COMPOSES (EX : TR TABLEAU REEL)
!     MODE : <-- donnee, --> resultat, <-> Donnee modifiee
!            --- tableau de travail
!===============================================================================

!===============================================================================
! Module files
!===============================================================================

use paramx
use dimens, only: ndimfb
use numvar
use optcal
use entsor
use cstphy
use pointe
use parall
use period
use lagpar
use lagran
use ppppar
use ppthch
use ppincl
use mesh

!===============================================================================

implicit none

! Arguments

integer          idbia0 , idbra0
integer          nvar   , nscal

integer          ia(*)

double precision coefa(ndimfb,*) , coefb(ndimfb,*)
double precision rtp(ncelet,*)
double precision propce(ncelet,*)
double precision gradpr(ncelet,3) , gradvf(ncelet,9)
double precision ra(*)

! Local variables

integer          idebia, idebra

integer          inc , iccocg , iclipr
integer          ipcliu , ipcliv , ipcliw
integer          iromf
integer          iel
double precision unsrho

!===============================================================================

idebia = idbia0
idebra = idbra0

!===============================================================================
! 0. PARAMETRES
!===============================================================================

!-->Rappel de differents paramtrage possibles pour le calcul du gradient

!     INC     = 1               ! 0 RESOL SUR INCREMENT 1 SINON
!     ICCOCG  = 1               ! 1 POUR RECALCUL DE COCG 0 SINON
!     NSWRGR  = 100             ! mettre 1 pour un maillage regulie (par defaut : 100)
!     IMLIGR  = -1              ! LIMITATION DU GRADIENT : < 0 PAS DE LIMITATION
!     IWARNI  = 1               ! NIVEAU D'IMPRESSION
!     EPSRGR  = 1.D-8           ! PRECISION RELATIVE POUR LA REC GRA 97
!     CLIMGR  = 1.5D0           ! COEF GRADIENT*DISTANCE/ECART
!     EXTRAG  = 0               ! COEF GRADIENT*DISTANCE/ECART


!-->Parametrage des calculs des gradients

inc     = 1
iccocg  = 1

!===============================================================================
! 1. CALCUL DE :  - (GRADIENT DE PRESSION)/ROM
!===============================================================================

iclipr = iclrtp(ipr,icoef)

! Calcul du gradient de pression


! ---> TRAITEMENT DU PARALLELISME ET DE LA PERIODICITE

if (irangp.ge.0.or.iperio.eq.1) then
  call synsca(rtp(1,ipr))
  !==========
endif

call grdcel &
!==========
 ( ipr , imrgra , inc    , iccocg ,                            &
   nswrgr(ipr)  , imligr(ipr)  ,                               &
   iwarni(ipr)  , nfecra ,                                     &
   epsrgr(ipr)  , climgr(ipr)  , extrag(ipr)  ,                &
   ia     ,                                                    &
   rtp(1,ipr)  , coefa(1,iclipr) , coefb(1,iclipr) ,           &
   gradpr(1,1)     , gradpr(1,2)     , gradpr(1,3)     ,       &
   ra     )

! Pointeur sur la masse volumique en fonction de l'ecoulement

if ( ippmod(icp3pl).ge.0 .or. ippmod(icfuel).ge.0 ) then
  iromf = ipproc(irom1)
else
  iromf = ipproc(irom)
endif

! Calcul de -Grad P / Rom

do iel = 1,ncel
  unsrho = 1.d0 / propce(iel,iromf)
  gradpr(iel,1) = -gradpr(iel,1) * unsrho
  gradpr(iel,2) = -gradpr(iel,2) * unsrho
  gradpr(iel,3) = -gradpr(iel,3) * unsrho
enddo

!===============================================================================
! 2. CALCUL DU GRADIENT DE VITESSE :
!===============================================================================

if (modcpl.gt.0 .and. iplas.ge.modcpl) then

  ipcliu = iclrtp(iu,icoef)
  ipcliv = iclrtp(iv,icoef)
  ipcliw = iclrtp(iw,icoef)

! ---> TRAITEMENT DU PARALLELISME ET DE LA PERIODICITE

  if (irangp.ge.0.or.iperio.eq.1) then
    call synvec(rtp(1,iu), rtp(1,iv), rtp(1,iw))
    !==========
  endif

!     COMPOSANTE X
!     ============

  call grdcel &
  !==========
 ( iu  , imrgra , inc    , iccocg ,                        &
   nswrgr(iu)   , imligr(iu)  ,                            &
   iwarni(iu)   , nfecra ,                                 &
   epsrgr(iu)   , climgr(iu)  , extrag(iu)  ,              &
   ia     ,                                                &
   rtp(1,iu)   , coefa(1,ipcliu) , coefb(1,ipcliu) ,       &
   gradvf(1,1)     , gradvf(1,2)     , gradvf(1,3)     ,   &
   ra     )

!     COMPOSANTE Y
!     ============

  call grdcel &
  !==========
 ( iv  , imrgra , inc    , iccocg ,                         &
   nswrgr(iv)   , imligr(iv)  ,                             &
   iwarni(iv)   , nfecra ,                                  &
   epsrgr(iv)   , climgr(iv)  , extrag(iv)  ,               &
   ia     ,                                                 &
   rtp(1,iv)   , coefa(1,ipcliv) , coefb(1,ipcliv) ,        &
   gradvf(1,4)     , gradvf(1,5)     , gradvf(1,6)     ,    &
   ra     )

!     COMPOSANTE Z
!     ============

  call grdcel &
  !==========
 ( iw  , imrgra , inc    , iccocg ,                         &
   nswrgr(iw)   , imligr(iw)  ,                             &
   iwarni(iw)   , nfecra ,                                  &
   epsrgr(iw)   , climgr(iw)  , extrag(iw)  ,               &
   ia     ,                                                 &
   rtp(1,iw)   , coefa(1,ipcliw) , coefb(1,ipcliw) ,        &
   gradvf(1,7)     , gradvf(1,8)     , gradvf(1,9)     ,    &
   ra     )

endif

!----
! FIN
!----

return

end subroutine

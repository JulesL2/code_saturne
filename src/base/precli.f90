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

!> \file precli.f90
!> \brief Preparation of boundary conditions determination
!> Boundary faces of precedent step are used.
!> Except at first time step, where arrays \ref itypfb and
!> \ref pointe::itrifb "itrifb" are undefined.
!
!------------------------------------------------------------------------------

!------------------------------------------------------------------------------
! Arguments
!------------------------------------------------------------------------------
!   mode          name          role
!------------------------------------------------------------------------------
!> \param[in]     nvar          total number of variables
!> \param[out]    rcodcl        boundary condition values:
!>                               - rcodcl(1) value of the dirichlet
!>                               - rcodcl(2) value of the exterior exchange
!>                                 coefficient (infinite if no exchange)
!>                               - rcodcl(3) value flux density
!>                                 (negative if gain) in w/m2 or roughness
!>                                 in m if icodcl=6
!>                                 -# for the velocity \f$ (\mu+\mu_T)
!>                                    \gradv \vect{u} \cdot \vect{n}  \f$
!>                                 -# for the pressure \f$ \Delta t
!>                                    \grad P \cdot \vect{n}  \f$
!>                                 -# for a scalar \f$ cp \left( K +
!>                                     \dfrac{K_T}{\sigma_T} \right)
!>                                     \grad T \cdot \vect{n} \f$
!______________________________________________________________________________

subroutine precli &
 ( nvar   ,                                                       &
   rcodcl )

!===============================================================================
! Module files
!===============================================================================

use paramx
use numvar
use optcal
use cstphy
use cstnum
use entsor
use pointe
use albase
use ppppar
use ppthch
use ppincl
use mesh

!===============================================================================

implicit none

! Arguments

integer          nvar

double precision rcodcl(nfabor,nvar,3)

! Local variables

integer          ifac

!===============================================================================

! itypfb has already been initialized with default value, do not modify it here.

! rcodcl has also been intialized.

! En ALE, on initialise aussi le tableau IALTYB
if (iale.ge.1) then
  do ifac = 1, nfabor
    ialtyb(ifac) = 0
  enddo
endif

! Physical module
if (ippmod(iphpar).ge.1) then
  call ppprcl(nvar, izfppp, rcodcl)
endif

!----
! End
!----

return
end subroutine

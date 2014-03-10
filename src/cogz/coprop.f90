!-------------------------------------------------------------------------------

! This file is part of Code_Saturne, a general-purpose CFD tool.
!
! Copyright (C) 1998-2014 EDF S.A.
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

subroutine coprop
!================

!===============================================================================
! Purpose:
! --------

! Define state variables for gas combustion,
! diffusion and premixed flame.

!-------------------------------------------------------------------------------
! Arguments
!__________________.____._____.________________________________________________.
! name             !type!mode ! role                                           !
!__________________!____!_____!________________________________________________!
!__________________!____!_____!________________________________________________!

!     Type: i (integer), r (real), s (string), a (array), l (logical),
!           and composite types (ex: ra real array)
!     mode: <-- input, --> output, <-> modifies data, --- work array
!===============================================================================

!===============================================================================
! Module files
!===============================================================================

use paramx
use dimens
use numvar
use optcal
use cstphy
use entsor
use cstnum
use ppppar
use ppthch
use coincl
use cpincl
use ppincl
use radiat
use ihmpre

!===============================================================================

implicit none

! Local variables

integer       idirac, nprini

character(len=80) :: f_name, f_label

!===============================================================================

! Initialization

nprini = nproce

! Flamme de diffusion chimie 3 points
!====================================

if (ippmod(icod3p).ge.0) then

  call add_property_field('temperature', 'Temperature', itemp)

  call add_property_field('ym_fuel', 'Ym_Fuel', iym(1))
  call add_property_field('ym_oxyd', 'Ym_Oxyd', iym(2))
  call add_property_field('ym_prod', 'Ym_Prod', iym(3))

endif

! Premixed flame - EBU model
!===========================

if (ippmod(icoebu).ge.0) then

  call add_property_field('temperature', 'Temperature', itemp)

  call add_property_field('ym_fuel', 'Ym_Fuel', iym(1))
  call add_property_field('ym_oxyd', 'Ym_Oxyd', iym(2))
  call add_property_field('ym_prod', 'Ym_Prod', iym(3))

endif

! Premixed flame - LWC model
!===========================

if (ippmod(icolwc).ge.0) then

  call add_property_field('temperature', 'Temperature', itemp)
  call add_property_field('molar_mass',  'Molar_Mass',  imam)
  call add_property_field('source_term', 'Source_Term', itsc)

  call add_property_field('ym_fuel', 'Ym_Fuel', iym(1))
  call add_property_field('ym_oxyd', 'Ym_Oxyd', iym(2))
  call add_property_field('ym_prod', 'Ym_Prod', iym(3))

  do idirac = 1, ndirac

    write(f_name,  '(a,i1)') 'rho_local_', idirac
    write(f_label, '(a,i1)') 'Rho_Local_', idirac
    call add_property_field(f_name, f_label, irhol(idirac))

    write(f_name,  '(a,i1)') 'temperature_local_', idirac
    write(f_label, '(a,i1)') 'Temperature_Local_', idirac
    call add_property_field(f_name, f_label, iteml(idirac))

    write(f_name,  '(a,i1)') 'ym_local_', idirac
    write(f_label, '(a,i1)') 'Ym_Local_', idirac
    call add_property_field(f_name, f_label, ifmel(idirac))

    write(f_name,  '(a,i1)') 'wm_local_', idirac
    write(f_label, '(a,i1)') 'wm_Local_', idirac
    call add_property_field(f_name, f_label, ifmal(idirac))

    write(f_name,  '(a,i1)') 'amplitude_local_', idirac
    write(f_label, '(a,i1)') 'Amplitude_Local_', idirac
    call add_property_field(f_name, f_label, iampl(idirac))

    write(f_name,  '(a,i1)') 'chemical_st_local_', idirac
    write(f_label, '(a,i1)') 'Chemical_ST_Local_', idirac
    call add_property_field(f_name, f_label, itscl(idirac))

    write(f_name,  '(a,i1)') 'molar_mass_local_', idirac
    write(f_label, '(a,i1)') 'M_Local_', idirac
    call add_property_field(f_name, f_label, imaml(idirac))

  enddo

endif

! Additional fields for radiation
!================================

if (iirayo.ge.1) then
  if (ippmod(icod3p).eq.1 .or.                                  &
      ippmod(icoebu).eq.1 .or. ippmod(icoebu).eq.3 .or.         &
      ippmod(icolwc).eq.1 .or. ippmod(icolwc).eq.3 .or.         &
      ippmod(icolwc).eq.5) then
    call add_property_field('kabs',          'KABS',  ickabs)
    call add_property_field('temperature_4', 'Temp4', it4m)
    call add_property_field('temperature_3', 'Temp3', it3m)
  endif
endif

! Nb algebraic (or state) variables
!   specific to specific physic: nsalpp
!   total: nsalto

nsalpp = nproce - nprini
nsalto = nproce

! Code_Saturne GUI
! ================
! Construction de l'indirection entre la numerotation du noyau et XML
if (iihmpr.eq.1) then
  call uicopr (nsalpp, ippmod, ipppro, ipproc,                       &
               icolwc, iirayo, itemp, imam, iym, ickabs, it4m, it3m, &
               itsc, irhol, iteml, ifmel, ifmal, iampl, itscl,       &
               imaml)

endif

return

end subroutine

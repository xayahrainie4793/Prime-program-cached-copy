/*

ecpp.c - executable for elliptic curve primality proofs

Copyright (C) 2021, 2022 Andreas Enge

This file is part of CM.

CM is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3 of the license, or (at your
option) any later version.

CM is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with CM; see the file COPYING. If not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "params.h"

int main (int argc, char* argv [])
{
   mpz_t n;
   bool print, verbose, debug, trust, check;
   int phases;
   char *filename;

   mpz_init (n);
   cm_pari_init ();
   evaluate_parameters_ecpp (argc, argv, n, &print, &filename, &verbose,
      &debug, &trust, &check, &phases);

   cm_ecpp (n, CM_MODPOLDIR, filename, getenv ("CM_ECPP_TMPDIR"),
      print, trust, check, phases, verbose, debug);

   cm_pari_clear ();
   mpz_clear (n);

   return 0;
}

/* primes.c -- (C) 2006 Mark Rodenkirch, Geoffrey Reynolds.

   init_prime_sieve(p) :- prepare the sieve to generate all primes up to p.
   prime_sieve(p0,p1,fun) :- call fun(p) for each prime p0 <= p <= p1.
   fini_prime_sieve() :- release resources ready for another call to init().

   Original code was supplied by Mark Rodenkirch and hacked about by
   Geoffrey Reynolds for use in srsieve.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include <assert.h>
#include <math.h>
#include <inttypes.h>
#include <stdlib.h>
#include "sr1sieve.h"
#include "arithmetic.h"
#include "bitmap.h"
#include "memset_fast32.h"


/* For the sieve to generate all primes up to p, prime_table will need to
   contain all primes up to sqrt(p).
*/
static uint32_t *prime_table, primes_in_prime_table;

/* This table is the same size as prime_table. Its values correspond to the
   smallest composite greater than the low end of the range being sieved.
*/
static uint64_t *composite_table;

/* Candidate odd numbers are sieved in blocks of size range_size. This
   variable should not exceed the size (in bits) of level 2 cache, and
   must be a multiple of 32 (64 on 64-bit machines).
*/
static uint_fast32_t range_size;

/* primes_used_in_range is the current number of primes in prime_table that
   are being used to sieve the current range. This variable is increased in
   steps of size PRIMES_USED_STEP up to a maximum of primes_in_prime_table.
*/
static uint32_t primes_used_in_range;
#define PRIMES_USED_STEP 2000

/* Call check_progress() after every PROGRESS_STEP candidates are processed.
 */
#define PROGRESS_STEP 65536


/* Return an upper bound on the number of primes <= n.
 */
static uint32_t primes_bound(uint32_t n)
{
  return 1.088375*n/log(n)+168;
}

/* The smallest acceptable argument to init_prime_sieve()
 */
#define MINIMUM_PMAX 10

void init_prime_sieve(uint64_t pmax)
{
  uint32_t low_prime_limit, max_low_primes, *low_primes, low_prime_count;
  uint_fast32_t *sieve;
  uint32_t sieve_index, max_prime, max_primes_in_table;
  uint32_t p, minp, i, composite;

  pmax = MAX(MINIMUM_PMAX,pmax);
  max_prime = sqrt(pmax)+1;
  low_prime_limit = sqrt(max_prime)+1;
  max_low_primes = primes_bound(low_prime_limit);
  low_primes = xmalloc(max_low_primes * sizeof(uint32_t));

  low_primes[0] = 3;
  low_prime_count = 1;
  for (p = 5; p < low_prime_limit; p += 2)
    for (minp = 0; minp <= low_prime_count; minp++)
    {
      if (low_primes[minp] * low_primes[minp] > p)
      {
        low_primes[low_prime_count] = p;
        low_prime_count++;
        break;
      }
      if (p % low_primes[minp] == 0)
        break;
    }

  assert(low_prime_count <= max_low_primes);

  /* Divide max_prime by 2 to save memory, also because already know that
     all even numbers in the sieve are composite. */
  sieve = make_bitmap(max_prime/2,"Sieve of Erathosthenes bitmap");
  fill_bits(sieve,1,max_prime/2-1);

  for (i = 0; i < low_prime_count; i++)
  {
    /* Get the current low prime. Start sieving at 3x that prime since 1x
       is prime and 2x is divisible by 2. sieve[1]=3, sieve[2]=5, etc. */
    composite = 3*low_primes[i];
    sieve_index = (composite-1)/2;

    while (composite < max_prime)
    {
      /* composite will always be odd, so add 2*low_primes[i] */
      clear_bit(sieve,sieve_index);
      sieve_index += low_primes[i];
      composite += 2*low_primes[i];
    }
  }

  free(low_primes);
  max_primes_in_table = primes_bound(max_prime);
  prime_table = xmalloc(max_primes_in_table * sizeof(uint32_t));
  primes_in_prime_table = 0;

  for (i = first_bit(sieve); i < max_prime/2; i = next_bit(sieve,i+1))
  {
    /* Convert the value back to an actual prime. */
    prime_table[primes_in_prime_table] = 2*i + 1;
    primes_in_prime_table++;
  }

  assert(primes_in_prime_table <= max_primes_in_table);

  free(sieve);
  composite_table = xmalloc(primes_in_prime_table * sizeof(uint64_t));

  if (range_size == 0)
  {
    /* Use up to half of L2 cache, but no more than 2Mb. */
    range_size = 4*L2_cache_size*1024;
    if (range_size > 4*4096*1024)
      range_size = 4*4096*1024;
    else if (range_size < 4*16*1024)
      range_size = 4*16*1024;
    if (verbose_opt)
      report(1,"Using %"PRIuFAST32"Kb for the Sieve of Eratosthenes bitmap.",
             range_size/8/1024);
  }
}

void fini_prime_sieve(void)
{
  free(prime_table);
  free(composite_table);
}

/* Setup for primes of the form p=1 (mod 2^y).
 */
static void setup_sieve(uint64_t low_end_of_range, int y)
{
  uint64_t max_prime, last_composite;
  uint32_t i, save_used_in_range;
  const uint_fast32_t mask = (1 << y) - 1;

  if (primes_used_in_range >= primes_in_prime_table)
    return;

  save_used_in_range = primes_used_in_range;

  if (primes_used_in_range == 0)
    primes_used_in_range = PRIMES_USED_STEP;

  while (primes_used_in_range < primes_in_prime_table)
  {
    max_prime = prime_table[primes_used_in_range - 1];
    max_prime *= max_prime;
    if (max_prime > low_end_of_range + ((uint64_t)range_size << y))
      break;
    primes_used_in_range += PRIMES_USED_STEP;
  }

  if (primes_used_in_range > primes_in_prime_table)
    primes_used_in_range = primes_in_prime_table;

  /* Find the largest composite greater than low_end_of_range. */ 
  for (i = save_used_in_range; i < primes_used_in_range; i++)
  {
    max_prime = prime_table[i];
    last_composite = (low_end_of_range / max_prime) * max_prime;

    /* Find the next composite = 1 (mod 2^y)
     */
    do last_composite += max_prime;
    while (((uint_fast32_t)last_composite & mask) != 1);

    composite_table[i] = last_composite;
  }
}

void prime_sieve(uint64_t low_prime, uint64_t high_prime,
                 void (*fun)(uint64_t,int))
{
  uint64_t low_end_of_range, candidate;
  uint_fast32_t *sieve;
  uint32_t i, j, k;
  const uint32_t mod = seq_mod;

  assert(primes_in_prime_table > 0);
  assert(range_size % UINT_FAST32_BIT == 0);

  if (low_prime <= prime_table[primes_in_prime_table-1])
  {
    /* Skip ahead to low_prime. A binary search would be faster. */
    for (i = 0; prime_table[i] < low_prime; i++)
      ;
    if (seq_parity == 0) /* mixed parity terms */
    {
      uint_fast32_t qr_even, qr_odd;
      while (i < primes_in_prime_table)
      {
        for (j = MIN(i+PROGRESS_STEP/8,primes_in_prime_table); i < j; i++)
        {
          candidate = prime_table[i];
          if (candidate > high_prime)
            return;
          if (mod) /* Predictable */
          {
            uint32_t qr_mod = (candidate/2) % mod;
            qr_even = test_bit(seq_map[0],qr_mod);
            qr_odd = test_bit(seq_map[1],qr_mod);
          }
          else /* No lookup table available */
          {
            int sym = legendre64(kc_core,candidate);
            qr_even = (sym == 1);
            qr_odd = (sym == legendre64(b_term,candidate));
          }
          if (qr_even || qr_odd)
          {
            check_events(candidate);
            fun(candidate, qr_even ? (qr_odd ? 0 : 1) : -1);
          }
        }
        check_progress();
      }
    }
    else /* single parity terms */
    {
      uint_fast32_t qr;
      while (i < primes_in_prime_table)
      {
        for (j = MIN(i+PROGRESS_STEP/8,primes_in_prime_table); i < j; i++)
        {
          candidate = prime_table[i];
          if (candidate > high_prime)
            return;
          if (mod) /* Predictable */
          {
            qr = test_bit(seq_map[0],(candidate/2) % mod);
          }
          else /* No lookup table available */
          {
            int sym = legendre64(kc_core,candidate);
            if (seq_parity == 1)
              qr = (sym == 1);
            else
              qr = (sym == legendre64(b_term,candidate));
          }
          if (qr)
          {
            check_events(candidate);
            fun(candidate,seq_parity);
          }
        }
        check_progress();
      }
    }
    low_end_of_range = prime_table[primes_in_prime_table-1]+1;
  }
  else /* Set low_end_of_range to the greatest even number <= low_prime */
    low_end_of_range = (low_prime | 1) - 1;

  sieve = make_bitmap(range_size,"Sieve of Eratosthenes bitmap");
  primes_used_in_range = 0;

  while (low_end_of_range <= high_prime)
  {
    setup_sieve(low_end_of_range,1);
    memset_fast32(sieve,UINT_FAST32_MAX,range_size/UINT_FAST32_BIT);

    for (i = 0; i < primes_used_in_range; i++)
      if (composite_table[i] < low_end_of_range + 2*range_size)
      {
        uint_fast32_t prime = prime_table[i];
        uint_fast32_t sieve_index = (composite_table[i] - low_end_of_range)/2;

        do clear_bit(sieve,sieve_index);
        while ((sieve_index += prime) < range_size);

        composite_table[i] = low_end_of_range + 2*(uint64_t)sieve_index + 1;
      }

    k = MIN((high_prime-low_end_of_range+1)/2,range_size);
    i = first_bit(sieve);
    if (seq_parity == 0) /* Mixed parity terms */
    {
      uint_fast32_t qr_even, qr_odd;
      while (i < k)
      {
        for (j = MIN(i+PROGRESS_STEP,k); i < j; i = next_bit(sieve,i+1))
        {
          candidate = low_end_of_range + 2*i + 1;
          if (mod)
          {
            uint32_t qr_mod = (candidate/2) % mod;
            qr_even = test_bit(seq_map[0],qr_mod);
            qr_odd = test_bit(seq_map[1],qr_mod);
          }
          else /* No lookup table available */
          {
            int sym = legendre64(kc_core,candidate);
            qr_even = (sym == 1);
            qr_odd = (sym == legendre64(b_term,candidate));
          }
          if (qr_even || qr_odd)
          {
            check_events(candidate);
            fun(candidate, qr_even ? (qr_odd ? 0 : 1) : -1);
          }
        }
        check_progress();
      }
    }
    else /* Single parity terms */
    {
      uint_fast32_t qr;
      while (i < k)
      {
        for (j = MIN(i+PROGRESS_STEP,k); i < j; i = next_bit(sieve,i+1))
        {
          candidate = low_end_of_range + 2*i + 1;
          if (mod)
          {
            qr = test_bit(seq_map[0],(candidate/2) % mod);
          }
          else /* No lookup table available */
          {
            int sym = legendre64(kc_core,candidate);
            if (seq_parity == 1)
              qr = (sym == 1);
            else
              qr = (sym == legendre64(b_term,candidate));
          }
          if (qr)
          {
            check_events(candidate);
            fun(candidate,seq_parity);
          }
        }
        check_progress();
      }
    }
    low_end_of_range += 2*range_size;
  }

  free(sieve);
}

#if CHECK_FOR_GFN
/* Sieve only for primes p of the form p=1 (mod 2^y).
 */
void prime_sieve_gfn(uint64_t low_prime, uint64_t high_prime,
                     void (*fun)(uint64_t,int), int y)
{
  uint64_t low_end_of_range, candidate;
  uint_fast32_t *sieve;
  uint32_t i, j, k;
  const uint32_t mask = (1 << y) - 2;

  assert(primes_in_prime_table > 0);
  assert(range_size % UINT_FAST32_BIT == 0);
  assert(y > 1);

  if (low_prime <= prime_table[primes_in_prime_table-1])
  {
    /* Skip ahead to low_prime. A binary search would be faster. */
    for (i = 0; prime_table[i] < low_prime; i++)
      ;
    while (i < primes_in_prime_table)
    {
      for (j = MIN(i+PROGRESS_STEP/8,primes_in_prime_table); i < j; i++)
      {
        candidate = prime_table[i];
        if (candidate > high_prime)
          return;
        if (!(candidate & mask))
        {
          check_events(candidate);
          fun(candidate,seq_parity);
        }
      }
      check_progress();
    }
    low_end_of_range = prime_table[primes_in_prime_table-1]+1;
  }
  else
    low_end_of_range = (low_prime | 1) - 1;

  if ((uint_fast32_t)low_end_of_range & (uint_fast32_t)mask)
    low_end_of_range = (low_end_of_range | ((UINT64_C(1) << y)-1)) + 1;

  sieve = make_bitmap(range_size,"Sieve of Eratosthenes bitmap");
  primes_used_in_range = 0;

  while (low_end_of_range <= high_prime)
  {
    setup_sieve(low_end_of_range,y);
    memset_fast32(sieve,UINT_FAST32_MAX,range_size/UINT_FAST32_BIT);

    for (i = 0; i < primes_used_in_range; i++)
      if (composite_table[i] < low_end_of_range + ((uint64_t)range_size << y))
      {
        uint_fast32_t prime = prime_table[i];
        uint_fast32_t sieve_index = (composite_table[i]-low_end_of_range) >> y;

        do clear_bit(sieve,sieve_index);
        while ((sieve_index += prime) < range_size);

        composite_table[i] = low_end_of_range + ((uint64_t)sieve_index << y)+1;
      }

    k = MIN((high_prime - low_end_of_range + (1 << y)-1) >> y, range_size);
    i = first_bit(sieve);
    while (i < k)
    {
      for (j = MIN(i+PROGRESS_STEP,k); i < j; i = next_bit(sieve,i+1))
      {
        candidate = low_end_of_range + ((uint64_t)i << y) + 1;
        check_events(candidate);
        fun(candidate,seq_parity);
      }
      check_progress();
    }
    low_end_of_range += ((uint64_t)range_size << y);
  }

  free(sieve);
}
#endif
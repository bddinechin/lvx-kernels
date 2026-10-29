/* Driver for the black-scholes kernel.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Separate from src/black_scholes.c for the same reason as the others: the kernel
 * stays comparable to upstream, the timing and checking are ours, and
 * upstream's own driver is C++ with <iostream>.
 *
 * The option parameters sweep the same range binomial's driver uses, so the
 * two kernels price the same inputs -- a European call here against an
 * American put there, so the prices differ, but a change in the inputs shows
 * up in both and neither can drift unnoticed.
 *
 * The parameters sweep a range rather than repeating one option. A
 * single option repeated `count' times would let the compiler hoist nothing
 * extra -- the kernel recomputes per option either way -- but it would make
 * the checksum insensitive to all but one path through the tree, and a
 * benchmark whose answer does not depend on most of its input is a poor
 * oracle. The ranges are the ISPC example's: spot and strike near 100, a year
 * or so to expiry, rates and volatilities in the usual bands.
 *
 * The checksum is a scaled integer sum of the prices, not a float: a float
 * sum compared for equality across a native and a cross build is a trap --
 * the two may legitimately differ in the last bit and the suite would read
 * that as a wrong answer. Scaling to milli-units and summing as integers
 * compares what the benchmark is actually about, the price to three decimals,
 * and leaves bit-level FP agreement to validation/ in lvx-csw, which exists
 * for exactly that and does it properly.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernels.h"

#ifndef BS_COUNT
#define BS_COUNT 64
#endif

static uint64_t
cycles (void)
{
#ifdef __lvx__
    uint64_t c;
    __asm__ __volatile__ ("get %0 = $frcc\n\t;;" : "=r"(c) : : "memory");
    return c;
#else
    return 0;   /* the native build is the oracle; see mandelbrot-main.c */
#endif
}

int
main (void)
{
    const int count = BS_COUNT;

    float *Sa = malloc ((size_t)count * sizeof *Sa);
    float *Xa = malloc ((size_t)count * sizeof *Xa);
    float *Ta = malloc ((size_t)count * sizeof *Ta);
    float *ra = malloc ((size_t)count * sizeof *ra);
    float *va = malloc ((size_t)count * sizeof *va);
    float *result = malloc ((size_t)count * sizeof *result);
    if (!Sa || !Xa || !Ta || !ra || !va || !result) {
        printf ("status alloc_failed\n");
        return 1;
    }

    for (int i = 0; i < count; ++i) {
        const float t = (float)i / (float)count;
        Sa[i] = 80.f + 40.f * t;          /* spot   80 .. 120 */
        Xa[i] = 100.f;                    /* strike fixed, so moneyness sweeps */
        Ta[i] = 0.5f + 1.5f * t;          /* expiry 0.5 .. 2 years */
        ra[i] = 0.01f + 0.05f * t;        /* rate    1% .. 6%  */
        va[i] = 0.10f + 0.40f * t;        /* vol    10% .. 50% */
        result[i] = -1.f;
    }

    const uint64_t t0 = cycles ();
    black_scholes_serial (Sa, Xa, Ta, ra, va, result, count);
    const uint64_t t1 = cycles ();

    /* Milli-units, summed as integers: see the header. A negative price is
     * impossible for a put bounded below by zero, so the -1 prefill also
     * reports an option the kernel never wrote. */
    uint64_t sum = 0;
    int unwritten = 0;
    for (int i = 0; i < count; ++i) {
        if (result[i] < 0.f) { unwritten++; continue; }
        sum += (uint64_t)(result[i] * 1000.f + 0.5f);
    }

    printf ("kernel black_scholes\n");
    printf ("count %d\n", count);
    printf ("elapsed %llu\n", (unsigned long long)(t1 - t0));
    printf ("checksum %llu\n", (unsigned long long)sum);
    printf ("unwritten %d\n", unwritten);
    printf ("status %s\n", unwritten == 0 ? "ok" : "unwritten_options");

    free (Sa); free (Xa); free (Ta); free (ra); free (va); free (result);
    return unwritten == 0 ? 0 : 1;
}

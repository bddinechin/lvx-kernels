/* Binomial-tree American put option.
 *
 * A C translation of
 *   autovec-benchmark/src/microkernels/binomial/binomial_serial.cpp
 * (https://github.com/sergisiso/autovec-benchmark), itself modified from the
 * ISPC examples.  BSD-3-Clause, three stacked notices; see LICENSE.txt.
 *
 * WHY THIS KERNEL.  The backward sweep is a shape nothing else in lvx-perf
 * has:
 *
 *     for (j = BINOMIAL_NUM-1; j >= 0; --j)
 *         for (k = 0; k < j; ++k)
 *             V[k] = ((1 - Pu) * V[k] + Pu * V[k+1]) / disc;
 *
 * Two things about it.
 *
 * The k loop reads V[k+1] and writes V[k], so iteration k reads what
 * iteration k+1 later overwrites: an **anti-dependence at distance 1**, not a
 * flow dependence.  It is therefore vectorizable -- load the whole vector
 * before storing it -- and the j loop around it is the serial one.  Every
 * recurrence lvx-perf measures so far is a flow dependence; this is the other
 * kind.
 *
 * And the trip count **shrinks**: 63, 62, ... 2, 1, 0.  Most of those are
 * shorter than a vector, so almost every j iteration is tail.  That is what
 * `taild' exists for -- it builds a lane mask from an index and a bound in one
 * ALU_TINY syllable -- and what the partial-vector/while_ult path exists for
 * (lvx-gcc-while-ult-plan: partial vectors are on, cond_<optab> next).  No
 * other kernel here presents a loop whose trip count is mostly tail.
 *
 * There is also a divide in the recurrence.  Without -ffast-math it stays a
 * divide, latency 13 on the FD functional unit, inside a loop-carried chain --
 * so this kernel prices that too.
 *
 * TWO DELIBERATE DEVIATIONS FROM UPSTREAM, both named because the port is
 * otherwise faithful and because each is a defect rather than a choice.
 *
 * 1. Upstream guards the unspecialised side with `#ifndef SPECIALIZE' where
 *    its siblings use `#if SPECIALIZE'.  Built with -DSPECIALIZE=0 the macro
 *    IS defined, so the guard is false, BINOMIAL_NUM keeps its zero
 *    initialiser, V[] is zero-length and every loop runs zero trips -- a
 *    benchmark that measures nothing and still prints a result.  It only works
 *    if SPECIALIZE is left undefined.  This file uses `#if SPECIALIZE'
 *    throughout, as convolution and mandelbrot upstream both do.
 *
 * 2. Upstream's unspecialised side then assigns `BINOMIAL_NUM = 64' from a
 *    literal in the same function, which GCC constant-propagates -- so
 *    nothing about the step count is actually withheld and the axis reduces to
 *    `__restrict__' alone.  Here the step count arrives as a parameter, which
 *    is how the siblings withhold their constants and the only way the axis
 *    measures what it claims.
 *
 * SPECIALIZE=1   BINOMIAL_NUM is a constant and the six arrays are restrict.
 * SPECIALIZE=0   the step count arrives at run time and nothing is restrict.
 * Unlike mandelbrot, this kernel does carry the aliasing half of the axis.
 *
 * ON THE SIZE.  The step count is upstream's 64; `count' -- how many options
 * -- is the knob, because the work is count x (64 powf calls + 2016 inner
 * iterations) and the ISS has to finish.  The shape is upstream's.
 */
#include <math.h>

#include "kernels.h"

#ifndef BN_STEPS
#define BN_STEPS 64
#endif

#if SPECIALIZE
enum { BINOMIAL_NUM = BN_STEPS };
#else
static int BINOMIAL_NUM;
#endif

#if SPECIALIZE
void
binomial_put_serial (float *__restrict__ Sa, float *__restrict__ Xa,
                     float *__restrict__ Ta, float *__restrict__ ra,
                     float *__restrict__ va, float *__restrict__ result,
                     int count, int nsteps)
{
    (void)nsteps;
#else
void
binomial_put_serial (float Sa[], float Xa[], float Ta[],
                     float ra[], float va[],
                     float result[], int count, int nsteps)
{
    BINOMIAL_NUM = nsteps;
#endif
    float V[BINOMIAL_NUM];

    for (int i = 0; i < count; ++i) {
        float S = Sa[i], X = Xa[i];
        float T = Ta[i], r = ra[i];
        float v = va[i];

        float dt = T / BINOMIAL_NUM;
        float u = expf (v * sqrtf (dt));
        float d = 1.f / u;
        float disc = expf (r * dt);
        float Pu = (disc - d) / (u - d);

        for (int j = 0; j < BINOMIAL_NUM; ++j) {
            float upow = powf (u, (float)(2 * j - BINOMIAL_NUM));
            /* std::max(0.f, e) upstream, which is `0 < e ? e : 0' and not
             * fmaxf: the two differ on a NaN, and the translation should not
             * decide that quietly. */
            float e = X - S * upow;
            V[j] = 0.f < e ? e : 0.f;
        }

        for (int j = BINOMIAL_NUM - 1; j >= 0; --j)
            for (int k = 0; k < j; ++k)
                V[k] = ((1 - Pu) * V[k] + Pu * V[k + 1]) / disc;

        result[i] = V[0];
    }
}

int bn_steps (void) { return BN_STEPS; }

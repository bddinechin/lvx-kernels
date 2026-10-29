/* Black-Scholes: a European call, by the closed form.
 *
 * A C translation of
 *   autovec-benchmark/src/microkernels/black-scholes/black-scholes_serial.cpp
 * (https://github.com/sergisiso/autovec-benchmark), itself modified from the
 * ISPC examples.  BSD-3-Clause, three stacked notices; see LICENSE.txt.  The
 * only C++ it drops is an unused <algorithm> include; the arithmetic, the
 * polynomial and the branch are upstream's unchanged.
 *
 * WHY THIS KERNEL.  Two things, and the first is why it was ported.
 *
 * The loop body calls `logf', `expf' and `sqrtf' on every option, and the `i'
 * loop carries no dependence at all -- it is embarrassingly parallel.  So
 * whether it vectorizes comes down to one question: can the compiler
 * vectorize a call to the maths library?  GCC can only do that when a vector
 * version is declared to it, and nothing declares one here.  That makes this
 * the first benchmark in lvx-perf that points at lvx-sleef, which exists and
 * which no measurement has so far had any reason to ask about.  If the loop
 * is blocked only by the calls, the gap between this row and a hypothetical
 * vector-libm row is what sleef is worth on LVX.
 *
 * `sqrtf' is the exception and the control: LVX has `fsqrtw' as an
 * instruction, so that one call can be inlined where `expf' and `logf'
 * cannot.  If anything here vectorizes, it should be the part that does not
 * call out.
 *
 * And CND carries a branch -- `if (X > 0.f) w = 1.f - w;' -- on a value that
 * varies per lane, so a vectorized version has to if-convert it into a
 * select.  That is the same `blend*' question mandelbrot_masked answered yes
 * to, in a kernel that did not have to be restructured to ask it.
 *
 * SPECIALIZE=1   the six arrays are restrict.
 * SPECIALIZE=0   they are not.
 * And that is the whole axis for this kernel: upstream withdraws no constants
 * here, only the aliasing.  Of the four ported so far, convolution carries
 * all three classes, binomial bounds and aliasing, mandelbrot bounds and
 * arithmetic, and this one aliasing alone -- which is worth knowing when
 * reading a row where SPEC moves nothing.
 */
#include <math.h>

#include "kernels.h"

/* Cumulative normal distribution, by Abramowitz-Stegun 26.2.17 as upstream
 * has it: a fifth-order polynomial in 1/(1 + 0.2316419 L), scaled by the
 * standard normal density.  Not a libm call, so the polynomial itself is
 * vectorizable; the `expf' inside it is not. */
static inline float
CND (float X)
{
    float L = fabsf (X);

    float k = 1.f / (1.f + 0.2316419f * L);
    float k2 = k * k;
    float k3 = k2 * k;
    float k4 = k2 * k2;
    float k5 = k3 * k2;

    const float invSqrt2Pi = 0.39894228040f;
    float w = (0.31938153f * k - 0.356563782f * k2 + 1.781477937f * k3 +
               -1.821255978f * k4 + 1.330274429f * k5);
    w *= invSqrt2Pi * expf (-L * L * .5f);

    if (X > 0.f)
        w = 1.f - w;
    return w;
}

#if SPECIALIZE
void
black_scholes_serial (float *__restrict__ Sa, float *__restrict__ Xa,
                      float *__restrict__ Ta, float *__restrict__ ra,
                      float *__restrict__ va, float *__restrict__ result,
                      int count)
{
#else
void
black_scholes_serial (float Sa[], float Xa[], float Ta[], float ra[],
                      float va[], float result[], int count)
{
#endif
    for (int i = 0; i < count; ++i) {
        float S = Sa[i], X = Xa[i];
        float T = Ta[i], r = ra[i];
        float v = va[i];

        float d1 = (logf (S / X) + (r + v * v * .5f) * T) / (v * sqrtf (T));
        float d2 = d1 - v * sqrtf (T);

        result[i] = S * CND (d1) - X * expf (-r * T) * CND (d2);
    }
}

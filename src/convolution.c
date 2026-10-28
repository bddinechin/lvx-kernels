/* 2-D convolution of an int image with a 3x3 mask.
 *
 * A faithful C translation of
 *   autovec-benchmark/src/microkernels/convolution/convolution_serial.cpp
 * (https://github.com/sergisiso/autovec-benchmark, BSD-3-Clause; see
 * LICENSE.txt).  The C++ original uses nothing but `constexpr' and a
 * `__restrict__' parameter, so the loop nest below is the upstream one
 * unchanged; the translation exists because the LVX target has no C++
 * standard library (see README.md).
 *
 * WHY THIS KERNEL.  Two reasons, and the second is the one that mattered.
 *
 * It is the only *integer* SIMD kernel of the eight upstream micro-kernels:
 * four 32-bit lanes, a multiply-accumulate over a sliding window, which on
 * lvx-2 is `mulxwq'/`maddx*' territory.  lvx-perf's other suites are
 * floating-point-heavy (PolyBench, TSVC) or hand-written SIMD over SIMDe
 * (lvx-ssw, lvx-turbopfor); none asks what the auto-vectorizer does with an
 * integer window reduction.
 *
 * And it carries SPECIALIZE, which is the whole point of the upstream suite
 * reduced to a single `-D'.  Compiled one way, the mask's *contents*, the
 * bounds and the non-aliasing are all compile-time facts; compiled the other,
 * none of them is.  That axis is what two separate findings in this project
 * arrived at the hard way -- PolyBench's mean 2.15x from `-DPOLYBENCH_USE_RESTRICT'
 * alone, and lvx-csw/docs/handoff-lvx-perf-tsvc.md wanting the three TSVC
 * loops whose distances are symbolic -- and here it is a build flag.
 *
 * SPECIALIZE=1   mask contents, m and n are constants; pointers are restrict.
 *                The compiler may strength-reduce the x1/x2/x3, delete the
 *                zero row entirely, and assume image and out do not overlap.
 * SPECIALIZE=0   mask, m and n arrive at run time and nothing is restrict.
 *                The multiply must happen, the trip counts are unknown and
 *                the stores may alias the loads.
 *
 * ON THE SIZE.  Upstream specialises to n = 8192, which makes `out' 8189^2
 * ints -- 268 MB, and far past what an ISS run can carry.  Upstream's own
 * comment records that the original ranged 16 to 1024.  So N is a knob here,
 * defaulting to a size an ISS sweep finishes in: the shape is upstream's, the
 * extent is ours.  Keep it a power of two so the index arithmetic stays as
 * cheap as upstream intended.
 */
#include "kernels.h"

#ifndef CONV_N
#define CONV_N 128
#endif
#define CONV_M 3

#if SPECIALIZE
static const int conv_mask[CONV_M * CONV_M] = { 1, 2, 3, 0, 0, 0, 3, 2, 1 };
enum { m = CONV_M, n = CONV_N };

void
convolve(int *__restrict__ image, int *__restrict__ out,
         int p_n, int p_m, const int *p_mask)
{
    (void)p_n; (void)p_m; (void)p_mask;
#else
static const int *conv_mask;
static int m, n;

void
convolve(int *image, int *out, int p_n, int p_m, const int *p_mask)
{
    n = p_n;
    m = p_m;
    conv_mask = p_mask;
#endif

    for (int i = 0; i < n - m; ++i)
        for (int j = 0; j < n - m; ++j)
            for (int k = 0; k < m; ++k)
                for (int l = 0; l < m; ++l)
                    out[i * (n - m) + j] +=
                        image[(i + k) * n + j + l] * conv_mask[k * m + l];
}

int conv_n(void) { return CONV_N; }
int conv_m(void) { return CONV_M; }

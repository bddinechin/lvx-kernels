/* Mandelbrot, restructured so the vectorizable loop is the innermost one.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * NOT upstream. src/mandelbrot.c is the faithful translation of
 * autovec-benchmark's kernel, and it establishes that GCC declines to
 * vectorize that nest at every level: the `i' loop is an "unsupported
 * outerloop form" because the iteration loop sits inside it, a loop reports
 * "multiple nested loops", and the innermost declines with "unsupported use
 * in stmt" on the early-exit line. So the faithful kernel never reaches the
 * question it was ported for -- whether GCC generates lvx-2's lane-mask
 * family for a divergent loop -- because it declines before masking is the
 * issue.
 *
 * This is that question asked properly, and it is the shape lvx-ssw and
 * lvx-turbopfor already use: several rows over one problem, so that the rows
 * are the measurement. Here the rows are "as a compiler meets it" and "as a
 * compiler could use it".
 *
 * WHAT CHANGED, and nothing else did. The pixel loop is strip-mined by VLEN
 * and the iteration loop hoisted outside it, so the innermost loop is a
 * fixed-length pass over lanes with no control flow:
 *
 *     for (k = 0; k < maxIterations; ++k)
 *         for (l = 0; l < VLEN; ++l) {
 *             int act = zr[l]*zr[l] + zi[l]*zi[l] <= 4.f;   <- compare to mask
 *             itc[l] += act;                                <- masked add
 *             zr[l] = act ? cx[l] + nr : zr[l];             <- select/blend
 *             zi[l] = act ? cy[l] + ni : zi[l];
 *         }
 *
 * which is what the ISA has `comp*' and `blend*' for. The arithmetic is
 * upstream's unchanged.
 *
 * WHY THE COUNT STAYS RIGHT WITHOUT AN ACTIVE-LANE ACCUMULATOR. Once a lane's
 * magnitude passes 4 its z is frozen by the selects, so its magnitude stays
 * past 4 and `act' stays 0 for every later iteration. The mask is therefore
 * monotone without being remembered, and `itc[l]' counts exactly the
 * iterations whose top-of-loop test passed -- which is what the scalar `it'
 * holds when it breaks. The checksum is asserted equal to the scalar
 * kernel's, which is the real proof; this paragraph is why it is expected to
 * hold rather than a hope.
 *
 * Freezing z also means no lane runs away: the scalar version avoids overflow
 * by breaking, and this one by not updating.
 *
 * THE WORK AMPLIFICATION, which is the honest cost of vectorizing a divergent
 * loop. The scalar kernel leaves a pixel the moment it escapes; this one runs
 * every lane of a block until the block is done, so a block holding one slow
 * pixel pays maxIterations for all VLEN. MB_BLOCKEXIT=1 (the default) recovers
 * most of that by leaving the k loop once no lane in the block is still
 * active -- block granularity instead of pixel granularity, which is what a
 * vector machine can actually do. Set it to 0 to measure the amplification on
 * its own: the difference between the two is what the block-level exit is
 * worth.
 *
 * SPECIALIZE and the size knobs behave as in src/mandelbrot.c. VLEN defaults
 * to 4, one 128-bit vector of floats; `width' must be a multiple of it, and
 * the driver refuses rather than quietly skipping a tail.
 */
#include "kernels.h"

#ifndef MB_WIDTH
#define MB_WIDTH 64
#endif
#ifndef MB_HEIGHT
#define MB_HEIGHT 64
#endif
#ifndef MB_MAXITER
#define MB_MAXITER 64
#endif
#ifndef MB_VLEN
#define MB_VLEN 4
#endif
#ifndef MB_BLOCKEXIT
#define MB_BLOCKEXIT 1
#endif

#if SPECIALIZE
static const float mx0 = -2.f;
static const float myy0 = -1.f;
static const float mx1 = 1.f;
static const float myy1 = 1.f;
enum { mwidth = MB_WIDTH, mheight = MB_HEIGHT, mmaxIterations = MB_MAXITER };
#else
static float mx0;
static float myy0;
static float mx1;
static float myy1;
static int mwidth;
static int mheight;
static int mmaxIterations;
#endif

void
mandelbrot_masked (float x0p, float y0p, float x1p, float y1p,
                   int widthp, int heightp, int maxIterationsp,
                   int output[])
{
#if SPECIALIZE
    (void)x0p; (void)y0p; (void)x1p; (void)y1p;
    (void)widthp; (void)heightp; (void)maxIterationsp;
#else
    mx0 = x0p;
    myy0 = y0p;
    mx1 = x1p;
    myy1 = y1p;
    mwidth = widthp;
    mheight = heightp;
    mmaxIterations = maxIterationsp;
#endif

    const float dx = (mx1 - mx0) / mwidth;
    const float dy = (myy1 - myy0) / mheight;

    for (int j = 0; j < mheight; ++j) {
        const float y = myy0 + j * dy;

        for (int i0 = 0; i0 < mwidth; i0 += MB_VLEN) {
            float cx[MB_VLEN], cy[MB_VLEN], zr[MB_VLEN], zi[MB_VLEN];
            int itc[MB_VLEN];

            for (int l = 0; l < MB_VLEN; ++l) {
                cx[l] = mx0 + (i0 + l) * dx;
                cy[l] = y;
                zr[l] = cx[l];
                zi[l] = cy[l];
                itc[l] = 0;
            }

            for (int k = 0; k < mmaxIterations; ++k) {
#if MB_BLOCKEXIT
                /* Is any lane still inside? A separate pass, so the lane loop
                 * below keeps no control flow of its own. */
                int any = 0;
                for (int l = 0; l < MB_VLEN; ++l)
                    any |= (zr[l] * zr[l] + zi[l] * zi[l] <= 4.f);
                if (!any)
                    break;
#endif
                for (int l = 0; l < MB_VLEN; ++l) {
                    const float mag = zr[l] * zr[l] + zi[l] * zi[l];
                    const int act = mag <= 4.f;
                    const float nr = zr[l] * zr[l] - zi[l] * zi[l];
                    const float ni = 2.f * zr[l] * zi[l];
                    itc[l] += act;
                    zr[l] = act ? cx[l] + nr : zr[l];
                    zi[l] = act ? cy[l] + ni : zi[l];
                }
            }

            for (int l = 0; l < MB_VLEN; ++l)
                output[j * mwidth + i0 + l] = itc[l];
        }
    }
}

/* The size accessors too, not only the row's own knobs: the driver asks the
 * kernel what it was compiled for, and exactly one kernel is linked into any
 * build, so each defines its own. */
int mb_width (void) { return MB_WIDTH; }
int mb_height (void) { return MB_HEIGHT; }
int mb_maxiter (void) { return MB_MAXITER; }

int mbm_vlen (void) { return MB_VLEN; }
int mbm_blockexit (void) { return MB_BLOCKEXIT; }

/* Mandelbrot set: iteration count per pixel.
 *
 * A faithful C translation of
 *   autovec-benchmark/src/microkernels/mandelbrot/mandelbrot_serial.cpp
 * (https://github.com/sergisiso/autovec-benchmark), itself modified from the
 * ISPC examples.  BSD-3-Clause, three stacked notices; see LICENSE.txt.  The
 * loop nest below, the `#pragma omp simd' and its placement are upstream's
 * unchanged; the C++ it drops is `constexpr' and two commented-out `std::cout'
 * lines, and the translation exists because the LVX target has no C++
 * standard library (README.md).
 *
 * WHY THIS KERNEL.  The innermost loop exits on a data-dependent condition:
 *
 *     for (it = 0; it < maxIterations; ++it) {
 *         if (z_re * z_re + z_im * z_im > 4.f) break;
 *
 * Vectorized over `i', the lanes of one vector leave at different iterations,
 * so the loop has to run until the last of them does with the departed lanes
 * masked off -- which on lvx-2 is `comp*' to a lane mask, `blend*' to keep the
 * surviving lanes, and the whole MASKM family.  lvx-perf exercises that family
 * hard in lvx-ssw, but only through hand-written SIMDe intrinsics: nothing
 * asks whether GCC's own vectorizer will generate it unaided.  This kernel
 * asks exactly that, and it is the reason it was ported second.
 *
 * THE PRAGMA IS INERT UPSTREAM, which is worth knowing before reading any
 * result.  GCC ignores `#pragma omp simd' without -fopenmp-simd, and
 * autovec-benchmark's common.mk passes no OpenMP flag to any compiler -- so
 * upstream's gcc builds measure the nest unaided and the pragma is decoration.
 * Here it is a knob (OMPSIMD=1), default off to stay faithful, because
 * turning it on is not neutral for LVX: an honoured `omp simd' sets
 * loop->safelen, which since lvx-gcc 1e89883136c survives into late RTL and
 * reaches the CMS dependence graph as a cross-iteration arc at that omega --
 * the machinery lvx-csw/docs/handoff-lvx-perf-tsvc.md records TSVC as unable
 * to reach, since TSVC carries no such pragmas.
 *
 * And note where upstream put it: on the *outer* `j' loop, with `i' nested
 * inside.  GCC does not vectorize outer loops, so even honoured it is unlikely
 * to do what it looks like it does.  Measure, do not assume.
 *
 * SPECIALIZE=1   the viewport, the dimensions and maxIterations are constants.
 * SPECIALIZE=0   all seven arrive at run time.
 * Neither side marks `output' restrict -- upstream does not, so the aliasing
 * class of the axis is not exercised by this kernel.  convolution carries it.
 *
 * ON THE SIZE.  Upstream specialises to 768 x 512 at 256 iterations: up to
 * 100 million inner iterations, which no ISS run finishes.  The dimensions are
 * knobs here, defaulting to a sweep that completes; the shape is upstream's.
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

/* Upstream spells the y bounds `yy0'/`yy1' rather than `y0'/`y1', and so does
 * this: `y0' and `y1' are Bessel functions in <math.h> and a file-scope
 * definition of one is a quiet collision. */
#if SPECIALIZE
static const float x0 = -2.f;
static const float yy0 = -1.f;
static const float x1 = 1.f;
static const float yy1 = 1.f;
enum { width = MB_WIDTH, height = MB_HEIGHT, maxIterations = MB_MAXITER };
#else
static float x0;
static float yy0;
static float x1;
static float yy1;
static int width;
static int height;
static int maxIterations;
#endif

void
mandelbrot_serial (float x0p, float y0p, float x1p, float y1p,
                   int widthp, int heightp, int maxIterationsp,
                   int output[])
{
#if SPECIALIZE
    (void)x0p; (void)y0p; (void)x1p; (void)y1p;
    (void)widthp; (void)heightp; (void)maxIterationsp;
#else
    x0 = x0p;
    yy0 = y0p;
    x1 = x1p;
    yy1 = y1p;
    width = widthp;
    height = heightp;
    maxIterations = maxIterationsp;
#endif

    float dx = (x1 - x0) / width;
    float dy = (yy1 - yy0) / height;

#if MB_OMPSIMD
    #pragma omp simd
#endif
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; ++i) {
            float x = x0 + i * dx;
            float y = yy0 + j * dy;

            int index = (j * width + i);
            float z_re = x, z_im = y;
            int it;
            for (it = 0; it < maxIterations; ++it) {
                if (z_re * z_re + z_im * z_im > 4.f) break;
                float new_re = z_re*z_re - z_im*z_im;
                float new_im = 2.f * z_re * z_im;
                z_re = x + new_re;
                z_im = y + new_im;
            }
            output[index] = it;
        }
    }
}

int mb_width (void) { return MB_WIDTH; }
int mb_height (void) { return MB_HEIGHT; }
int mb_maxiter (void) { return MB_MAXITER; }

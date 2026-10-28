/* Driver for the mandelbrot kernel.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Separate from src/mandelbrot.c for the same reason as convolution's: the
 * kernel file stays comparable to upstream line by line, and the timing,
 * checksum and printing are ours. Upstream's own driver is C++ with
 * <iostream> and cannot build for this target.
 *
 * The viewport is upstream's -- x in [-2, 1], y in [-1, 1] -- and that matters
 * for what the kernel measures rather than being arbitrary: it is the window
 * in which roughly a quarter of the pixels lie inside the set and run to
 * maxIterations while the rest leave early, which is the lane divergence the
 * kernel exists to exercise. A window entirely inside or entirely outside the
 * set would run every lane to the same depth and measure nothing.
 *
 * The checksum is the sum of the per-pixel iteration counts, and the escape
 * histogram is printed beside it: `escaped_early' counts the pixels that left
 * before maxIterations. A build whose masking is wrong tends to get the sum
 * nearly right and the histogram wrong, so the two together say more than
 * either.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernels.h"

static uint64_t
cycles (void)
{
#ifdef __lvx__
    uint64_t c;
    __asm__ __volatile__ ("get %0 = $frcc\n\t;;" : "=r"(c) : : "memory");
    return c;
#else
    /* The native build is the correctness oracle and nothing else; a host
     * cycle count is not comparable with $frcc. */
    return 0;
#endif
}

int
main (void)
{
    const int w = mb_width ();
    const int h = mb_height ();
    const int maxit = mb_maxiter ();

    int *out = malloc ((size_t)w * h * sizeof *out);
    if (!out) {
        printf ("status alloc_failed\n");
        return 1;
    }
    for (int i = 0; i < w * h; ++i)
        out[i] = -1;

    const uint64_t t0 = cycles ();
    mandelbrot_serial (-2.f, -1.f, 1.f, 1.f, w, h, maxit, out);
    const uint64_t t1 = cycles ();

    uint64_t sum = 0;
    int early = 0, saturated = 0, untouched = 0;
    for (int i = 0; i < w * h; ++i) {
        if (out[i] < 0) { untouched++; continue; }
        sum += (uint64_t)out[i];
        if (out[i] >= maxit) saturated++; else early++;
    }

    printf ("kernel mandelbrot\n");
    printf ("width %d\n", w);
    printf ("height %d\n", h);
    printf ("maxiter %d\n", maxit);
    printf ("elapsed %llu\n", (unsigned long long)(t1 - t0));
    printf ("checksum %llu\n", (unsigned long long)sum);
    printf ("escaped_early %d\n", early);
    printf ("saturated %d\n", saturated);
    /* Pre-filled with -1, so any pixel the kernel never wrote is visible
     * rather than reading as iteration zero. */
    printf ("untouched %d\n", untouched);
    printf ("status %s\n", untouched == 0 ? "ok" : "untouched_pixels");

    free (out);
    return untouched == 0 ? 0 : 1;
}

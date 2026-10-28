/* Driver for the convolution kernel: set the image up, time the kernel, print
 * a checksum.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Separate from src/convolution.c on purpose. The kernel file is a faithful
 * translation of upstream and should stay comparable to it line by line; the
 * timing, the checksum and the printing are ours and belong outside it.
 * Upstream's own driver is C++ with <iostream> and cannot build for this
 * target at all.
 *
 * Cycles come from $frcc, the free-running cycle counter, read directly --
 * one timed region, so there is no need for lvx-tsvc2's trick of overriding
 * gettimeofday (its 151 loops bracket their work 302 times, which is why that
 * file exists). On the ISS $frcc is curCycle(), so under LVX_CPU=minor these
 * are modelled cycles and under `atomic' they are a bundle count -- which is
 * why the harness refuses to report a cycle number from the functional model.
 *
 * The output is key/value lines, one per metric, which is what
 * harness/run.sh parses -- the same shape lvx-tsvc2's run-tsvc.sh emits.
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
    /* The native build is the correctness oracle and nothing else: its job is
     * to produce the checksum the target must match. A host cycle count would
     * not be comparable with $frcc and pretending otherwise is how a reference
     * ends up expressed in the wrong unit, so it reports no time at all. */
    return 0;
#endif
}

/* The mask upstream specialises to. Passed in on the SPECIALIZE=0 side and
 * ignored on the other, where the kernel holds its own copy -- so both sides
 * convolve with the same numbers and the checksums are comparable. */
static const int mask[9] = { 1, 2, 3, 0, 0, 0, 3, 2, 1 };

int
main (void)
{
    const int n = conv_n ();
    const int m = conv_m ();
    const int w = n - m;                 /* the output is w x w */

    int *image = malloc ((size_t)n * n * sizeof *image);
    int *out = malloc ((size_t)w * w * sizeof *out);
    if (!image || !out) {
        printf ("status alloc_failed\n");
        return 1;
    }

    /* A deterministic image with no zeros and no run of equal values, so a
     * wrong mask, a dropped row or an off-by-one in the window all change the
     * checksum. A constant image would hide every one of them. */
    for (int i = 0; i < n * n; ++i)
        image[i] = (i * 7 + 3) & 0xff;
    for (int i = 0; i < w * w; ++i)
        out[i] = 0;

    const uint64_t t0 = cycles ();
    convolve (image, out, n, m, mask);
    const uint64_t t1 = cycles ();

    /* Sum in 64 bits: w*w up to ~8k^2 terms of up to 255*15 does not fit in
     * 32, and a checksum that silently wraps is a checksum that agrees with
     * the wrong answer. */
    uint64_t sum = 0;
    for (int i = 0; i < w * w; ++i)
        sum += (uint64_t)(uint32_t)out[i];

    printf ("kernel convolution\n");
    printf ("n %d\n", n);
    printf ("m %d\n", m);
    printf ("elapsed %llu\n", (unsigned long long)(t1 - t0));
    printf ("checksum %llu\n", (unsigned long long)sum);
    printf ("status ok\n");

    free (image);
    free (out);
    return 0;
}

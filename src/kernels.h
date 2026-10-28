/* Shared declarations for the lvx-kernels suite.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef LVX_KERNELS_H
#define LVX_KERNELS_H

/* convolution: 2-D convolution of an int image with an m x m mask.
 *
 * Under SPECIALIZE the last three arguments are ignored -- the kernel has its
 * own constants -- and are passed anyway so that one driver builds against
 * both halves of the axis without an #if of its own. */
void convolve(int *image, int *out, int p_n, int p_m, const int *p_mask);

/* The size the kernel was compiled for, so the driver allocates and checks
 * against the same extent whichever side of SPECIALIZE it is on. */
int conv_n(void);
int conv_m(void);

/* mandelbrot: iteration count per pixel, with a data-dependent inner exit.
 *
 * As for convolution, the parameters are passed on both sides of SPECIALIZE
 * and ignored on the specialised one, so a single driver builds against
 * either. */
void mandelbrot_serial (float x0p, float y0p, float x1p, float y1p,
                        int widthp, int heightp, int maxIterationsp,
                        int output[]);

int mb_width (void);
int mb_height (void);
int mb_maxiter (void);

#endif /* LVX_KERNELS_H */

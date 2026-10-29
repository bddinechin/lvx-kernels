# lvx-kernels — standalone kernels as an LVX vectorization instrument

Upstream: https://github.com/sergisiso/autovec-benchmark (`src/microkernels/`),
BSD-3-Clause, three stacked notices — see `LICENSE.txt`. Published as Sergi
Siso, Wes Armour and Jeyarajan Thiyagalingam, *Evaluating Auto-Vectorizing
Compilers through Objective Withdrawal of Useful Information*, ACM TACO 16(4),
2019, [doi:10.1145/3356842](https://doi.org/10.1145/3356842).

**Status: `convolution`, `mandelbrot`, `mandelbrot_masked` and `binomial`.** The suite is laid out for the rest.

## Why a separate suite rather than more of `lvx-tsvc2`

`lvx-tsvc2` is a fork of UoB-HPC/TSVC_2 whose `main` tracks upstream. These
kernels come from a *third* upstream with its own lineage and three stacked
licences, so putting them inside that fork would corrupt the provenance model
`lvx-tsvc2`'s README states and make future TSVC_2 merges harder — and a suite
named `lvx-tsvc2` containing Mandelbrot and Black-Scholes would mislead the
next reader. The harnesses barely overlap either: `lvx-tsvc2` drives one binary
holding 141 loops with its own checksums and a CMS probe over `tsvc.c`, where
these are separate programs with their own drivers. What is shared is the
`--csw` chain and the TSV shape, both small enough to copy.

## What it measures, and the axes

Two axes, and each is a *pair*, because a pair is a measurement where a single
value is only a setting.

| axis | values | the pair measures |
|---|---|---|
| `VEC` | `vec` / `novec` | did the vectorizer fire on this shape, and did firing help |
| `SPEC` | `1` / `0` | **what upstream contributes**: what the information is worth |

`SPEC` is upstream's idea reduced to one `-D`. With `-DSPECIALIZE=1` the mask's
*contents*, the loop bounds and the non-aliasing are compile-time facts; with
`-DSPECIALIZE=0` none of them is. That axis is what this project has twice
arrived at by accident — PolyBench's mean 2.15x from `-DPOLYBENCH_USE_RESTRICT`
alone, and `lvx-csw/docs/handoff-lvx-perf-tsvc.md` wanting the three TSVC loops
whose dependence distances are symbolic — and here it is a build flag over a
whole kernel.

```bash
./harness/run.sh                            # convolution, both axes, lvx-2 -O2
./harness/run.sh --csw /path/to/lvx-csw
ARCH=lvx-1 ./harness/run.sh                 # the scalar core
STATIC_ONLY=1 ./harness/run.sh              # build + static metrics, no ISS
SPEC=0 VEC=novec ./lvx/run-kernel.sh convolution    # one configuration
VECREPORT=1 ./lvx/run-kernel.sh convolution         # -fopt-info-vec-all
./lvx/run-kernel.sh --native convolution            # the correctness oracle
```

`harness/run.sh` writes `results/<kernel>-<arch>-<opt>-n<N>.tsv`: four rows,
`SPEC` x `VEC`, each with its cycles, checksum, kernel-scoped static size and
the toolchain that produced it. The ratios are left to the reader — an
aggregate would average a kernel the vectorizer transformed with one it
refused and describe neither.

## Correctness is a gate, not a column

Every row's checksum must equal the native oracle's, built from the same two
source files. And the oracle is checked against *itself* across `SPEC` first:
withdrawing information must not change the answer, so if the two native
builds disagree the sweep stops before spending four ISS runs.

## What the first sweep found

`convolution`, lvx-2, `-O2`, N=128, `LVX_CPU=minor`:

| `SPEC` | `VEC` | kernel cycles | kernel bundles | SIMD in kernel |
|---|---|---:|---:|---:|
| 1 | vec | 2,039,070 | 20 | **0** |
| 1 | novec | 2,025,215 | 25 | **0** |
| 0 | vec | 1,816,534 | 31 | **0** |
| 0 | novec | 2,255,780 | 31 | **0** |

All four compute the oracle's checksum.

**The kernel does not vectorize on LVX, in any configuration.**
`-fopt-info-vec-all` says why, and the reasons are structural rather than
LVX's: the `i` and `j` loops are "not vectorized: multiple nested loops", the
`k` loop "complicated access pattern", and the innermost `l` loop — three
trips — "Loop costings not worthwhile". The two loops the report *does* call
vectorized are the driver's initialisation loops, outside the timed region.

**`SPEC` does what it says.** The specialised kernel is 20 bundles against 31:
it drops the mask's zero row entirely and strength-reduces the x1/x2/x3 into
shifts and adds, leaving a single `maddx`. What it does *not* do is win — it
executes fewer bundles (1.04M against 1.13M whole-program) at a worse
cycles-per-bundle (1.97 against 1.61), and the product comes out 12% slower.
A shorter dependence chain is what an in-order VLIW wants, and specialising
shortened the *work* while lengthening the *chain*. Worth pursuing; not yet a
finding, for two reasons below.

**Two caveats on those numbers, both real.** The bundle counts are
whole-program while the cycles are kernel-only, so cycles-per-bundle is an
estimate and not an attribution — counting the kernel's dynamic bundles wants
a region of interest the harness does not yet mark. And `lvx-gcc` was being
rebuilt and reinstalled in place *while this sweep ran*, with another session
editing `config/lvx/lvx.cc` between two of the four configurations. That is
why every row now records its toolchain, and why these four should be re-run
together before anything is concluded from them.

**One trap, recorded because it looked like a result.** Counting SIMD
mnemonics over the whole image read 67 for every vectorized build and 51 for
every unvectorized one, identical on both sides of `SPEC` — which looks like a
measurement and is libc. The kernel's own count is zero in all four. Hence
`simd_kernel` beside `simd_image`, and the entry symbol named per kernel in
`lvx/run-kernel.sh` rather than guessed from the file name. The same caution
`lvx-polybench` and `lvx-tsvc2` both record: use `-fopt-info-vec-all` to learn
whether a loop vectorized, never an instruction count.

## What mandelbrot found: GCC declines at every level

`mandelbrot`, lvx-2, `-O2`, 64x64 at 64 iterations, `LVX_CPU=minor`:

| `SPEC` | `VEC` | kernel cycles | kernel bundles | SIMD in kernel |
|---|---|---:|---:|---:|
| 1 | vec | 2,192,665 | 23 | **0** |
| 1 | novec | 2,192,665 | 23 | **0** |
| 0 | vec | 2,196,158 | 40 | **0** |
| 0 | novec | 2,196,158 | 40 | **0** |

All four compute the oracle's checksum (83,524, with 1,091 pixels saturated
and 3,005 escaping early -- the viewport really does diverge).

**`vec` and `novec` are identical, to the cycle.** The vectorizer does nothing
here, and `-fopt-info-vec-all` says why at each of the three levels: the `i`
loop is an "unsupported outerloop form" -- it has the `it` loop inside it --
one loop reports "multiple nested loops", and the innermost declines with
"unsupported use in stmt" on the early-exit line, `it` being live after the
loop. So every level refuses, for a different reason.

**Which means this kernel does not yet ask the question it was ported for.**
The hope was to find out whether GCC's vectorizer generates lvx-2's
`comp*`/`blend*`/MASKM family for a divergent loop unaided. It never gets as
far as needing a mask: it declines before masking is the issue. That is a real
answer, and a negative one about this *source shape* rather than about LVX.

**And the pragma is confirmed inert.** `OMPSIMD=0` and `OMPSIMD=1` produce
byte-identical code. Upstream puts `#pragma omp simd` on the *outer* `j` loop,
GCC does not vectorize outer loops, and upstream passes no OpenMP flag anyway
-- so it is decoration in all three senses. The knob stays because switching
it on sets `loop->safelen`, which reaches LVX's CMS dependence graph, and that
is worth a probe even when the vectorizer is unmoved.

**`SPEC` moves the code and not the clock.** 23 bundles against 40 -- 1.7x the
static size -- for a 0.16% difference in cycles. The hot loop is the `it` loop,
which does not depend on any of the withdrawn information; the constants only
reach the addressing around it. A cleaner demonstration than convolution's
that static size and dynamic cost are different questions.

**To ask the masking question** the loop has to be restructured so the
vectorizable loop is the innermost one: strip-mine `i` and run all lanes of
the `it` loop under an active mask, the way ISPC does it. That is a different
kernel rather than a flag, and it belongs beside this one as a second row --
the shape `lvx-ssw` uses for its three Smith-Waterman rows and
`lvx-turbopfor` for its three translation layers. Not written yet.

## What the masked row found: the ISA's mask family, generated unaided

`src/mandelbrot.c` establishes that GCC declines to vectorize upstream's nest
at every level, so it never reaches the masking question.
`src/mandelbrot_masked.c` asks it properly: the pixel loop strip-mined by
VLEN, the iteration loop hoisted outside it, so the innermost loop is a
fixed-length pass over lanes with no control flow. **Not upstream** — this is
the second row, the shape `lvx-ssw` uses for its three Smith-Waterman rows.

It produces the scalar kernel's checksum exactly (83,524, with the same 1,091
saturated and 3,005 early), which is the proof the masking is right.

**GCC vectorizes it, and reaches for the lane-mask family on its own:**

```
fcompwq.oge $r0 = $r16r17, $r0r1     ; packed FP compare to a lane mask
blendzwq    $r0r1 = $r32r33, $r0     ; zeroing blend by that mask
```

beside `ffmawq` x2, `fmulwq`, `addwq`, `splatwq` x2 and `floatwq` — 13 SIMD
instructions in the kernel against 1 unvectorized. `blendzwq` is worth
naming: `lvx-csw/CLAUDE.md` records it as added on 2026-10-12 because it "does
not read its destination, so it needs no all-zero operand to be materialised
first… it is what a masked lane-wise operation's select should be, and what
GCC's `cond_add` uses". Here it is, in auto-vectorized code, which is the
first evidence in `lvx-perf` that the addition pays.

lvx-2, `-O2`, 64x64 at 64 iterations, VLEN=4, `LVX_CPU=minor`:

| row | `SPEC` | `VEC` | cycles | bundles | SIMD |
|---|---|---|---:|---:|---:|
| faithful | 1 | vec | 2,192,665 | 23 | 0 |
| faithful | 1 | novec | 2,192,665 | 23 | 0 |
| masked | 1 | vec | **3,224,117** | 51 | **13** |
| masked | 1 | novec | 4,642,566 | 53 | 1 |
| masked, no block exit | 1 | vec | 7,299,460 | 41 | 7 |
| masked, no block exit | 1 | novec | 7,273,639 | 43 | 1 |

Three results, and the third is the one to take seriously.

**Vectorizing the masked row is worth 1.44x** (3.22M against 4.64M), the first
row in this suite where the vectorizer helps at all. `SPEC` barely moves it:
1.42x on the withheld side, the hot loop not depending on the withdrawn
information.

**Four lanes buy 1.44x, not 4x**, and the block-exit pair says where the rest
went: with the exit off, the same code costs 7.30M and vectorizing buys
nothing (0.996x). Running every lane of a block to the block's depth is a
2.26x work amplification, and the block-level exit recovers over half of it.
(Curiously GCC also vectorizes *less* without the exit — 7 SIMD instructions
instead of 13 — so removing the `any` reduction cost the lane loop something
too.)

**And the restructuring is a net loss against the plain scalar nest**:
3.22M against 2.19M, 1.47x slower even vectorized and even with the block
exit. The mechanism is the amplification: the scalar kernel leaves each pixel
the moment it escapes, averaging 20.4 iterations (83,524 / 4,096), while a
block of four adjacent pixels usually contains one that saturates at 64, so
most blocks run near 64. Roughly 3x the work against 4 lanes, less overhead,
lands slightly behind. That is a finding about this *shape* at this
`maxIterations`, not about the ISA — the mask family does what it should; what
cannot pay is 4-wide SIMD against a 3x amplification. Lower `maxIterations`,
or compaction rather than masking, would move it.

## What binomial found: the shrinking loop vectorizes, and `taild` does not fire

The backward sweep is the shape nothing else here has:

```c
for (j = BINOMIAL_NUM-1; j >= 0; --j)
    for (k = 0; k < j; ++k)
        V[k] = ((1 - Pu) * V[k] + Pu * V[k+1]) / disc;
```

The `k` loop reads `V[k+1]` and writes `V[k]`, so iteration *k* reads what
iteration *k+1* later overwrites — an **anti-dependence at distance 1**, not a
flow dependence, and therefore vectorizable with a load-before-store. Every
other recurrence in `lvx-perf` is a flow dependence. And the trip count
**shrinks**: 63, 62, … 1, 0.

lvx-2, `-O2`, 16 options at 64 steps, `LVX_CPU=minor`:

| `SPEC` | `VEC` | cycles | bundles | SIMD |
|---|---|---:|---:|---:|
| 1 | vec | **636,567** | 104 | 12 |
| 1 | novec | 1,178,644 | 55 | 0 |
| 0 | vec | 637,895 | 119 | 12 |
| 0 | novec | 1,182,772 | 70 | 0 |

**Vectorizing is worth 1.85x** — the best ratio in this suite, and it confirms
the anti-dependence is handled: `-fopt-info-vec-all` reports the `k` loop
"vectorized using 32 byte vectors and unroll factor 8", with an "epilogue loop
vectorized using 16 byte vectors". The SIMD is `splatwq` x6, `fmulwq` x3 and
`ffmawq` x3 — arithmetic; the compares that remain are scalar.

**But `taild` does not appear, and no mask does.** This is the loop whose trip
count is mostly tail, which is what `taild` and the partial-vector/`while_ult`
path exist for — and GCC reached for neither. It peeled a narrower epilogue
instead: 8 lanes in the main body, 4 in the epilogue, scalar for the rest.
That is the classic answer, not the masked one.

It costs less than it sounds, which is worth saying so the gap is not
overstated: with trip counts 63 down to 1, a vectorization factor of 8 covers
every `j >= 8`, and those hold **1,988 of the 2,016** inner iterations — 98.6%.
So epilogue peeling leaves only 1.4% of the work to narrower code here. The
finding is about *which* mechanism GCC chose, not about a large loss; a
`maxIterations` an order of magnitude smaller would move the balance and is
the configuration to try next (`BN_STEPS=8`).

**`SPEC` is worth 0.2%** (636,567 against 637,895) despite 104 bundles against
119 — the same pattern as mandelbrot, for the same reason: the hot loop does
not depend on what was withdrawn.

### Two defects in upstream's binomial, both deliberately not reproduced

Named because the port is otherwise faithful and because each is a defect
rather than a choice.

Upstream guards its unspecialised side with `#ifndef SPECIALIZE` where its
siblings use `#if SPECIALIZE`. Built with `-DSPECIALIZE=0` the macro *is*
defined, the guard is false, `BINOMIAL_NUM` keeps its zero initialiser, `V[]`
is zero-length and **every loop runs zero trips** — a benchmark that measures
nothing and still prints a result. It only works if `SPECIALIZE` is left
undefined. This port uses `#if SPECIALIZE` throughout.

And upstream's unspecialised side then assigns `BINOMIAL_NUM = 64` from a
literal in the same function, which GCC constant-propagates — so nothing about
the step count is actually withheld and the axis collapses to `__restrict__`
alone. Here the step count arrives as a parameter, as the siblings' withheld
constants do.

## The kernels are translated to C, and have to be

Upstream is C++11. The LVX target has **no C++ standard library** — `#include
<vector>` is "No such file or directory" — and the installed `lvx-mbr-g++` is
stale enough to be unusable: it rejects `-march=lvx-2`, and it emits `make $r0
= 4` where the assembler now requires `maked`, so it cannot assemble its own
output. (It comes from `lvx-gcc-build-cxx/`, which `lvx-csw`'s README keeps
deliberately separate.)

That costs little for the kernels and nothing for the drivers. The upstream
`X_serial.cpp` files use almost no C++ — `std::max`, `constexpr`, and
`<vector>`/`<iostream>` only in the drivers, which are replaced here anyway.
`ao` is the exception: it has a `vec` class with operator overloading and
`const vec &` parameters, so it is the one that needs real translation work.

## Sizes are ours; shapes are upstream's

Upstream specialises `convolution` to n = 8192, which makes the output
8189² ints — 268 MB, and far past an ISS run. Upstream's own comment records
that the original ranged 16 to 1024. So `N` is a knob (`N=128` by default,
powers of two, to keep the index arithmetic as cheap as upstream intended).
The loop nest is upstream's unchanged.

## What is next, in order

1. **black-scholes** — `logf`/`expf`/`sqrtf`: a vector-libm test, and the
   first benchmark in `lvx-perf` pointing at `lvx-sleef`. The branch inside
   `CND()` is an if-conversion test.
2. **matrixmult** — `double`, 32x32 fixed: unroll-and-jam of a small known
   matmul, distinct from PolyBench's large dense `gemm`.

**Skip `stencil`** — `lvx-polybench/stencils` already covers that shape.
`ao` and `lattice_boltzmann` are lowest priority.

## Wiring it into `lvx-perf`

Not done yet, and it needs three things beyond a remote:

- `bin/lvx-perf-import` carries a hard-coded suite list (`SUITES="polybench
  mibench ssw turbopfor tsvc2"` and a submodule list beside it); `kernels`
  goes in both.
- `lvx-perf/README.md` wants a row in "The suites" and a row in the Licensing
  table (BSD-3-Clause, three stacked notices).
- the submodule itself, once `git@github.com:bddinechin/lvx-kernels.git`
  exists.

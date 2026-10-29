#!/usr/bin/env bash
#
# Build and run one kernel in one configuration, printing key/value metrics.
# SPDX-License-Identifier: BSD-3-Clause
#
# Usage:
#   ./lvx/run-kernel.sh convolution              lvx-2 -O2, vectorized, SPECIALIZE=1
#   SPEC=0 ./lvx/run-kernel.sh convolution       the same kernel told nothing
#   VEC=novec ./lvx/run-kernel.sh convolution    the vectorizer off
#   ARCH=lvx-1 ./lvx/run-kernel.sh convolution   the scalar core
#   ./lvx/run-kernel.sh --native convolution     the host oracle, for checksums
#   STATIC_ONLY=1 ./lvx/run-kernel.sh convolution   build + static metrics only
#
# THE TWO AXES, and why each is a pair.
#
# VEC is what a vectorization suite exists to ask: the same source with
# -ftree-vectorize and with -fno-tree-vectorize, so the ratio says whether
# firing helped rather than whether it happened. (Use VECREPORT=1 to find out
# whether it happened; counting SIMD mnemonics conflates the kernel with
# everything else in the image, which is the guidance lvx-polybench and
# lvx-tsvc2 both record.)
#
# SPEC is upstream's contribution: -DSPECIALIZE=1 hands the compiler the mask's
# contents, the bounds and the non-aliasing; -DSPECIALIZE=0 withholds all
# three. The pair measures what the information is worth, which is the thing
# this project has twice measured by accident -- PolyBench's 2.15x from
# -DPOLYBENCH_USE_RESTRICT, and the three TSVC loops with symbolic distances.
#
# harness/run.sh sweeps both and divides.
set -u

HERE=$(cd "$(dirname "$0")/.." && pwd)

CSW_ARG=""
NATIVE=0
args=()
while [ $# -gt 0 ]; do
    case "$1" in
        --csw)    CSW_ARG=${2:?--csw needs a path}; shift 2 ;;
        --csw=*)  CSW_ARG=${1#--csw=}; shift ;;
        --native) NATIVE=1; shift ;;
        *)        args+=("$1"); shift ;;
    esac
done
KERNEL=${args[0]:-convolution}

ARCH=${ARCH:-lvx-2}
OPT=${OPT:-O2}
VEC=${VEC:-vec}
SPEC=${SPEC:-1}
N=${N:-128}

case "$VEC" in
    vec)   vecflag="-ftree-vectorize" ;;
    novec) vecflag="-fno-tree-vectorize" ;;
    *) echo "VEC must be vec or novec" >&2; exit 2 ;;
esac
case "$SPEC" in 0|1) ;; *) echo "SPEC must be 0 or 1" >&2; exit 2 ;; esac

src="$HERE/src/$KERNEL.c"
drv="$HERE/lvx/$KERNEL-main.c"
[ -f "$src" ] || { echo "error: no such kernel: $src" >&2; exit 2; }
[ -f "$drv" ] || { echo "error: no driver: $drv" >&2; exit 2; }

# Per-kernel size macros. The shapes are upstream's; the extents are ours,
# because upstream sizes for a native run (convolution specialises to an
# 8192-wide image, mandelbrot to 768x512 at 256 iterations -- 100 million inner
# iterations) and an ISS sweep has to finish.
case "$KERNEL" in
    convolution)
        DEFS="-DSPECIALIZE=$SPEC -DCONV_N=$N"
        ;;
    mandelbrot|mandelbrot_masked)
        DEFS="-DSPECIALIZE=$SPEC -DMB_WIDTH=${MB_WIDTH:-64}"
        DEFS="$DEFS -DMB_HEIGHT=${MB_HEIGHT:-64} -DMB_MAXITER=${MB_MAXITER:-64}"
        # `#pragma omp simd' needs -fopenmp-simd or GCC ignores it, and
        # upstream's own makefiles pass no OpenMP flag -- so the pragma is
        # decoration there. Off by default to stay faithful; on, it sets
        # loop->safelen, which reaches LVX's CMS dependence graph.
        DEFS="$DEFS -DMB_OMPSIMD=${OMPSIMD:-0}"
        [ "${OMPSIMD:-0}" = 1 ] && DEFS="$DEFS -fopenmp-simd"
        # The masked row's own knobs: the lane count, and whether the block
        # leaves the iteration loop once no lane of it is still inside.
        if [ "$KERNEL" = mandelbrot_masked ]; then
            DEFS="$DEFS -DMB_VLEN=${VLEN:-4} -DMB_BLOCKEXIT=${BLOCKEXIT:-1}"
        fi
        ;;
    binomial)
        # The step count is upstream's 64; `count' -- the number of options --
        # is the knob, the work being count x (64 powf calls + 2016 inner
        # iterations).
        DEFS="-DSPECIALIZE=$SPEC -DBN_STEPS=${BN_STEPS:-64}"
        DEFS="$DEFS -DBN_COUNT=${BN_COUNT:-16}"
        ;;
    black_scholes)
        # Only the option count is a knob: upstream withdraws no constants for
        # this kernel, so there is nothing else to parameterise.
        DEFS="-DSPECIALIZE=$SPEC -DBS_COUNT=${BS_COUNT:-64}"
        ;;
    *)
        DEFS="-DSPECIALIZE=$SPEC"
        ;;
esac

# The host oracle. Built from the same two files, so a checksum difference is
# the target's and not a different program's.
if [ "$NATIVE" = 1 ]; then
    out=$(mktemp -d)/native
    # shellcheck disable=SC2086
    cc -$OPT $vecflag $DEFS -I"$HERE/src" -w -o "$out" "$drv" "$src" -lm || exit 1
    exec "$out"
fi

# lvx-perf's bin/lvx-csw-path owns this chain; the fallbacks are for a
# standalone checkout that has no superproject.
resolve_csw() {
    [ -n "$CSW_ARG" ] && { printf '%s\n' "$CSW_ARG"; return 0; }
    [ -n "${LVX_CSW:-}" ] && { printf '%s\n' "$LVX_CSW"; return 0; }
    local super c
    super=$(git -C "$HERE" rev-parse --show-superproject-working-tree 2>/dev/null) || super=
    [ -n "$super" ] && [ -x "$super/bin/lvx-csw-path" ] && "$super/bin/lvx-csw-path" 2>/dev/null && return 0
    for c in ${super:+"$super/../lvx-csw"} "$HERE/../lvx-csw" "$HOME/lvx-csw"; do
        [ -d "$c/lvx-mds" ] || [ -d "$c/validation" ] || continue
        (cd "$c" && pwd); return 0
    done
    return 1
}

CSW=$(resolve_csw) || {
    echo "error: cannot find lvx-csw. It holds the toolchain and the ISS." >&2
    echo "       Pass --csw <path>, set \$LVX_CSW, or keep the checkouts as" >&2
    echo "       siblings; lvx-perf's bin/lvx-csw-path owns the chain." >&2
    exit 2; }
BIN=$CSW/lvx-toolchain/bin
LDSCRIPTS=$CSW/lvx-newlib/libgloss/lvx-mbr/linker_scripts
[ -x "$BIN/lvx-mbr-gcc" ] || { echo "error: no lvx-mbr-gcc in $BIN" >&2; exit 2; }

case "$ARCH" in
    lvx-1) GEM5BIN=gem5-lvx-1.opt ;;
    lvx-2) GEM5BIN=gem5-lvx-2.opt ;;
    *) echo "error: unknown ARCH='$ARCH' (lvx-1 or lvx-2)" >&2; exit 2 ;;
esac
[ "${STATIC_ONLY:-0}" = 1 ] || [ -x "$CSW/lvx-gem5/build/$GEM5BIN" ] || {
    echo "error: missing ISS for $ARCH: $CSW/lvx-gem5/build/$GEM5BIN" >&2
    exit 2; }

WD=$(mktemp -d); trap 'rm -rf "$WD"' EXIT

echo "== $KERNEL $ARCH -$OPT $VEC SPEC=$SPEC ${N:+N=$N} ${OMPSIMD:+OMPSIMD=$OMPSIMD} =="

# Which compiler produced this. Not decoration: lvx-gcc is rebuilt and
# reinstalled in place while measurements are being taken -- that happened
# during this suite's first sweep, with another session editing
# config/lvx/lvx.cc between two of the four configurations -- so a cycle
# number without a compiler beside it cannot be compared with anything. The
# same reasoning lvx-tsvc2's CMS probe applies, and the reason lvx-perf's
# snapshots record the toolchain revision.
echo "toolchain $("$BIN/lvx-mbr-gcc" --version | head -1 | sed 's/^lvx-mbr-gcc (GCC) //')"
echo "toolchain_mtime $(date -r "$BIN/lvx-mbr-gcc" +%Y-%m-%dT%H:%M:%S)"

vecreport=""
[ "${VECREPORT:-0}" = 1 ] && vecreport="-fopt-info-vec-all=$WD/vec.txt -fno-inline"

t0=$SECONDS
# shellcheck disable=SC2086
"$BIN/lvx-mbr-gcc" -$OPT -march="$ARCH" $vecflag $vecreport $DEFS -I"$HERE/src" -w \
    -T lvx-sim.ld -L"$LDSCRIPTS" \
    -o "$WD/$KERNEL.elf" "$drv" "$src" -lm \
    || { echo "BUILD FAIL"; exit 1; }
echo "compile_seconds $((SECONDS - t0))"

if [ "${VECREPORT:-0}" = 1 ] && [ -f "$WD/vec.txt" ]; then
    rep="$HERE/results/vecreport-$KERNEL-$ARCH-$OPT-$VEC-spec$SPEC.txt"
    mkdir -p "$HERE/results"; cp "$WD/vec.txt" "$rep"
    echo "vec_loops_vectorized $(grep -c 'loop vectorized' "$rep" || true)"
    echo "vec_report $rep"
fi

"$BIN/lvx-mbr-objdump" -d "$WD/$KERNEL.elf" > "$WD/dis.txt" 2>/dev/null
echo "text_bytes $("$BIN/lvx-mbr-size" -A "$WD/$KERNEL.elf" | awk '$1==".text"{print $2}')"
echo "insns_static $(grep -cE '^[[:space:]]+[0-9a-f]+:' "$WD/dis.txt")"
echo "bundles_static $(grep -c ';;' "$WD/dis.txt")"

# The whole image, and then the kernel alone. Both, because the first one
# lies: on this suite's first sweep `simd_insns' over the image read 67 for
# every vectorized build and 51 for every unvectorized one, identical on both
# sides of SPECIALIZE -- which looked like a measurement and was libc. The
# kernel's own count was zero in all four. That is the trap lvx-polybench and
# lvx-tsvc2 both warn about, and the cure is to look at the function.
#
# The entry symbol per kernel; one line each as kernels are added, because
# deriving it from the file name would be a guess.
case "$KERNEL" in
    convolution) KERNELFN=convolve ;;
    mandelbrot)        KERNELFN=mandelbrot_serial ;;
    mandelbrot_masked) KERNELFN=mandelbrot_masked ;;
    binomial)          KERNELFN=binomial_put_serial ;;
    black_scholes)     KERNELFN=black_scholes_serial ;;
    *)           KERNELFN=$KERNEL ;;
esac
awk -v fn="$KERNELFN" '
    $0 ~ ("^[0-9a-f]+ <" fn ">:") { f = 1; next }
    /^[0-9a-f]+ </                { f = 0 }
    f' "$WD/dis.txt" > "$WD/fn.txt"
echo "kernel_symbol $KERNELFN"
echo "simd_image $(grep -coE '\b[a-z]+(dp|wq|ho|bx)\b' "$WD/dis.txt" || true)"
echo "insns_kernel $(grep -cE '^[[:space:]]+[0-9a-f]+:' "$WD/fn.txt")"
echo "bundles_kernel $(grep -c ';;' "$WD/fn.txt")"
echo "simd_kernel $(grep -coE '\b[a-z]+(dp|wq|ho|bx)\b' "$WD/fn.txt" || true)"

[ "${STATIC_ONLY:-0}" = 1 ] && exit 0

# `atomic' is gem5's default and is FUNCTIONAL: one cycle per bundle, so its
# $frcc is a bundle count and not a time. minor is the only model whose cycles
# mean anything, which is why it is the default here and why harness/run.sh
# records which model produced a row.
LVX_CPU=${LVX_CPU:-minor}
export LVX_CPU
echo "cpu_model $LVX_CPU"
"$CSW/lvx-gem5/build/$GEM5BIN" --outdir="$WD/m5out" \
    "$CSW/lvx-gem5/tests/lvx/run_lvx.py" "$WD/$KERNEL.elf" 2>&1 \
    | sed -n 's/^\(kernel\|n\|m\|width\|height\|maxiter\|vlen\|blockexit\|count\|steps\|unwritten\|elapsed\|checksum\|escaped_early\|saturated\|untouched\|status\) /\1 /p'

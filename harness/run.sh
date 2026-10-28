#!/usr/bin/env bash
#
# Sweep a kernel over both axes and write one TSV row per configuration.
# SPDX-License-Identifier: BSD-3-Clause
#
# Usage:
#   ./harness/run.sh                      every kernel, both axes, lvx-2 -O2
#   ./harness/run.sh convolution          one kernel
#   ./harness/run.sh --csw /path/to/lvx-csw
#   ARCH=lvx-1 ./harness/run.sh           the scalar core
#   STATIC_ONLY=1 ./harness/run.sh        build and static metrics, no ISS
#
# Writes results/<kernel>-<arch>-<opt>-n<N>.tsv: four rows, SPEC x VEC, with
# the cycles, the checksum, the static size and the toolchain that produced
# them. The ratios are left to the reader and to bin/lvx-perf-trend rather
# than computed here, for the reason lvx-tsvc2 records: a single aggregate
# averages a loop the vectorizer transformed with one it refused and describes
# neither. Four rows side by side are two ratios and a correctness check.
#
# THE CORRECTNESS GATE IS NOT OPTIONAL. Every row's checksum must equal the
# native oracle's, which is built from the same two source files. A
# performance number from a program computing the wrong answer is worse than
# no number, and withdrawing information must not change the answer -- so the
# oracle checks the SPEC axis as much as the target does.
set -u

HERE=$(cd "$(dirname "$0")/.." && pwd)
ARCH=${ARCH:-lvx-2}
OPT=${OPT:-O2}
N=${N:-128}

csw_args=()
kernels=()
while [ $# -gt 0 ]; do
    case "$1" in
        --csw)   csw_args+=(--csw "${2:?--csw needs a path}"); shift 2 ;;
        --csw=*) csw_args+=("$1"); shift ;;
        *)       kernels+=("$1"); shift ;;
    esac
done
[ ${#kernels[@]} -gt 0 ] || kernels=(convolution)

mkdir -p "$HERE/results"
rc=0

for kernel in "${kernels[@]}"; do
    # The oracle first: if the host and the target disagree there is nothing
    # to measure, and finding that out after four ISS runs wastes them.
    ref=$(SPEC=1 "$HERE/lvx/run-kernel.sh" --native "$kernel" \
          | sed -n 's/^checksum \([0-9]*\)/\1/p')
    ref0=$(SPEC=0 "$HERE/lvx/run-kernel.sh" --native "$kernel" \
           | sed -n 's/^checksum \([0-9]*\)/\1/p')
    if [ -z "$ref" ] || [ "$ref" != "$ref0" ]; then
        echo "$kernel: the native oracle disagrees with itself across SPECIALIZE" >&2
        echo "  SPEC=1 -> ${ref:-<none>}   SPEC=0 -> ${ref0:-<none>}" >&2
        rc=1; continue
    fi
    echo "$kernel: oracle checksum $ref"

    # The size tag names the extent that was actually swept, which differs
    # per kernel -- a file called `-n128' for mandelbrot would describe
    # nothing it was run at.
    case "$kernel" in
        convolution) tag="n$N" ;;
        mandelbrot)  tag="${MB_WIDTH:-64}x${MB_HEIGHT:-64}i${MB_MAXITER:-64}" ;;
        *)           tag="default" ;;
    esac
    tsv="$HERE/results/$kernel-$ARCH-$OPT-$tag.tsv"
    printf 'kernel\tarch\topt\tsize\tspec\tvec\tcycles\tchecksum\tcorrect\tbundles_kernel\tinsns_kernel\tsimd_kernel\tcpu_model\ttoolchain\n' > "$tsv"

    for spec in 1 0; do
        for vec in vec novec; do
            out=$(SPEC=$spec VEC=$vec N=$N ARCH=$ARCH OPT=$OPT \
                  "$HERE/lvx/run-kernel.sh" "${csw_args[@]+"${csw_args[@]}"}" "$kernel" 2>&1) || {
                echo "$kernel spec=$spec vec=$vec: run failed" >&2; rc=1; continue; }
            get() { printf '%s' "$out" | sed -n "s/^$1 \\(.*\\)/\\1/p" | head -1; }
            cyc=$(get elapsed); sum=$(get checksum)
            corr=no; [ -n "$sum" ] && [ "$sum" = "$ref" ] && corr=yes
            [ "$corr" = yes ] || { [ "${STATIC_ONLY:-0}" = 1 ] && corr=n/a; }
            [ "$corr" = no ] && rc=1
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$kernel" "$ARCH" "$OPT" "$tag" "$spec" "$vec" \
                "${cyc:--}" "${sum:--}" "$corr" \
                "$(get bundles_kernel)" "$(get insns_kernel)" \
                "$(get simd_kernel)" \
                "$(get cpu_model)" "$(get toolchain)" >> "$tsv"
        done
    done
    echo "  -> $tsv"
    column -t -s"$(printf '\t')" "$tsv" 2>/dev/null || cat "$tsv"
done

[ $rc -eq 0 ] || echo "SOME ROWS ARE NOT CORRECT -- see the correct column" >&2
exit $rc

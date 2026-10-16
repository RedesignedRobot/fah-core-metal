#!/bin/sh
# build.sh [-j JOBS] [OUT]
#
# Fetches OpenMM with the Metal platform and df64 mixed precision at one pinned commit, builds it as static
# libraries (Reference, CPU, CPU PME and Metal), and links the core against them. OUT (default ./build) must
# not exist. It ends up holding:
#   OUT/src         the OpenMM checkout, with patches/*.patch applied
#   OUT/build       the CMake build tree
#   OUT/prefix      lib/libOpenMM_static.a, lib/plugins/lib*_static.a, include/
#   OUT/core        minicore, one arm64 executable with OpenMM inside
#   OUT/BUILD-INFO  commit, patches, toolchain, sizes and timings
#
# Needs an Apple silicon Mac with Xcode or the Command Line Tools (macOS 15 SDK or later), git, CMake 3.17
# or later, and Ninja or make. No Python, SWIG or Doxygen.
set -eu

repo=https://github.com/RedesignedRobot/openmm.git
# Branch metal-mixed-master: OpenMM master d3e32b4da (the Metal platform, openmm#5488) plus df64 mixed precision.
commit=90fa9544cd38aa866e47ed415c9ef1b6dd8c5aca
jobs=6
while getopts j: opt; do
    case $opt in
    j) jobs=$OPTARG ;;
    *) sed -n '2,15p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND-1))
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/build}
[ -e "$out" ] && { echo "$out exists; pick a new directory" >&2; exit 2; }
[ "$(uname -s)/$(uname -m)" = Darwin/arm64 ] || { echo "build on an Apple silicon Mac" >&2; exit 2; }
for tool in git cmake xcrun; do
    command -v $tool > /dev/null || { echo "$tool is not on PATH" >&2; exit 2; }
done
if command -v ninja > /dev/null; then generator=Ninja; else generator="Unix Makefiles"; fi
sdk=$(xcrun --sdk macosx --show-sdk-version)
case $sdk in
1[5-9]*|2[0-9]*) ;;
*) echo "the macOS SDK is $sdk; the Metal platform compiles MSL 3.2, which needs the macOS 15 SDK" >&2; exit 2 ;;
esac

mkdir -p "$out"
out=$(cd "$out" && pwd)
start=$(date +%s)
log() { echo "$(date -u +%H:%M:%SZ) $*"; }

log "fetching $commit from $repo"
git init -q "$out/src"
git -C "$out/src" fetch -q --depth 1 "$repo" "$commit"
git -C "$out/src" -c advice.detachedHead=false checkout -q FETCH_HEAD
head=$(git -C "$out/src" rev-parse HEAD)
[ "$head" = "$commit" ] || { echo "fetched $head, asked for $commit" >&2; exit 1; }
for p in "$here"/patches/*.patch; do
    git -C "$out/src" apply "$p"
    log "applied $(basename "$p")"
done

# The deployment target is 15.0 so dyld refuses macOS 14, which lacks MSL 3.2, instead of failing inside Metal.
log "configuring ($generator, SDK $sdk)"
cmake -S "$out/src" -B "$out/build" -G "$generator" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_INSTALL_PREFIX="$out/prefix" \
    -DOPENMM_BUILD_SHARED_LIB=OFF \
    -DOPENMM_BUILD_STATIC_LIB=ON \
    -DOPENMM_BUILD_METAL_LIB=ON \
    -DOPENMM_BUILD_METAL_TESTS=OFF \
    -DOPENMM_BUILD_CPU_LIB=ON \
    -DOPENMM_BUILD_PME_PLUGIN=ON \
    -DOPENMM_BUILD_OPENCL_LIB=OFF \
    -DOPENMM_BUILD_CUDA_LIB=OFF \
    -DOPENMM_BUILD_HIP_LIB=OFF \
    -DOPENMM_BUILD_AMOEBA_PLUGIN=OFF \
    -DOPENMM_BUILD_RPMD_PLUGIN=OFF \
    -DOPENMM_BUILD_DRUDE_PLUGIN=OFF \
    -DOPENMM_BUILD_PYTHON_WRAPPERS=OFF \
    -DOPENMM_BUILD_C_AND_FORTRAN_WRAPPERS=OFF \
    -DOPENMM_BUILD_EXAMPLES=OFF \
    -DOPENMM_GENERATE_API_DOCS=OFF \
    -DBUILD_TESTING=OFF \
    > "$out/cmake-configure.log" 2>&1 || { tail -n 30 "$out/cmake-configure.log"; exit 1; }
configured=$(date +%s)
log "building with $jobs jobs"
cmake --build "$out/build" --parallel "$jobs" > "$out/cmake-build.log" 2>&1 || { grep -m 20 -i 'error' "$out/cmake-build.log"; exit 1; }
cmake --install "$out/build" > "$out/cmake-install.log" 2>&1 || { tail -n 20 "$out/cmake-install.log"; exit 1; }
built=$(date +%s)

lib=$out/prefix/lib
for a in libOpenMM_static.a plugins/libOpenMMMetal_static.a plugins/libOpenMMCPU_static.a plugins/libOpenMMPME_static.a; do
    [ -f "$lib/$a" ] || { echo "FAIL: $lib/$a is missing" >&2; exit 1; }
done
nm -g -U "$lib/plugins/libOpenMMMetal_static.a" 2> /dev/null | grep -q ' T _registerMetalPlatform$' ||
    { echo "FAIL: libOpenMMMetal_static.a has no registerMetalPlatform" >&2; exit 1; }
[ -z "$(find "$out/prefix" -name '*.dylib')" ] || { echo "FAIL: the prefix holds a dylib" >&2; exit 1; }

log "linking the core"
sh "$here/src/link.sh" "$out/prefix" "$out/core"
linked=$(date +%s)

{
    echo "commit $head"
    echo "repo $repo"
    for p in "$here"/patches/*.patch; do echo "patch $(basename "$p") sha256 $(shasum -a 256 "$p" | cut -c1-64)"; done
    echo "cmake $(cmake --version | head -n 1 | awk '{print $3}'), $generator, $jobs jobs"
    echo "clang $(xcrun clang --version | head -n 1)"
    echo "sdk macosx $sdk, host macOS $(sw_vers -productVersion) $(sw_vers -buildVersion), $(sysctl -n machdep.cpu.brand_string)"
    for a in libOpenMM_static.a plugins/libOpenMMMetal_static.a plugins/libOpenMMCPU_static.a plugins/libOpenMMPME_static.a; do
        echo "$(basename "$a") $(stat -f %z "$lib/$a") bytes"
    done
    echo "minicore $(stat -f %z "$out/core/minicore") bytes, sha256 $(shasum -a 256 "$out/core/minicore" | cut -c1-64)"
    echo "seconds: fetch+configure $((configured-start)), build+install $((built-configured)), link $((linked-built))"
} > "$out/BUILD-INFO"
cat "$out/BUILD-INFO"
"$out/core/minicore" --info

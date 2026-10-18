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
# Needs an Apple silicon Mac with Xcode or the Command Line Tools (macOS 15 SDK or later), git, CMake 3.24
# or later, and Ninja or make. No Python, SWIG or Doxygen.
#
# Opt in to other builds from the environment: MINICORE_ARCH=x86_64 builds for Intel Macs, on an Intel Mac or
# as a cross-build on Apple silicon; MINICORE_OPENCL=1 adds OpenMM's OpenCL platform, for AMD GPUs.
set -eu

repo=https://github.com/RedesignedRobot/openmm.git
# Branch metal-mixed-master: OpenMM master d3e32b4da (the Metal platform, openmm#5488) plus df64 mixed precision.
commit=90fa9544cd38aa866e47ed415c9ef1b6dd8c5aca
jobs=6
while getopts j: opt; do
    case $opt in
    j) jobs=$OPTARG ;;
    *) sed -n '2,17s/^# \{0,1\}//p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND-1))
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/build}
[ -e "$out" ] && { echo "$out exists; remove it or pass a new OUT" >&2; exit 2; }
arch=${MINICORE_ARCH:-arm64}
opencl=${MINICORE_OPENCL:-0}
case $arch in arm64|x86_64) ;; *) echo "MINICORE_ARCH is $arch, not arm64 or x86_64" >&2; exit 2 ;; esac
case $opencl in 0) opencl_lib=OFF ;; 1) opencl_lib=ON ;; *) echo "MINICORE_OPENCL is $opencl, not 0 or 1" >&2; exit 2 ;; esac
if [ "$arch" = arm64 ]; then
    [ "$(uname -s)/$(uname -m)" = Darwin/arm64 ] || { echo "build on an Apple silicon Mac" >&2; exit 2; }
else
    [ "$(uname -s)" = Darwin ] || { echo "build on a Mac" >&2; exit 2; }
fi
for tool in git cmake xcrun; do
    command -v $tool > /dev/null || { echo "$tool is not on PATH" >&2; exit 2; }
done
# An Intel-only CMake, such as an old /usr/local Homebrew's, runs under Rosetta, and every compiler it starts
# then fails in xcrun ("libxcrun.dylib ... missing compatible architecture"). A script cmake is not checked.
if [ "$(uname -m)" = arm64 ] && cmake_archs=$(lipo -archs "$(command -v cmake)" 2> /dev/null); then
    case " $cmake_archs" in
    *" arm64"*) ;;
    *) echo "$(command -v cmake) is $cmake_archs only; install an Apple silicon CMake (brew install cmake, in /opt/homebrew) and put it first on PATH" >&2; exit 2 ;;
    esac
fi
# OpenMM's CMakeLists.txt at $commit starts with CMAKE_MINIMUM_REQUIRED(VERSION 3.24).
cmake_version=$(cmake --version | awk 'NR == 1 {print $3}')
case $cmake_version in
3.2[4-9]*|3.[3-9][0-9]*|[4-9].*|[1-9][0-9].*) ;;
*) echo "CMake is $cmake_version; OpenMM at $commit needs 3.24 or later" >&2; exit 2 ;;
esac
if command -v ninja > /dev/null; then generator=Ninja; else generator="Unix Makefiles"; fi
sdk=$(xcrun --sdk macosx --show-sdk-version)
case $sdk in
1[5-9]*|2[0-9]*) ;;
*) echo "the macOS SDK is $sdk; the Metal platform compiles MSL 3.2, which needs the macOS 15 SDK" >&2; exit 2 ;;
esac
# Apple clang takes libc++ from <toolchain>/usr/include/c++/v1 whenever that directory exists, and then
# leaves the SDK's usr/include/c++/v1 out of the search. A partial copy there, left by an older Command Line
# Tools install, fails every C++ file with "'cstdint' file not found". If a probe compile hits that, every
# clang this script starts, CMake's and link.sh's xcrun clang++, gets the SDK's libc++ through the clang
# driver's CCC_OVERRIDE_OPTIONS ("#" keeps it quiet).
sdk_path=$(xcrun --sdk macosx --show-sdk-path)
probe() {
    printf '#include <cstdint>\n#include <exception>\nint main() { return 0; }\n' |
        xcrun clang++ -x c++ -std=c++17 -stdlib=libc++ -arch "$arch" -isysroot "$sdk_path" -fsyntax-only - 2>&1
}
libcxx=default
if ! probe_log=$(probe); then
    toolchain_libcxx=$(dirname "$(xcrun -f clang++)")/../include/c++/v1
    if [ -d "$toolchain_libcxx" ] && [ -f "$sdk_path/usr/include/c++/v1/cstdint" ]; then
        toolchain_libcxx=$(cd "$toolchain_libcxx" && pwd -P)
        export CCC_OVERRIDE_OPTIONS="# +-nostdinc++ +-cxx-isystem$sdk_path/usr/include/c++/v1"
        libcxx="$sdk_path/usr/include/c++/v1, not $toolchain_libcxx"
        probe_log=$(probe) || libcxx=
    else
        libcxx=
    fi
    if [ -z "$libcxx" ]; then
        echo "$probe_log" | head -n 5 >&2
        echo "xcrun clang++ can't compile #include <cstdint> against $sdk_path; its search list:" >&2
        echo | xcrun clang++ -x c++ -stdlib=libc++ -isysroot "$sdk_path" -E -v - 2>&1 | sed -n '/search starts/,/End of search/p' >&2
        exit 2
    fi
    echo "$toolchain_libcxx hides the SDK's libc++; building with $sdk_path/usr/include/c++/v1" >&2
fi

mkdir -p "$out"
out=$(cd "$out" && pwd)
start=$(date +%s)
log() { echo "$(date -u +%H:%M:%SZ) $*"; }
fail() { tail -n 40 "$1" >&2; echo "build.sh: $2 failed; the full log is $1" >&2; exit 1; }

log "fetching $commit from $repo"
git init -q "$out/src"
git -C "$out/src" fetch -q --depth 1 "$repo" "$commit"
git -C "$out/src" -c advice.detachedHead=false checkout -q FETCH_HEAD
for p in "$here"/patches/*.patch; do
    git -C "$out/src" apply "$p"
    log "applied $(basename "$p")"
done

# The deployment target is 15.0 so dyld refuses macOS 14, which lacks MSL 3.2, instead of failing inside Metal.
log "configuring ($generator, SDK $sdk, $arch, OpenCL $opencl_lib)"
cmake -S "$out/src" -B "$out/build" -G "$generator" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_INSTALL_PREFIX="$out/prefix" \
    -DOPENMM_BUILD_SHARED_LIB=OFF \
    -DOPENMM_BUILD_STATIC_LIB=ON \
    -DOPENMM_BUILD_METAL_LIB=ON \
    -DOPENMM_BUILD_METAL_TESTS=OFF \
    -DOPENMM_BUILD_CPU_LIB=ON \
    -DOPENMM_BUILD_PME_PLUGIN=ON \
    -DOPENMM_BUILD_OPENCL_LIB=$opencl_lib \
    -DOPENMM_BUILD_OPENCL_TESTS=OFF \
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
    > "$out/cmake-configure.log" 2>&1 || fail "$out/cmake-configure.log" "configuring OpenMM"
configured=$(date +%s)
log "building with $jobs jobs"
cmake --build "$out/build" --parallel "$jobs" > "$out/cmake-build.log" 2>&1 || fail "$out/cmake-build.log" "building OpenMM"
cmake --install "$out/build" > "$out/cmake-install.log" 2>&1 || fail "$out/cmake-install.log" "installing OpenMM"
built=$(date +%s)

log "linking the core"
MINICORE_ARCH=$arch MINICORE_OPENCL=$opencl sh "$here/src/link.sh" "$out/prefix" "$out/core"
linked=$(date +%s)

{
    echo "commit $commit"
    echo "repo $repo"
    for p in "$here"/patches/*.patch; do echo "patch $(basename "$p") sha256 $(shasum -a 256 "$p" | cut -c1-64)"; done
    echo "arch $arch, OpenCL $opencl_lib"
    echo "cmake $cmake_version, $generator, $jobs jobs"
    echo "clang $(xcrun clang --version | head -n 1)"
    echo "libc++ $libcxx"
    echo "sdk macosx $sdk, host macOS $(sw_vers -productVersion) $(sw_vers -buildVersion), $(sysctl -n machdep.cpu.brand_string)"
    libs="libOpenMM_static.a plugins/libOpenMMMetal_static.a plugins/libOpenMMCPU_static.a plugins/libOpenMMPME_static.a"
    [ "$opencl" = 1 ] && libs="$libs plugins/libOpenMMOpenCL_static.a"
    for a in $libs; do
        echo "$(basename "$a") $(stat -f %z "$out/prefix/lib/$a") bytes"
    done
    echo "minicore $(stat -f %z "$out/core/minicore") bytes, sha256 $(shasum -a 256 "$out/core/minicore" | cut -c1-64)"
    echo "seconds: fetch+configure $((configured-start)), build+install $((built-configured)), link $((linked-built))"
} > "$out/BUILD-INFO"
cat "$out/BUILD-INFO"
if [ "$(uname -m)" = "$arch" ]; then
    "$out/core/minicore" --info
else
    log "minicore is $arch and this Mac is $(uname -m), so minicore --info was not run"
fi

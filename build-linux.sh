#!/bin/sh
# build-linux.sh [-v OPENMM_VERSION] [-c CUDA_VERSION] [OUT]
#
# Builds minicore for Linux x86-64 against conda-forge's OpenMM, so it folds on NVIDIA GPUs through OpenMM's
# CUDA platform. The default is the newest OpenMM there, 8.6.1 for CUDA 13; `-v 8.2.0 -c 11.8` is the OpenMM
# and CUDA generation core 0x27 8.2.1 ships. OUT (default ./build-linux) must not exist. It ends up holding:
#   OUT/env         a conda-forge environment: OpenMM with its plugins, the CUDA libraries it needs, g++,
#                   bzip2 and zlib; minicore runs against it
#   OUT/xtc_utils   OpenMM's XTC writer at the commit the environment's OpenMM was built from
#   OUT/core        minicore, linked against OUT/env/lib with an rpath to it
#   OUT/BUILD-INFO  versions, sizes and timings; OUT/env-packages.txt lists the environment
#
# Needs Linux x86-64, curl, tar and bzip2. micromamba is fetched into OUT; nothing is written outside it.
# The CUDA platform needs the NVIDIA driver at run time, not at build time.
set -eu

openmm=8.6.1
cuda=13
while getopts v:c: opt; do
    case $opt in
    v) openmm=$OPTARG ;;
    c) cuda=$OPTARG ;;
    *) sed -n '2,15p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND-1))
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/build-linux}
[ -e "$out" ] && { echo "$out exists; pick a new directory" >&2; exit 2; }
[ "$(uname -s)/$(uname -m)" = Linux/x86_64 ] || { echo "build on Linux x86-64" >&2; exit 2; }
for tool in curl tar bzip2; do
    command -v $tool > /dev/null || { echo "$tool is not on PATH" >&2; exit 2; }
done

mkdir -p "$out/mm"
out=$(cd "$out" && pwd)
env=$out/env
start=$(date +%s)
log() { echo "$(date -u +%H:%M:%SZ) $*"; }

log "fetching micromamba"
curl -fsSL https://micro.mamba.pm/api/micromamba/linux-64/latest | tar -xj -C "$out/mm" bin/micromamba
mamba() { MAMBA_ROOT_PREFIX=$out/mm "$out/mm/bin/micromamba" "$@"; }
log "creating the environment: openmm $openmm, cuda-version $cuda"
mamba create -y -q -p "$env" -c conda-forge --override-channels "openmm=$openmm" "cuda-version=$cuda" gxx bzip2 zlib \
    > "$out/env-create.log" 2>&1 || { tail -n 30 "$out/env-create.log"; exit 1; }
mamba list -p "$env" > "$out/env-packages.txt"
created=$(date +%s)

# The XTC writer is in OpenMM's Python wrappers, which conda ships compiled; its C++ comes from the same commit.
revision=$("$env/bin/python" -I -c 'import openmm.version as v; print(v.git_revision)')
version=$("$env/bin/python" -I -c 'import openmm.version as v; print(v.full_version)')
case $version in
"$openmm"*) ;;
*) echo "the environment has OpenMM $version, asked for $openmm" >&2; exit 1 ;;
esac
xtc=$out/xtc_utils
mkdir -p "$xtc/include" "$xtc/src"
for f in include/xtc.h include/xdrfile.h include/xdrfile_xtc.h src/xtc.cpp src/xdrfile.cpp src/xdrfile_xtc.cpp; do
    curl -fsSL "https://raw.githubusercontent.com/openmm/openmm/$revision/wrappers/python/openmm/app/internal/xtc_utils/$f" \
        -o "$xtc/$f"
done
log "xtc_utils at $revision"

log "compiling minicore"
mkdir "$out/core"
# xdrfile.cpp is OpenMM's, warnings and all; -w keeps them out of minicore's own.
for f in xtc xdrfile xdrfile_xtc; do
    "$env/bin/g++" -std=c++17 -O2 -w -I"$xtc/include" -c "$xtc/src/$f.cpp" -o "$out/core/$f.o"
done
"$env/bin/g++" -std=c++17 -O2 -Wall -pthread \
    -I"$env/include" -I"$xtc/include" \
    "$here/src/minicore.cpp" "$here/src/host.cpp" "$here/src/wufiles.cpp" "$out/core"/*.o \
    -L"$env/lib" -Wl,-rpath,"$env/lib" -lOpenMM -lbz2 -lz -ldl \
    -o "$out/core/minicore"
rm "$out/core"/*.o
linked=$(date +%s)

lib=$(ldd "$out/core/minicore" | awk '/libOpenMM\.so/ {print $3}')
case $lib in
"$env"/lib/*) ;;
*) echo "FAIL: minicore resolves libOpenMM.so to '$lib', not $env/lib" >&2; exit 1 ;;
esac

{
    echo "openmm $version git $revision (conda-forge), cuda-version $cuda"
    grep -E '^ *(openmm|cuda-version|cudatoolkit|cuda-nvrtc|libcufft|gxx|libstdcxx|python) ' "$out/env-packages.txt" |
        awk '{print "package " $1 " " $2 " " $3}'
    echo "micromamba $(mamba --version)"
    echo "g++ $("$env/bin/g++" -dumpfullversion)"
    echo "host $(uname -sr), $(grep -m 1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')"
    echo "minicore $(stat -c %s "$out/core/minicore") bytes, sha256 $(sha256sum "$out/core/minicore" | cut -c1-64)"
    echo "seconds: environment $((created-start)), xtc_utils+compile $((linked-created))"
} > "$out/BUILD-INFO"
cat "$out/BUILD-INFO"
"$out/core/minicore" --info

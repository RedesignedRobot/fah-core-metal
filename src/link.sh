#!/bin/sh
# SPDX-License-Identifier: MIT
# link.sh <prefix> <core dir>
# Links minicore against a static OpenMM prefix from build.sh: one executable with OpenMM and its
# Reference, CPU and Metal platforms inside, no plugin directory and no dylib of its own. It is signed ad
# hoc with the hardened runtime and no entitlements. The script fails if the executable links anything
# outside the OS.
set -eu
[ $# -eq 2 ] || { echo "usage: link.sh <prefix> <core dir>" >&2; exit 2; }
prefix=$1
core=$2
src=$(cd "$(dirname "$0")" && pwd)
# OpenMM's own XTC writer (wrappers/python/openmm/app/internal/xtc_utils), compiled from the source tree
# build.sh fetched next to the prefix.
xtc=$prefix/../src/wrappers/python/openmm/app/internal/xtc_utils
[ -f "$xtc/src/xtc.cpp" ] || { echo "no xtc_utils at $xtc" >&2; exit 2; }

[ -e "$core" ] && { echo "$core exists; pick a new directory" >&2; exit 2; }
mkdir -p "$core"
obj=$(mktemp -d "${TMPDIR:-/tmp}/minicore-xtc.XXXXXX")
trap 'rm -rf "$obj"' EXIT
# xdrfile.cpp is GROMACS code bundled with OpenMM; its warnings are silenced here only, and the fix belongs upstream.
for f in xtc xdrfile xdrfile_xtc; do
    xcrun clang++ -std=c++17 -O2 -arch arm64 -mmacosx-version-min=15.0 -Wall -Wno-deprecated-declarations \
        -Wno-unused-variable -Wno-unused-but-set-variable -Wno-implicit-const-int-float-conversion \
        -I"$xtc/include" -c "$xtc/src/$f.cpp" -o "$obj/$f.o"
done
# -force_load keeps every object of libOpenMM_static.a. The serialization proxies register from an
# __attribute__((constructor)) that nothing references, so a plain archive link drops them and
# XmlSerializer fails with "no serialization proxy registered for type 'System'". The Metal archive
# needs no force_load: registerMetalPlatform() pulls in what it uses.
xcrun clang++ -std=c++17 -O2 -arch arm64 -mmacosx-version-min=15.0 -Wall -Wno-reorder-ctor -Wno-unused-function \
    -I"$prefix/include" -I"$xtc/include" -DOPENMM_USE_STATIC_LIBRARIES -DLEPTON_USE_STATIC_LIBRARIES \
    "$src/minicore.cpp" "$src/host.cpp" "$src/wufiles.cpp" "$obj/xtc.o" "$obj/xdrfile.o" "$obj/xdrfile_xtc.o" \
    "$prefix/lib/plugins/libOpenMMMetal_static.a" "$prefix/lib/plugins/libOpenMMCPU_static.a" \
    "$prefix/lib/plugins/libOpenMMPME_static.a" -Wl,-force_load,"$prefix/lib/libOpenMM_static.a" \
    -lbz2 -lz -lobjc -framework Metal -framework Foundation -framework QuartzCore -framework IOKit -framework CoreFoundation \
    -o "$core/minicore"
codesign --force --sign - --options runtime "$core/minicore"

if otool -L "$core/minicore" | tail -n +2 | grep -v -e '/usr/lib/' -e '/System/Library/'; then
    echo "FAIL: minicore links outside the OS" >&2
    exit 1
fi
minos=$(vtool -show-build "$core/minicore" | awk '/minos/ {print $2}')
echo "minicore $(stat -f %z "$core/minicore") bytes, minos $minos, $(codesign -dv "$core/minicore" 2>&1 | grep -o 'flags=[^ ]*')"

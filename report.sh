#!/bin/bash
# report.sh [OUT]: folds the bundled test unit with OUT/core/minicore (OUT is build.sh's, ./build by
# default) and prints a filled-in run report: every field of .github/ISSUE_TEMPLATE/run-report.yml in the
# form's order, then a link that opens the form with those fields filled. It sends nothing anywhere; you
# read it and paste it. It exits with minicore's exit code.
#
# The fold runs in a fresh temp dir, removed on exit, so a unit/ you made by hand is left alone. Only the
# report goes to stdout (./report.sh | pbcopy copies it); progress goes to stderr.
set -u

here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/build}
core=$out/core/minicore
form=https://github.com/RedesignedRobot/fah-core-metal/issues/new
[ -x "$core" ] || { echo "no $core; run ./build.sh first" >&2; exit 2; }
[ "$(uname -s)" = Darwin ] || { echo "report.sh describes a Mac; run it on one" >&2; exit 2; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/minicore-report.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# The machine, as About This Mac and System Information show it.
chip=$(sysctl -n machdep.cpu.brand_string)
gpu_cores=$(system_profiler SPDisplaysDataType 2> /dev/null | sed -n 's/^ *Total Number of Cores: *//p' | head -n 1)
[ -n "$gpu_cores" ] || gpu_cores="not listed in System Information"
ram="$(( $(sysctl -n hw.memsize) / 1073741824 )) GB"
macos="$(sw_vers -productVersion) ($(sw_vers -buildVersion))"
build=$("$core" --info 2>&1)

cp -R "$here/test/alanine-dipeptide" "$tmp/unit" || exit 2
echo "Folding the bundled test unit with $core..." >&2
"$core" -dir "$tmp/unit" -gpu-vendor apple -gpu 0 > "$tmp/console.txt" 2>&1
code=$?
echo "minicore exited $code" >&2

log=$tmp/unit/logfile_01.txt
[ -f "$log" ] || : > "$log"
# The first block of nine; the log repeats it after "Using ...".
gpu_info=$(grep 'GPU info:' "$log" | head -n 9)
[ -n "$gpu_info" ] || gpu_info="(no GPU info lines in the log)"
if [ "$code" = 100 ]; then
    tests=$(tar -xOf "$tmp/unit/wuresults_01.dat" science.log 2> /dev/null | grep -E 'Running tests|All tests passed')
    [ -n "$tests" ] || tests="(no state-test lines in the result's science.log)"
else
    # The end of the log from the last "Completed ... steps" line, or, if stepping never started, its
    # error lines.
    last=$(grep -n 'Completed .* steps' "$log" | tail -n 1 | cut -d: -f1)
    if [ -n "$last" ]; then
        tests=$(tail -n "+$last" "$log")
    else
        tests=$(grep -E 'ERROR:|An exception occurred|Core Shutdown' "$log")
    fi
    [ -n "$tests" ] || tests="(the log has no error line; minicore printed:)
$(tail -n 20 "$tmp/console.txt")"
fi

# Percent-encodes $1 byte by byte for a URL query value.
encode() {
    local LC_ALL=C s=$1 c i n
    for (( i = 0; i < ${#s}; i++ )); do
        c=${s:i:1}
        case $c in
        [A-Za-z0-9.~_-]) printf '%s' "$c" ;;
        # bash 3.2 reads a byte above 0x7f as negative.
        *) printf -v n '%d' "'$c"; printf '%%%02X' $(( n & 255 )) ;;
        esac
    done
}

# GitHub fills an issue form's input and textarea fields from query parameters named by their ids
# (docs.github.com: "Creating an issue from a URL query" and "Syntax for GitHub's form schema", key id).
link="$form?template=run-report.yml&title=$(encode "Run report: $chip, $gpu_cores GPU cores, macOS $macos")"
link="$link&chip=$(encode "$chip")&gpu-cores=$(encode "$gpu_cores")&ram=$(encode "$ram")"
link="$link&macos=$(encode "$macos")&build=$(encode "$build")&exit-code=$code"
long="$link&gpu-info=$(encode "$gpu_info")&state-tests=$(encode "$tests")"
# Past about 7 KB GitHub answers 500, and past 9 KB 414 URI Too Long; then the two long fields are left to
# paste.
[ ${#long} -le 6000 ] && link=$long

field() {
    printf '%s\n%s\n\n' "### $1" "$2"
}
field Chip "$chip"
field "GPU cores" "$gpu_cores"
field RAM "$ram"
field "macOS version" "$macos"
field Build "$build"
field "Exit code" "$code"
field "GPU info lines" "$gpu_info"
field "State tests" "$tests"
field Notes "(yours: how long the build took, what went wrong, what you changed)"
field Check "Tick the box: everything above comes from the bundled test unit."
echo "Open a run report with these fields filled in, read it over, tick the box and submit:"
echo "$link"
[ "$link" = "$long" ] || echo "(The GPU info lines and state tests are too long for the link; paste them from above.)"
exit "$code"

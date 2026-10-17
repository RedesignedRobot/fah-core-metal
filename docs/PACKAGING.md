# Packaging minicore for macOS

This page is for whoever packages Folding@home's client for macOS and wants to ship minicore with it. It
answers the questions from [issue #2](https://github.com/RedesignedRobot/fah-core-metal/issues/2): which
user the core runs as, whether the root-owned `/var/empty` home matters, where the Metal cache goes, and
what signing it needs.

fah-core-metal is an independent project, not part of Folding@home. I measured every claim below on an M3
Ultra with macOS 27.2, with the bundled test unit and no FAH client, or I quote Apple's documentation. Each
claim names its source. Paths that start with a lane's name, such as `packaging-guide/` or `service-user/`,
are in my private lab repo under `experiments/030-upstream-metal/lanes/`, as in
[EVIDENCE.md](EVIDENCE.md#packaging). Each lane's REPORT.md has the commands, logs and every sudo command.
The build I tested is this repo at 2a08b87 (`packaging-guide/REPORT.md` section 2).

## The short version

The sections below give each point's source.

- The core runs as whatever user the client runs as. As `_fahclient` from a system LaunchDaemon with the
  client's plist keys, and as `nobody`, it folded the test unit on the GPU and exited 100.
  `/var/empty` doesn't matter, because minicore reads nothing from HOME.
- The Metal shader cache goes in the user's `getconf DARWIN_USER_CACHE_DIR`, under `/var/folders`, which
  macOS already gives each service user. The installer needs to do nothing for it.
- Ad hoc signing with the hardened runtime and no entitlements, as `build.sh` makes it, runs as is. An
  unsigned core is killed at exec. A core with `com.apple.quarantine` waits at exec for a Gatekeeper prompt
  that no one answers.
- For distribution, FAH re-signs with its Developer ID, a secure timestamp and the hardened runtime, and
  notarizes. Apple can't staple a ticket to a bare executable.
- The core is one arm64 executable for macOS 15 or later. It links only libraries and frameworks that ship
  with macOS.
- A boot with nobody logged in is untested.

## 1. How the client starts the core

### Arguments

fah-client starts a core on an Apple GPU with
`-dir <unit id> -suffix 01 -version <client version> -lifeline <client pid> -gpu-platform opencl -gpu-vendor apple -opencl-platform 0 -opencl-device 0 -gpu 0`,
at nice 19 (`corekit/README.md` section 3, from fah-client-bastet's Unit.cpp and cbang's
SystemUtilities.cpp). minicore folded the test unit to exit 100 with exactly that line, and with the
`-metal-platform 0 -metal-device 0 -gpu-uuid none -np 1 -checkpoint 15` form that fah-client-bastet#459
adds (`packaging-guide/REPORT.md` section 7, cases client-args and metal-args).

| Argument | What minicore does with it (`src/minicore.cpp` `parseOptions`, lines 751-794) |
| --- | --- |
| `-dir DIR` | The work dir. Required: without it minicore exits 115 |
| `-suffix S` | File suffix, `01` from the client |
| `-version V` | Logged |
| `-lifeline PID` | minicore exits 103 when that pid is gone |
| `-checkpoint MINUTES` | Checkpoint interval |
| `-gpu N` | Must be 0 on Metal, since Metal uses the system GPU. 1 exits 126 |
| `-gpu-vendor apple`, `-metal-device N` | Select Metal. They win over `-gpu-platform` (`choosePlatform`, lines 432-455). `-metal-platform` alone doesn't, like every `-<api>-platform` |
| `-gpu-platform cuda\|opencl\|hip` | Without the above, asks for a platform this build lacks: exit 115 ("CUDA is not in this core") |
| `-<api>-device N` | The device. Like `-gpu`, it must be 0 on Metal |
| `-<api>-platform N` | Accepted, unused |
| `-gpu-uuid`, `-np` | Accepted, unused |
| `--info` | Prints `minicore (fah-core-metal), OpenMM 8.6, platforms: Reference CPU Metal` and exits 0 |
| `-verbose` | Adds minicore's own timings to science.log |
| `--platform Reference\|CPU\|Metal` | Forces a platform, for testing |

An argument minicore doesn't know is skipped, with its value, after an "Ignoring invalid or unsupported
argument" line on stderr, as core 0x27 does (`src/minicore.cpp` lines 769-771).

### Files in the work dir

Everything minicore writes is under `-dir` (`service-user/REPORT.md`, "What minicore needs").
Measured in every run (`packaging-guide/REPORT.md` section 7):

- It reads the unit: `core.xml`, `system.xml.bz2`, `integrator.xml`, `state.xml.bz2`.
- While it runs: `logfile_01.txt`, `wudata_01.lock` (the pid, held with `flock` so a second core on the
  same unit exits 98), `wuinfo_01.dat` (512 bytes of progress), `viewerTop.json` and `viewerFrame<N>.json`,
  and `01/` with `checkpoint`, `numRetries`, `science.log`, `stepsDone` and `xtcAtoms.csv.bz2`.
- After exit 100: `wuresults_01.dat`, the result tar (about 223 KB for the test unit), next to the log,
  the lock and 101 viewer frames. `01/` is gone.

minicore prints nothing on a normal run: its stdout and stderr go into science.log, and every run's
captured output was empty (`packaging-guide/REPORT.md` section 5). Before the logs are open it writes to
stderr: argument errors, the unknown-argument line, and the error for a work dir it can't write
(section 7 there, cases no-dir, bad-number and read-only).

### Exit codes

| Exit | Name | How I got it (`packaging-guide/REPORT.md` section 7 unless noted) |
| ---: | --- | --- |
| 0 | | `--info` |
| 98 | CORE_RESTART | A second core on a unit whose lock the first holds |
| 100 | FINISHED_UNIT | The client's arguments, both forms |
| 102 | INTERRUPTED | SIGINT during the run: "Caught signal SIGINT(2)" |
| 103 | CLIENT_DIED | The `-lifeline` pid exited: "Lost lifeline PID ..." |
| 114 | BAD_WORK_UNIT | core.xml asks for precision `quad` |
| 115 | BAD_ARGUMENTS | No `-dir`; `-gpu one`; `-gpu-platform cuda` |
| 116 | MISSING_WORK_FILES | An empty work dir: "Cannot find core.xml" |
| 117 | FILE_IO_ERROR | A read-only work dir |
| 121 | UNKNOWN_ERROR | Not triggered. Any other exception (`main()`'s catch-all) |
| 125 | GPU_INITIALIZATION_ERROR | Not triggered at 2a08b87. The Metal Context fails to build (`src/minicore.cpp` 1490; `no-metal-device/REPORT.md`) |
| 126 | GPU_UNAVAILABLE_ERROR | `-gpu 1`; and a process macOS gives no Metal device (a sandbox that denies the GPU): "macOS gives this process no Metal device (MTLCreateSystemDefaultDevice() returned nil)". CI checks the same on GitHub's paravirtual runners (`.github/workflows/build.yml`) |
| 127 | WU_STALLED | Not triggered. No step progress for 600 s (the watchdog) |
| 137 | | Not minicore's: the kernel killed an unsigned copy at exec (section 3) |

The client stops a core with SIGINT and kills it 60 s later (`corekit/README.md` section 3). minicore
answers SIGINT, which I measured, and SIGTERM (`src/minicore.cpp` header comment, line 40) with exit 102
within about a second. The client's LaunchDaemon plist sets ExitTimeOut 60, the time launchd waits
between SIGTERM and SIGKILL when it stops the service (`man 5 launchd.plist`).

## 2. Service user

The service-user lane (`service-user/REPORT.md`) ran minicore 13 times: as `nobody` and as `_fahclient`
from a system-domain LaunchDaemon, with and without SessionCreate, with the FAH client's own plist keys,
and as `nobody` under `sudo -u nobody env -i`. Every run got the GPU and exited 100. For this page I ran
14 more as `_fahclient` with the client's keys (section 3). What they show:

- **The user doesn't matter.** `nobody` and `_fahclient` behaved the same. Neither needs admin rights, a
  shell or a login session. In a plain daemon the process sits in launchd's initial audit session with no
  GUI or console access, and with SessionCreate it gets a fresh session with no flags at all. Metal compute
  worked in both. SessionCreate jobs log SkyLight errors such as `This user is not allowed access to the
  window system right now`. Compute doesn't use the window server, and the folds passed beside them
  (`service-user/REPORT.md`, "What the rows show").
- **`/var/empty` doesn't matter.** minicore reads no HOME, TMPDIR or home directory. Its only `getenv`
  is in a branch that isn't built on macOS. Runs with HOME `/var/empty` and with HOME unset folded the same
  (`service-user/REPORT.md`, "What minicore needs" and rows sudo-nobody-nohome and daemon-*).
- **The work dir has to be writable by the service user.** A read-only one exits 117 (section 1).
- **`sudo -u nobody` is not a service test.** It keeps the caller's audit session and Aqua bootstrap. Only
  the LaunchDaemon runs model the client's service (`service-user/REPORT.md`, "What the rows show").

### The Metal cache

- **Where.** `getconf DARWIN_USER_CACHE_DIR`, which doesn't depend on HOME or TMPDIR. On my Mac that is
  `/var/folders/0b/w0qpqkf9001cs2g2gvftknf00000gl/C/` for `_fahclient` and
  `/var/folders/zz/zyxvpxvq6csfxvn_ngzzzzzvzzzzzy/C/` for `nobody`, owned by that user, mode 700. Both
  existed before any test. Metal writes `com.apple.metal/` (compiled libraries and pipelines) and
  `com.apple.metalfe/` (the compiler's module cache) there (`service-user/REPORT.md`, "The Metal shader
  cache").
- **A user's first start** compiles every kernel: 6.9-7.2 s of start-up instead of 0.1 s
  (`service-user/REPORT.md`; `packaging-guide/REPORT.md` section 6, daemon-hr-cold).
- **A new core file** recompiles its 24 Metal source libraries once per user: about 4.5 s of start-up.
  A byte-identical copy at a new path did it too, so a core update costs one such start per user
  (`packaging-guide/REPORT.md` section 6).
- **An unwritable cache** costs the 7 s on every start, and neither minicore nor Metal logs anything about
  it (`service-user/REPORT.md`).
- **Every later start** of the same file by the same user took 0.1 s (`packaging-guide/REPORT.md`
  section 5, the second runs).

### Untested

- **A boot with nobody logged in.** I was logged in at the console for every run, and I didn't reboot.
  The daemon's own session had no GUI access, but a GUI login existed elsewhere on the machine. A boot
  to the login window, FileVault before unlock and Fast User Switching are untested.
- **The core as the client's child.** No FAH client ran. The cores ran as the LaunchDaemon's own process,
  with the client's plist keys.

## 3. Signing

### What `build.sh` makes

`src/link.sh` signs minicore with `codesign --force --sign - --options runtime`: an ad hoc signature with
the hardened runtime, no entitlements, no timestamp, no team (`packaging-guide/REPORT.md` section 3:
`flags=0x10002(adhoc,runtime)`, `Signature=adhoc`, `TeamIdentifier=not set`). Its identifier is
codesign's default for a binary outside a bundle, `minicore-` plus the LC_UUID.
`codesign -vvv --strict` passes. `spctl -vvv --assess --type exec` rejects it (exit 3), as it rejected
every ad hoc variant in the corekit lane (`corekit/README.md` section 5, "Codesigning").

### What runs

I signed copies of the same build several ways and folded the test unit with each, as myself and as
`_fahclient` from a system LaunchDaemon with the FAH client's plist keys (SessionCreate, ProcessType
Interactive, LowPriorityIO, Umask 18, ExitTimeOut 60, KeepAlive SuccessfulExit false;
`packaging-guide/REPORT.md` section 5, which has the plist):

| Signature | As myself | As `_fahclient`, LaunchDaemon |
| --- | --- | --- |
| Ad hoc, hardened runtime, no entitlements (as built) | 100, 11 state tests passed | 100, 11 passed (cold, warm, two more files) |
| The same plus `com.apple.security.cs.allow-jit` | 100 | 100 |
| The same plus `com.apple.security.cs.allow-unsigned-executable-memory` | 100 | 100 |
| Ad hoc, no hardened runtime | 100 | 100 |
| Unsigned (`codesign --remove-signature`) | 137, killed at exec | 137, killed at exec |
| As built, with `com.apple.quarantine` | never started; I stopped it after 136 s | never started; launchd stopped it after 60 s |

In every run that started, `codesign` on the running pid showed the file's own flags and entitlements and
"dynamically valid" (`packaging-guide/REPORT.md` section 5).

### Entitlements: none

The smallest set that folds is the empty one: as built, cold and warm, as myself and as `_fahclient`.
allow-jit and allow-unsigned-executable-memory changed no exit code, compile count or time. Metal compiles
the kernels in its own MTLCompilerService process, so minicore maps no writable and executable memory
(`packaging-guide/REPORT.md` section 5). Apple says the hardened runtime disallows JIT by default and to
"use only the entitlements that are absolutely necessary"
([Hardened Runtime](https://developer.apple.com/documentation/security/hardened-runtime)).

One case where allow-jit could matter: OpenMM's Lepton expression compiler JITs custom expressions on the
CPU into `MAP_JIT` memory
([allow-jit](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.allow-jit)).
Without the entitlement it falls back to its interpreter. On a custom-integrator test that gave the same
results and the same time (`corekit/README.md` section 5, "Codesigning"). The test unit has no custom
expressions, so my runs don't show it.

### Gatekeeper and quarantine

- On a core file's first exec, syspolicyd scanned it, logged `Code did not match any currently allowed
  policy` and `GK evaluateScanResult: 2`, and let it run. AMFI logged `is adhoc signed`. Later execs of
  the same file logged no scan. This held from a LaunchDaemon as `_fahclient` too
  (`packaging-guide/REPORT.md` section 5).
- An unsigned core: the kernel logged `ASP: Security policy would not allow process` and killed it
  (exit 137). arm64 macOS needs a signature of some kind, and ad hoc meets it (`corekit/README.md`
  section 5, "Codesigning"), so the ad hoc signature is the minimum.
- A quarantined core: syspolicyd logged `GK evaluateScanResult: 1` and `Prompt shown (6, 0), waiting for
  response`, and the process waited at exec until I stopped it. From the LaunchDaemon the quarantined
  copy also sat at exec until launchd stopped it at 60 s. My first prompt was still unanswered then, so I
  can't tell whether that exec waited on it or would have waited anyway (`packaging-guide/REPORT.md`
  section 5). Either way a quarantined ad hoc core doesn't run unattended. Whether the client marks the
  cores it downloads with `com.apple.quarantine` is untested; `xattr -l` on a core the real client
  fetched would show it.

### What an ad hoc signature can't show

- Whether Apple's notary service accepts minicore.
- Whether a quarantined, notarized Developer ID core starts without waiting on a prompt. I have no
  Developer ID to test it.
- How Gatekeeper's first-exec scan treats a Developer ID core. `spctl --assess` rejects every ad hoc
  signature, so its verdict on this build says nothing about a Developer ID one.
- Whether a Developer ID signature changes where Metal caches kernels. My runs show the cache is per user
  and per core file.

### Developer ID steps for FAH, from Apple's documentation

Apple's requirements for notarization: a Developer ID Application certificate ("Don't use a Mac
Distribution, ad hoc, Apple Developer, or local development certificate"), the hardened runtime for "app
and command line targets", a secure timestamp, no `com.apple.security.get-task-allow`, and the macOS 10.9
SDK or later
([Notarizing macOS software before distribution](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)).
minicore already has the hardened runtime, no entitlements and SDK 27.2. It lacks the certificate and the
timestamp. For a nonbundled executable, Apple's commands are:

```sh
# Sign: Developer ID, secure timestamp, hardened runtime, and an identifier, since minicore has no bundle.
# Not under sudo, and without --deep.
codesign -s "Developer ID Application: <team>" -f --timestamp -o runtime -i "<identifier>" minicore

# Check: valid under notarization's rules, a Timestamp line, flags include runtime, no entitlements.
codesign -vvv --deep --strict minicore
codesign -dvv minicore
codesign -d --entitlements :- minicore

# Notarize: the service takes a zip, a disk image or a signed flat package.
ditto -c -k --keepParent minicore minicore.zip
xcrun notarytool store-credentials "<profile>"
xcrun notarytool submit minicore.zip --keychain-profile "<profile>" --wait
xcrun notarytool log <submission id> --keychain-profile "<profile>"

# Gatekeeper's view once notarized.
spctl -vvv --assess --type exec minicore
```

Sources: [Creating distribution-signed code for macOS](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac)
(`--timestamp`, `-o runtime`, `-i` for nonbundled code, no sudo, no `--deep` when signing),
[Customizing the notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)
(`ditto`, `notarytool`), and
[Resolving common notarization issues](https://developer.apple.com/documentation/security/resolving-common-notarization-issues)
(the `codesign` and `spctl` checks).

Stapling: "Although tickets are created for standalone binaries, it's not currently possible to staple
tickets to them" (Customizing the notarization workflow). Apple staples so that "Gatekeeper can find the
ticket even when a network connection isn't available". So a bare minicore's ticket can only be found
online. If FAH ships the core inside an installer package or a disk image, Apple says to sign each layer
and "only notarize the outermost container"
([Packaging Mac software for distribution](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution)).

Where the signing goes is FAH's choice. FAH's own signing step can re-sign the built binary with the
command above, since `codesign -f` replaces the ad hoc signature, or `src/link.sh`'s codesign line can
take the identity. Either way the result needs the checks above.

## 4. Requirements

From `packaging-guide/REPORT.md` section 3 (`vtool`, `lipo`, `otool`) on the 2a08b87 build:

| | |
| --- | --- |
| Architecture | arm64 only, not a fat binary (`-arch arm64` in `src/link.sh`) |
| macOS | 15.0 or later: LC_BUILD_VERSION minos 15.0, built with SDK 27.2. `build.sh` needs SDK 15 or later and sets the deployment target to 15.0 |
| GPU | Apple Silicon. minicore exits 126 if Metal gives it no device or one without the Apple7 family, M1 or later (`src/host.cpp`; `no-metal-device/REPORT.md`) |
| Size | 7,091,440 bytes |
| Linked | `/usr/lib/libbz2.1.0.dylib`, `/usr/lib/libz.1.dylib`, `/usr/lib/libobjc.A.dylib`, `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib`, and the Metal, Foundation (weak), QuartzCore, IOKit and CoreFoundation frameworks: 9 LC_LOAD_DYLIB and 1 LC_LOAD_WEAK_DYLIB |
| Search paths | No LC_RPATH |
| Plugins | None. OpenMM's Reference, CPU and Metal platforms are linked in |

**Self-contained.** OpenMM is linked in statically. `src/link.sh` fails the build if anything links outside
`/usr/lib` or `/System/Library`. At run time a fold loaded 551 images, every one under `/System/` or
`/usr/lib/` except minicore itself (`DYLD_PRINT_LIBRARIES=1` on a copy without the hardened runtime:
dyld ignores `DYLD_*` variables in a hardened process, so the as-built copy printed nothing). Install the
single file; nothing goes beside it.

macOS 15 and 26 are untested: I ran only macOS 27.2, and the run report in section 5 is macOS 27.0.1.

## 5. Run reports

Run reports from other Macs come in through the
[run-report form](https://github.com/RedesignedRobot/fah-core-metal/issues/new?template=run-report.yml).
So far:

| Chip | GPU cores | Memory | macOS | Exit | State tests | Source |
| --- | ---: | ---: | --- | ---: | --- | --- |
| M3 Ultra | 60 | 96 GB | 27.2 | 100 | 11 of 11 | `packaging-guide/REPORT.md` section 5, [EVIDENCE.md](EVIDENCE.md#tested-macs) |
| M3 | 10 | 16 GB | 27.0.1 | 100 | 11 of 11 | [issue #5](https://github.com/RedesignedRobot/fah-core-metal/issues/5), [EVIDENCE.md](EVIDENCE.md#tested-macs) |

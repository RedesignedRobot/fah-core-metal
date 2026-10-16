# Parity with core 0x27

Each behaviour of FahCore_27 8.2.1 that a client, a work server or a person reading the files can see,
next to what minicore does. Core 0x27 here means that build, folding project 18264 in October 2026.

Four audits of minicore at fab6676 found the gaps: the client contract, the captures of core 0x27 at
work, the science set-up, and the failure paths. They are in the lab repo, which is private, under
`experiments/030-upstream-metal/reviews/gap-{client,captures,science,failure}.md`. This table merges
their rows, one row per behaviour. The last column names the audit rows it covers: cl, ca, sc and fa,
with each audit's own numbering. Commit ceb9707 holds every fix.

Verdicts:

- **match**: the same files, bytes, lines or exit, or the same rule where the hardware differs.
- **fixed, ceb9707**: differed at fab6676 and now matches.
- **FAH's call**: needs something only FAH holds (a private key, a private file format, its own build
  or its unit assignment). minicore does the nearest thing it can do in the open.
- **not observable**: no record shows what core 0x27 does. minicore's choice is given.
- **open**: could be closed and wasn't, with the reason.

Sources:

| Key | What |
| --- | --- |
| S:N | Line N of the `strings` output of the FahCore_27 8.2.1 binary (sha256 b88dedb003def95a...). A string proves the text is in the core, not when the core prints it |
| obs | Lab `lanes/fah-observe-gpu/REPORT.md`: core 0x27 under fah-client 8.5.6 on Linux with an RTX 5060 Ti, captures 2 to 7, and the strace of WU2's last core process |
| log | Core 0x27's own logfile_01.txt and science.log from the P18264 units it finished (WU2 is R153 C0 G272), on the bench |
| rw | Lab `lanes/core-realwu/REPORT.md` and its `results/real/` |
| Bm, B6 | fah-client-bastet main at 706d6a0 and v8.5.6 at 93b9681 |
| Cm | cbang master at a8309df, a close proxy for the CBang 1.7.2 core 0x27 links |
| forum | foldingforum.org, `viewtopic.php?p=N` |
| run | A case of the lane's `failure.sh` in gpuq 2235 or 2236 on the M3 Ultra's GPU ([EVIDENCE](EVIDENCE.md#core-gap-runs)) |

## Arguments and launch

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| A1 | Core package | The client checks the package's sha256 and FAH's certificate, then unpacks `<base>/FahCore_27` (Bm `Core.cpp:126-196`) | One static binary, no package | FAH's call: only FAH signs cores | cl 1.1, 1.2 |
| A2 | Core test | `FahCore_27 --info` must exit 0 (Bm `Core.cpp:200-269`; S:20100) | `--info` lists OpenMM's version and platforms, exit 0 | match | cl 1.3 |
| A3 | Library path and core-meta.json | Dynamic, so it needs the client's `DYLD_LIBRARY_PATH`; the client never reads core-meta.json | Static, reads no environment variable | match | cl 1.4, 1.5, 2.9 |
| A4 | Before the launch | The client unlinks wuresults_01.dat and rotates logfile_01.txt (Bm `Unit.cpp:622-632`) | Opens both logs to append | match | cl 2.1 |
| A5 | Arguments a client sends | `-dir -suffix -version -lifeline [-gpu-uuid] -gpu-platform -gpu-vendor -opencl-platform -opencl-device [-cuda-platform -cuda-device] -gpu` (Bm `Unit.cpp:635-680`; obs capture 3) | Takes every one; `-gpu-uuid`, `-np` and the `-<api>-platform` values go unused | match | cl 2.2, 2.3 |
| A6 | An argument the core doesn't know | Ignored: core 0x27 ran with `-gpu-uuid` and `-cuda-platform`, and neither is in its strings (obs capture 3); cbang's warning is "Ignoring invalid or unsupported argument '" (S:20359) | Was 115 before the log opened. Now prints cbang's warning to stderr and goes on (run `args-unknown`, `args-verbose`: 100) | fixed, ceb9707 | cl 2.4, fa A1 |
| A7 | No GPU argument | "Core requires at least one of 'cuda-device' or 'opencl-device' to be specified and not disabled." (S:19510) | Ran Metal on device 0. Now logs that line and exits 115 (run `args-no-gpu`) | fixed, ceb9707 | cl 2.6, fa A6 |
| A8 | `-gpu` as the device | Used as `-opencl-device` when that is missing (S:19504) | Same | match | cl 2.7 |
| A9 | `-suffix` missing | "Must provide suffix for core working files" (S:19727) | Takes 01 | open: the README's "Try it" runs minicore by hand without it, and every client sends `-suffix 01` | cl 2.5 |
| A10 | Options a client never sends | libFAH's `-checkpoint`, `-verbose` and others (S:19720-19738) | Takes `-checkpoint MINUTES` and `-verbose`, plus `--platform` for control runs | match | cl 2.4, sc 39 |
| A11 | Standard streams | Points fds 1 and 2 at `01/science.log`, so OpenMM's own output reaches the returned log (obs line 205; S:19729) | Wrote to stdout, which the client nulls. Now `dup2`s both onto `01/science.log` | fixed, ceb9707 | cl 2.10, 8.4, ca F5 |
| A12 | Process group, priority, CPU pinning | The client sets all three (Cm `Subprocess.cpp:343`, `SystemUtilities.cpp:855`; Bm `Unit.cpp:686-696`) | Starts no child, inherits nice 19 | match | cl 2.11-2.13 |

## Input files

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| I1 | Unit header | 512 bytes before the tar: version, core type, project, run, clone, gen, payload size; "Project: P (Run R, Clone C, Gen G)" in logfile_01.txt only (log; S:19875-19878) | Same fields; the line was in both logs. Now logfile_01.txt only (run `unit-header`) | fixed, ceb9707 | ca L4, rw |
| I2 | Unit digests and signature | "Digital signatures verified", or "Bad work unit. Digital signatures don't match." (S:19884-19886) | Checks neither; logs "Digital signatures not checked: version N header for core 0x27, signature scheme private to FAH" | FAH's call: needs FAH's key | ca L6, cl 7 (118), fa U3 |
| I3 | Encrypted inputs | `.xml.enc`, `smk.enc` (S:19312-19313, 19416-19417) | Exits 114 naming the file | FAH's call: needs FAH's key | sc 1, fa U6 |
| I4 | Work dir | Extracts the tar into `<dir>/01/` and keeps its own files there; "Reading tar file X" in logfile_01.txt on the first start only (obs strace; log) | Kept the inputs in memory and its files in `<dir>`, and logged "Reading tar file" on every start in science.log. Now extracts into `01/` once (run `sigterm-stepping`: one "Reading tar file core.xml" over two starts) | fixed, ceb9707 | cl 2.8, ca F2, L5 |
| I5 | Finding each input | "Checking for system.xml.enc" ... "Found system.xml.bz2" for `.xml.enc`, `.xml.gz.enc`, `.xml.bz2.enc`, `.xml`, `.xml.gz`, `.xml.bz2` (S:19325-19333; log) | Knew `.xml` and `.xml.bz2` only. Now the same six names, lines and order, with gzip (run `unit-gzip`) | fixed, ceb9707 | sc 1 |
| I6 | An input missing | "Cannot find state{.xml.enc\|.xml.gz.enc\|.xml.bz2.enc\|.xml\|.xml.gz\|.xml.bz2}" (S:19330-19333) | Now that line, exit 116 (run `unit-no-state`) | fixed, ceb9707 (the line); not observable (the exit) | fa U4 |
| I7 | No wudata_01.dat | The client never starts the core: "Missing WU data" (Bm `Unit.cpp:622-625`) | Reads loose files in `<dir>`; with none it exits 114 on the missing core.xml (run `no-unit`) | not observable | fa U1 |
| I8 | Empty, truncated or corrupt wudata | The digest check fails first: "Guru Meditation: unit hash" (S:19722); the exit is not public | 114 with the logs, naming the bad member (run `unit-empty`, `unit-truncated`, `unit-tar-header`, `unit-bz2-member`) | FAH's call: the digest needs FAH's header scheme | fa U2, U3 |
| I9 | A member that doesn't deserialize | Not public | 114 with the logs (run `unit-bad-system`) | not observable | fa U5 |
| I10 | A unit for another core type | "Core, unit mismatch.  Core type is X unit wants Y" (S:19690-19691) | Took the type from the header and ran any type. Now takes it from its own name (FahCore_27), logs that line, exits 121 (run `unit-type-mismatch`) | fixed, ceb9707 (the line); not observable (the exit) | cl 4.3, fa U7 |
| I11 | Unit lock | `wudata_01.lock` holds the core's pid for the whole run; a second core logs "Trying to acquire exclusive lock on unit", then "Failed to lock WU" (obs line 207; S:19692-19694) | Had no lock. Now `flock`s the same file with its pid; a second core logs both lines and exits 98 (run `lock-held`) | fixed, ceb9707 (the lock); not observable (the exit) | ca F1, cl 3.3, fa S8 |
| I12 | A start stopped partway through unpacking | Extracts the tar while `01/checkpt.crc` is missing and creates checkpt.crc after the last member; a restart that finds it extracts nothing (obs strace, `chimaera/run/strace-1.txt` lines 7274-7313 and 7752; restart 690-733) | Took integrator, state and system in `01/` as proof of an unpacked tar, so a stop after them and before core.xml left a unit the next start dumped with 114. Now writes an empty `01/unpacked` after the last member and extracts while it is missing (unpack-fix cases `unpack-cut-int`, `unpack-cut-kill` and `sigterm-packed`, gpuq 2238 and 2241) | fixed, ef7fd14 | core-gap review P2 |

## core.xml options

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| X1 | Both XML forms | Attributes (WU2) or text elements (perses) | Reads both | match | sc 41 |
| X2 | The 26 options and their defaults | numSteps, checkpointFreq (-5), xtcFreq, viewerFreq (-1), globalVarFreq, globalVarFilename, trrFreq, the xtc and trr atom options, saveForcesInTrr, precision (single), forceTolerance (5), energyTolerance (10), maxRetriesFromLastCheckpoint (2), disableCheckpointStateTests, disableViewer, MWExclusionThreshold (0), DisablePmeStream (1), minimize (0), minimizeTolerance (10), minimizeMaxIterations (5000), minimizeReportFreq (5) (S:19427-19472) | Read all but the trr options and minimizeReportFreq. Now all 26, same defaults | fixed, ceb9707 | sc 42-56 |
| X3 | An interval as a percent | "negative numbers indicate percentage"; WU2 sends `-5%` (S:19456) | Takes `-5` and `-5%` | match | sc 36 |
| X4 | checkpointFreq 0 | "0 disables checkpointing" (S:19456) | Was numSteps. Now no periodic checkpoint; step 0 and the end still get one (run `corexml-checkpoint-zero`: 2) | fixed, ceb9707 | sc 35 |
| X5 | trrFreq | Writes `records.trr` in GROMACS format ("GMX_trn_file"), forces when saveForcesInTrr is 1; "Appending to TRR file" (S:19322-19323, 19573, 19653) | Was 114. Now the same file and line, returned in the result (run `corexml-trr`: 4 frames, steps 500 to 2000, forces) | fixed, ceb9707 | sc 33, 47, fa C8 |
| X6 | globalVarFreq | Header `Step,Time`, the Context parameters, the integrator globals, `volume`, 15 digits (alchemiscale's globals.csv; S:19574-19575) | Was `step`, globals in declaration order, no Time or volume. Now core 0x27's columns (run `corexml-globals`) | fixed, ceb9707 | sc 29, 52 |
| X7 | minimize | Before the sanity checks, with CoreMinReporter: "  Minimization: iteration: N System energy: E Restraint energy R Max constraint error: C" every minimizeReportFreq iterations, then "  Minimization complete" (S:19355-19361, 19606, 19615) | Was after the first check, no reporter. Now core 0x27's order and lines (run `corexml-minimize`) | fixed, ceb9707 | sc 28, 55, 56 |
| X8 | A bad precision value | Option text "platform precision ['mixed', 'double', 'single']" (S:19462) | Was 125 with no results. Now 114 with the logs (run `corexml-precision`) | fixed, ceb9707 | fa C6 |
| X9 | precision double | Fails on a GPU without fp64: "Failed to create a GPU-enabled OpenMM Context." (S:19514) | Metal has no fp64: 125 with OpenMM's reason (run `corexml-double`) | match | sc 10, fa C7 |
| X10 | Missing core.xml, numSteps 0, a cut tag, a non-numeric value | Not public; cbang prints "Invalid value for option '" (S:20159-20162) | 114 naming the key (run `corexml-missing`, `corexml-numsteps-zero`, `corexml-cut-tag`, `corexml-numsteps-text`, `corexml-checkpoint-text`) | not observable | fa C1-C4 |
| X11 | An unknown option, a negative tolerance or retry limit | Not public | Ignores an unknown option (logged with `-verbose`); the tolerance fails the first test, 114; the limit gives up at the first bad state (run `corexml-unknown`, `corexml-negative-tolerance`, `corexml-negative-retries`) | not observable | sc 57, fa C5, C9, C10 |
| X12 | disableCheckpointStateTests | Skips the checkpoint tests: "  Disabling checkpoint state tests." (S:19442, 19585) | Skipped them silently. Now logs the line | fixed, ceb9707 | sc 22, 49 |
| X13 | DisablePmeStream, MWExclusionThreshold, the xtc atom options | Read with the defaults above | Same; DisablePmeStream goes to a platform that declares it, which Metal doesn't | match | sc 11, 46, 51, 54 |

## Checkpoints

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| K1 | Cadence | At step 0, every checkpointFreq and at the end (obs strace) | Same | match | ca C1, C2 |
| K2 | Files | One `01/checkpoint`, with `stepsDone` and `numRetries` ("0") rewritten after each (obs capture 4) | Kept checkpoint.bin, checkpoint.prev.bin and retries.txt in `<dir>`. Now `01/checkpoint`, `stepsDone` and `numRetries` alone | fixed, ceb9707 | ca F3, F4, sc 34, fa K1 |
| K3 | checkpt.crc | FAH's ChecksumManager file of hashes beside the checkpoint (S:19659-19672) | None | FAH's call: the format is FAH's | ca F4, sc 34 |
| K4 | Write method | Rewrites `checkpoint` in place and relies on checkpt.crc to catch a cut-off write (obs strace) | Writes a temp file with a length and FNV-1a footer, then renames it | FAH's call: without checkpt.crc an in-place write would leave a torn checkpoint undetected | ca F3, F13, fa S4 |
| K5 | Contents | `Context::createCheckpoint` (strings:53) | Same blob behind a header that names the unit | match | sc 34 |
| K6 | Resume | "  Binary checkpoint file found. Restoring context from checkpoint...", "  Binary checkpoint read. Resuming from step N" (S:19589-19590) | Logged "resumed from checkpoint at step N". Now core 0x27's lines (run `sigkill-resume`) | fixed, ceb9707 | ca C5, sc 37 |
| K7 | Outputs past the checkpoint | "File is longer than at last checksum, truncating: " (S:19664); positions.xtc cut back (obs capture 6) | Cuts positions.xtc, records.trr, the globals file and later viewer frames back; now with core 0x27's line | fixed, ceb9707 | sc 37, fa S6 |
| K8 | A damaged checkpoint | checkpt.crc fails: "Failed to validate checkpoint"; "Could not resume from checkpoint:" (S:19597, 19725); what follows is not public | "  Could not resume from checkpoint: ... failed its length, hash or header check", then a fresh start from step 0 (run `checkpoint-truncate`, `checkpoint-flip`) | fixed, ceb9707 (the line); not observable (what follows) | fa K1-K3 |
| K9 | A checkpoint from another unit | checkpt.crc hashes the core's files and the unit (S:19659-19664, 19722) | Loaded any checkpoint of the same size. Now the header holds an FNV-1a of state.xml, and another unit's checkpoint is refused: "Could not resume from checkpoint: ... belongs to another work unit" (run `checkpoint-other-unit`) | fixed, ceb9707 | fa K4 |
| K10 | A stop | Writes no checkpoint: a stop at 3% restarted from step 0 (obs capture 7) | Wrote one at the stop step. Now none | fixed, ceb9707 | ca C4, sc 38, fa S2, cl 6.7 |
| K11 | Time-based checkpoints | libFAH's "Checkpoint every # minutes." (S:19738); no run shows it | Honours `-checkpoint MINUTES` | not observable | sc 39 |

## Log lines

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| L1 | Log levels | logfile_01.txt: banner, unit, intervals, platforms, context, progress, checkpoints, speed, saved files, errors, shutdown. science.log: banner and everything after the unit lines, plus the core's stdout (log) | Wrote every line to both. Now core 0x27's split; minicore's own numbers only with `-verbose` | fixed, ceb9707 | ca L1, L4, L5, L20 |
| L2 | Banner | 80-column star rows, `%11s:` keys wrapped at 80 columns: Core, libFAH, CBang, System, OpenMM sections (log; S:19284 on) | Had none. Now the Core, System and OpenMM sections in cbang's layout, with minicore's own values | fixed, ceb9707 | ca L2 |
| L3 | Banner rows FAH's build fills | The core's version, build date, revision, compiler flags, maintainers; the libFAH and CBang sections | Not printed | FAH's call: they describe FAH's build and libraries | ca L2, L3 |
| L4 | cbang System rows CPU ID, Free Memory, Has Battery, On Battery | From cbang's CPUID and power probes (log) | Not printed | open: no core 0x27 log shows what cbang prints for these on an Apple CPU | ca L2 |
| L5 | Identity | "Folding@home GPU Core27 Folding@home Core", "Version 8.2.1" (log) | "minicore (fah-core-metal), OpenMM 8.6.0" | FAH's call: the name and version are FAH's | ca L3 |
| L6 | GPU info | Nine "  GPU info:" lines after the version line and again after "Using ..." (log; S:19301-19311) | Had none. Now the nine lines twice; PCI, Compute and Driver are "none", "unknown" and the macOS version, since an Apple GPU has none of the three | fixed, ceb9707 | ca L7, cl G17 |
| L7 | Interval lines, "Completed", "Checkpoint completed" | (log; S:19577, 19684-19686) | Same text | match | ca L8, L13, L14 |
| L8 | Platform list | "There are N platforms available.", "Platform N: X", and "  opencl-device 0 specified" under the platform asked for (log) | Now the device line too, under Metal | fixed, ceb9707 | ca L9 |
| L9 | Context lines | "Attempting to create CUDA context:", "  Configuring platform CUDA", "  Using CUDA on CUDA Platform and gpu 0" (log) | The same with Metal | match | ca L11 |
| L10 | Platform settings | A line for each property set: DeviceIndex, UseBlockingSync, precision, DisablePmeStream (log) | A line for each property the platform declares; Metal declares Precision only, so "    Setting platform precision to mixed" | match | ca L12, sc 9, 11, 12 |
| L11 | OpenCL's platform dump | OpenCL's own listing in science.log (log) | None | open: this core holds no OpenCL platform | ca L10 |
| L12 | Speed | "Performance since last checkpoint: %.10g ns/day" over whole seconds; "Average performance" is the last interval's figure, `%g` (log: every value is 21600/n) | Fractional seconds and a whole-run average. Now whole seconds and the last interval | fixed, ceb9707 | ca L15, L16 |
| L13 | science.log sections | Init and Main blocks, launch time, arguments, tolerances, input search and loading, Ewald, integrator type, constraint tolerance, time step, the sanity checks, water and molecular weights, KeepAtoms, XTC atoms, viewer topology, Context parameters, "MD ready", retries, watchdog, "Appending to XTC file", binary checkpoint, end lines (log; 55 templates) | Printed none of them. Now each, in core 0x27's order | fixed, ceb9707 | ca L19, E4, sc 5-7, 21 |
| L14 | Lines core 0x27 doesn't print | None | Printed 19 of its own templates. Now behind `-verbose` | fixed, ceb9707 | ca L20 |
| L15 | State test failure text | "Potential energy error of ", ", threshold of ", "Force RMSE error of ", "NaNs detected in positions." (S:19633-19647) | Same text | match | ca C7, sc 20 |
| L16 | Saving result file | One line a member, each member read after its line; the logfile named `../logfile_01.txt` (log; obs strace) | All lines first, logfile named `logfile_01.txt`. Now core 0x27's order and names | fixed, ceb9707 | ca L17 |
| L17 | Shutdown line | After the result is written, so in neither returned log (log) | Was the last line of both. Now after the result | fixed, ceb9707 | ca L18, T4, T5 |
| L18 | Word for word | 87 line shapes in WU2's logfile_01.txt | 35 match on the P18264 cut (19 at fab6676). The other 52 name FAH's build (L3, L5), the signature check (I2), the client's arguments, the CPU and GPU, and the CUDA platform | fixed, ceb9707 (16 shapes); FAH's call for the rest, but rows L4 and L11, which are open | rw |

## wuinfo

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| W1 | Layout | 512 B: type at 0, "Core27 p18264" at 4, 100 at 84, percent done at 88, steps per percent at 92 (obs) | Same, byte for byte | match | ca F11, cl 4.1 |
| W2 | Cadence | At every 1% (obs strace) | Same; it also wrote one on a stop, which it no longer does | fixed, ceb9707 | ca F12, cl 4.2 |
| W3 | Type | The core's own type; the client drops a wuinfo of another type (Bm `Unit.cpp:725`) | From the header, else 0x24. Now from the core's name | fixed, ceb9707 | cl 4.3 |
| W4 | Write method, all files but the checkpoint | In place with O_TRUNC (obs strace) | Temp file and rename | open: strace alone sees it, the client reads the same bytes, and a rename never shows it a half-written file | ca F13, cl 4.2, 9.1 |

## Signals and exit codes

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| S1 | Handlers | Installed 6 ms after exec (obs capture 2) | Were installed after option parsing. Now first in `main` | fixed, ceb9707 | cl 6.2 |
| S2 | Stop during start-up | "Caught signal SIGINT(2)", "Exiting, please wait. . .", "ERROR:102: Core startup was interrupted by client.", "Folding@home Core Shutdown: INTERRUPTED", 102 (log; S:19314, 20424, 20468) | The same bytes, 102 at once (run `sigint-startup`) | match | ca X1, fa S1, cl 6.4, 6.6 |
| S3 | Stop while stepping | 102 within a second, no checkpoint, no wuinfo, the shutdown line (obs capture 7) | Wrote a checkpoint and its own line. Now as core 0x27 (run `sigterm-stepping`, `stop-resume`) | fixed, ceb9707 | ca C4, X2, sc 38, fa S2, cl 6.7 |
| S4 | Second signal | "Exiting, please wait. . .Caught signal SIGINT(2)" on one line, "WARNING:Next signal will force exit", a blank line, the shutdown line; a third signal kills (obs capture 6; S:20423) | Repeated the first lines. Now the same bytes, then the default action (run `sigint-twice`) | fixed, ceb9707 | ca X3, fa S3, cl 6.5 |
| S5 | Stop during the final write | Not observed | Waited for the result. Now a stop past the final checkpoint exits at once, and the next start writes the result; once it is written, stops are ignored while `01/` goes (run `sigint-final`) | not observable | fa S5 |
| S6 | SIGHUP, SIGQUIT | Named in the core (S:20457-20458); no capture shows handlers | SIGINT and SIGTERM | not observable | cl 6.3 |
| S7 | The client's SIGKILL | 60 s after SIGINT (obs capture 4) | Answers within a second | match | ca X4, cl 6.1, 6.8, 6.9 |
| S8 | Lifeline | `kill(pid, 0)` from start-up, then "Lost lifeline PID N, exiting" (obs; S:20122) | Checked between stepping chunks only, wrote a checkpoint, exit 103. Now a thread polls every 500 ms from start-up, logs that line and exits 103 with no checkpoint (run `lifeline-dead`) | fixed, ceb9707 (the polling and line); not observable (the exit) | cl 3.1, 3.2, fa S7 |
| S9 | Watchdog | "  Starting watchdog..." before the step-0 tests; "Watchdog triggered, requesting soft shutdown down", exit 127 (S:19567, 19682; forum p=370662, p=335096) | Armed at stepping, its own line. Now armed at "Starting watchdog...", core 0x27's line, 127 after 600 s without progress | fixed, ceb9707 (not run: a 600 s GPU hang can't be injected) | cl 5.2, fa D4 |
| S10 | Exits seen from core 0x27 | 98, 100, 102, 114, 127 (obs; log; forum) | The same for the same causes | match | cl 7 |
| S11 | Exits no record shows from core 0x27 | 103, 115, 116, 117, 121, 125, 126 | ExitCode.h's code for each cause | not observable | cl 7 |
| S12 | Log copy and the empty-log check | The client tails logfile_01.txt and errors when it copies nothing (Bm `Unit.cpp:700, 903-907`) | Whole lines, flushed; only 115 and a work dir it can't write come before the log | match | cl 8.1-8.3 |

## State tests

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| T1 | Reference context | CPU with no properties, Reference if CPU fails (S:42, 19527, 19549) | Same | match | sc 2, 15, 16 |
| T2 | First check | state.xml's stored forces and energies against CPU (S:19521) | Compared energies only. Now forces too | fixed, ceb9707 | sc 17 |
| T3 | Tests at step 0 | GPU against CPU at set-up, then again at the step-0 checkpoint (log:368, 402-407) | One. Now two | fixed, ceb9707 | sc 18, ca C6 |
| T4 | Tests at checkpoints | "  Running tests", "  All tests passed." before each checkpoint, after "Average performance" at the end (log) | Same (P18264 cut: 3 of 3 pass) | match | sc 19, ca C6 |
| T5 | What a test checks | FAHBench's StateTests: NaNs, |v| > 17.47 nm/ps, half the velocities zero, |F| > 50000 kJ/mol/nm, force RMSE, potential and kinetic energy (S:19633-19647) | Same checks and limits | match | sc 20, 21 |
| T6 | Zero-velocity check at a fresh start | Not visible | Off for the step-0 tests, on after | not observable | sc 23 |
| T7 | A unit that fails its first check | "ERROR:Potential energy error of X, threshold of Y" and the reference line, then 114 with logfile, science.log and the state file (cores 0x23 and 0x24, forum t=42176) | Same (run `start-energy-tolerance`) | match | fa B1 |
| T8 | A NaN in the unit's state | Same path as T7 | Was 121 with no result. Now 114 with the logs and the state file (run `start-nan-position`) | fixed, ceb9707 | fa B1 |
| T9 | A bad state while stepping | "An exception occurred at step N: ...", "ERROR:98: Attempting to restart from last good checkpoint by restarting core.", 98 (forum p=338624; S:19598, 19602) | Same (run `blowup-retries`) | match | fa B2, B3, D1 |
| T10 | Retry limit | Gives up at the second bad state with maxRetries 2: "Max number of attempts to resume from last checkpoint (2) reached. Aborting.", "ERROR:114: ...", returns logfile, globals, science.log and state.xml (forum p=338624; S:19600-19601) | Gave up at the third with "Max Retries Reached" and returned the logs. Now as core 0x27 (run `blowup-retries`: 98, then 114) | fixed, ceb9707 | fa B4, sc 40, ca C9, cl 7 (114) |
| T11 | Retry count | `numRetries`: "Checking for existence of retries file (numRetries) ...", "There have been N retries already.", "Updating retries file (numRetries) with N retries." (S:19564-19566, 19581-19583) | Kept retries.txt silently. Now the file and lines | fixed, ceb9707 | fa B5, ca C8 |
| T12 | A resumed checkpoint fails its test | Not public | 98, then 114 at the limit (run `restart-test-fails`) | not observable | fa B6 |
| T13 | The client's own retry ceiling | Ten 98s under 2 min each delete the unit (Bm `Unit.cpp:1037-1047`) | Gives up at 114 first | match | fa B7 |

## Output files and timing

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| O1 | xtcAtoms.csv.bz2 | 2,824 B at step 0 (obs strace) | Byte-identical (P18264 cut) | match | ca F6, T7 |
| O2 | viewerTop.json | At step 0, before viewerFrame0 | Byte-identical (P18264 cut) | match | ca F7 |
| O3 | viewerFrameN.json | One atom a line, `%g`, nm, 101 frames by percent (log) | Same layout and count | match | ca F8 |
| O4 | viewerFrame0 values | 175 of 2,520 lines differ in the last digit from `%g` of the input doubles (rw `viewerframe0-vs-commit4.txt`) | `%g` of the input doubles | open: the cause is not found, and the frame is for display | ca F9 |
| O5 | positions.xtc | No step-0 frame, precision 1000 (log) | Same steps, times and precision (P18264 cut: one frame at 5000) | match | ca F10, sc 31 |
| O6 | Order at step 0 | xtcAtoms, viewerTop, viewerFrame0, wuinfo and "Completed 0", the tests, the checkpoint (obs strace) | Tests, frame, checkpoint, then wuinfo. Now core 0x27's order | fixed, ceb9707 | ca O1, sc 18 |
| O7 | Order at 1% and checkpoint steps | wuinfo, xtc, viewer; at a checkpoint wuinfo, tests, xtc, checkpoint, viewer (obs strace) | xtc and viewer first. Now core 0x27's order | fixed, ceb9707 | ca O2, O3 |
| O8 | Final step | stepCount = input + numSteps + 1 (rw `extra-step.txt`: every P18264 gen) | Was input + numSteps. Now + 1 (P18264 cut: 341,105,272) | fixed, ceb9707 | ca E1, sc 30 |
| O9 | checkpointState.xml.bz2 | Positions, velocities, forces, energies, parameters, box (log) | Same members, by OpenMM 8.6, which 8.2's reader takes | match | ca E2, sc 8, 32 |
| O10 | checkpointIntegrator.xml | The unit's integrator.xml byte for byte, from a third read (log) | Was re-serialized (199 B against 171). Now the third read's bytes | fixed, ceb9707 | ca E3, T6 |
| O11 | PME | Alpha and grid from the default box, rounded to 7-smooth sizes, pinned with `setPMEParameters`: "Ewald parameters: alpha 2.62826 nx 189 ny 180 nz 160" (log:345-346) | Left both to each platform (189 x 176 x 154). Now the same alpha, grid and line (P18264 cut) | fixed, ceb9707 | sc 4, 5 |
| O12 | Barostat, motion remover, seeds, custom integrators | Left as the unit has them (sc 24-27) | Same | match | sc 24-27 |
| O13 | Clean-up | Unlinks the 13 files in `01/` and removes it, then exits 100 (obs strace) | Left about 342 MB. Now removes `01/` (P18264 cut, run `straight`) | fixed, ceb9707 | ca E6, cl 9.4 |
| O14 | A full disk | cbang's "write() failed", "xtc write failed..." (S:19321, 20755) | 117 for each file; an xtc frame that didn't grow the file was lost silently, and is now 117 too (run `disk-full`) | fixed, ceb9707 | fa F2, F3 |
| O15 | A work dir it can't write | Can't log either (S:19730, 20440) | 117 with no log (run `dir-readonly`, `dir-missing`) | match | fa F1 |
| O16 | Timing of a checkpoint | 0.95 s to rewrite 136 MB (obs strace) | Not measured | open: the lane's brief allowed no timing runs | ca C3 |

## The result package

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| R1 | Container | 512-byte header (version 1100, digests, a 200-byte signature), then the tar | The tar alone | FAH's call: the signature needs FAH's key | ca T1, E5, cl 9.2 |
| R2 | Members and order | logfile_01.txt, checkpointIntegrator.xml, checkpointState.xml.bz2, positions.xtc, science.log, xtcAtoms.csv.bz2 (log) | Same (P18264 cut), plus records.trr and the globals file when core.xml asks | match | ca T2, cl 9.3 |
| R3 | Member headers | Mode 0666, uid and gid 0, one mtime, ustar | Same | match | ca T3 |
| R4 | Where each log member ends | logfile_01.txt at "Saving result file ../logfile_01.txt", science.log at "Saving result file science.log" (log) | Ran on past both. Now ends at each (P18264 cut) | fixed, ceb9707 | ca T4, T5 |
| R5 | A unit given up on | Logs, state and globals, 114 (forum t=42176, p=338624) | Same (run `start-nan-position`, `blowup-retries`) | fixed, ceb9707 | fa B1, B4 |

## GPU selection and error paths

| # | Behaviour | Core 0x27 (source) | minicore | Verdict | Rows |
| --- | --- | --- | --- | --- | --- |
| G1 | Platform from the arguments | CUDA or OpenCL from `-gpu-platform` and `-gpu-vendor`; "Bad gpu-vendor flag passed" (S:19364-19385) | Metal from `-gpu-vendor apple` or `-metal-*`; another vendor gets 115 "X is not in this core" (run `args-nvidia`, `args-amd-opencl`) | FAH's call: FAH assigns units to cores by GPU | cl 10.3, fa A2, A3 |
| G2 | Device index past the last GPU | The Context fails: "Failed to create OpenCL context:" (S:19526) | 126 naming the device (run `args-gpu-index`, `args-metal-device`) | not observable | fa A4, A5 |
| G3 | No usable GPU at Context creation | "Attempting to create CUDA context:", "Failed to create CUDA context:", "Failed to create a GPU-enabled OpenMM Context." (S:19512-19526) | The first line, then 125 with OpenMM's reason | not observable | fa D2 |
| G4 | GPU lost or out of memory while stepping | The T9 path (forum p=354483) | Same | match | fa D1, D3 |
| G5 | CUDA-only and OpenCL-only properties | DeviceIndex, UseBlockingSync set; DeterministicForces and UseCpuPme never set (log; S) | Metal declares none of the first two; the last two stay unset | match | sc 12-14 |
| G6 | Apple GPU in the client | 8.5.6 skips GPUs without a PCI id; client main detects Apple GPUs (B6 `GPUResources.cpp:155`; Cm `GPUVendor.h:50`) | Nothing to do | FAH's call: the client is FAH's | cl 10.1, 10.2 |

## Where the audits disagreed

Each was settled from the source named.

- **Unknown arguments.** cl 2.4 called 115 a match, citing core-parity's record of 115. That 115 was
  minicore's own. obs capture 3 shows core 0x27 running with `-gpu-uuid` and `-cuda-platform`, neither
  in its strings, so it ignores what it doesn't know. fa A1 holds: row A6.
- **Stall exit.** fa G2 proposed 98, because 8.5.6 dumps a 127. forum p=370662 and p=335096 show
  libFAH cores exiting 127, and client main restarts on it. cl 5.2 holds: row S9.
- **Stop while stepping.** cl 6.7 and fa S2 called minicore's checkpoint on a stop by design, since it
  keeps the work. obs capture 7 shows core 0x27 writes none, and the choice is not FAH's. ca C4 and sc
  38 hold: row S3.
- **Two checkpoint files.** fa K1 called the second copy by design. It guarded against a stop's
  checkpoint that no test had checked (minicore-logic P1-3). With no checkpoint on a stop that case is
  gone, and core 0x27 keeps one file. ca F3 holds: row K2.
- **Retry limit.** sc 40 called the count a match. forum p=338624 shows core 0x22 0.0.10 giving up at
  the second bad state with maxRetries 2, and core 0x27 carries the same strings. fa B4 holds: row T10.
- **Work dir.** cl 2.8 called the flat layout by design. obs's strace shows `01/`, and nothing in it is
  FAH's. ca F2 holds: row I4.
- **No GPU argument.** fa A6 called running Metal by design, since a Mac has one GPU. S:19510 shows core
  0x27 refuses to start. cl 2.6 holds: row A7.
- **Unit for another core type.** fa U7 called it by design, since the server assigns units. The client
  drops the progress of a core whose wuinfo type is wrong (Bm `Unit.cpp:725`), and S:19691 shows core
  0x27 checks. cl 4.3 holds: row I10.
- **Tests at step 0.** ca C6 called the cadence a match. log:368 and 402-407 show two GPU tests before
  step 0 is out. sc 18 holds: row T3.
- **Write method.** ca F13 and fa S4 called it by design. For the checkpoint the reason is FAH's file,
  checkpt.crc (row K4). For the other files no FAH file is involved, so row W4 records it as open.

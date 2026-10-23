# Fact check of the README

Every sentence of the README at 0d2d901 that states a fact, checked against the lab's raw files, not
against the lab's reports. Each gets one verdict:

- **verified**: a raw file or a command shows it as written.
- **overstated**: true for less than the sentence says. The new wording says only what the raw file
  shows.
- **wrong**: a raw file contradicts it.

Sources:

- `lab:` paths are in the private lab repo under `experiments/030-upstream-metal/lanes/`.
- `bench:` paths are in the M3 Ultra's bench directory.
- "gpuq N" is job N of the M3 Ultra's GPU queue.
- `FahCore_27` is the core 0x27 8.2.1 binary, sha256 b88dedb003def95a, and "S:N" is line N of its `strings`
  output, as in `docs/PARITY.md`.
- Line numbers in the README column are at 0d2d901.

Checked on 2026-10-16. The lane ran one GPU job of its own, gpuq 2237, which ran the "Try it" command on
this repo's source.

## README

### Opening

| Line | Old text | Verdict | Source | New text |
| --- | --- | --- | --- | --- |
| 3 | "A Folding@home core for Apple Silicon GPUs, built on OpenMM's Metal platform." | verified | `build.sh` pins openmm/openmm c403e95b3, master at the merge of openmm#5524 (`gh api repos/openmm/openmm/pulls/5524`: "Optimizations to Metal platform", peastman, merged 2026-10-23T21:51:03Z), which sits on openmm#5519 ("Mixed precision for Metal") and carries the Metal platform of openmm#5488 (`gh api repos/openmm/openmm/issues/5488`: "Metal Platform", peastman, merged 2026-10-14T00:02:22Z). From 2026-10-23 to 2026-10-24 it pinned 9e320c6c3, the merge of openmm#5519. At 0d2d901 it pinned the fork RedesignedRobot/openmm 90fa9544c; `otool -L` of the gpuq 2237 binary lists Metal.framework and OS libraries only | unchanged |
| 10 | Figure 1: "A work unit goes from the FAH client through minicore and OpenMM Metal to the Mac's GPU, and the result goes back." | verified | `lab:core-parity/REPORT.md:218-226`, the loopback run of fah-client main 706d6a0 with core v3 | unchanged |
| 12-13 | "minicore is a Folding@home core that runs FAH's OpenMM work units on the GPU of Apple Silicon Macs through Metal." | verified | One FAH project (P18264, units WU2 and WU3) and the four core-parity test systems; the next sentence now gives that scope | unchanged |
| 13 | "Tested on the M3 Ultra and the M2, it folds a real FAH unit," | overstated | No Mac ran the real unit past step 62,500 of 1,250,000. `grep -rhoE 'Completed [0-9,]+ out of [0-9,]+ steps' lab:core-realwu/results` tops out at "Completed 62500 out of 1250000 steps" (`ultra-62500`, `night-62500`, `m2-62500`). Runs to a finished result used copies cut to 5,000 and 10,000 steps (`m2-fin-5000`, `ultra-fin-10000`) | "On the M3 Ultra and the M2 it ran the first 5% of a real FAH unit, project 18264 with 919,221 atoms, and ran shortened copies of that unit to a finished result." |
| 13-14 | "project 18264 with 919,221 atoms" | verified | `lab:core-realwu/REPORT.md:5`: P18264 R153 C0 G272, 919,221 atoms | kept |
| 14 | "and matches FAH's core 0x27 on every check FAH doesn't control." | overstated | `docs/PARITY.md` has 121 rows: 38 match, 52 fixed at ceb9707, 11 FAH's call, 12 not observable and 6 open. The open rows (A9, L4, L11, W4, O4, O16) are not FAH's to control. The OpenMM 8.6 version strings are minicore's choice. The M2 has not run 4 of Figure 7's rows at ceb9707 | "On the M3 Ultra it matches FAH's core 0x27 on 20 of the 25 items in Figure 7; the other 5 are listed under "Where it differs"." |

### Speed

| Line | Old text | Verdict | Source | New text |
| --- | --- | --- | --- | --- |
| 20 | RTX 5060 Ti, core 0x27 8.2.1, CUDA, "192 s" per 1% | verified | `lab:core-realwu/results/real/timing.txt:2`: median 192.0 s over 99 intervals, 186 to 199. I re-derived it from the bench viewer frames' mtimes, and `docs/data/progress.csv` holds the same 99 values | unchanged |
| 20 | "5.37 h" per unit | verified | `lab:core-realwu/REPORT.md:5`: 19:04:41Z to 00:26:51Z, 19,330 s. The span includes a client-restart test 23 s into the first start (`lab:fah-observe-gpu/REPORT.md:43-44`); from the restart at 19:05:36 it is 5.35 h | kept; the new note says "timed from its first start" |
| 21 | M3 Ultra, minicore, Metal, "167 s" per 1% | overstated | `lab:core-realwu/results/night-62500/timing.txt:1-2`: lab minicore commit 5 (binary sha256 8e3c8173), not this repo's build. It covers the first 5% only (intervals 168, 167, 165, 165, 167 s), in gpuq 2151's night slot at nice 0, with a load average of 7.14 at start where the night rule asks for 3. The table set it beside a whole-unit wall clock with no qualifier | kept, with a note under the table: "The RTX 5060 Ti row is one whole unit FAH's client ran, timed from its first start. The M3 Ultra row is the first 5% of the same unit, run by the lab's build of minicore before this repo, in a night slot with a load average of 7.14 at the start; 4.66 h is 100 times that 167 s plus start-up and checkpoints." |
| 21 | "4.66 h" per unit | overstated | `night-62500/timing.txt:5`: 26.7 s + 100 x 167.0 s + 20 x 2 s = 16,767 s. An extrapolation, not a run | "4.66 h, estimated" |
| 21 | "macOS 27" | verified | `BUILD-INFO` of gpuq 2196: "host macOS 27.2 26B5101f, Apple M3 Ultra" | unchanged |
| 28 | Figure 2: "Hours per P18264 unit, 5 ns of a 919,221-atom system." | overstated | 1,250,000 x 4 fs = 5 ns (`lab:core-realwu/REPORT.md:5`). The bar for the M3 Ultra is the estimate above, drawn like a measurement | "... The M3 Ultra bar is estimated from its first 5%." The figure now labels it "4.66 h est." (`docs/data/speed.csv` column `unit_h_from`) |

### Trajectories and accuracy

| Line | Old text | Verdict | Source | New text |
| --- | --- | --- | --- | --- |
| 30 | Heading "Same science" | overstated | With randomSeed 0 no two runs share a trajectory (`lab:core-realwu/REPORT.md` section 3). Figures 3 and 4 show divergence at the rate of two CUDA seeds, and a matching box volume. That supports the same statistics, not the same science | "Trajectories and box volume" |
| 37 | Figure 3: "Metal's trajectory parts from the real run's as fast as two CUDA runs part from each other." | overstated | `docs/data/distance.csv` has 12 rows, steps 5,000 to 60,000, equal to `lab:core-realwu/results/frames/real-s1-ultra-m2.tsv` and `s1-vs-s3.tsv` at those steps (0 mismatches). No Metal run reaches past 62,500 | "Over the first 60,000 steps, Metal's trajectory parts ..." |
| 44 | Figure 4: "Across the whole unit, a CUDA run with another seed samples the real run's box volume." | verified | `docs/data/volume.csv` 250 rows equal `lab:core-realwu/results/frames/real-s3-full.tsv`; means 9,307.09 and 9,307.15 nm³, SD 5.95 and 6.19 | unchanged |
| 53 | Figure 5: "Worst state-test errors on four test systems, Metal on both Macs beside CUDA, against FAHBench's limits." | verified | `docs/data/accuracy.csv` equals the 12 raw logs (each 6 state lines, 6 pass, 0 fail). Limits: FoldingAtHome/fah-bench `fahbench/StateTests.h:20-21`, force 5 and energy 10. Metal ran lab 7a92bbba against Reference; CUDA ran `pycore.py` on OpenMM 8.6.1 (`docs/EVIDENCE.md`, "Which code") | unchanged |
| 60 | Figure 6: "Energy drift in NVE: Metal at the OpenMM commit this repo builds can't be told apart from CUDA." | overstated | Recomputed from `lab:drift-fix/results/raw/mm90fa/s1..s10` and `lab:cuda-ref/results/drift/cuda-mixed/s1..s10`: -0.0010 ± 0.0006 against -0.0001 ± 0.0011 kJ/mol/ps, Welch t -0.73. "Can't be told apart" holds only at 10 runs each | "Energy drift in NVE, 10 runs each: Metal at the OpenMM commit this repo builds can't be told apart from CUDA (Welch t -0.7)." |

### Parity

| Line | Old text | Verdict | Source | New text |
| --- | --- | --- | --- | --- |
| 69 | Figure 7: "Each file and behaviour of core 0x27's run of P18264, checked on each Mac." | overstated | The M2 last ran 3fc8475 (`git diff --stat 3fc8475 fab6676 -- src build.sh patches` is empty). ceb9707 changed four of the rows: checkpointIntegrator.xml (its 3fc8475 run wrote 199 B, `lab:reviews/gap-captures/result-tar.csv`), final step count, work dir layout, and log sections and order (PARITY rows L1, L2 and L13: at fab6676 there was no banner and none of core 0x27's science.log sections) | "... checked on each Mac. The M2 last ran 3fc8475, before ceb9707 changed four of the rows." `docs/data/parity.csv` marks the four M2 cells `not-rerun`, and the figure draws them as dashes |
| 71 | "Each Mac also passed all 101 core-parity checks" | overstated | The 101 checks ran lab minicore 7a92bbba ("core v3"), not this repo's build: `lab:core-parity/REPORT.md:186-190` (M3 Ultra, 20,000 steps per unit) and `core-parity/results/m2-units/summary.log` (M2, 4,000 steps). I re-derived them fail-closed: 101 RESULT lines per Mac, all pass, no zero-test check, all 18 xtc checks at 20 frames, all 18 result checks listing all 8 members | "Each Mac also passed all 101 core-parity checks, run with the lab's build of minicore before this repo," |
| 71 | "and every state test it ran on the real unit." | verified | `grep -c 'State test at step.*pass'` and `grep -ci fail` on each run's own log in `lab:core-realwu/results/`: M3 Ultra `ultra-62500` 2, `ultra-fin-10000` 3, `wu3-ultra-12500` 1, `ultra-fin4-1000` 2, `ultra-fin5-1000` 2, `night-62500` 2 = 12; M2 `m2-62500` 2, `m2-fin-5000` 3 = 5; 0 fail words | "... and every state test it ran on the real unit: 12 on the M3 Ultra and 5 on the M2." |
| 71-72 | "The build in this commit passed 65 of 65 failure and stop-resume cases on the M3 Ultra;" | verified, with a gap | gpuq 2235: alanine 52 of 53, fah-gbsa 3, fah-neq 3, fah-tip4pew 3, fah-tip3p-hmr 1. gpuq 2236: checkpoint-other-unit and fah-tip3p-hmr's two remaining cases, after the harness was fixed. 53 + 3 + 3 + 3 + 3 = 65 CASE lines, all pass on the final runs. In 2235 the harness also hit two shell syntax errors, because it was edited while the job read it. The bench directories of both jobs are deleted. Their output survives only in the core-gap lane's private session record | "... on the M3 Ultra, three of them in a second run after fixes to the test script." |
| 72-73 | "the M2 ran the build before it, and the change since is the same C++ on both Macs." | overstated | Both halves are true. Together they imply that the M2's results carry over to ceb9707, and nothing ran ceb9707 on the M2 | "The M2 last ran 3fc8475, the build before ceb9707." |
| 73-74 | "docs/PARITY.md lists every behaviour of core 0x27 next to minicore's." | verified, scoped | PARITY.md scopes itself to behaviours "a client, a work server or a person reading the files can see": 121 rows from the strings, captures, logs and client source. Its own check is below | unchanged |
| 74-75 | "The runs and raw records behind every number are in docs/EVIDENCE.md." | verified | Each number in the new README has a row in EVIDENCE.md. This commit adds the tested macOS row and the gpuq 2237 row | unchanged |

### Where it differs

| Line | Old text | Verdict | Source | New text |
| --- | --- | --- | --- | --- |
| 79 | "Each difference comes from FAH's keys, FAH's build or the hardware:" | wrong | PARITY.md has 6 open rows, none of them FAH's keys or build or the hardware: A9, L4 (four cbang System rows minicore doesn't print), L11, W4, O4 and O16. The OpenMM 8.6 version is minicore's choice, not FAH's | "The differences in Figure 7 come from FAH's keys and file formats, the OpenMM version or the hardware. docs/PARITY.md also lists six behaviours it leaves open, such as four cbang System rows minicore doesn't print." |
| 81 | Signed result header needs FAH's private key | verified | `lab:core-realwu/results/real/real-files.txt:23` (512-byte header, then the tar); PARITY R1 | unchanged |
| 82-83 | Checkpoint format, "and the client reads neither" | verified | PARITY K3, K4. fah-client-bastet 706d6a0: of its 56 .cpp and .h files, only comments in `Unit.cpp:855-895` mention a checkpoint, and none opens one | unchanged |
| 84 | "minicore builds OpenMM 8.6 ... core 0x27 uses 8.2." | verified | bench `fah-wu/p18264-r153-c0-g272/results/final/logfile_01.txt:70` "Version: 8.2.0" and `:75` "Version 8.2.1"; PARITY L5 | unchanged |
| 85 | "35 of core 0x27's 87 log line shapes match word for word." | verified, scoped | `logshape.py` on the real final logfile_01.txt and gpuq 2235's log: "real 87 shapes, minicore 59, shared 35" (core-gap session record). The M2 at 3fc8475 shares 19 | "On the M3 Ultra at this commit, 35 of ..." |
| 85-86 | "The rest name FAH's build and signature check, the client's arguments, or the Mac's hardware and the Metal platform." | overstated | PARITY L18 leaves rows L4 and L11 open | "... or the Mac's hardware and the Metal platform, apart from the open rows in PARITY.md." |

### Try it and Build it

| Line | Old text | Verdict | Source | New text |
| --- | --- | --- | --- | --- |
| 90 | "On an Apple Silicon Mac with macOS 15 or later," | overstated | The build targets macOS 15.0 (`build.sh` `CMAKE_OSX_DEPLOYMENT_TARGET`) and needs MSL 3.2 (fork `MetalContext.cpp:454`). Every run was on macOS 27.2: gpuq 2196's `BUILD-INFO` says "host macOS 27.2 26B5101f, Apple M3 Ultra" and `lab:core-timing/results/m2/BUILD-INFO-static` says "host macOS 27.2 26B5101f, Apple M2" | "... macOS 15 or later (tested on macOS 27.2), ..." |
| 90 | "Xcode or the Command Line Tools, git and CMake" | verified | `build.sh:13-14,32-35`: git, cmake and xcrun are required; Ninja is used if present, else make. gpuq 2196 used cmake 4.4.3 and Ninja | unchanged |
| 91-98 | Clone, `./build.sh`, copy the test unit, run it: "this builds the core and folds a small test unit" | verified, one gap | Build: gpuq 2196, a `git clone --no-local` of 3fc8475, 906 s (`BUILD-INFO`: fetch and configure 16, build and install 880, link 10; minicore sha256 aabfedc0). `build.sh`, `src/link.sh` and `patches/` are unchanged since. `link.sh` at 0d2d901 against that prefix gives sha256 5e7cc197761cb69f. Run: gpuq 2237, the exact command at 0d2d901's source: exit 100 in 15 s, 101 viewer frames, six result members, 11 "All tests passed." and 0 "fail" in science.log. Gap: the GitHub URL can't be cloned until the repo is public; every clone was local | unchanged |
| new | (missing) where the output goes | wrong by omission | gpuq 2237: 0 bytes on stdout and 0 on stderr. The log is `unit/logfile_01.txt`, ending "Folding@home Core Shutdown: FINISHED_UNIT" | "The core prints nothing to the terminal and exits 100 when the unit is done. Its log is `unit/logfile_01.txt`, and the result is the tar `unit/wuresults_01.dat`, whose science.log holds the state tests." |
| 103-104 | "fetches OpenMM with the Metal platform at a pinned commit, builds it as static libraries and links one self-contained executable." | verified | `BUILD-INFO` of the 2026-10-24 build: commit c403e95b35546bc700c97875c9265a92d1b43ecb, repo https://github.com/openmm/openmm.git, patches 0001 to 0005, four static libraries (2026-10-23: commit 9e320c6c3; at 0d2d901: commit 90fa9544c, the fork); `otool -L` shows only /usr/lib and /System libraries | unchanged |
| 107-108 | `./build.sh -j 6 build`, `--info` | verified | gpuq 2196 ran 6 jobs; PARITY A2 (`--info` exits 0) | unchanged |

### FAQ

| Line | Old text | Verdict | Source | New text |
| --- | --- | --- | --- | --- |
| 115-117 | "Not through a core FAH ships today", openmm#2489 (2019), fah-client-bastet#303 (2024) | verified | `gh api`: #2489 "Future of osx GPU support" 2019-11-28; #303 "Support for Apple's integrated GPUs" 2024-11-05. Client 8.5.6 skips GPUs without a PCI id (PARITY G6) | unchanged |
| 117-118 | "minicore folds a real FAH unit on Apple Silicon GPUs." | overstated | As line 13 | "minicore runs a real FAH unit on Apple Silicon GPUs; the longest run so far is the first 5% of one." |
| 122-123 | "It takes the arguments fah-client passes a core, and fah-client ran it against a local stand-in for FAH's servers." | overstated | `lab:core-parity/REPORT.md:220-226`: the client was fah-client master 706d6a0 patched to trust a test CA, and the core was core v3 (lab 7a92bbba), not this repo's build | "It takes the arguments fah-client passes a core. fah-client main, changed only to trust a test signing key, ran the lab's build of minicore against a local stand-in for FAH's servers." |
| 123 | "It is not an official FAH core, and FAH has not adopted it." | verified | No FAH record says otherwise; the core carries no FAH signature (PARITY R1) | unchanged |
| 127-128 | "Apple Silicon Macs with macOS 15 or later, since the Metal kernels need Metal Shading Language 3.2. I have run it on the M3 Ultra and the M2." | overstated | As line 90 | "... I have run it on the M3 Ultra and the M2, both on macOS 27.2." |
| 132-133 | "the M3 Ultra takes 0.87 times the time core 0x27 takes on an RTX 5060 Ti, measured on one unit, one Mac and one card." | overstated | 167 / 192 = 0.87, per 1%, with the M3 Ultra's first 5% on the lab build against the card's whole unit (`night-62500/timing.txt:6`) | "On P18264 the M3 Ultra takes 0.87 times the time per 1% that core 0x27 takes on an RTX 5060 Ti. The Mac's figure is the first 5% of the unit with the lab's build of minicore, and the card's is the whole unit; one unit, one Mac and one card." |
| 137-138 | "Its state-test errors and energy drift sit beside CUDA's, and its trajectories part from core 0x27's as fast as two CUDA runs part from each other." | overstated | As Figures 3, 5 and 6 | "... and over the first 60,000 steps its trajectory parts from core 0x27's as fast as two CUDA runs part from each other." |
| 142 | "Its results carry no FAH signature, it never connects to FAH's servers, and adoption is up to FAH." | verified | PARITY R1; `grep -nE '\b(socket\|connect\|getaddrinfo\|curl\|URLSession\|CFNetwork)\b' src/*` prints nothing | unchanged |

### Credits and license

| Line | Old text | Verdict | Source | New text |
| --- | --- | --- | --- | --- |
| 146 | OpenMM by Peter Eastman and the OpenMM contributors | verified | OpenMM's own notices (NOTICE) | unchanged |
| 147-148 | Peter Eastman's Metal platform, openmm#5488 | verified | `gh api repos/openmm/openmm/issues/5488`: "Metal Platform", user peastman | unchanged |
| 149-150 | Andrew Thall, "Extended-Precision Floating-Point Numbers for GPU Computation" (2006) | not checked against a raw file | The fork's `platforms/metal` tree does not cite it; `grep -rni thall` prints nothing. This is a bibliographic credit, and no lab file decides it | unchanged |
| 151-152 | "Its FAHBench state tests are the ones minicore runs." | verified | `src/minicore.cpp:23-25` lists FAHBench's StateTests; its limits are 17.47 nm/ps and 50000 kJ/mol/nm. PARITY T5 matches S:19633-19647 | unchanged |
| 156-157 | "The patches in `patches/` and everything `build.sh` fetches are OpenMM's." | overstated | The patches are this repo's edits to OpenMM's CMake files. NOTICE gives them OpenMM's license for the files they change | "The patches in `patches/` edit OpenMM's files and carry OpenMM's license for them, and `build.sh` fetches OpenMM's tree." |
| 157-158 | "The Metal platform and the xdrfile code linked into minicore are under the LGPL 3 or later. `NOTICE` lists each part." | wrong by omission | `src/link.sh:34` also compiles `xtc.cpp` (MIT, Copyright 2023 Accellera, in its header). The Metal platform includes Apple's metal-cpp `Metal.hpp` (Copyright 2020-2024 Apple Inc., Apache 2.0, `libraries/metal-cpp/LICENSE.txt` of the fetched tree). Neither is in NOTICE, and OpenMM's Licenses.txt doesn't list metal-cpp | adds "Apple's metal-cpp header compiled into it under the Apache License 2.0, and the xtc writer from OpenMM's tree under the MIT License". NOTICE gains both entries |
| 160-161 | Not affiliated or endorsed; never connects; reads and writes one directory | verified | The socket grep above; gpuq 2237 wrote only inside `unit/` | unchanged |
| 163 | "Built and tested with Claude Opus 5.5." | not a lab fact | No raw file decides it. Left to the head: see "Left to the head" | unchanged |

## Data and figures

- **CSVs against the raw data.** Every row checks out: `speed.csv`, `progress.csv` (99 real values from the
  bench viewer frames' mtimes, and the M3 Ultra values from `night-62500.progress`), `distance.csv`,
  `volume.csv`, `accuracy.csv` and `drift.csv`. Two changes:
  - `parity.csv`: four M2 cells are now `not-rerun` (Figure 7 above).
  - The box volume range is now 9,299 to 9,318 nm³, since the raw maximum is 9,317.67.
- **`speed.csv` columns.** It gains `unit_h_from` ("full run", "first 5%, x100"). Its condition now names the
  lab build.
- **Regeneration.** At 0d2d901, `python3 docs/figures/make.py` in a temporary copy wrote every SVG,
  `social.png` and `social-renderer.txt` byte-identical to the tree.
  - After this commit's changes, two runs in a row give identical shasums.
  - The figures that changed are speed, parity and the social card.
  - The social card is rendered by Chrome 155.0.8059.80.
- **"GPU busy 88-90%".** `grep -rniE 'GPU busy|88-90|88 to 90'` over README.md, docs/, CITATION.cff and
  NOTICE prints nothing.

## docs/PARITY.md

- **Run cases.** Each of the 50 case names it cites ("run `...`") has a "CASE name ... pass" line in
  gpuq 2235's or 2236's output, from the core-gap session record. `blowup-retries` appears as its two
  steps, -98 and -114. The FAIL lines for `corexml-double`, `unit-gzip` and `start-nan-position` in that
  record come from earlier dry runs.
- **Strings.** The `FahCore_27` binary hashes to b88dedb003def95ac31d24fa9a33543b0f36850ec05bacf4b1c47df0e5dbdc3e,
  as the table says.
  - Across the 52 rows citing S:N, 64 quoted fragments were checked against the cited lines.
  - Most misses are cbang splitting one message into pieces.
  - Five rows cited the wrong line, now corrected:
    - X10 "Invalid value for option '" is S:20386, not S:20159-20162.
    - X7 adds S:19545.
    - T10 adds S:19584.
    - T11 adds S:19592.
    - S8 adds S:20123.
- **Match rows without a source.** Six match rows named no source for core 0x27's side. Each now names one:
  - A3: `FahCore_27.dynamic.txt`, Bm `CoreProcess.cpp:40-53`. The old text named `DYLD_LIBRARY_PATH`,
    macOS's variable, for a Linux ELF.
  - X1: WU2's core.xml.
  - X13: S:19432-19435, 19459-19460, 19465, 19470.
  - K5: "strings:53" becomes S:53.
  - O12: S:94, 129, 132, 162.
  - R3: rw `real-files.txt`. At 0d2d901, gpuq 2237's tar has mode 0666, uid and gid 0, one mtime and the
    ustar magic.
- **Other match rows.** The remaining match rows rest on the four audits named in their last column.
- **Unpack row.** Row I12 came from the core-unpack-fix lane after 0d2d901. This lane changed only its
  capture citation, which named the build host.

## Other files

- **docs/index.html.**
  - The meta description, og:description, twitter:description and JSON-LD said "runs real FAH work
    units". They now say "runs FAH's OpenMM work units ..., tested on the first 5% of a real FAH unit".
  - The og:image:alt now says the M3 Ultra's 4.66 h is estimated.
  - The page's copies of README sentences match the fixes above.
- **docs/llms.txt.** "Runs on Apple Silicon Macs (M-series GPUs)" named chips no one ran, and is gone.
  The lead and the 0.87 sentence follow the README.
- **CITATION.cff.** No claims beyond title, author, license and URL. Verified.
- **docs/EVIDENCE.md.**
  - The parity counts are now per Mac.
  - The opening-lines paragraph and the loopback row are scoped.
  - The box volume range is now 9,318.
  - New rows: the tested macOS and gpuq 2237.
  - It notes that gpuq 2235 and 2236 survive only in a session record.
- **docs/design-notes.md:85-86.** These lines described `checkpoint.bin.failed` and
  `checkpoint.prev.bin`, which ceb9707 removed. They now describe `01/checkpoint`, `01/numRetries`, 98
  and 114 (`src/minicore.cpp:1774-1790`).
- **NOTICE.** Adds metal-cpp and xtc.cpp.
- **History.** Two commits on main, 3fea7b5 and 14ddc0f, name the build host in a citation in PARITY row
  I12. This commit cites that capture as "obs strace-1.txt", so the tree is clean, and the rewrite of
  history is held for the owner. The name is left out here, since this file would match the grep. Apart
  from those two commits, one `git log --all -p | grep -nE` over every commit printed nothing. It searched
  for:
  - home and temp paths
  - the bench and lab directory names
  - host names
  - private-key and ssh-key headers
  - GitHub and Anthropic token prefixes and the Context7 key name
  - IPv4 addresses
  - the owner's mail name
  - the FAH unit hashes

  The pattern itself is in the lane report, not here, since this file would match it. The only binary
  blobs in history are the Inter fonts, `social.png` and the test unit's bz2 files.
  `test/make-unit.py` builds the test unit from OpenMM's alanine-dipeptide PDB, so it holds no FAH data.

## Left to the head

- **Repo description and topics.** Both are outward-facing, so this lane did not change them.
  - The description says the core "folds real FAH work units".
  - The topics include `m1`, a chip no one ran.
  - Proposed command:

```
gh repo edit RedesignedRobot/fah-core-metal --remove-topic m1 --description "Folding@home on Apple Silicon GPUs: an independent OpenMM core on Metal, run on the M3 Ultra and M2 for the first 5% of a real FAH unit and checked file by file against FAH core 0x27."
```

- **The last README line.** "Built and tested with Claude Opus 5.5." credits an AI model in the README.
  The owner's rule bars AI attribution in commits and PRs. Whether it covers the README is the owner's
  call.

# Evidence

Every number and claim in the README, with the run that produced it and where the raw record is.

The runs on the real unit and the core-parity checks were made in my lab repo, which is private. Paths
below that start with `core-realwu/` or `core-parity/` are under its
`experiments/030-upstream-metal/lanes/`. Each REPORT.md there gives the commands. FAH's unit, its
result files and the real core's logs stay on the bench machine. Neither repo holds them, only
statistics taken from them.

## Which code produced which result

| Result | Code |
| --- | --- |
| Real unit runs, file-by-file parity, the 167 s per 1% night timing | minicore at lab commit 416b78c7 and the commits before it, as each row below names |
| 101 core-parity checks | minicore at lab commit 7a92bbba ("core v3"), state tests against the Reference platform |
| CUDA replay | OpenMM 8.2.0 from conda-forge, `replay.py` in `core-realwu/scripts/` |
| Test unit fold and stop and resume checks (gpuq 2181, 2182) | this repo at 414c69a (source as in 3fc8475, which only sets the scripts' executable bits) |
| P18264 start and resume (gpuq 2180) | this repo's source just before 414c69a, without its stop-on-an-output-step fix, which that run's stops did not reach |
| Fresh-clone build, "Try it" transcript, start-up interrupt | this repo at 3fc8475 (gpuq 2196, 2197) |
| P18264 cut to 5,000 steps and run to the end on the M3 Ultra | the same fresh-clone build (gpuq 2199) |
| P18264 on the M2: 1% from the input, the 5,000-step cut to the end, a start-up interrupt | this repo's source as in 3fc8475, built on the M2 by `build.sh -j 4` (minicore sha256 0af48ff0) |
| Tonight's timing | FILLED-IN-LATER |
| Accuracy on four test systems (Figure 6) | Metal: minicore at lab commit 7a92bbba. CUDA: `cuda-ref/scripts/pycore.py`, a Python port of minicore's straight run and FAHBench's state test, on conda-forge OpenMM 8.6.1 |
| Energy drift (Figure 7) | OpenMM's own VerletIntegrator through `drift/scripts/run1ns.py`, no core |

This repo's minicore is the lab's 416b78c7 reworked after two code reviews (`docs/design-notes.md`).
On a clean run it writes the same files, members and log lines.

## The unit

P18264 run 153 clone 0 gen 272: 919,221 atoms (2,520 solute), 1,250,000 steps of 4 fs (5 ns),
LangevinMiddle, PME, MonteCarloBarostat, mixed precision. One 1% is 12,500 steps.
Source: `core-realwu/REPORT.md`, the paragraph headed "Unit", and `core-realwu/results/unit-layout.txt`.

## Speed

| README figure | Value | Source |
| --- | --- | --- |
| Real core, time per 1% | 192 s | Median of the real run's 1% intervals from its viewer frames 0-99; `core-realwu/results/real/timing.txt`, made by `core-realwu/scripts/realtiming.py`; `core-realwu/REPORT.md` section 5 |
| Real core, time per unit | 5.37 h | 19,330 s wall, 2026-10-15 19:04:41Z to 00:26:51Z; same files |
| M3 Ultra, time per 1% | 167 s | gpuq job 2151, night slot, nice 0, minicore 416b78c7 (binary sha256 8e3c8173). 1% intervals 168, 167, 165, 165, 167 s from host-clock stamps of the progress lines. The load average was 7.14 at the start, above the 3 the night rule asks for. `core-realwu/results/night-62500/timing.txt` |
| M3 Ultra, time per unit | 4.66 h | 26.7 s start-up + 100 x 167 s + 20 checkpoints x 2 s; same file |
| M2, time per 1% | 1,280 s | Viewer frames 1-4 of a 62,500-step run at nice 10, 1,288, 1,279, 1,279, 1,278 s, minicore 942bac4b, by day; `core-realwu/results/m2-62500/files.txt`; `core-realwu/REPORT.md` section 5 |
| M2, time per unit | 35.6 h, 74% of 48 h | 100 x 1,280 s plus start-up and checkpoints; same section |
| M3 Ultra against the real core | 0.87 | 167 / 192 |

`docs/data/speed.csv` holds the real core and M3 Ultra rows, and Figure 2 is drawn from it.

The README gives no M2 speed. Its numbers stay here: 1,280 s per 1% and 35.6 h per unit (74% of the 48 h
timeout) in the lab's run above, and 1,298 s for the first 1% with this repo's build (frames 0 and 1 of a
run from the input on 2026-10-16, nice 0, load average 1.0 mean and 2.5 peak in its first minute, an
interval that also holds the step-0 checkpoint).

| Figure 3 | Value | Source |
| --- | --- | --- |
| Real core, seconds per 1% | 99 intervals between viewer frames 0 and 99, median 192 s, 186 to 199 s | Modification times of the real run's `viewerFrame0.json` to `viewerFrame99.json` on the bench, read with `stat`; the same times `core-realwu/results/real/timing.txt` uses |
| M3 Ultra, seconds per 1% | 168, 167, 165, 165, 167 s | `core-realwu/results/night-62500/timing.txt`, host-clock stamps of the progress lines |

`docs/data/progress.csv` holds both columns.

## Trajectories and volume

| README figure | Value | Source |
| --- | --- | --- |
| Distance from the real frame at step 5,000 | M3 Ultra 0.475 nm, M2 0.479 nm, CUDA seed 1 0.479 nm | `core-realwu/results/frames/real-s1-ultra-m2.tsv`, made by `core-realwu/scripts/frames.py` (solute atoms, minimum image, no fitting); `core-realwu/REPORT.md` section 3 |
| CUDA seed 1 against CUDA seed 3 at step 5,000 | 0.481 nm | `core-realwu/results/frames/s1-vs-s3.tsv` |
| randomSeed 0 | A new seed per Context | The unit's integrator.xml; `core-realwu/REPORT.md` section 3 |
| Box volume over the unit | 9,307.1 nm³ mean for both, sd 5.9 (real) and 6.2 (CUDA seed 3) | 250 frames each; `core-realwu/results/frames/real-s3-full.tsv` and `real-s3-summary.txt` |
| Box volume range, first 62,500 steps | 9,299 to 9,317 nm³ in all four runs | `core-realwu/REPORT.md` section 3 |

`docs/data/distance.csv` (every 5,000 steps to 60,000) and `docs/data/volume.csv` (every 5,000 steps
of the unit) are those tables' columns.

## What was tested and where

| README claim | Count | Source |
| --- | --- | --- |
| The real core folded the whole unit | fah-client 8.5.6, core 0x27 8.2.1 (OpenMM 8.2.0, CUDA), RTX 5060 Ti, Linux, 2026-10-15, exit 100 | `core-realwu/REPORT.md` header; `core-realwu/results/real/real-files.txt` |
| Real-unit state tests on the M3 Ultra | 12 pass, 0 fail | Lines starting "State test at step" in each run's own log: `ultra-62500` 2, `ultra-fin-10000` 3, `wu3-ultra-12500` 1, `ultra-fin4-1000` 2, `ultra-fin5-1000` 2, `night-62500` 2, all under `core-realwu/results/` |
| Real-unit state tests on the M2 | 5 pass, 0 fail | `m2-62500` 2, `m2-fin-5000` 3 |
| 101 core-parity checks on each Mac | 101 of 101 on the M3 Ultra and on the M2 | `core-parity/REPORT.md` section 4, `core-parity/results/metal-*/summary.log`, `core-parity/results/m2-units/summary.log` |
| State tests in those checks | 156 pass, 1 fail on each Mac | Counted in the 27 per-case logs per Mac (`core-parity/results/metal-*/*.log` and `m2-units/*.log`, leaving out `summary.log` and `run-units-*.log`, which repeat lines). The one fail is `gbsa-badstart.log`, whose core.xml sets energyTolerance 1e-9 so that its first test fails |
| This repo's build resumes to the same frames | 101 viewer frames and 100 XTC frames at steps 250 to 25,000, as a straight fold | gpuq jobs 2181 (SIGINT at 50%, resume) and 2182 (19 SIGINTs, each landing on an output step, then a resume each time); state tests 11 pass in the straight fold, 12 and 30 pass in the resumed ones, 0 fail |
| This repo's build on P18264 | Start, SIGINT, resume from the checkpoint, SIGINT: 2 state tests pass, 0 fail, exit 102 both times | gpuq job 2180, test 4 |
| This repo's build, P18264 to the end, M3 Ultra | A copy of the unit with numSteps 5,000 and checkpointFreq 2,500, all else unchanged: exit 100, 101 viewer frames, viewerTop.json sha256 9431b3ab and xtcAtoms.csv.bz2 sha256 616d8e69 as the real run's, wuinfo_01.dat 512 B, the six result members in order with mode 0666 and uid and gid 0, checkpointIntegrator.xml 199 B, stepCount 341,105,271 (input 341,100,271 + 5,000), 3 state tests pass, 0 fail | gpuq job 2199 |
| This repo's build, P18264, M2 | From the input to 1%: viewerTop.json and xtcAtoms.csv.bz2 hashes as above, wuinfo_01.dat 512 B, 1 state test pass. The 5,000-step copy to the end: the same facts as the M3 Ultra's row, 3 state tests pass, 0 fail. SIGINT 3 s into start-up: the four lines, exit 102 | Runs under the M2's job lock on 2026-10-16 |
| State tests on the real unit, this repo's build | 5 pass on the M3 Ultra (2180 test 4: 2; 2199: 3), 4 pass on the M2 (1; 3), 0 fail | The rows above |
| "The M2, a base 8 GB chip" | Apple M2, 8 GB (8,589,934,592 B from `sysctl hw.memsize`) | The M2 run's own header line |

The state-test counts above come from counting the "State test at step" lines in the raw logs, pass
and FAIL, and not from a harness's summary.

Command for the core-parity count, run in `core-parity/results`:

```
files=$(find metal-gbsa metal-neq metal-tip3p-hmr metal-tip4pew -name '*.log' ! -name summary.log ! -name 'run-units*')
grep -h 'State test at step' $files | grep -c ': pass$'
grep -h 'State test at step' $files | grep -c FAIL
```

and the same with `m2-units` for the M2.

## How it matches

Each row of the README's table and of `docs/data/parity.csv` is a row of `core-realwu/REPORT.md`
section 1, which names the evidence file for each.

| README figure | Value | Source row |
| --- | --- | --- |
| Unit container | 512-byte header, then a tar of four files | "wudata_01.dat container" |
| core.xml keys | seven | "core.xml keys" |
| Intervals | 5%, 1%, 0.4% (62,500, 12,500 and 5,000 steps) | "Intervals" |
| State test platform and limits | CPU; force 1e7, energy 1e9 from core.xml | "State test platform", "State test limits" |
| Interrupt at start-up | four lines, exit 102 | "Interrupt during start-up"; for this repo's build, gpuq job 2187 |
| wuinfo_01.dat | 512 B | "wuinfo_01.dat" |
| viewerTop.json | 56,183 B, byte-identical | "viewerTop.json" |
| viewerFrameN.json | 101 frames | "viewerFrameN.json" |
| positions.xtc | 250 frames, 2,520 atoms | "positions.xtc" |
| xtcAtoms.csv.bz2 | 2,824 B, byte-identical | "xtcAtoms.csv.bz2" |
| Result members | six, same order, plain ustar | "wuresults members", "wuresults_01.dat container" |
| Exit | 100 | "Exit" |
| Log shapes | 87 in the real logfile_01.txt, 19 word for word | "logfile_01.txt". The lab's comparison (`core-realwu/results/logs/shapes-real-logfile-vs-commit4.txt`) found 18; this repo's build adds "Saving result file positions.xtc". `core-realwu/scripts/logshape.py` on the real log and each Mac's 5,000-step copy (gpuq 2199 and the M2 run) prints "real 87 shapes, minicore 28, shared 19" |
| Energy conservation | Metal mixed -0.0010 ± 0.0006, CUDA mixed -0.0001 ± 0.0011 kJ/mol/ps (mean ± SE, 10 seeds each), Welch t -0.7 | Not from core-realwu: `drift-fix/REPORT.md`, Verdict and the drift table (lab commit 3ec69c3c). Metal on fork 90fa9544c, the commit `build.sh` pins, raw `drift-fix/results/raw/mm90fa/s1..s10` and `drift-fix/results/drift-runs.csv`; CUDA is OpenMM 8.6.1, raw `cuda-ref/results/drift/cuda-mixed/` |

## Where it differs

| README claim | Source |
| --- | --- |
| Signed result header | `core-realwu/REPORT.md` section 1 row "wuresults_01.dat container" and section 2 |
| Checkpoint format | Real: section 1 row "Checkpoint files in the work dir". minicore: `src/wufiles.cpp` `writeAtomically` (length and 64-bit FNV-1a footer) and `src/minicore.cpp` `checkpointHeader` |
| OpenMM version strings | Section 1 rows "checkpointIntegrator.xml" and "checkpointState.xml.bz2" |
| Log wording, 19 of 87 | Section 1 row "logfile_01.txt" and the "Log shapes" row above |
| One extra MD step, 1,250,001 steps | Section 2, last paragraph; `core-realwu/results/real/extra-step.txt`, made by `core-realwu/scripts/extrastep.py` |

## Try it and Build it

| README figure | Value | Source |
| --- | --- | --- |
| Test unit | 2,269 atoms, 25,000 steps of 4 fs (100 ps) | `test/make-unit.py`, `test/alanine-dipeptide/core.xml`; the fold's own log line "2269 atoms" |
| Transcript, 16 s | Lines from the fold's standard output, exit 100, 101 viewer frames, 100 XTC frames, state tests 11 pass and 0 fail | gpuq job 2197: `./build/core/minicore -dir unit -gpu-vendor apple -gpu 0` in the fresh clone of 3fc8475 |
| OpenMM commit | 90fa9544c, three patches | `build.sh`, `patches/` |
| Build time, 15 minutes | 906 s: fetch and configure 16 s, build and install 880 s, link 10 s; load average about 70 at the start | gpuq job 2196, `BUILD-INFO` of the fresh clone of 3fc8475 (`git clone --no-local` of this branch). minicore 7,036,640 bytes, sha256 aabfedc0acd41d4e |
| Start-up interrupt on this repo's build | P18264 SIGINT 3 s into start-up: "Caught signal SIGINT(2)", "Exiting, please wait. . .", "ERROR:102: Core startup was interrupted by client.", "Folding@home Core Shutdown: INTERRUPTED", exit 102 | gpuq job 2197 |

## Accuracy (Figure 6)

| README figure | Value | Source |
| --- | --- | --- |
| Test systems | gbsa 2,489 atoms, neq 2,269, tip3p-hmr 29,451, tip4pew 38,876 | `core-parity/REPORT.md` section 3 |
| Tests | 6 per system and arm (step 0 and five checkpoints), against Reference; 72 in all, 72 pass | `core-parity/results/metal-<unit>/<unit>-straight.log` (M3 Ultra), `core-parity/results/m2-units/<unit>-straight.log` (M2), `cuda-ref/results/cuda-mixed/<unit>-straight.log` (RTX 5060 Ti, OpenMM 8.6.1); `cuda-ref/REPORT.md` header and section 1 |
| Worst force RMSE | 0.054 kJ/mol/nm on Metal (tip4pew, both Macs, 0.05438), 0.052 on CUDA (tip4pew, 0.05209); limit 5 | The largest "force RMSE" over each log's "State test at step" lines |
| Worst \|ΔPE\| | 0.61 kJ/mol on Metal (tip3p-hmr, M2, 0.6073), 0.28 on CUDA (tip4pew, 0.2765); limit 10 | The largest "\|dPE\|" over the same lines |

`docs/data/accuracy.csv` holds the worst values per system and arm, read from those logs with a regular
expression over the "State test at step" lines.

## Energy drift (Figure 7)

| README figure | Value | Source |
| --- | --- | --- |
| System and protocol | amber20-dhfr, 23,558 atoms, PME; 10 ps LangevinMiddle, then 1 ns VerletIntegrator NVE at 2 fs, constraint tolerance 1e-8; drift is the least-squares slope of total energy against time | `drift-fix/REPORT.md` section 3; `drift/scripts/run1ns.py` (`np.polyfit(times, energies, 1)`) |
| Metal mixed, fork 90fa9544c | -0.0010 ± 0.0006 kJ/mol/ps, 10 runs | `drift-fix/results/drift-table.md`, raw `drift-fix/results/raw/mm90fa/s1..s10` |
| Metal mixed before openmm#5492, fork 043395619 | -0.0047 ± 0.0009, 10 runs | same table, lane drift's raw files |
| CUDA mixed, OpenMM 8.6.1, RTX 5060 Ti | -0.0001 ± 0.0011, 10 runs | same table, raw `cuda-ref/results/drift/cuda-mixed/` |
| CPU control | +0.0026 ± 0.0008, 4 runs, fork 043395619 | same table |
| Welch t, Metal 90fa9544c against CUDA mixed | -0.7 (14.4 dof) | same table |
| openmm#5492 | "Keep exact positions when reorderAtoms wraps a molecule", merged 2026-10-14; the fork carries it as ef5a25e2c, an ancestor of 90fa9544c | `drift-fix/REPORT.md` Verdict; GitHub |

`docs/data/drift.csv` holds the four rows.

## Parity per Mac (Figure 8)

Each row of `docs/data/parity.csv` is a row of `core-realwu/REPORT.md` section 1. The two columns name
where each Mac showed it.

| Row | M3 Ultra | M2 |
| --- | --- | --- |
| Unit header fields, core.xml keys, output intervals, state test platform and limits | `core-realwu/results/ultra-62500/`, gpuq 2199 | `core-realwu/results/m2-62500/`, the M2 runs of this repo's build |
| State test schedule | `core-realwu/results/ultra-fin-10000/` (step 0 and each checkpoint) | `core-realwu/results/m2-fin-5000/`, `m2-62500` (step 0 and 5%) |
| Interrupt at start-up | gpuq 2197 | The M2 SIGINT run of this repo's build |
| wuinfo_01.dat, viewerTop.json, xtcAtoms.csv.bz2 | gpuq 2199 | The M2 1% and 5,000-step runs |
| viewerFrameN.json, result members and order, result tar format, exit code, final step count, checkpointIntegrator.xml, OpenMM version strings | gpuq 2199 | The M2 5,000-step run |
| positions.xtc | `core-realwu/results/ultra-62500/xtc-frames.tsv` | `core-realwu/results/m2-62500/xtc-frames.tsv` |
| Box volume | `core-realwu/REPORT.md` section 3 | same |
| Log sections and order, log wording | `logshape.py` on gpuq 2199's log | `logshape.py` on the M2 5,000-step run's log |
| Checkpoint format, work dir layout | `src/wufiles.cpp`, `src/minicore.cpp`; the same binary on both | same |
| Result header signature, unit signature check | Need FAH's keys; `core-realwu/REPORT.md` section 2 | same |

Counts: 17 match, 6 by design, 2 FAH only, the same on both Macs.

## Which Macs

| README claim | Source |
| --- | --- |
| arm64 only | `src/link.sh`, `-arch arm64` |
| macOS 15.0 or later | `build.sh`, `-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0`, and the comment above it; `src/link.sh` fails a binary whose minos is below 15.0 |
| Metal Shading Language 3.2 | Fork 90fa9544c, `platforms/metal/src/MetalContext.cpp` line 454, `options->setLanguageVersion(MTL::LanguageVersion3_2)` |
| Apple7 and later | The GPU families of Apple Silicon Macs. The Metal platform checks no family; the M3 Ultra and the M2 are the two I ran |

## Abstract, key results and the at-a-glance table

Every number there repeats one from a section above: 919,221 atoms (the unit), 0.87 and the 1% and unit
times (speed), 0.475 and 0.481 nm (trajectories), 9,307.1 nm³ (volume), 0.054 and 0.052 kJ/mol/nm and
the limit of 5 (accuracy), the drift values and t -0.7 (energy drift), 17, 6 and 2 of 25 (parity per Mac),
12 of 12 and 5 of 5 state tests and 101 of 101 checks (what was tested). `docs/data/results.csv` holds the
strip's text.

## Background and FAQ

| README claim | Source |
| --- | --- |
| openmm#2489 "Future of osx GPU support", 2019-11-28 | `gh api repos/openmm/openmm/issues/2489`: title and created_at |
| fah-client-bastet#303 "Support for Apple's integrated GPUs", 2024-11-05, open | `gh api repos/FoldingAtHome/fah-client-bastet/issues/303`: title, created_at and state, read 2026-10-16 |
| fah-client ran minicore against a local stand-in for FAH's servers and took its results | `core-parity/REPORT.md` section 5 (`loopback.sh`): fah-client master 706d6a0 against a fake work server on the same machine |
| Results carry no FAH signature, nothing connects to FAH's servers | "Where it differs"; `src/` opens no socket |

## Credits and the last line

| README claim | Source |
| --- | --- |
| Andrew Thall, "Extended-Precision Floating-Point Numbers for GPU Computation" (2006) | The df64 operations in the fork's Metal mixed precision follow it; the paper's own title page |
| openmm#5488 "Metal Platform", by Peter Eastman, merged 2026-10-14 | GitHub |
| "Built and tested with Claude Opus 5.5." | The model that wrote this repo and ran its tests |

## The Pages files

`docs/index.html`, `docs/llms.txt` and the Open Graph text repeat sentences of the README word for word,
so each claim there has its source in the sections above. `docs/social.png` repeats Figure 2's hours.

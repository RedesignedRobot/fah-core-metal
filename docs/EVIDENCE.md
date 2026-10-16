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
| Tonight's timing | FILLED-IN-LATER |

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

`docs/data/speed.csv` holds these rows. The speed chart is drawn from it.

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
| Log shapes | 87 in the real logfile_01.txt, 18 word for word | "logfile_01.txt"; `core-realwu/results/logs/shapes-real-logfile-vs-commit4.txt` |
| Energy conservation | Metal mixed -0.0010 ± 0.0006, CUDA mixed -0.0001 ± 0.0011 kJ/mol/ps (mean ± SE, 10 seeds each), Welch t -0.7 | Not from core-realwu: `drift-fix/REPORT.md`, Verdict and the drift table (lab commit 3ec69c3c). Metal on fork 90fa9544c, the commit `build.sh` pins, raw `drift-fix/results/raw/mm90fa/s1..s10` and `drift-fix/results/drift-runs.csv`; CUDA is OpenMM 8.6.1, raw `cuda-ref/results/drift/cuda-mixed/` |

## Where it differs

| README claim | Source |
| --- | --- |
| Signed result header | `core-realwu/REPORT.md` section 1 row "wuresults_01.dat container" and section 2 |
| Checkpoint format | Real: section 1 row "Checkpoint files in the work dir". minicore: `src/wufiles.cpp` `writeAtomically` (length and 64-bit FNV-1a footer) and `src/minicore.cpp` `checkpointHeader` |
| OpenMM version strings | Section 1 rows "checkpointIntegrator.xml" and "checkpointState.xml.bz2" |
| Log wording | Section 1 row "logfile_01.txt" |
| One extra MD step, 1,250,001 steps | Section 2, last paragraph; `core-realwu/results/real/extra-step.txt`, made by `core-realwu/scripts/extrastep.py` |

## Try it and Build it

| README figure | Value | Source |
| --- | --- | --- |
| Test unit | 2,269 atoms, 25,000 steps of 4 fs (100 ps) | `test/make-unit.py`, `test/alanine-dipeptide/core.xml`; the fold's own log line "2269 atoms" |
| Transcript, 16 s | Lines from the fold's standard output, exit 100, 101 viewer frames, 100 XTC frames, state tests 11 pass and 0 fail | gpuq job 2197: `./build/core/minicore -dir unit -gpu-vendor apple -gpu 0` in the fresh clone of 3fc8475 |
| OpenMM commit | 90fa9544c, three patches | `build.sh`, `patches/` |
| Build time, 15 minutes | 906 s: fetch and configure 16 s, build and install 880 s, link 10 s; load average about 70 at the start | gpuq job 2196, `BUILD-INFO` of the fresh clone of 3fc8475 (`git clone --no-local` of this branch). minicore 7,036,640 bytes, sha256 aabfedc0acd41d4e |
| Start-up interrupt on this repo's build | P18264 SIGINT 3 s into start-up: "Caught signal SIGINT(2)", "Exiting, please wait. . .", "ERROR:102: Core startup was interrupted by client.", "Folding@home Core Shutdown: INTERRUPTED", exit 102 | gpuq job 2197 |

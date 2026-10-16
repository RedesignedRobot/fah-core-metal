# README design notes

I read four READMEs I like before writing this one, and took one or two habits from each.

## uv (astral-sh/uv)

- The hero is a single benchmark chart with a one-line caption saying what was measured
  ("Installing Trio's dependencies with a warm cache"). The chart comes in light and dark versions
  through `<picture>` and `prefers-color-scheme`.
- One sentence under the name says what it is. The claim "10-100x faster" links to the benchmark
  that backs it.
- Highlights come first as short bullets, then installation, then features in their own sections.

Taken: the `<picture>` pair for every figure, a caption under each chart that names the workload,
and a link from each headline number to its source.

## ripgrep (BurntSushi/ripgrep)

- The speed table names the machine, the corpus and the exact command for every row, and gives the
  ratio next to the time.
- A section called "Why shouldn't I use ripgrep?" lists the honest reasons not to.
- "Building" is a few commands that work from a fresh clone.

Taken: every timing names the hardware, the unit and the OS. The "Where it differs" section plays
the same role as "Why shouldn't I": it lists the differences before a reader finds them.

## TigerBeetle (tigerbeetle/tigerbeetle)

- 72 lines. One italic sentence, links to the docs, then a "Start" block of console commands with
  their real output.

Taken: "Try it" shows a real transcript of the test fold, not a description of one. The README
stays short and pushes detail into `docs/`.

## lazygit (jesseduffield/lazygit)

- An "Elevator Pitch" in the author's own voice, first person, before any feature list.
- Each feature has a heading, one or two sentences and a GIF.

Taken: the first person singular voice. I left out the table of contents, the badges and the
sponsor banner. A README this short does not need them.

## The page and its figures

The figures carry the README. Under the hero, two sentences say what minicore is and what it matches.
Each section after that is a figure with a numbered, one-sentence italic caption, and the method, data
file and conditions behind each figure are in `docs/EVIDENCE.md`. The figures share one palette: blue
for Metal (the M2 a lighter blue than the M3 Ultra), green for CUDA on NVIDIA, grey for the real core
0x27 run and other references. They also share one font stack, and gridlines are the only decoration.

## What I avoid

- Badges with nothing behind them. There is no CI, no release and no package, so there are no badges.
- Adjectives in place of numbers. A claim about speed or agreement carries its number and its source
  in `docs/EVIDENCE.md`.
- Screenshots of terminals. Transcripts are text so they can be searched and copied.

# Core design notes

minicore here is the lab's core at lab commit 416b78c7, the commit that folded the real unit, reworked
after two reviews of that code (one on logic and error paths, one on code quality). The clean-run
output is the same: the same files, members, order and log lines.

## What I took out

The lab core had options for experiments that a FAH client never passes. I deleted them, not
gated them, so the shipped binary has one code path:

- `--stop-at`, `--steps`, `--seed`, `--inject-nan-at` and the timing switches (`--time-gpu`,
  `--compile-time`). The timing job stops the core with SIGINT instead, the client's own pause path.
- The GPU-time and pipeline-compile hooks (`gputime.cpp`, `compiletime.cpp`) and the objc calls they
  needed.
- The `final-state.xml`, `final.bin` and pid files the lab scripts read. They wrote into the FAH work
  dir.
- Fields that were parsed and never read, and the `viewerOn` flag that mirrored `viewer > 0`.

What remains: the client's arguments (`-dir`, `-suffix`, `-version`, `-lifeline`, `-checkpoint`,
`-gpu`, `-gpu-vendor`, `-gpu-platform`, `-opencl-device`, `-cuda-device`, and `-gpu-uuid`, `-np`
and `-X-platform` read and ignored), `--platform` to force Reference, CPU or Metal, and `--info`.

## Fixes beyond the clean run

- The step-0 checkpoint comes after the step-0 viewer frame and globals row, so a kill right after it
  no longer loses frame 0 on restart.
- A checkpoint that fails its resume test is renamed `checkpoint.bin.failed`, and the core falls back to
  `checkpoint.prev.bin`. One bad pause no longer costs the unit.
- A stop that lands on an output step writes that step's frames first. Before, a SIGINT during the last
  chunk before a 1% step lost that viewer frame and XTC frame, and the resumed unit returned 100 frames
  instead of 101. The lab code has the same bug.
- core.xml: a truncated closing tag no longer loops forever, `dev=` no longer reads as `v=`, and
  `</xtcAtoms` no longer matches `</xtcAtomsMore>`.
- bunzip2 reads every stream, as `bzip2 -d` does. pbzip2 writes one stream per block.
- `globalVarFilename` cannot name a file the core owns.
- File errors exit FILE_IO_ERROR (117). Writes loop on short writes, sync with `F_FULLFSYNC` and report
  `errno`.
- The watchdog stays armed through every GPU sync, and wuinfo_01.dat is written at each 1%, as core 0x27
  does, not every second.
- The work unit's XML is freed after start-up, and a checkpoint is copied once on save.

## Review items I left as they are

- A second signal repeats core 0x27's first two lines. Core 0x27 warns "Next signal will force exit"
  instead. No client I know of sends a second one before its 61 s kill.
- A stop during the final result write waits for the write. The final checkpoint is on disk and every
  write is atomic, so a kill there loses nothing.
- A unit that fails its first state test recompresses its state XML instead of returning the original
  bytes. This happens once per bad unit.
- The start-up banner names minicore and its OpenMM version. Core 0x27 names itself, and copying that
  would claim to be it.

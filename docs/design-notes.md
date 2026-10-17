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

## The second pass: colour and pictures

For the banner, badges, pipeline and cards I read five more READMEs that announce hard things.

- RPCS3: two badges with logos, then one plain sentence saying what it is. Taken: a short badge row
  under the name, every badge a link.
- ollama and ghostty: a centred mark at the top and the tagline under it, with nothing else above
  the fold. Taken: the centred banner with the name and tagline, and the badges centred under it.
- uv and ruff: one chart in light and dark through `<picture>`, centred, with an italic caption
  naming the workload. Taken: every new picture comes in both themes, and the pipeline has a caption.
- Tauri: a full-width splash image, then static shields.io badges in a few chosen colours. Taken:
  the full-width banner, and badges coloured from the figures' palette, blue to violet to pink in
  the order they appear.
- Asahi Linux keeps its pictures on its website, and its repos' READMEs are plain text. Taken: the
  Pages site carries the same pictures as the README.

The banner draws a protein cartoon in code, a coil into an alpha helix and then a three-stranded
beta sheet, coloured from one end of the chain to the other as molecular viewers colour a chain. The
cards keep the headline numbers in the README's order, with their full text in the alt text and in a
table under them, so the numbers stay searchable.

## The page and its figures

The figures carry the README. Under the hero, two sentences say what minicore is and what it matches.
Each section after that is a figure with a numbered, one-sentence italic caption, and the method, data
file and conditions behind each figure are in `docs/EVIDENCE.md`. The figures share one palette: blue
for Metal (the M2 a lighter blue than the M3 Ultra), green for CUDA on NVIDIA, grey for the real core
0x27 run and other references. They also share one font stack, and gridlines are the only decoration.

## What I avoid

- Badges with nothing behind them. The badges under the banner are static: two carry a number from
  `docs/EVIDENCE.md` and link to its row, the rest name the platform, the backend, the license and the
  docs. There is no CI, no release and no package, so there is no build, version or download badge.
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
- A checkpoint that fails its resume test counts as a bad state: the core adds one to `01/numRetries`
  and exits 98 for a restart from `01/checkpoint`, and gives up with 114 at maxRetries, as core 0x27
  does. A good checkpoint clears the count. Since ceb9707 the core keeps the one `01/checkpoint` and
  writes none on a stop, so a pause no longer leaves an untested checkpoint behind.
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

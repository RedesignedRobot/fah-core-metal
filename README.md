# fah-core-metal

A Folding@home core for Apple Silicon GPUs, built on OpenMM's Metal platform.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/hero-dark.svg">
  <img alt="FAH client to minicore to OpenMM Metal to Apple GPU" src="docs/figures/hero-light.svg">
</picture>

*Figure 1. A work unit goes from the FAH client through minicore and OpenMM Metal to the Mac's GPU, and the result goes back.*

minicore is a Folding@home core that runs FAH's OpenMM work units on the GPU of Apple Silicon Macs
through Metal. On the M3 Ultra and the M2 it ran the first 5% of a real FAH unit, project 18264 with
919,221 atoms, and ran shortened copies of that unit to a finished result. On the M3 Ultra it matches FAH's
core 0x27 on 20 of the 25 items in Figure 7; the other 5 are listed under "Where it differs".

## Speed

| Machine | Core | Per 1% | Per unit |
| --- | --- | ---: | ---: |
| RTX 5060 Ti, Linux | core 0x27 8.2.1, CUDA | 192 s | 5.37 h |
| M3 Ultra, macOS 27 | minicore, Metal | 167 s | 4.66 h, estimated |

The RTX 5060 Ti row is one whole unit FAH's client ran, timed from its first start. The M3 Ultra row is the
first 5% of the same unit, run by the lab's build of minicore before this repo, in a night slot with a
load average of 7.14 at the start; 4.66 h is 100 times that 167 s plus start-up and checkpoints.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/speed-dark.svg">
  <img alt="Hours per P18264 unit, M3 Ultra against the real core" src="docs/figures/speed-light.svg">
</picture>

*Figure 2. Hours per P18264 unit, 5 ns of a 919,221-atom system. The M3 Ultra bar is estimated from its first 5%.*

## Trajectories and box volume

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/distance-dark.svg">
  <img alt="Distance from the real trajectory against the distance between two CUDA seeds" src="docs/figures/distance-light.svg">
</picture>

*Figure 3. Over the first 60,000 steps, Metal's trajectory parts from the real run's as fast as two CUDA runs part from each other.*

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/volume-dark.svg">
  <img alt="Box volume over the unit, real run against the CUDA replay" src="docs/figures/volume-light.svg">
</picture>

*Figure 4. Across the whole unit, a CUDA run with another seed samples the real run's box volume.*

## Accuracy

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/accuracy-dark.svg">
  <img alt="Worst state-test force and energy error per unit against the Reference platform, Metal and CUDA" src="docs/figures/accuracy-light.svg">
</picture>

*Figure 5. Worst state-test errors on four test systems, Metal on both Macs beside CUDA, against FAHBench's limits.*

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/drift-dark.svg">
  <img alt="Energy drift of DHFR in NVE, Metal before and after #5492, CUDA mixed and CPU" src="docs/figures/drift-light.svg">
</picture>

*Figure 6. Energy drift in NVE, 10 runs each: Metal at the OpenMM commit this repo builds can't be told apart from CUDA (Welch t -0.7).*

## Parity with core 0x27

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/parity-dark.svg">
  <img alt="Parity with core 0x27 item by item on the M3 Ultra and the M2" src="docs/figures/parity-light.svg">
</picture>

*Figure 7. Each file and behaviour of core 0x27's run of P18264, checked on each Mac. The M2 last ran 3fc8475, before ceb9707 changed four of the rows.*

Each Mac also passed all 101 core-parity checks, run with the lab's build of minicore before this repo,
and every state test it ran on the real unit: 12 on the M3 Ultra and 5 on the M2. The build in this commit
passed 65 of 65 failure and stop-resume cases on the M3 Ultra, three of them in a second run after fixes
to the test script. The M2 last ran 3fc8475, the build before ceb9707. [docs/PARITY.md](docs/PARITY.md)
lists every behaviour of core 0x27 next to minicore's. The runs and raw records behind every number are
in [docs/EVIDENCE.md](docs/EVIDENCE.md).

## Where it differs

The differences in Figure 7 come from FAH's keys and file formats, the OpenMM version or the hardware.
[docs/PARITY.md](docs/PARITY.md) also lists six behaviours it leaves open, such as four cbang System rows
minicore doesn't print.

- **Signed result header.** It needs FAH's private key, so minicore returns the tar alone and signs nothing.
- **Checkpoint format.** Core 0x27 checks `01/checkpoint` against checkpt.crc, a FAH file format. minicore
  writes the same file name with a length and hash footer instead, and the client reads neither.
- **OpenMM version strings.** minicore builds OpenMM 8.6 with the Metal platform, and core 0x27 uses 8.2.
- **Log wording.** On the M3 Ultra at this commit, 35 of core 0x27's 87 log line shapes match word for
  word. The rest name FAH's build and signature check, the client's arguments, or the Mac's hardware and
  the Metal platform, apart from the open rows in PARITY.md.

## Try it

On an Apple Silicon Mac with macOS 15 or later (tested on macOS 27.2), Xcode or the Command Line Tools,
git and CMake, this builds the core and folds a small test unit:

```
git clone https://github.com/RedesignedRobot/fah-core-metal.git
cd fah-core-metal
./build.sh
cp -R test/alanine-dipeptide unit
./build/core/minicore -dir unit -gpu-vendor apple -gpu 0
```

The core prints nothing to the terminal and exits 100 when the unit is done. Its log is
`unit/logfile_01.txt`, and the result is the tar `unit/wuresults_01.dat`, whose science.log holds the
state tests.

## Build it

`build.sh` fetches OpenMM with the Metal platform at a pinned commit, builds it as static libraries and
links one self-contained executable.

```
./build.sh -j 6 build
./build/core/minicore --info
```

## FAQ

### Can Folding@home use the Apple Silicon GPU?

Not through a core FAH ships today, though people have asked since
[openmm#2489](https://github.com/openmm/openmm/issues/2489) (2019) and
[fah-client-bastet#303](https://github.com/FoldingAtHome/fah-client-bastet/issues/303) (2024). minicore
runs a real FAH unit on Apple Silicon GPUs; the longest run so far is the first 5% of one.

### Does this work with the official Folding@home client?

It takes the arguments fah-client passes a core. fah-client main, changed only to trust a test signing
key, ran the lab's build of minicore against a local stand-in for FAH's servers. It is not an official FAH core, and FAH has not adopted it.

### Which Macs does it run on?

Apple Silicon Macs with macOS 15 or later, since the Metal kernels need Metal Shading Language 3.2. I
have run it on the M3 Ultra and the M2, both on macOS 27.2.

### How fast is it compared to NVIDIA?

On P18264 the M3 Ultra takes 0.87 times the time per 1% that core 0x27 takes on an RTX 5060 Ti. The
Mac's figure is the first 5% of the unit with the lab's build of minicore, and the card's is the whole
unit; one unit, one Mac and one card.

### Is it as accurate as the official core?

Its state-test errors and energy drift sit beside CUDA's, and over the first 60,000 steps its trajectory
parts from core 0x27's as fast as two CUDA runs part from each other.

### Can I fold for Folding@home points with it today?

No. Its results carry no FAH signature, it never connects to FAH's servers, and adoption is up to FAH.

## Credits

- [OpenMM](https://openmm.org), by Peter Eastman and the OpenMM contributors, does the molecular dynamics.
- Peter Eastman's Metal platform for OpenMM, [openmm#5488](https://github.com/openmm/openmm/pull/5488),
  is the GPU backend.
- Andrew Thall's "Extended-Precision Floating-Point Numbers for GPU Computation" (2006) describes the
  double-float arithmetic behind the df64 mixed precision.
- [Folding@home](https://foldingathome.org) defines the work units, the client and core 0x27 that this
  core is checked against. Its FAHBench state tests are the ones minicore runs.

## License

The code in this repo is under the MIT License (`LICENSE`). OpenMM's code keeps OpenMM's licenses and
notices. The patches in `patches/` edit OpenMM's files and carry OpenMM's license for them, and
`build.sh` fetches OpenMM's tree. The Metal platform and the xdrfile code linked into minicore are under
the LGPL 3 or later, Apple's metal-cpp header compiled into it under the Apache License 2.0, and the xtc
writer from OpenMM's tree under the MIT License. `NOTICE` lists each part.

fah-core-metal is not affiliated with or endorsed by Folding@home. It never connects to FAH servers. It
reads a work unit from a directory and writes its result into that directory.

Built and tested with Claude Opus 5.5.

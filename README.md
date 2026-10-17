# fah-core-metal

Folding@home on Apple Silicon GPUs.

People have asked for it since 2019 ([openmm#2489](https://github.com/openmm/openmm/issues/2489)) and
2024 ([fah-client-bastet#303](https://github.com/FoldingAtHome/fah-client-bastet/issues/303)). Here it
is. minicore is a Folding@home core that runs FAH's real OpenMM work units on the Mac's GPU through
OpenMM's Metal platform. It folded a whole real FAH unit faster than FAH's own core on an NVIDIA card.

| | P18264, a whole real FAH unit: 919,221 atoms, 5 ns |
| ---: | --- |
| **5.06 h** | on Apple Silicon, against 5.37 h for FAH's core 0x27 on an NVIDIA RTX 5060 Ti (182 s against 192 s per 1%) |
| **21 of 21** | state tests passed on that unit, as FAH's core passed 21 |
| **Six files** | in the result, in FAH's order with FAH's frame counts; three output files byte-identical to FAH's core's; box volume 9,307.4 against 9,307.1 nm³ |
| **101 of 101** | core-parity checks passed |
| **65 of 65** | failure and stop-resume cases passed |

## Speed

| Machine | Core | Per 1% | Per unit |
| --- | --- | ---: | ---: |
| Apple Silicon, M3 Ultra | minicore, Metal | 182 s | 5.06 h |
| NVIDIA RTX 5060 Ti | FAH's core 0x27, CUDA | 192 s | 5.37 h |

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/speed-dark.svg">
  <img alt="Hours per P18264 unit, Apple Silicon against FAH's core on NVIDIA" src="docs/figures/speed-light.svg">
</picture>

*Hours per whole P18264 unit.*

### Also on NVIDIA

minicore built for CUDA (OpenMM 8.6.1) ran the unit's first 11% on the same card at 180 s per 1%,
against FAH's core's 187 s. On a 5,000-step cut its viewerTop.json and xtcAtoms.csv.bz2 were
byte-identical to FAH's core's. Run conditions are in [EVIDENCE.md](docs/EVIDENCE.md#speed).

## Accuracy

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/accuracy-dark.svg">
  <img alt="Worst state-test force and energy error per test system against the Reference platform, Metal and CUDA" src="docs/figures/accuracy-light.svg">
</picture>

*Worst state-test errors, Metal beside CUDA, far inside FAHBench's limits.*

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/drift-dark.svg">
  <img alt="Energy drift of DHFR in NVE, Metal before and after #5492, CUDA mixed and CPU" src="docs/figures/drift-light.svg">
</picture>

*Energy drift in NVE, 10 runs each, Metal beside CUDA (Welch t -0.7).*

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/distance-dark.svg">
  <img alt="Distance from FAH's trajectory against the distance between two CUDA seeds" src="docs/figures/distance-light.svg">
</picture>

*Metal parts from FAH's trajectory as fast as two CUDA runs part from each other.*

## Parity with FAH's core

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/figures/parity-dark.svg">
  <img alt="Parity with core 0x27 item by item, minicore on Apple Silicon" src="docs/figures/parity-light.svg">
</picture>

Every behaviour FAH's client acts on matches core 0x27. The rest needs FAH's own signing keys or is a
format the client never reads. [docs/PARITY.md](docs/PARITY.md) lists every row.

## Try it

On an Apple Silicon Mac with macOS 15 or later, Xcode or the Command Line Tools, git and CMake:

```sh
git clone https://github.com/RedesignedRobot/fah-core-metal.git
cd fah-core-metal
./build.sh
cp -R test/alanine-dipeptide unit
./build/core/minicore -dir unit -gpu-vendor apple -gpu 0; echo "exit $?"
```

Like any FAH core, the fold logs to `unit/logfile_01.txt`. Exit 100 is FINISHED_UNIT, and the result
is `unit/wuresults_01.dat`.

## Build it

`./build.sh [-j JOBS] [OUT]` fetches OpenMM with the Metal platform at a pinned commit and links
`OUT/core/minicore`, one self-contained executable. OUT, `./build` by default, must not exist:

```sh
rm -rf build
./build.sh
```

## FAQ

### Can Folding@home use the Apple Silicon GPU?

minicore does. It runs FAH's real OpenMM work units on the Mac's GPU.

### How fast is it against NVIDIA?

Faster than FAH's core on an RTX 5060 Ti, 5.06 h against 5.37 h for the whole of P18264.

### Is it as accurate as FAH's core?

Yes. It passed all 21 of the unit's state tests, as FAH's core did, and its errors and drift sit with
CUDA's.

### Can I fold for Folding@home points with it today?

No. FAH signs its cores and their results with its own private key, so that step is FAH's.

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

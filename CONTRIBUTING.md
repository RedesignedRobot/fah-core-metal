# Contributing

Thanks for looking. fah-core-metal has been a one-person project so far, and I'd be glad of help. You
don't need to know OpenMM or Metal to make a difference here.

## What helps most

- **Run reports.** Build it, fold the bundled test unit and post how it went with the
  [run report form](https://github.com/RedesignedRobot/fah-core-metal/issues/new?template=run-report.yml),
  or reply to [Run it on your Mac](https://github.com/RedesignedRobot/fah-core-metal/discussions/1). A
  failure is as useful as a pass. So far it has only run on my own Macs, all on macOS 27.2, so most
  chips and every earlier macOS are new ground.
- **Bug reports with logs.** Use the
  [bug report form](https://github.com/RedesignedRobot/fah-core-metal/issues/new?template=bug-report.yml).
  The log lines it asks for are what let me find the problem.
- **Docs.** If a step in the README or `docs/` was unclear or wrong for you, a fix is welcome.
- **Code fixes with a test.** Say what you ran to check the fix and on which chip.

Questions go to [Discussions Q&A](https://github.com/RedesignedRobot/fah-core-metal/discussions/categories/q-a).
Security problems go through [SECURITY.md](SECURITY.md), not a public issue.

## Two hard rules

1. **Never commit, attach or paste Folding@home work units, results, your F@H ID or your passkey.** Not
   in code, logs, screenshots or issues. Use the bundled test unit in `test/alanine-dipeptide`. The top
   lines of a log hold your Mac's hostname and folder paths, so trim them before you paste.
2. **Nothing that touches FAH's servers, signing or validation.** minicore never connects to FAH's
   servers. It reads a work unit from a directory and writes its result into that directory, and it stays
   that way. I'll close any PR that talks to FAH's servers, fakes, strips or works around FAH's signatures,
   or tries to get results accepted for credit. That step is FAH's.

## Build

You need an Apple Silicon Mac with macOS 15 or later, Xcode or the Command Line Tools (macOS 15 SDK or
later), git, CMake 3.24 or later, and Ninja or make.

```sh
git clone https://github.com/RedesignedRobot/fah-core-metal.git
cd fah-core-metal
./build.sh
```

`./build.sh [-j JOBS] [OUT]` fetches OpenMM with the Metal platform at a pinned commit, applies
`patches/`, builds it as static libraries and links `OUT/core/minicore`. OUT is `./build` by default and
must not exist, so `rm -rf build` before you build again. `OUT/BUILD-INFO` records the commit, patches
and toolchain.

If you only changed files in `src/`, you don't need to rebuild OpenMM. Link a new core against the prefix
you already have, into a directory that doesn't exist yet:

```sh
sh src/link.sh build/prefix build/core-new
```

On Linux x86-64 with an NVIDIA GPU, `./build-linux.sh` builds minicore against conda-forge's OpenMM.

## Test

There's no separate test suite. The test is the bundled unit: 25,000 steps of alanine dipeptide in water,
with a state test against the CPU platform at every checkpoint. Use a fresh copy for each run:

```sh
rm -rf unit
cp -R test/alanine-dipeptide unit
./build/core/minicore -dir unit -gpu-vendor apple -gpu 0; echo "exit $?"
```

It should print `exit 100` (FINISHED_UNIT), which minicore returns only if every state test passed. The
log is `unit/logfile_01.txt`. The state-test lines are in science.log, inside the result:

```sh
tar -xOf unit/wuresults_01.dat science.log | grep -E 'Running tests|All tests passed'
```

Add `-verbose` to the minicore line to get each test's force and energy figures in science.log as well.

To check a change without the GPU, fold the same unit on OpenMM's CPU platform with
`./build/core/minicore -dir unit --platform CPU`. That checks loading, checkpoints, state tests and the
result file, but not Metal. It's what CI runs, because GitHub's macOS runners have no usable Metal GPU.

`test/make-unit.py` rebuilds the test unit from OpenMM's own test PDB. You only need it if you change the
unit, and it needs OpenMM with Python.

## Pull requests

- Branch from `main`, keep one change per PR, and fill in the template: what changed, the commands you
  ran, and the chip you ran them on.
- I review every PR before it merges, and I do the merging. Changes reach `main` only through a PR I've
  approved.
- CI builds every PR that touches more than docs on GitHub's Apple Silicon runners and folds the test unit
  on the CPU platform. For a first-time contributor, it waits for me to approve the run.
- AI-assisted contributions are welcome. Say so in the PR: which tool, what it did, and what you checked
  yourself. This project was built with Claude, so I mean it.
- By opening a PR you agree your contribution is under this repo's MIT License. The files in `patches/`
  edit OpenMM's code and keep OpenMM's licenses.

Everyone here follows the [Code of Conduct](CODE_OF_CONDUCT.md).

# Security

## Reporting a problem

Please report security problems privately, through GitHub's private vulnerability reporting: open the
repository's **Security and quality** tab ([Security](https://github.com/RedesignedRobot/fah-core-metal/security))
and click **Report a vulnerability**. Only I can see the report. Please don't open a public issue or
discussion for it.

Tell me what you ran, on which chip and macOS (or Linux and GPU), and what happened. If you can show it
with the bundled test unit, or a unit you made yourself with `test/make-unit.py`, that's ideal. Never
attach a Folding@home work unit, a result, your F@H ID or your passkey.

I'll reply as soon as I can, work on a fix with you in the advisory's private fork if that helps, and
credit you when it's published, unless you'd rather I didn't.

## What counts

Anything in this repo (minicore, `build.sh`, `build-linux.sh`, `src/link.sh`, the patches and the
workflows) that could:

- **Corrupt results.** A fold that ends in exit 100 with a wrong result, a state test that passes when it
  should fail, a resume from a checkpoint that continues from the wrong state, or a result file that
  doesn't hold what was folded.
- **Crash or harm the host.** A work unit or input file that makes minicore write outside the unit's
  directory, run code, use up memory or disk without limit, or take the Mac down, rather than fail with
  an error and an exit code.
- **Leak paths or IDs.** Anything that puts your paths, hostname, user name, F@H ID or passkey somewhere it
  gets shared or returned. The top lines of `logfile_01.txt` hold the hostname and folder paths, as FAH's
  own core 0x27 writes them. That's known. Anything beyond it counts.
- **Change what the build fetches.** Anything that makes `build.sh` or `build-linux.sh` build from
  something other than what they pin, or lets a pull request's CI run reach secrets or write to the repo.

## What doesn't

- Folding@home's servers, client, signing or credit. Report those to Folding@home. minicore never connects
  to FAH's servers and never checks or writes FAH's signatures.
- A crash with a clear error message and exit code on bad input. That's a bug, so use the
  [bug report form](https://github.com/RedesignedRobot/fah-core-metal/issues/new?template=bug-report.yml).

## Versions

I fix problems on `main` and in the next release.

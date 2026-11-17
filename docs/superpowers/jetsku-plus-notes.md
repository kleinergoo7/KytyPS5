# jetsku-plus branch

Base: Jetsku/KytyPS5 `main` (int21, commit 744705ee). This is the fork with the
Astro Bot device-loss fixes, older-GPU support, native performance overlay,
GoldHEN-style game patches, and PGO build — the combination tested on RX 580-class
hardware.

## Goal

Build from this tree with the genuinely-unique improvements from the old
`astrobot-dynamic-buffer-radv-fix` branch (pushed to kleinergoo7 separately)
cherry-picked on top. Skip improvements Jetsku already has:

Already in Jetsku (DO NOT port):
- `KYTY_LOOP_GUARD`, `KYTY_PS_LIVE_EXEC`, `KYTY_PS_APPEND_LIVE_ELECTION`
- `KYTY_HANG_WATCHDOG` (full 3500-line version, better than ours)
- perf overlay / `DebugCounters` (native `performanceOverlay.cpp`)

Unique to the old branch (candidates to cherry-pick):
- `KYTY_DUMP_ELF` — dump decrypted main-program ELF for byte-patch analysis (eb443fe1)
- 16-bit write-watcher count (9a498762) — Jetsku still has 7-bit with overflow
- CPU stall watcher + recording-worker labels (df0389ac, 9fbc7903, 90ec77d6)
- `run_ds.sh` 30 Hz vblank default for desktop responsiveness (88e3b924)

## Build

Use `./build-linux.sh` (clang/ninja/ccache). Runs use the .zar game dump; for
Astro Bot the best-supported version is PPSA21567 v01.018.000 (or 01.019.000).

# GOAL

Make **PPSA21567 v01.019.000** boot correctly on the **RX 580 (RADV POLARIS10)** from the ROM
format we actually have: the **AMPR zstd repack** at
`/home/reng/disk/Downloads/[ASTRO BOT]-[PPSA21567]/` (`ampr_zstd.index` + 11 `.pak` +
`fakelib/libSceAmpr.sprx`). No unpacking to disk, no llvmpipe, no regressions to the working
old-`.zar` path.

## State
- Virtual AMPR filesystem is built and proven: gray screen gone, indexed files/dirs resolve,
  no fatal/device loss (`jetsku-plus` HEAD `b4c4b6a2`, local commits).
- **Still stuck**: static black loading frame through 210 s, never reaches "PRESS ANY BUTTON".
- Previous goal/evidence: see commit `9e763667` and `.superpowers/sdd/2026-11-18-ampr-pack-vfs/`.

## Next step
Trace what the guest waits on after loading `data/prein/spx` (432 entries): are the `.spx` reads
and APR submissions completing? Compare `/tmp/opencode/ampr-boot/run.log` with the working
`/tmp/opencode/ampr-boot-oldzar/run.log`, then fix that specific blocker.

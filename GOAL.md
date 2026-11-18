# GOAL — MET

**PPSA21567 v01.019.000 boots and gets in-game on the RX 580 (RADV POLARIS10)** from the AMPR zstd
repack at `/home/reng/disk/Downloads/[ASTRO BOT]-[PPSA21567]/` — no unpacking, no llvmpipe, no
regression to the working old-`.zar` path.

## Evidence
- Rendered frames from the packed dump: warp intro at 20/40 s, in-game desert/cinematic at 90 s
  (`/tmp/opencode/final-verify/f_*.png`, 1.3 MB/frame, four different hashes, `fatal=0`,
  `DrawRun 42,640 draws/10s`). User confirmed "it does work and gets in-game".
- Old `.zar` regression: unchanged (title screen, `ampr_pack_tests` fixture guard).
- Load time optimized from **48 s → 5 s** to first rendered frame (`/tmp/opencode/measure-load.sh`,
  independently measured, `fatal=0`).

## What fixed it
- Virtual AMPR filesystem (`Common::AmprPack` + `.amprz` `ArchiveReader` + mount hook) serves all
  `/app0/data/...` files from `ampr_zstd.index` / 11 `.pak` volumes.
- Index-only directories resolved at mount.
- One strong `AmprPack` per game root — index parsed once instead of on every open.
- Packed paths checked in memory before `ResolvePathIgnoringCase` — removed a host directory walk
  per file (the 10x load-time win).

## State
- Branch `jetsku-plus` at `865f90f6` in `/tmp/opencode/jetsku-plus`; all 7 tasks implemented and
  reviewed (no Critical/Important findings). Product commits are **local only** — push to
  `kleinergoo7` only on request.
- Full history: `.superpowers/sdd/2026-11-18-ampr-pack-vfs/progress.md`,
  `docs/superpowers/plans/2026-11-18-ampr-pack-vfs.md`,
  `docs/superpowers/specs/2026-11-18-ampr-pack-vfs-design.md`.

## Boot command
`cd /tmp/opencode/jetsku-plus && bash tools/local/test_ampr_boot.sh` (1600×900, preset env, 30 Hz,
Mailbox, GoldHEN patches). 900p confirmed by window geometry.

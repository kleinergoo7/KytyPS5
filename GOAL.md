# GOAL: Astro Bot running on the RX 580

**Objective:** Make Astro Bot boot and play on the AMD RX 580 (RADV POLARIS10) in KytyPS5,
preferably on the newest dump (PPSA21567 v01.019.000), with no llvmpipe.

## Current state (verified 2026-11-18)

### WORKING — old dump
- Emulator: `/tmp/opencode/jetsku-plus`, branch `jetsku-plus`, HEAD `b4c4b6a2` (local commits; push
  to `kleinergoo7` only when asked).
- ROM: `/mnt/hdd/roms/ps5/ASTRO_BOT.zar` — **PPSA21564 v01.007.000**.
- Settings: 1600×900, `u59-preset.json` env, Mailbox, `--skip-notice-screen true`, `--no-splash`,
  `--printf-direction Silent`. 30 Hz or 60 Hz both work; user tested 60 Hz manually and said
  "works good".
- Result: ASTRO BOT title screen ("PRESS ANY BUTTON") → galaxy map → robot room. `DrawRun`
  ~804,119 draws cumulative, `alive=yes fatal=0`. Screenshots: `/tmp/opencode/oldzar_play/`.

### BLOCKED — new dump (the desired target)
- ROM: `/home/reng/disk/Downloads/[ASTRO BOT]-[PPSA21567]` — **PPSA21567 v01.019.000**.
- It is an **AMPR zstd repack** (`ampr_zstd.index` + 11 `ampr_zstd-*.pak` + `fakelib/libSceAmpr.sprx`),
  not a plain PS5 dump. Kyty only read loose files / `.zar`, so all `/app0/data/...` reads failed
  and the game sat on a static gray screen.
- Built a full virtual filesystem for it (commits `8204aec4`, `e634e183`, `17c31f3b`, `885508e7`,
  `c532f6bb`, `fefc4f00`, `b4c4b6a2`):
  - `Common::AmprPack` parses `ampr_zstd.index` (166,728 files / 733,324 chunks / 11 volumes),
    exact FNV-1a lookup + case-folded fallback, zstd chunk decompression.
  - `.amprz` `ArchiveReader` backend makes virtual paths work through `Common::File` (files +
    index-only directories).
  - `MountPoints::ResolvePath` redirects missing host files/directories into the pack.
  - Unit tests pass (`ampr_pack_tests`), old `.zar` regression passes.
- **Proof the VFS works:** gray rectangle is gone; 99/99 indexed stats succeed; the game's
  `data/prein/spx` directory opens with 432 entries via `.amprz`; no fatal/device loss.
- **Remaining stall:** the game still stays on a static near-black/checkerboard frame through
  210 s and never reaches the title screen. DrawRun stays ~6.7k/10 s (vs 160k/10 s on old dump).
- **Ruled out:** GoldHEN patches on/off; fresh pipeline cache; 30 Hz vs vblank default;
  GPU event-queue timeouts (the *working* old dump has more of them); missing kernel APIs in
  upstream `libAmpr` (direct swap fails to compile — ours is a superset with all 107 NIDs and
  the same upstream fixes); pipeline-cache poisoning; host-file reads (no "APR read missing" lines).
- `libAmpr` resolves some fonts/XML as missing (`SIE-ShinGoPr6N-Heavy.otf`, `FuturaStd-Medium.otf`,
  `data/prompt/prompt.xml`, `Resident.xml`) — present in **neither** index, so likely optional
  probes, not necessarily the blocker.

## Next concrete steps
1. Find what the guest waits on after it finishes reading `data/prein/spx` (last real activity in
   `/tmp/opencode/ampr-boot/run.log`). Compare with old-ROM log at the equivalent point
   (`/tmp/opencode/ampr-boot-oldzar/run.log`).
2. Check whether the 432 `.spx` files the game enumerates are actually read, and whether APR
   submissions for them complete (trace `sceAmprSubmitCommandBuffer` / `sceAmprWaitCommandBuffer`).
3. If the dump is structurally incomplete (the .pak set lacks files the game requires), the only
   fix is a genuine PS5 dump of v01.019.000 or a complete repack.
4. Do NOT break the working old-ROM path. Re-run `bash tools/local/test_ampr_boot.sh
   /mnt/hdd/roms/ps5/ASTRO_BOT.zar` after any change.

## Key paths
- Plan / spec / ledger: `docs/superpowers/plans/2026-11-18-ampr-pack-vfs.md`,
  `docs/superpowers/specs/2026-11-18-ampr-pack-vfs-design.md`,
  `.superpowers/sdd/2026-11-18-ampr-pack-vfs/progress.md`.
- Task reports: `.superpowers/sdd/2026-11-18-ampr-pack-vfs/task-{1,2,3,4,5}-report.md`.
- Boot harness: `/tmp/opencode/jetsku-plus/tools/local/test_ampr_boot.sh`.
- Preset: `/tmp/opencode/u59-preset.json`. Patch JSON:
  `/tmp/opencode/jetsku-plus/docs/superpowers/astro-10190000-patches.json`.
- Subagents/models: `opencode-go/hy4-flash-free`.

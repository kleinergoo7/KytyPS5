# Astro Bot upstream fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the GPU hang (device loss) and enable lower internal render resolution / graphical settings for Astro Bot PPSA21564 v01.007.000 on RX 580, using ports of Jetsku/KytyPS5 U59 and pcportastrobot findings.

**Architecture:** Two independent tracks, progressed by multiple simple subagents:
(A) a GoldHEN-style game-patch JSON applied through the existing `--game-patch` loader, once the three byte patterns are located in v01.007.000; (B) recompiler/emulator ports of Jetsku's GI-probe hang fix and VRAM reducers. Track A can ship a working CLI patch with no C++ change; Track B needs careful ports with review.

**Tech Stack:** C++ (Kyty loader/recompiler), CMake/Ninja via `./build-linux.sh` with ccache, GoldHEN-style mods JSON, `KYTY_*` env switches, GitHub raw API for upstream diffs.

**Spec:** `docs/superpowers/notes/2026-11-17-upstream-repos-research.md`

## Global Constraints

- Do not change the always-miss IMAGE_BVH_INTERSECT_RAY translation, the graceful NGG/GS skip, the ccache build setup, or the DynamicBuffer lowering criteria (only ReadConstBuffer reads whose GetBufferResource arg reaches ValueOpcode::Phi).
- FSR 4 stays user-supplied HelixSR DLLs only; no new dependencies.
- Build: `PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build-linux.sh kyty_emulator kyty_launcher`.
- Game run: `KYTY_GAME=/home/reng/disk/roms/ps5/ASTRO_BOT.zar timeout <N> ./tools/local/run_ds.sh > <log> 2>&1; echo "exit=$?"`.
- Our dump is **PPSA21564 v01.007.000**. All VAs from upstream (PPSA21567 v01.019.000) are wrong and must be re-derived by byte-pattern search in this dump.
- `shader_cfg_tests` ADD_TID abort (exit 134) is pre-existing; no push. Local commits only.
- Model for all subagents: `opencode-go/beam-free` (user directive).

## Review Focus

- A patch JSON with a wrong `id`/`version` is silently ignored — verify the loader logs "Game patch".
- A byte pattern that matches multiple sites could patch the wrong code — require unique match.
- `KYTY_LOOP_GUARD` ported with the wrong hash namespace still would not prove the GI hang — verify the hash matches our log (`0x657ad04626bf9d55`).
- VRAM switches that change rendering could regress image — always capture a screenshot comparison for A/B.
- The game patch must be re-applied on every boot (it patches mapped code) — test two consecutive launches.

---

## Track A — Game patches (offline, parallel-safe investigation)

### Task A1: Locate the three byte patterns in v01.007.000

**Files:** Create: `tools/local/astro-patches/find_patterns.py`; Create: `tools/local/astro-patches/README.md`

**Interfaces:**
- Consumes: the 48 GB `.zar` at `/home/reng/disk/roms/ps5/ASTRO_BOT.zar`, the three pattern byte strings from the spec note.
- Produces: VAs in 01.007.000 for `fixed_1080p`, `non_tiled_lighting`, `gi_probes_off`, or "not present".

- [ ] **Step 1:** Read the spec note's three patterns (fixed_1080p `44037308`, non-tiled `0f84f1080000`, GI `0fb682a0070000`). Find a way to search the dumped eboot binary without full extraction: check if kyty_emulator has a dump/CLI option or if a `.zar` reader exists locally; if none, stream-extract only `eboot.elf` (the file is the first members; use the archive format or a minimal Python parser).
- [ ] **Step 2:** Search for each 6-8 byte pattern; report count + VAs. If a pattern has multiple hits, note the surrounding bytes that disambiguate.
- [ ] **Step 3:** Write a short README documenting how to re-run the search and the results. Commit the script + README (no patch JSON yet).

### Task A2: Write and validate the game-patch JSON

**Files:** Create: `tools/local/astro-patches/astro-01.007.000.json`

**Interfaces:**
- Consumes: VAs from Task A1; `--game-patch` loader format in `src/loader/gamePatch.cpp:68-98`.
- Produces: a valid GoldHEN-style JSON with `id=PPSA21564`, `version=01.007.000`, `process=eboot.elf`, three mods.

- [ ] **Step 1:** Read the exact JSON schema from `gamePatch.cpp`. Write the JSON with the three mods, each `enabled: true`, `memory` entries with `offset`/`off`/`on` as hex strings.
- [ ] **Step 2:** Validate JSON syntax with `python3 -m json.tool`.
- [ ] **Step 3:** Run the game with `--game-patch <json>` for 60 s; confirm the log prints `Game patch:` and the mods are applied (no `invalid mod`/`title mismatch`). Capture the first frame screenshot.
- [ ] **Step 4:** Confirm the dynamic resolution level is 0 (search the log for the patch's effect or compare VRAM/frame time). Record evidence. Commit the JSON with a README note.

### Task A3: A/B the game patches against the GPU hang

**Files:** none (runtime A/B only); Create: `docs/superpowers/notes/2026-11-17-astrobot-patch-ab.md`

- [ ] **Step 1:** Run 320 s with all three mods applied; check for `vkQueueSubmit failed` / `ring gfx timeout`.
- [ ] **Step 2:** Run 320 s with only `gi_probes_off` to isolate which mod stops the hang.
- [ ] **Step 3:** Write the results note with exact commands, exit codes, and log lines. Commit.

## Track B — Recompiler / emulator ports (sequential, reviewed)

### Task B1: Port `KYTY_LOOP_GUARD` diagnostic from Jetsku #231

**Files:** Modify: shader recompiler + graphicsRun (exact files from the upstream commit).

**Interfaces:**
- Consumes: Jetsku commit `b74acf1b` from CHANGE-CATALOG #231.
- Produces: `KYTY_LOOP_GUARD=<n>` and `KYTY_LOOP_GUARD_SHADERS=<hash>` env switches; a flip-time log line "Loop guard: N".

- [ ] **Step 1:** Fetch the upstream diff for #231 via GitHub API and save to the SDD workspace. Identify the minimal subset: loop-guard only (skip PS_LIVE_EXEC and GDS_TRACE).
- [ ] **Step 2:** Apply the minimal port; build.
- [ ] **Step 3:** Run 320 s with `KYTY_LOOP_GUARD=64 KYTY_LOOP_GUARD_SHADERS=657ad04626bf9d55`; check the log for "Loop guard". Commit.
- [ ] **Step 4:** Task reviewer checks spec compliance + code quality.

### Task B2: Port VRAM reducers (`KYTY_VRAM_GC_BUDGET`, `KYTY_FUNCTION_ARRAY_SHRINK`)

**Files:** from upstream commits (identify via GitHub API).

- [ ] **Step 1:** Fetch the two upstream changes; write a port plan to the SDD workspace with exact files.
- [ ] **Step 2:** Implement the smaller one first; build; A/B VRAM at 60 s (`mem_info_vram_used`).
- [ ] **Step 3:** Implement the second; A/B; commit separately.
- [ ] **Step 4:** Task reviewer.

### Task B3: Port the hang watchdog (optional, only if B1 fails)

- [ ] **Step 1:** Fetch `docs/HANG-WATCHDOG.md` implementation files from Jetsku; estimate port size.
- [ ] **Step 2:** If < ~500 lines, port; build; run with `KYTY_HANG_WATCHDOG=1`; confirm `watchdog.txt` appears on stall.
- [ ] **Step 3:** Commit + review.

## Execution method

User directed: multiple subagents, simple tasks. Track A1/A2/A3 are parallel-safe investigations; Track B tasks are sequential with per-task review per subagent-driven-development.

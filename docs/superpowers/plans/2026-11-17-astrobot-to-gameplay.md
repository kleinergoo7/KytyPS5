# Get Astro Bot to actual gameplay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Determine whether Astro Bot PPSA21564 v01.007.000 reaches interactive gameplay on the RX 580, and if not, remove the blocker — producing a clear "in-game / stuck at <X> / blocked by <Y>" conclusion with evidence.

**Architecture:** Measurement-first. Two candidate blockers are already identified from evidence:
(1) the ~5 min GPU device loss (mitigated by `KYTY_LOOP_GUARD=64`), and (2) a ~235 ms GPU-timeline wait per frame with VRAM at 6.4/8 GiB against a 6632 MiB collector threshold — consistent with VRAM-pressure texture thrash. The plan first ESTABLISHES the actual state (is it in-game?), then attacks only the confirmed blocker.

**Tech Stack:** C++ (Kyty renderer/caches), `KYTY_LIVE_FILE` screenshot command, ccache build, `run_ds.sh` with the .zar.

**Spec:** `docs/superpowers/notes/2026-11-17-upstream-repos-research.md`

## Global Constraints
- Runs use the .zar ONLY (`KYTY_GAME=/home/reng/disk/roms/ps5/ASTRO_BOT.zar`), NEVER `/home/reng/disk/roms/ps5/astro/` (very slow).
- Always include the GPU-hang mitigation in runs: `KYTY_LOOP_GUARD=64 KYTY_LOOP_GUARD_SHADERS=657ad04626bf9d55`.
- Build: `PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build-linux.sh kyty_emulator kyty_launcher`.
- Do NOT change IMAGE_BVH_INTERSECT_RAY always-miss, NGG/GS skip, ccache, or DynamicBuffer lowering criteria. No new deps, no push.
- `shader_cfg_tests` exit 134 (ADD_TID) is pre-existing.
- Keep all existing improvements; no reverts.
- Subagents on beam-free (user directive); one writer at a time, simple tasks.

## Review Focus
- A screenshot of a loading/title screen must not be mistaken for gameplay — verify with multiple frames + input.
- The loop guard changes shader results; never conclude "rendering correct" from a guarded run alone.
- VRAM numbers must be measured, not assumed — compare `mem_info_vram_used` with/without a candidate fix.

---

### Task G1: Establish the actual state — screenshot + input probe (read-only run)

- [ ] **Step 1:** Run 120 s with loop guard + a `KYTY_LIVE_FILE` command file that requests `screenshot /tmp/opencode/astro-screen.ppm` at ~30 s and ~90 s, and `flipfile` if available. Command format: `id command args` — read `tools/local/deck/decklive.py` or `live-control.h:843` for exact file format and how answers appear in the log.
- [ ] **Step 2:** Convert the PPM(s) to PNG (`python3` or `convert`) and view them. Determine: title screen / loading / actual 3D gameplay?
- [ ] **Step 3:** Send a controller input (via the live vpad command `vpaddown`/`vpad` — see live-control.h for exact syntax) and re-screenshot to see if the screen changes. Conclusion: responsive or frozen.
- [ ] **Step 4:** Write a findings note to `docs/superpowers/notes/2026-11-17-astrobot-state.md` with the screenshots (or paths) and the conclusion. No code change.

### Task G2: If stuck in loading/VRAM thrash — measure and apply a VRAM fix

**Only proceed if G1 confirms loading/stall, not gameplay.**

- [ ] **Step 1:** Sample `mem_info_vram_used` every 5 s over a 120 s run and correlate with the `SLOW Frame` / `download_wait` lines. Confirm whether the 235 ms wait tracks VRAM near the 6632 MiB collector threshold.
- [ ] **Step 2:** A/B the existing `KYTY_TEXTURE_HOST_AT` / texture-cache host-placement switches (already in our tree, `textureCache.cpp:346`) to force more textures into host memory: try `KYTY_TEXTURE_HOST_AT=0.25` vs default. Measure frame time + VRAM.
- [ ] **Step 3:** If that helps, consider porting Jetsku's `KYTY_VRAM_GC_BUDGET` (diff at `/tmp/opencode/vram-gc.diff`, large port — separate task with its own review). Only if the A/B proves VRAM is the blocker.
- [ ] **Step 4:** Record the result; commit only a verified improvement.

### Task G3: If it IS in gameplay but slow — performance triage

**Only proceed if G1 confirms gameplay.**

- [ ] **Step 1:** Use the new debug panel (F2 / `KYTY_DEBUG_PANEL=1`) to capture draws/s, dispatches/s, submits/s, GPU waits during gameplay. Record numbers.
- [ ] **Step 2:** A/B `KYTY_VBLANK` (60 vs 30) and `PRESENT_MODE` (Fifo vs Mailbox) for GPU headroom vs desktop responsiveness.
- [ ] **Step 3:** Record the measured fps and the dominant cost; decide if further optimization is warranted.

### Task G4: Final report

- [ ] **Step 1:** Write `docs/superpowers/notes/2026-11-17-astrobot-gameplay-verdict.md`: is the game playable, at what fps, with which env vars, and what still blocks it.
- [ ] **Step 2:** Commit the verdict note. No push.

## Execution method
Subagent-driven development: one simple task per subagent, reviewer after each. G1 is a read-only run (no writer); G2/G3 modify only after G1's verdict is committed.

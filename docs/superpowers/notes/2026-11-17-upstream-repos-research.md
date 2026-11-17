# Research: Jetsku/KytyPS5 + pcportastrobot (2026-11-17)

Both repos checked for (a) optimizations, (b) lower rendered resolution, (c) lower graphical settings,
and (d) the current GPU-hang blocker.

## Critical match: our GPU hang is a known Astro Bot GI-probe hang on AMD

Jetsku's EXPERIMENTAL.md: "On AMD GPUs with the two patch mods off, the GPU can be lost
(VK_ERROR_DEVICE_LOST) ... AMD players should keep the patch mods on or use int15.1."

CHANGE-CATALOG.md #231 (root cause): the game's GI ray-bundle pass builds per-pixel fragment
linked lists with a **pixel-shader DS_APPEND on a GDS counter**. Kyty's pixel EXEC includes helper
invocations, so the append can elect a helper lane; Vulkan discards a helper's atomic, the subgroup
gets an undefined base, duplicate node indices make the lists cyclic, and
`GfxGiProbeTraceLinkedList (0x657ad04626bf9d55)` walks them forever -> device loss.

Our log confirms `0x657ad04626bf9d55`:
`/tmp/opencode/astro-30-320.log:460: Table: CS 0x657ad04626bf9d55: table mode refused ... an image the shader writes`

Portable recompiler-level fixes (Jetsku #231, all default-off switches):
- `KYTY_PS_APPEND_LIVE_ELECTION=1` — DS_APPEND/DS_CONSUME elects a non-helper lane.
- `KYTY_PS_LIVE_EXEC=1|all` — pixel shaders with DS_APPEND/DS_CONSUME start EXEC = non-helper
  invocations (PS5 pixel valid mask); `S_WQM_B64` adds helpers back. Touches SpirvEmitter + IR
  `IsHelperInvocation` op — a real port, not trivial.
- `KYTY_LOOP_GUARD=<n>` + `KYTY_LOOP_GUARD_SHADERS=<hash>` — diagnostic loop budget per shader;
  would confirm the infinite-walk hypothesis cheaply (reported at flips as "Loop guard: N").

None of these switches exist in our tree. `KYTY_LOOP_GUARD` is the cheapest first port (diagnostic).

## Lower rendered resolution / lower graphical settings (pcportastrobot game-patches)

Three GoldHEN-style byte patches in `Source/game-patches/` for **PPSA21567 v01.019.000**:
- `fixed_1080p.toml` — dynamic-resolution level forced to 0 = 1920x1080 (levels: 0=1080p,
  1-2=2432x1368, 3=3328x1872, 4=3840x2160). Patch at va 0x73e5ab1. Rationale: emulated
  end-of-pipe clocks make the game measure the translator and stay at 4K.
- `non_tiled_lighting.toml` — selects the game's existing non-tiled deferred-lighting renderer
  (va 0x73b4522). Lower GPU cost + stability on AMD.
- `gi_probes_off.toml` — disables GI probes and their lighting shaders (va 0x70ce313).
  Lower GPU cost + the AMD device-loss workaround.

**Version mismatch:** our dump is **PPSA21564 v01.007.000** (log line 24). The VAs above are for
PPSA21567 v01.019.000 — they will NOT apply directly. Need to reverse-engineer equivalent sites in
01.007.000 (byte patterns may still match, like the community 01.018->01.019 ports did).

## Our tree already has the patch application mechanism

`src/loader/gamePatch.cpp` + `--game-patch <json>` (ETAHen/GoldHEN-style mods JSON). Format:
`{"id":"TITLE_ID","version":"APP_VER","process":"eboot.elf","mods":[{"name":...,
"enabled":true,"memory":[{"offset":"HEX","off":"HEX","on":"HEX"}]}]}`.
It validates title_id + version against `SystemContentParamSfoGetString` before applying — so a
patch file for 01.007.000 with the found offsets will be accepted. No patch JSON is currently shipped.

## portable optimizations (Jetsku U59 / pcportastrobot, all opt-in env switches)

Jetsku U59 (our tree is older; most are missing — verify before porting):
- `KYTY_VRAM_GC_BUDGET` — VRAM collected against live budget: RTX 3090 Sky Garden 6.3 GB instead
  of 9.4 GB, peak 7.5 instead of 12.4, no fps change. Directly relevant to our 8 GB card.
- `KYTY_FUNCTION_ARRAY_SHRINK` — shrink unreachable shader arrays before the driver sees them
  (32-224 dwords in Astro Bot). Lower VRAM.
- `KYTY_RAM_SMALL_UPLOAD_RING` — ~430-540 MB less RAM, ~385 MB less shared GPU mem, 60-150 MB
  less VRAM.
- `KYTY_DRAW_PREP_PRIORITY=1` + `KYTY_DRAW_PREP_SPIN_US=10` + `KYTY_DRAW_PREP_COLD_SPIN_US=5`
  (pcportastrobot HYBRID_TUNING, measured +4.17% mean fps, +12.37% 1% low on their PC).
- `KYTY_ASTRO_CB_SCALE=2` (default in Jetsku) — doubles Astro Bot draw-thread command buffers to
  fix "too many objects/batches" for 01.018; game-logged as "Game patch: Astro Bot draw-thread
  command buffers x2".
- `KYTY_SUBMIT_MIN_INTERVAL_US`, `KYTY_IDLE_FLUSH_DRAWS`, `KYTY_GFX_SLICE_DRAWS`,
  `APS5_PIPELINED_DRAWS=64` (their hand-off queue 8->64: +12% world map).
- Hang watchdog `KYTY_HANG_WATCHDOG=1` + `docs/HANG-WATCHDOG.md` — would dump all thread waits,
  PM4 packets, shader hashes on a 5 s stall; much better than RADV's empty device-fault report.
  Not in our tree; sizeable but high diagnostic value for this GPU hang.

pcportastrobot PERFORMANCE.md caveat: they are **CPU-limited** (~4900 draws/frame, GPU headroom),
so lowering resolution did not raise fps there. Our RX 580 run showed GPU at 97-100% busy, so the
resolution lever is more promising for us — but the game registers a fixed 3840x2160 swapchain and
scales internally, so window size (--screen-width) does nothing; only the dynamic-resolution patch
would change the internal render target.

## Recommended next steps (cheapest first)

1. Port `KYTY_LOOP_GUARD` (+ shader hash filter) from Jetsku #231 — cheap diagnostic that can
   confirm the infinite GI linked-list walk with hash 0x657ad04626bf9d55.
2. Reverse the three byte patterns in our 01.007.000 eboot and write a GoldHEN-style patch JSON;
   apply via existing `--game-patch`. This both lowers GPU cost and may stop the device loss.
3. Port `KYTY_VRAM_GC_BUDGET` / `KYTY_FUNCTION_ARRAY_SHRINK` for the 8 GB card.
4. Consider porting the hang watchdog for future hangs.

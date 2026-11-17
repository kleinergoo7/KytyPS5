# Debug Counters Performance Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Port upstream KytyPS5/KytyPS5's `DebugCounters` instrumentation + a minimal ImGui performance panel into our tree, enabled by `KYTY_DEBUG_PANEL=1` / F2, so GPU-hang and perf debugging has per-subsystem counters (draws, dispatches, submits, GPU waits/faults, shader/pipeline compile times).

**Architecture:** Header-only lock-free counter system (copied from upstream) + instrumentation at existing function anchors in our tree (function-level, not line-exact) + a new small ImGui panel rendered through the existing SystemOverlay, reusing the existing ImGui/font setup. Deliberately EXCLUDE upstream's macOS-only `HostStats` GPU rows (empty on Linux/RX580) and the full 895-line overlay — our existing `KYTY_FPS_HUD` already covers frame times/VRAM/CPU.

**Tech Stack:** C++ headers/atomics, ImGui (already in tree), ccache build via `./build-linux.sh`.

**Spec:** `docs/superpowers/notes/2026-11-17-upstream-repos-research.md` + upstream files in `/tmp/opencode/upstream-perf/` (debugCounters.h, performanceOverlay.cpp).

## Global Constraints
- Do NOT change IMAGE_BVH_INTERSECT_RAY translation, NGG/GS skip, ccache build, or DynamicBuffer lowering criteria. No new dependencies, no push.
- Build: `PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build-linux.sh kyty_emulator kyty_launcher`
- Run: `KYTY_GAME=/home/reng/disk/roms/ps5/ASTRO_BOT.zar timeout <N> ./tools/local/run_ds.sh ...` (NEVER the extracted dump).
- Counters must be default-off (zero overhead when panel hidden): `DebugCounters::Add` already early-returns when `!Enabled()`.
- Do NOT touch B1/B2 files (loop guard / append-election) until B2 lands.
- Subagents on beam-free (user directive).

## Review Focus
- A counter instrumented at the wrong function could double-count or miss — verify each Add sits once on the real path.
- Disabled counters must cost ~nothing — verify `Add` early-return and no timers constructed when disabled.
- The panel must not change rendering — compare screenshots with panel off/on.
- F2 hotkey must not conflict with existing input (F8 cycles the old HUD).

---

### Task P1: Add header-only DebugCounters + wire a minimal panel to read/print them

**Files:**
- Create: `src/common/debugCounters.h` (copy from /tmp/opencode/upstream-perf/debugCounters.h, keep only counters we instrument; no new deps)
- Create: `src/graphics/presentation/debugPanel.h` / `.cpp` (minimal ImGui panel; functions `DrawDebugPanel(GraphicContext&, extent)`, `Toggle()`, `Enabled()`)
- Modify: `src/graphics/presentation/systemOverlay.h/.cpp` — call `DrawDebugPanel` when enabled (find the existing ImGui draw block used by the FPS HUD)
- Modify: `src/main.cpp` or `src/local/perf-overlay.cpp` — read `KYTY_DEBUG_PANEL`, call `DebugCounters::SetEnabled` + `DebugPanel::SetEnabled`
- Modify: `src/graphics/presentation/window/window.cpp` — F2 hotkey to toggle (avoid F8 which cycles old HUD)

**Interfaces:**
- Consumes: `DebugCounters::Add/Get/ScopedTimer` (header), existing `SystemOverlay` ImGui context.
- Produces: `DebugPanel::Draw`, `DebugPanel::Toggle`, `DebugPanel::Enabled`.

- [ ] **Step 1:** Copy `debugCounters.h`; trim the `Counter` enum to the ones we instrument below (keep Draws, Dispatches, QueueSubmits, GpuWaits, GpuWaitNs, GpuFaults, ShadersCompiled, ShaderCompileNs, PipelinesCreated, PipelineCreateNs, FlipsRequested, LateVblanks, DroppedPresents, GpuThreadBusyNs, PresentNs). Build to confirm header compiles.
- [ ] **Step 2:** Implement `debugPanel.cpp`: read all counters, compute deltas/rates once per second (upstream `ReadCounters`/`Refresh` pattern), render with `ImGui::Text`/`TextColored` in a top-right window. No new fonts/textures. Show fps from existing `PerfOverlay::Summarize(1.0)` if easy; otherwise just counts/rates.
- [ ] **Step 3:** Wire into `systemOverlay.cpp` draw path next to the existing overlay; gate on `DebugPanel::Enabled()`. Add F2 toggle in `window.cpp` and `KYTY_DEBUG_PANEL` env read. Build.
- [ ] **Step 4:** Run 60 s with `KYTY_DEBUG_PANEL=1`; confirm panel visible (grep log for any init line or take a screenshot via existing capture) and counters non-zero (Draws/Dispatches/QueueSubmits). Run with it off; confirm no panel and no errors.
- [ ] **Step 5:** Commit `feat(debug): add DebugCounters + minimal ImGui debug panel (F2, KYTY_DEBUG_PANEL)`. Task review.

### Task P2: Instrument the counters at our-tree anchors

**Files (one task, many small edits — same subsystem: counters):**
- `src/graphics/host_gpu/renderer/renderDraw.cpp` (ExecutePreparedDraw, ~1440): `Add(Draws)` per draw.
- `src/graphics/host_gpu/renderer/renderCompute.cpp` (~710, direct + indirect dispatch): `Add(Dispatches)`.
- `src/graphics/host_gpu/renderer/commandScheduler.cpp` (~740, Submit): `Add(QueueSubmits)`.
- `src/graphics/host_gpu/renderer/masterSemaphore.cpp` (~114, Wait): `ScopedTimer(GpuWaits, GpuWaitNs)` / `Add(GpuWaits)`.
- `src/loader/runtimeLinker.cpp` (~879, fault handler GPU path): `Add(GpuFaults)`.
- `src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp` (~1727 compile, ~4542/4873 pipelines): `ScopedTimer(ShadersCompiled, ShaderCompileNs, Peak::ShaderCompileNs)` and PipelinesCreated/Ns.
- `src/graphics/presentation/videoOut.cpp` (~1055 Reserve, ~893 PresentThread): `Add(FlipsRequested)`, `Add(LateVblanks)`.
- `src/graphics/presentation/window/swapchain.cpp` (~2113 retry exhausted, ~1894 PresentLoop): `Add(DroppedPresents)`, `ScopedTimer(PresentNs)`.
- `src/graphics/guest_gpu/graphicsRun.cpp` (~1174 ThreadRun): `ScopedTimer(GpuThreadBusyNs)`.

- [ ] **Step 1:** Add `DebugCounters::Add/ScopedTimer` at each anchor. Each edit is one line; verify each site is on the actual hot path (read 5 lines of context). Do not instrument both a wrapper and its callee for the same counter.
- [ ] **Step 2:** Build.
- [ ] **Step 3:** Run 60 s `KYTY_DEBUG_PANEL=1`; confirm counters increment (Draws/Dispatches/Submits non-zero, GPU waits present). Compare with panel off: no functional change.
- [ ] **Step 4:** Commit `feat(debug): instrument draw/dispatch/submit/compile counters`. Task review.

### Task P3: Optional — add cache + audio/IO counters after P2 is validated
Only if P1/P2 are clean and more detail is wanted. Not required for the debugging goal.

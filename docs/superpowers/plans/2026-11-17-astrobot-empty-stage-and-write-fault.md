# Astro Bot: empty shader stage drop + GPU write-fault diagnosis

**Context:** Astro Bot (PPSA21564) on RX 580 (RADV POLARIS10) now gets past the
lowered dynamic-buffer shader and the 0x300000020 GPU write fault (direct
memory raised to 16 GiB). It aborts at
`EXIT_IF(!runtime)` in `RenderExecutor::PrepareBindingsInto`
(`src/graphics/host_gpu/renderer/pipeline/descriptors.cpp:1211`) after
`Warning: NGG/GS draw skipped`. `ShaderStageRuntime::operator bool` is
`program != nullptr` (`src/graphics/shader/shader.h:46`).

The empty-stage drop follows the existing graceful NGG/GS skip decision:
a draw the host cannot build a pipeline for is dropped, not fatal.

## Global Constraints

- Do not change the always-miss `IMAGE_BVH_INTERSECT_RAY` translation, the
  graceful NGG/GS skip in `pipelineCache.cpp`, or the ccache build setup.
- Do not broaden the DynamicBuffer lowering criteria (only `ReadConstBuffer`
  reads whose `GetBufferResource` argument reaches `ValueOpcode::Phi`).
- No new dependencies. No llvmpipe. FSR 4 stays user-supplied HelixSR DLLs.
- Build: `PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build-linux.sh kyty_emulator kyty_launcher`
  (ccache; never disable it).
- Game run: `KYTY_GAME=/home/reng/disk/roms/ps5/ASTRO_BOT.zar timeout <N> ./tools/local/run_ds.sh > <log> 2>&1; echo "exit=$?"`
- `shader_cfg_tests` failing with `ADD_TID did not terminate requirements analysis`
  (exit 134) is pre-existing and not a regression.
- Do not push. Local commits only.
- Keep `EXIT_IF(!runtime)` in `PrepareBindingsInto`: it is a valid invariant.
  The fix belongs at the caller that must drop the draw.

## Task 1 — Drop draws with an empty shader-stage runtime

**Files:**
- Modify: `src/graphics/host_gpu/renderer/renderDraw.cpp` (guard before
  `PrepareGraphicsBindingsInto` in `ExecutePreparedDraw`, around line 1485)
- Modify only if the diagnosis proves it necessary:
  `src/graphics/host_gpu/renderer/pipeline/descriptors.cpp`

**Interfaces:**
- Consumes: `ShaderStageRuntime` (`program` pointer, `operator bool`),
  `DrawRenderState::vs_input_info.stage` / `ps_input_info.stage` /
  `ps_active`, `GraphicsPrograms`.
- Produces: a dropped draw (`return true`) when the vertex stage program is
  missing, or when `ps_active` is set but the pixel stage program is missing.
  Same outcome as the existing mesh/NGG skip: no pipeline, no binding
  preparation, no crash.

- [ ] **Step 1: Reproduce and identify the empty stage**

Run the game for 120 s and confirm the abort:
`grep -n "NGG/GS draw skipped\|condition (!runtime)\|descriptors.cpp:1211" <log>`.
Then read `ExecutePreparedDraw` and `PrepareGraphicsBindingsInto` and state
in the report which stage has `program == nullptr` and why (the existing
`mesh_active && !state.programs.vertex` guard does not cover a null stage
program).

- [ ] **Step 2: Add the minimal guard**

In `ExecutePreparedDraw`, before `PrepareGraphicsBindingsInto`, drop the draw
when the vertex stage program is null, or when `state.ps_active` is true and
the pixel stage program is null. One condition, one `return true`, with a
one-line comment saying why. Do not restructure binding preparation and do
not touch `PrepareBindingsInto`'s `EXIT_IF`.

- [ ] **Step 3: Build**

Run the build command above. Expected: build succeeds.

- [ ] **Step 4: Runtime verification**

Run the game for 180 s into `/tmp/opencode/astro-task1.log`.
Expected: no `condition (!runtime)` abort, exit is not the descriptor abort
(a later blocker or a clean timeout is acceptable). Record the exact next
blocker from the log — do not guess.

- [ ] **Step 5: Regression check**

Run:
`PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build-linux.sh shader_cfg_tests >/tmp/opencode/build-tests-task1.log 2>&1 && ./_Build/linux/shader_cfg_tests >/tmp/opencode/run-tests-task1.log 2>&1; echo "exit=$?"`
Expected: only the pre-existing ADD_TID abort (exit 134), no new failure.

- [ ] **Step 6: Commit**

```bash
git add src/graphics/host_gpu/renderer/renderDraw.cpp
git commit -m "fix(renderer): drop draws with an empty shader stage runtime"
```

Expected: commit contains only the guard (plus a test if one was practical).

## Task 2 — Diagnose the 0x300000020 GPU write

**Files:**
- Read-only: `src/graphics/shader/recompiler/ir/passes/DynamicBuffer.cpp`,
  `src/graphics/host_gpu/renderer/cache/bufferCache.cpp`,
  `src/kernel/memory.cpp`, `/tmp/opencode/503d7f.final.ir`,
  `/tmp/opencode/astro-extmem.log`
- Create: `docs/superpowers/notes/2026-11-17-astrobot-write-fault.md`

**Interfaces:**
- Consumes: the existing IR/SPIR-V dumps and the 16 GiB run logs.
- Produces: a written diagnosis with ranked hypotheses, the evidence for and
  against each, and one concrete, minimal next experiment. No production code
  change in this task.

- [ ] **Step 1: Re-check the fault shape**

From `/tmp/opencode/astro-extmem.log`, quote the fault line (guest PC,
access, vaddr) and confirm access is a write. Confirm the lowered shader IR
contains no global store to that address (only `SharedAtomicIAdd32` and
`OpImageWrite`).

- [ ] **Step 2: Check the BDA/direct-memory boundary**

Determine what the guest does with `0x300000020`: is it a BDA-table lookup
that missed, a buffer descriptor base, or a driver/JIT scratch address?
Check `GetBdaPointer` coverage and the `ReportedDirectMemorySize` accounting
in `src/kernel/memory.cpp` (16 GiB bump, `KYTY_DIRECT_MEMORY_MB` override).

- [ ] **Step 3: Rank hypotheses and name one experiment**

Write the note with: (a) ranked hypotheses, (b) evidence for/against each,
(c) the single cheapest experiment that would confirm or kill the top
hypothesis, including the exact command/env var and the log line to look for.

- [ ] **Step 4: Commit the note**

```bash
git add docs/superpowers/notes/2026-11-17-astrobot-write-fault.md
git commit -m "docs: diagnose Astro Bot 0x300000020 GPU write"
```

Expected: note committed; no production code changed.

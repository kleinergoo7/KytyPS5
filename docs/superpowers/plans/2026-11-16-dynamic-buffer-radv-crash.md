# Dynamic Buffer Lowering RADV Crash Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the ported `LowerDynamicBufferReads` pass produce a shader that compiles and runs on RADV/Polaris (RX 580) so Astro Bot progresses past the current SIGSEGV instead of crashing in `libvulkan_radeon.so`.

**Architecture:** Keep the existing lowering strategy (replace loop-carried `ReadConstBuffer(GetBufferResource(phi,...))` with a raw BDA `LoadAddressU32` computed from the V# base+offset) but make its IR/SPIR-V valid for this backend/driver. Diagnose using the captured artifacts for `hash=0x503d7f066a496c3c` (`/tmp/opencode/503d7f.before.ir`, `/tmp/opencode/503d7f.after.ir`, `/tmp/opencode/503d7f.spv`, `/tmp/opencode/503d7f.spvasm`) and fix the smallest cause: either the lowered IR shape/pass ordering (so emission stays structured) or the specific SPIR-V structuring/emission issue exposed by it. Do not redesign BVH/mesh handling.

**Tech Stack:** C++ (Kyty shader recompiler IR/passes, SPIR-V emitter), CMake/Ninja via `./build-linux.sh` with ccache, `spirv-val`/`spirv-dis` (linuxbrew), `tools/local/run_ds.sh` with `KYTY_GAME=/home/reng/disk/roms/ps5/ASTRO_BOT.zar`.

**Spec:** No separate spec file. Binding context: `DynamicBuffer.cpp` lowers only `ReadConstBuffer` reads whose `GetBufferResource` arg reaches `ValueOpcode::Phi` (PR #558 semantics); pass runs before `IR::TrackResources(ir)` in `ShaderRecompiler.cpp`. Current blocker: `Unhandled host exception: type=1 code=11 ... access=2 address=0x0000000000000040` in `libvulkan_radeon.so` (RADV POLARIS10), `exit 65`, after lowered compute pipeline creation (`/tmp/opencode/astro-dce.log`). `spirv-val` reports one error for the lowered shader: `Selection must be structured` at `OpBranchConditional %10000 %180 %117` (`503d7f.spvasm:11653`, block `%116` has no `OpSelectionMerge` under loop header `%115`). A/B: disabling the transform reverts to `ResourceTracking.cpp:183` on the same hash.

## Global Constraints

- Do not change the always-miss `IMAGE_BVH_INTERSECT_RAY` translation.
- Do not change the graceful NGG/GS draw skip or ccache build changes.
- `LowerDynamicBufferReads` must only fire for `ReadConstBuffer` reads whose `GetBufferResource` arg reaches `ValueOpcode::Phi`; other shaders must compile identically.
- Use `./build-linux.sh` for builds and `KYTY_GAME=/home/reng/disk/roms/ps5/ASTRO_BOT.zar timeout 180 ./tools/local/run_ds.sh` for runtime checks; report the exact next blocker, do not guess.
- RX 580 (RADV, GFX8/Polaris) has no `VK_EXT_mesh_shader` and no fragment shader barycentrics; do not depend on them.
- Keep `ResourceKind::Flat`/BDA full-address semantics correct (`address_is_full=true`, 48-bit V# base = dword0 | (dword1 & 0xffff)<<32, carry on base+offset) unless evidence shows a different form is required.

## Review Focus

- Lowering applies to non-phi (constant/SRT) buffer descriptors or to the wrong instruction/block, corrupting unrelated reads (compare lowered sites against phi-reaching `GetBufferResource` dwords in `/tmp/opencode/503d7f.before.ir`).
- Emitted SPIR-V for `0x503d7f066a496c3c` remains structurally invalid (`spirv-val` nonzero) or the RADV SIGSEGV at `address=0x40` persists with the same signature.
- Lowered address math is wrong for 48-bit carry/offset (base_hi mask, wrap/carry, `immediate` offset from `memory_info`) causing bad device addresses.
- `uses_dma`/BDA capability plumbing or memory metadata index handling breaks tracking/emission for the new `LoadAddressU32` (e.g. stale/duplicate memory_info usage).
- Regressions in existing recompiler behavior: `shader_cfg_tests` failure signature must remain the pre-existing `graphics buffer ADD_TID did not terminate requirements analysis` (exit 134), not a new failure.

## Tasks

### Task 1 — Reproduce and pin the invalid-SPIR-V cause

**Files:**
- Read: `src/graphics/shader/recompiler/ShaderRecompiler.cpp`
- Read: `src/graphics/shader/recompiler/ir/passes/DynamicBuffer.cpp`
- Read: `src/graphics/shader/recompiler/backend/spirv/*` (structuring/control-flow emission relevant to `OpSelectionMerge`/`OpLoopMerge`)
- Read/Artifacts: `/tmp/opencode/503d7f.before.ir`, `/tmp/opencode/503d7f.after.ir`, `/tmp/opencode/503d7f.spv`, `/tmp/opencode/503d7f.spvasm`, `/tmp/opencode/astro-dce.log`

**Interfaces:**
- Consumes: current integrated lowering in `DynamicBuffer.cpp` and call site in `ShaderRecompiler.cpp`; captured before/after IR and SPIR-V for `0x503d7f066a496c3c`.
- Produces: a short written diagnosis (in commit message or a note in plan workspace if executing via SDD) stating whether block `%116`’s missing selection merge is (a) introduced by the IR delta from lowering, (b) fixed by IR/pass-order/cleanup changes, or (c) an emitter structuring bug exposed by this shader, with the exact code location(s) to change.

- [ ] **Step 1: Confirm baseline artifacts and failure**

Run: `spirv-val /tmp/opencode/503d7f.spv; echo "---"; grep -n "Unhandled host exception" /tmp/opencode/astro-dce.log | head -2`  
Expected: `spirv-val` reports `Selection must be structured` for `OpBranchConditional %10000 %180 %117`; log shows RADV SIGSEGV `access=2 address=0x0000000000000040`, `exit 65`.

- [ ] **Step 2: Map lowered sites to IR delta**

Run: `grep -n "GetAddressResource\|LoadAddressU32" /tmp/opencode/503d7f.after.ir | grep -v '%15, %16' | head -20` and inspect the corresponding removed `ReadConstBuffer` regions in a unified diff of `/tmp/opencode/503d7f.before.ir` vs `/tmp/opencode/503d7f.after.ir`.  
Expected: lowered loads correspond only to the phi-carried V# reads (e.g. V# dwords `%7321/%7386|%7384/%7366` region around pc `0x2168+`), 4 reads lowered, no constant-descriptor reads altered.

- [ ] **Step 3: Determine cause of missing `OpSelectionMerge` at `%116`**

Inspect `503d7f.spvasm` around lines 11600-11670 and trace block `%116`/`%115` back through the SPIR-V emitter/CFG structuring for this shader; correlate with the IR blocks changed by lowering (renumbering-only vs structural). If feasible, test a minimal IR-side mitigation (e.g. running `ConstantPropagationPass`/`ResolveControlFlowIdentities`/`RemoveIdentities`/`EliminateDeadCode` after lowering, or adjusting insertion) by rebuilding and re-dumping SPIR-V for the hash, then `spirv-val`.  
Expected: identify a concrete, minimal fix point (pass ordering/cleanup, insertion, or emitter merge emission) or record that emitter structuring must be corrected for this pattern.

- [ ] **Step 4: Record diagnosis**

Write the diagnosis (what is invalid, why, and chosen fix) into the implementation notes/commit for Task 2.  
Expected: a single unambiguous fix direction.

### Task 2 — Implement the minimal fix and validate

**Files:**
- Modify: `src/graphics/shader/recompiler/ShaderRecompiler.cpp` and/or `src/graphics/shader/recompiler/ir/passes/DynamicBuffer.cpp` and/or the specific SPIR-V emitter/structuring file(s) identified in Task 1
- Test (only if a small, deterministic unit test is practical): `tests/shaderCfgTests.cpp`

**Interfaces:**
- Consumes: Task 1 diagnosis and exact fix location.
- Produces: `spirv-val`-clean SPIR-V for `0x503d7f066a496c3c` and an Astro Bot run that no longer SIGSEGVs in RADV at `access=2 address=0x40` for this lowered shader (or a clearly advanced/different blocker).

- [ ] **Step 1: Apply the smallest fix indicated by Task 1**

Implement the chosen change (pass-order/cleanup, insertion/order, or emitter selection-merge structuring) limited to the diagnosed cause. Do not broaden lowering criteria.  
Expected: code compiles; change is localized.

- [ ] **Step 2: Build**

Run: `PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build-linux.sh kyty_emulator kyty_launcher`  
Expected: build succeeds (ccache).

- [ ] **Step 3: Re-dump and validate SPIR-V for the target shader**

Run the game with a temporary dump of `0x503d7f066a496c3c` SPIR-V to `/tmp/opencode/503d7f.fixed.spv` (remove any temporary dump code after), then `spirv-dis /tmp/opencode/503d7f.fixed.spv -o /tmp/opencode/503d7f.fixed.spvasm && spirv-val /tmp/opencode/503d7f.fixed.spv`.  
Expected: `spirv-val` reports no errors (especially no `Selection must be structured`).

- [ ] **Step 4: Runtime verification**

Run: `KYTY_GAME=/home/reng/disk/roms/ps5/ASTRO_BOT.zar timeout 180 ./tools/local/run_ds.sh > /tmp/opencode/astro-fixed.log 2>&1; echo "exit=$?"` then `grep -n "dynamic buffer read lowering\|Unhandled host exception\|ResourceTracking.cpp:183\|address=0x0000000000000040" /tmp/opencode/astro-fixed.log | head -20`.  
Expected: no RADV SIGSEGV at `address=0x40` caused by this shader and no `ResourceTracking.cpp:183` abort for `0x503d7f066a496c3c`; record the exact next blocker if any (do not guess).

- [ ] **Step 5: Regression check**

Run: `PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build-linux.sh shader_cfg_tests >/tmp/opencode/build-tests-fixed.log 2>&1 && ./_Build/linux/shader_cfg_tests >/tmp/opencode/run-tests-fixed.log 2>&1; echo "exit=$?"` then inspect `/tmp/opencode/run-tests-fixed.log` tail.  
Expected: failure signature remains pre-existing `ShaderCfgTests: failed: graphics buffer ADD_TID did not terminate requirements analysis` (exit 134) or better; no new failure attributable to this change.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "fix(recompiler): emit valid lowered dynamic buffer load on RADV"
```

Expected: commit contains only the minimal fix (and any necessary test), with temporary dump/log hooks removed.

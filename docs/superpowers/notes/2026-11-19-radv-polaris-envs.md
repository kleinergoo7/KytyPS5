# RADV env vars applicable to Polaris (RX 580 / GFX8) — full audit

Source: https://docs.mesa3d.org/envvars.html (RADV section), audited 2026-11-19.
Our GPU: AMD Ellesmere RX 580, GFX8/Polaris. The hang is a graphics-queue `vkQueueSubmit`
blocking ~2.0 s (kernel: `ring gfx timeout, soft recovered`).

## Directly applicable to GFX8/Polaris (candidates to A/B)

### RADV_DEBUG (comma-separated)
- **hang** — detect GPU hang, dump report to `$HOME/radv_dumps_<pid>_<time>`. Primary diagnostic.
- **fullsync** — sync all pending work after every draw/dispatch (includes syncshaders + flush caches).
  If the hang vanishes => command-stream dependency/timing bug, not a single bad shader.
- **syncshaders** — sync shaders after every draw/dispatch (lighter than fullsync).
- **noibchaining** — disable chaining IB buffers. Relevant: hang is in a submit/replay.
- **dumpibs** — dump IBs (command streams) — see what the hanging submit contains.
- **pso_history** — pipeline hash + shader VA to `/tmp/radv_pso_history.log` (for UMR/Fossilize hang debug).
- **bo_history** — BO history to `/tmp/radv_bo_history.log` after each BO op.
- **vm** — add a gap between all VA allocations to check for page faults.
- **validateva** — track VA ranges for `radv_build_is_valid_va`.
- **allbos** — force all buffers referenced in submissions (can expose a missing-IB reference bug).
- **nobinning** — disable primitive binning.
- **nofastclears**, **nodcc**, **nodisplaydcc**, **nohiz**, **nofmask** (GFX6-10.3), **notccompatcmask** —
  disable GPU features one at a time (bisect which one hangs).
- **noatoc**, **notmz**, **novideo**, **noeso**, **nogpl**, **noheap**, **nocachecompat** — disable extensions/features.
- **shadowregs** — register shadowing (hang debug aid).
- **extra_md** — extra BO metadata for UMR.
- **noumr** — suppress UMR dumps (only with `hang`) when dumps are too slow.
- **shaders / shaderstats / spirvdump / nir / ir / asm / ps / cs** — shader dumps (per stage).
- **preoptir / prologs / epilogs / metashaders / dump_trap_handler** — compiler dumps.
- **startup** — print GPU info at startup.
- **psocachestats** — PSO cache hits/misses (verify precompilation).
- **checkir** — validate LLVM IR before shader compile.
- **llvmenable** — LLVM backend (debug builds only).
- **infos / img** — GPU / image info.

### RADV_TRAP_HANDLER  ← GFX8-SPECIFIC, high value
- "enable the experimental trap handler for debugging GPU hangs on **GFX8** (disabled by default)".
- Pairs with `RADV_TRAP_HANDLER_EXCP` (comma-separated): `mem_viol`, `float_div_by_zero`,
  `float_overflow`, `float_underflow`. Try `RADV_TRAP_HANDLER=1 RADV_TRAP_HANDLER_EXCP=mem_viol`
  to catch an out-of-bounds GPU access in the hanging shader.

### RADV_QUEUE_DISABLE
- `gfx` / `compute` / `vdec` / `venc` / `transfer` / `sparse`.
- `compute` is most relevant — the GI walker shader is compute; disabling it tests whether the
  hang is on the compute queue.

### RADV_PERFTEST (GFX8-applicable subset)
- **dmashaders** — upload shaders to invisible VRAM (for non-resizable BAR systems; our RX 580 has no ReBAR).
- **nosam** — disable optimizations that move driver objects to VRAM (relevant: VRAM pressure at 6.4/8 GiB).
- **nogttspill** — disable GTT spilling when allocating memory.
- **localbo** — enable local BOs.
- **lowlatencydec / lowlatencyenc** — decode/encode latency.
- NOT applicable (GFX10+): cswave32, gewave32, pswave32, rtwave64, nggc, msrtss.

### RADV_EXPERIMENTAL (GFX8-applicable subset)
- **hic** — VK_EXT_host_image_copy on GFX9 only → NOT applicable.
- **msrtss** — GFX9+ → NOT applicable.
- **sparse** — "GFX6, GFX7-8 APUs" → our RX 580 is a discrete GFX8, so NOT applicable.
- **video_decode / video_encode** — GFX6-9 → applicable (unlikely related to hang).
- **elf** — ELF format for shader binaries (needs LLVM build).

### Other RADV_*
- **RADV_TEX_ANISO** — force anisotropy (up to 16).
- **RADV_THREAD_TRACE_\*, RADV_CACHE_COUNTERS_BUFFER_SIZE, RADV_SPM_COUNTERS_CONFIG** — SQTT/RGP tracing
  (used with MESA_VK_TRACE=rgp; GFX10+ for some). `RADV_THREAD_TRACE_BUFFER_SIZE` may work on GFX8.
- **RADV_RRA_TRACE_\*** — ray tracing analyzer (no RT on GFX8) → not applicable.
- **RADV_PROFILE_PSTATE** — `standard`/`min_sclk`/`min_mclk`/`peak` — force GPU clocks; useful to rule out clock/power instability.
- **RADV_FORCE_VRS / _CONFIG_FILE** — GFX10.3+ → not applicable.

## Recommended A/B order for our hang
1. `RADV_DEBUG=hang` (+ `noumr` if dumps slow) — get the hang report, see the faulting shader/IB.
2. `RADV_TRAP_HANDLER=1 RADV_TRAP_HANDLER_EXCP=mem_viol` — GFX8-specific memory-violation trap.
3. `RADV_DEBUG=fullsync` — if hang vanishes => dependency/timing bug; if not => specific shader/feature.
4. `RADV_QUEUE_DISABLE=compute` — isolate compute queue.
5. `RADV_DEBUG=noibchaining` — isolate IB chaining.
6. `RADV_DEBUG` feature-bisect: `nodcc`, `nofastclears`, `nobinning`, `nohiz`, `nofmask`.
7. `RADV_PERFTEST=nosam,dmashaders` — VRAM/BAR-related.
8. `RADV_PROFILE_PSTATE=standard` — rule out clock instability.

## RESULT: RADV_DEBUG=hang,syncshaders produced a hang report (2026-11-17 20:30)

Dump: /home/reng/radv_dumps_212523_2026.11.17_20.30.08/
- trace.log: the command processor STOPPED at trace point 55 (NOP c0001000 marked
  "!!!!! This trace point was NOT reached by the CP !!!!!"). Immediately before it:
  a `WRITE_DATA` packet with `WR_CONFIRM = WAIT_FOR_WRITE_CONFIRMATION` writing to
  `DST_GDS_ADDR = 4096 (0x1000)` / full address 0x100001000, then CP_COHER sync
  (CP_COHER_SIZE=0xffffffff, POLL_INTERVAL=10). The hang is the CP waiting for a
  GDS write-confirmation that never arrives — a command-stream-level hang, NOT a shader infinite loop.
- registers.log GRBM_STATUS at hang: GUI_ACTIVE=1, CP_BUSY=1, CB_BUSY=1, DB_BUSY=1,
  IA_BUSY=1, SPI_BUSY=1, SX_BUSY=1, WD_BUSY=1; CB_CLEAN=0. vm_fault.log EMPTY (no memory violation).
- 2 SPIR-V modules dumped (99d904...spv, d27e665...spv) — likely the shaders bound around the hang.
- UMR tools not installed (umr_ring.log/umr_waves.log empty) so no wave state.
- app_info.log: RADV_PERFTEST=transfer_queue is deprecated (unrelated).

CONCLUSION: the ~2.0s graphics-queue vkQueueSubmit blocks in a GDS write-confirmation
wait; kernel ring gfx timeout is the same event. This points at the GDS buffer /
write-confirm path in the driver+emulator interaction, not a bad shader.

NEXT SWINGS (env-only, no code):
1. RADV_DEBUG=hang RADV_PERFTEST=nosam — driver objects to system memory (VRAM pressure).
2. RADV_DEBUG=hang RADV_QUEUE_DISABLE=compute — remove compute queue (GI walker).
3. RADV_DEBUG=hang,nodcc / nofastclears / nobinning / noibchaining — feature bisect.
4. RADV_TRAP_HANDLER=1 RADV_TRAP_HANDLER_EXCP=mem_viol — GFX8 memory-violation trap.
5. RADV_DEBUG=hang,fullsync — if the GDS-confirm wait is a race, fullsync may avoid it.

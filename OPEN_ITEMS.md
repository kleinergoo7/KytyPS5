# Open items — things that COULD still be fixed or optimized

Context: goal met (PPSA21567 v01.019.000 boots and gets in-game on RX 580 from the AMPR repack;
load 48 s → 5 s). Nothing below blocks the goal. Details/evidence:
`.superpowers/sdd/2026-11-18-ampr-pack-vfs/progress.md`, `GOAL.md`.
Product tree: `/tmp/opencode/jetsku-plus`, branch `jetsku-plus`, HEAD `865f90f6` (local only).

## Correctness / robustness (all Minor)
1. **`m_chunkCount` overflow guard** — `src/common/amprPack.cpp:230` lacks the `UINT32_MAX` check
   that `m_fileCount` has. Only reachable with a >34 GB index.
2. **Corrupt-chunk test coverage** — `tests/AmprPackTests.cpp` exercises an out-of-range volume id
   but not `codec > 1` or a bad zstd frame (`ZSTD_isError` / frame-size mismatch paths).
3. **Entry-id aliasing** — `src/common/archive.cpp` uses `firstChunk` as the record id; a future
   packer that deduplicates chunk ranges would alias two files. Use a stable per-record handle.
4. **Cache key not canonicalized** — Task 6's strong cache keys on raw `parent_path()`, while
   `OpenArchive` uses `absolute()`; relative vs absolute spellings of one root yield two `Init`s.
   Cache also never evicts (~40 MB/root — acceptable for one title).
5. **Case-shadowing corner** — Task 7's fast path can let a packed file shadow a loose host file
   differing only in case. No such collision in this dump.
6. **Test-only counters in production API** — `AmprPack::InitCount()` / `ResetInitCount()` and the
   `ResolvePath` counter ship in release builds; brief-sanctioned, not `#ifdef`-guarded.
7. **Test build cost** — `ampr_pack_tests` links the full emulator (~236 extra objects cold;
   ccache-mitigated). A pure seam could avoid this.

## Performance opportunities (beyond the 48 s → 5 s win)
8. **Decoded-chunk cache** — `AmprPack::Read` (`amprPack.cpp:365-420`) re-decompresses every 256 KiB
   zstd chunk on each read. Reference `ampr-emu-ng` uses a ~16 MiB decoded cache; could improve
   steady-state streaming and FPS.
9. **`ListDirectory` is O(descendants)** — `amprPack.cpp:320` scans ~166k paths per call for a
   top-level prefix. A precomputed directory→children map would help if directories are listed often.
10. **Single `ArchiveIoThread`** — all archive reads serialize through one worker
    (`archive.cpp:283-307`, shared with `.zar`). More workers could help parallel streaming.
11. **Folded path map cost** — ~27 MB of extra strings built at `Init`; could be lazy or more compact.

## Hardware limits (not software-fixable here)
12. **No `VK_EXT_mesh_shader`** — NGG draws are skipped (1 warning per run; ~108k dropped draws
    cumulatively in a 90 s run). Possible visual gaps in later levels. An NGG→compute+vertex fallback
    exists in `AbbasBHF/KytyPS5@rdna1-work` but is a large port.
13. **No `fragmentShaderBarycentric`** — warning at startup; possible rendering artifacts in some
    shaders.

## Process / verification
14. **Old `.zar` end-to-end regression** not re-run after Tasks 6–7 (unit fixture guard passes).
15. **Product commits not pushed** — `jetsku-plus` at `865f90f6` is local only; push needs explicit
    approval.

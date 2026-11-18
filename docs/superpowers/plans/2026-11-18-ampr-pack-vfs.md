# AMPR Pack VFS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make KytyPS5 boot Astro Bot PPSA21567 v01.019.000 from its AMPR zstd-packed folder by serving packed files through a virtual filesystem, without unpacking ~290 GiB to disk.

**Architecture:** Add a read-only AMPR zstd index/pack reader, expose it as a `Common::ArchiveReader` backend so the existing `archive!/member` path routing handles stat/open/read, and hook `MountPoints::ResolvePath` to synthesize a virtual archive path only when a real host file is missing. No changes to `libAmpr.cpp`; the old loose/`.zar` path stays untouched when no index is present.

**Tech Stack:** C++20, CMake/Ninja, clang 22 + ccache, system libzstd (already linked into `kyty_emulator`), existing `Common::ArchiveReader`/`Common::File` abstraction.

**Spec:** `docs/superpowers/specs/2026-11-18-ampr-pack-vfs-design.md`

## Global Constraints
- Implementation target: `/tmp/opencode/jetsku-plus`, branch `jetsku-plus` (HEAD 809121b6). Do NOT edit `/home/reng/Schreibtisch/KytyPS5-ds` product code.
- Build with system packages only: `env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin`, `CMAKE_CXX_FLAGS=-I/tmp/opencode/jetsku-intrin-shim`, ccache on.
- No llvmpipe. Target GPU AMD RX 580 / RADV POLARIS10. Window 1600×900.
- Source dump: `/home/reng/disk/Downloads/[ASTRO BOT]-[PPSA21567]` (index 22,016,800 B; 166,728 files; 733,324 chunks; 11 volumes).
- Keep all improvements; no reverts. Local commits only; push only `kleinergoo7`.
- Subagents: `opencode-go/hy4-flash-free`.
- Read-only game access. Never unpack packs to disk. Never modify the working old-ROM run.

## Review Focus
1. **No AMPR index present** (old `.zar`/loose dump): must behave exactly as before — Task 3 test mounts the old `.zar` and asserts a normal path.
2. **Case-mismatched guest path**: exact-case FNV binary search alone would miss; Task 1 test asserts a mixed-case lookup still resolves via the folded fallback.
3. **Cross-chunk / cross-volume read**: a read spanning a 256 KiB boundary and a volume boundary must return correct bytes — Task 1 test covers a cross-chunk range.
4. **Corrupt or oversized zstd frame**: must fail cleanly (return false / error), never crash or read past the volume — Task 1 test asserts a truncated frame is rejected.
5. **Mutating ops on a virtual path**: `open(O_CREAT|O_WRONLY)`, truncate, unlink must be rejected as EROFS/EACCES, not create a host file — Task 3 test asserts this.

---

### Task 1: AMPR zstd reader core

**Files:**
- Create: `src/common/amprPack.h`
- Create: `src/common/amprPack.cpp`
- Modify: `src/common/CMakeLists.txt` (add `amprPack.cpp` to `common`; link `libzstd_static` so `zstd.h` and the lib reach tests)
- Modify: `CMakeLists.txt` (add `ampr_pack_tests` target next to `archive_file_tests`)
- Test: `tests/AmprPackTests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `class Common::AmprPack` with `bool Init(const std::filesystem::path& gameRoot)`, `bool Lookup(std::string_view guestPath, FileInfo& out)`, `bool Read(const FileInfo&, uint64_t offset, size_t len, void* out)`.
  - `struct FileInfo { uint64_t size; uint32_t firstChunk; uint32_t chunkCount; }`.
  - `bool Exists(std::string_view guestPath)` convenience wrapper.

- [ ] **Step 1: Generate the golden oracle**

Run:
```bash
cd /home/reng/Schreibtisch/KytyPS5-ds/tools/local/static-precompile
python3 -c "
import sys, zlib, hashlib
sys.path.insert(0,'.')
from gamefs import Zar
z=Zar('/mnt/hdd/roms/ps5/ASTRO_BOT.zar')
h=z.lookup('data/common/font/FuturaStd-Bold.otf')
b=z.read_node(h[0])
print(len(b), hex(zlib.crc32(b)), hashlib.sha256(b).hexdigest())
z.close()
"
```
Expected: `28280` bytes and a fixed CRC32/SHA256; record these two literals in the test.

- [ ] **Step 2: Write the failing test**

In `tests/AmprPackTests.cpp`, assert:
- `Init` on the real dump returns true; `Lookup("/app0/data/common/font/FuturaStd-Bold.otf")` returns `size == 28280`, `firstChunk == 587370`.
- `Lookup` with mixed case (e.g. `/app0/DATA/COMMON/FONT/FUTURASTD-BOLD.OTF`) returns the same file.
- `Read` of the whole file produces CRC32/SHA256 equal to the Step 1 literals.
- `Read` of a range spanning a 256 KiB chunk boundary on a larger packed file returns success and the expected per-chunk decompressed length (`min(262144, size - k*262144)`).
- `Init` with a truncated `ampr_zstd.index` copy returns false; a deliberately corrupt chunk word makes `Read` return false, not crash.

- [ ] **Step 3: Run test to verify it fails**

Run:
```bash
cd /tmp/opencode/jetsku-plus
env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin cmake --build _Build/linux --target ampr_pack_tests --parallel && ./_Build/linux/ampr_pack_tests
```
Expected: build fails (symbols missing) or test fails.

- [ ] **Step 4: Implement `AmprPack`**

In `amprPack.cpp`: mmap `ampr_zstd.index`; validate magic `AMPRZPK1`, version 1, header size 128, chunk_shift 18, level 1..22, table contiguity, `strings_off+strings_len == file_size`; skip CRC recomputation. Store pointers into the mapping for file records (32 B), chunk words (8 B), volume sizes, strings. Build a `std::unordered_map<std::string, uint32_t>` keyed by ASCII-folded relative path for the mixed-case fallback. `Lookup`: strip `/app0/`, standard FNV-1a 64, binary-search `(hash, exact bytes)`, fall back to the folded map. `Read`: decode the chunk word as `off = w & 0xffffffff`, `vol = (w >> 32) & 0xfff`, `stored = ((w >> 44) & 0x3ffff) + 1`, `codec = w >> 62`; `pread` from a lazily opened `ampr_zstd-%03u.pak` (11 max); codec 0 copy raw, codec 1 `ZSTD_decompressDCtx` with `ZSTD_d_windowLogMax = 18` into a 262,144 B scratch; bound-check `off + stored <= volume_size[vol]`. One scratch + one `ZSTD_DCtx` per `AmprPack` instance; document that the backend must serialize reads per instance.

- [ ] **Step 5: Run tests and verify they pass**

Command from Step 3. Expected: all assertions pass.

- [ ] **Step 6: Commit**

```bash
git add src/common/amprPack.h src/common/amprPack.cpp src/common/CMakeLists.txt CMakeLists.txt tests/AmprPackTests.cpp
git commit -m "feat(ampr): read-only AMPR zstd index/pack reader with tests"
```

---

### Task 2: ArchiveReader backend

**Files:**
- Modify: `src/common/archive.cpp` (add backend class + `Formats[]` row)
- Modify: `src/common/archive.h` (if a declaration is needed)
- Test: `tests/AmprPackTests.cpp` (extend)

**Interfaces:**
- Consumes: `Common::AmprPack` from Task 1.
- Produces: a registered synthetic extension (choose `.amprz`) such that `Common::MakeArchivePath(gameRoot / "__ampr__.amprz", member)` opens through `Common::File` and `Common::OpenArchive`.

- [ ] **Step 1: Write the failing test**

Assert: for virtual path `P = Common::MakeArchivePath(root / "__ampr__.amprz", "data/common/font/FuturaStd-Bold.otf")`, `Common::File::IsFileExisting(P)`, `Common::File::Size(P) == 28280`, and `Common::File::Open(P, Read)` + `Read` yields the Step 1 golden CRC. Also assert `Common::File::IsFileExisting` on a member not in the index returns false.

- [ ] **Step 2: Run test to verify it fails**

Build/run as in Task 1 Step 3. Expected: FAIL (extension not registered / no backend).

- [ ] **Step 3: Implement the backend**

Add `AmprPackReaderBackend` implementing `Find(member) -> {id,size,is_file}`, `List(member) -> vector<DirEntry>` (prefix-derived synthetic directories), `Read(id, offset, size, data)` (delegates to `AmprPack::Read`, with a mutex guarding the shared scratch/DCtx). Its `Open` locates `ampr_zstd.index` in the synthetic archive file's parent directory and calls `AmprPack::Init`. Register `{".amprz", OpenAmprPack}` in `Formats[]`. Keep it read-only; no write path.

- [ ] **Step 4: Run tests and verify they pass**

Command from Step 1. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/common/archive.cpp src/common/archive.h tests/AmprPackTests.cpp
git commit -m "feat(ampr): ArchiveReader backend for packed game files"
```

---

### Task 3: Mount-time index load and path hook

**Files:**
- Modify: `src/kernel/fileSystem.cpp` (`MountPair` struct ~46-50, `Mount` ~260-273, `ResolvePath` ~341-379)
- Modify: `src/kernel/fileSystem.h` (if the struct is declared there)
- Test: `tests/AmprPackTests.cpp` (extend)

**Interfaces:**
- Consumes: `Common::AmprPack` and the `.amprz` archive path form from Tasks 1-2.
- Produces: guest `/app0/...` paths missing on disk resolve to virtual archive paths; all other paths unchanged.

- [ ] **Step 1: Write the failing test**

Assert:
- After mounting the real v01.019.000 folder at `/app0`, `LibKernel::FileSystem::GetRealFilename("/app0/data/common/font/FuturaStd-Bold.otf")` returns an archive path containing `!/`, and `Common::File::Size` of it is 28280.
- After mounting the old `ASTRO_BOT.zar` at `/app0`, the same call returns a normal path (no `!/`) and the font still opens — regression guard for Review Focus 1.
- Opening the virtual path with `O_CREAT|O_WRONLY`, truncate, and unlink are rejected; no host file is created.

- [ ] **Step 2: Run test to verify it fails**

Build/run as in Task 1 Step 3. Expected: FAIL (hook absent).

- [ ] **Step 3: Implement the hook**

Extend `MountPair` with `std::shared_ptr<Common::AmprPack> ampr` (or hold it in a side map keyed by mount point if adding to the struct is awkward). In `MountPoints::Mount`, after `p.archive = Common::OpenArchive(folder)`, if `<folder>/ampr_zstd.index` exists, construct `AmprPack`, `Init` it, and store it. In `ResolvePath`, after the normal host candidate is computed but before returning `{}`, if the candidate does not exist and `ampr->Lookup(relativeGuestPath, info)` succeeds, return `Common::MakeArchivePath(folder / "__ampr__.amprz", relativeGuestPath)`. Preserve `{}` for unmounted paths. Use a cheap case-folded lookup; do not add per-op disk scans.

- [ ] **Step 4: Run tests and verify they pass**

Command from Step 1. Expected: PASS.

- [ ] **Step 5: Build the full emulator**

Run the system-only build from Global Constraints; expected `build=0`.

- [ ] **Step 6: Commit**

```bash
git add src/kernel/fileSystem.cpp src/kernel/fileSystem.h tests/AmprPackTests.cpp
git commit -m "feat(ampr): resolve packed game files at mount time"
```

---

### Task 4: Boot the v01.019.000 dump on the RX 580

**Files:**
- Create: `tools/local/test_ampr_boot.sh`

**Interfaces:**
- Consumes: the built `kyty_emulator` from Task 3.

- [ ] **Step 1: Write the boot script**

Script sources `/tmp/opencode/u59-preset.json`, then runs the emulator with `--game "/home/reng/disk/Downloads/[ASTRO BOT]-[PPSA21567]"`, `--game-patch docs/superpowers/astro-10190000-patches.json`, `--screen-width 1600 --screen-height 900`, `--console-language 1 --vblank-frequency 30 --present-mode Mailbox --skip-notice-screen true --no-splash --printf-direction Console`. It captures the window with `import -window <id>` at 30, 60, 90 s via `xdotool`, writes to `/tmp/opencode/ampr-boot/`, and prints fatal/device-loss counts.

- [ ] **Step 2: Run it**

Run: `bash tools/local/test_ampr_boot.sh` from `/tmp/opencode/jetsku-plus`.
Expected: no fatal; screenshots show the ASTRO BOT title screen (not the gray rectangle); `DrawRun` counts in the hundreds of thousands.

- [ ] **Step 3: Regression check**

Repeat with `KYTY_GAME=/mnt/hdd/roms/ps5/ASTRO_BOT.zar` and confirm the old title screen is unchanged.

- [ ] **Step 4: Commit**

```bash
git add tools/local/test_ampr_boot.sh
git commit -m "test(ampr): boot harness for the packed v01.019.000 dump"
```

---

### Task 5: Resolve index-only directories in the mount hook (added after Task 4 blocker)

**Files:**
- Modify: `src/kernel/fileSystem.cpp` (`MountPoints::ResolvePath`, the `p.ampr` host-miss branch)
- Modify: `tests/AmprPackTests.cpp`
- Test: `ampr_pack_tests`

**Interfaces:**
- Consumes: `Common::AmprPack::IsDirectory(std::string_view)` and `ListDirectory` from Task 2.
- Produces: host-missing guest directory paths that exist only in the pack resolve to the `.amprz` virtual path, so `KernelStat`/`GetDirEntries` succeed.

**Why:** Task 4 proved the gray screen is gone and indexed files resolve, but the packed dump stalls on a static black frame because 351 guest paths reported "file not found" include index-only directories (`data/prein/spx`, `data/prein/levels/ui_setting/anim`, ...). The hook currently checks only `Lookup` (files).

- [ ] **Step 1: Write the failing test**

In `tests/AmprPackTests.cpp`, after mounting the packed folder at `/app0`, assert that
`GetRealFilename("/app0/data/prein/spx")` (and one `.../anim` directory) returns an archive path
containing `!/`, and that `Common::File::IsDirectoryExisting` succeeds on it with non-empty
`GetDirEntries` where the index has children. Assert the old `.zar` regression still passes.

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /tmp/opencode/jetsku-plus
env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin cmake --build _Build/linux --target ampr_pack_tests --parallel && ./_Build/linux/ampr_pack_tests
```
Expected: FAIL (directory host-miss is not redirected).

- [ ] **Step 3: Implement the directory branch**

In `ResolvePath`'s `p.ampr` host-miss branch, after the `Lookup` check fails, call
`p.ampr->IsDirectory(rel_path)`; on true return
`Common::MakeArchivePath(p.dir / "__ampr__.amprz", native_rel_path)` exactly as for files. Keep the
case-folded matching and the `{}`/ENOENT behavior unchanged. Keep the POSIX platform guard.

- [ ] **Step 4: Run tests; build full emulator**

Command from Step 2, then `cmake --build _Build/linux --target kyty_emulator --parallel`. Expected: PASS.

- [ ] **Step 5: Re-run the Task 4 boot test**

`bash tools/local/test_ampr_boot.sh` from `/tmp/opencode/jetsku-plus`; expect the ASTRO BOT title screen.

- [ ] **Step 6: Commit**

```bash
git add src/kernel/fileSystem.cpp tests/AmprPackTests.cpp
git commit -m "fix(ampr): resolve index-only directories at mount time"
```

---

## Self-review notes
- Spec coverage: reader (T1), archive routing (T2), mount hook (T3), boot proof (T4); case fallback, cross-chunk reads, no-index regression, and mutating-op rejection all have tests.
- Target tree is jetsku-plus throughout; no `KytyPS5-ds` product files are modified.
- Follow-ups deliberately left out: directory-listing merge (only if boot needs it), LZ4 packs, CRC verification, AMPR numeric IDs.

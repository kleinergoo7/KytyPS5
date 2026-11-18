# Design: virtual AMPR zstd-pack filesystem for KytyPS5

## Intent and why
Astro Bot PPSA21567 v01.019.000 is distributed as an AMPR-repacked folder: the game's data lives in
`ampr_zstd-000..010.pak` plus two indexes, not in a plain `data/` tree. Kyty only reads loose files and
`.zar`, so every `/app0/data/...` read fails and the game stalls on a static gray screen. Unpacking the
packs to a folder needs ~290 GiB free (only 284 GiB available) and hours of I/O. The goal is to boot
this dump on the RX 580 without unpacking, by teaching Kyty to serve packed files directly.

## Goals
- A game folder containing `ampr_zstd.index` + `ampr_zstd-*.pak` mounts and boots like a normal dump.
- Normal file reads (fonts, XML, audio, textures) and `libAmpr` path-to-ID resolution both work.
- The existing working `.zar`/loose-folder path is untouched when no AMPR index is present.

## Non-goals / anti-goals
- No writing, patching, or repacking of the game files. Read-only.
- No AMPR numeric-ID emulation or directory enumeration in the first cut.
- No LZ4 (`AMPRPAK4`) pack support — only the zstd format used by this dump.
- Do not modify the working old-ROM configuration.

## Constraints
- Build with system packages only; ccache; no llvmpipe.
- Preserve all improvements; local commits + explicit pushes only.
- Target 900p window, RX 580 / RADV POLARIS10.
- 166,728 files / 733,324 chunks / 11 volumes; index 22,016,800 B; logical data ~148.5 GiB.

## Approaches
1. **Virtual filesystem in Kyty's file layer** (chosen): parse `ampr_zstd.index`, and when a
   `/app0/...` path is missing on disk, look it up in the index and serve bytes from the volumes with
   zstd decompression. Reuses Kyty's existing file API, so both ordinary reads and `libAmpr` benefit.
   No new disk usage; cost is a new reader module plus a small mount hook.
2. Make only `src/libs/libAmpr.cpp` pack-aware: smaller, but ordinary reads of fonts/XML/audio would
   still fail, so the gray screen would remain. Rejected.
3. Unpack with `ps5-dump-forge`: no code, but needs ~300 GiB free and hours — already shown blocked.

## How (decided)
### Reader module `src/common/amprPack.{h,cpp}`
- `Init(root)`: mmap `ampr_zstd.index`; validate magic `AMPRZPK1`, version 1, header size 128,
  chunk_shift 18, level 1..22, CRCs, contiguous tables, `strings_off+strings_len == file_size`.
- `Lookup(game_path)`: strip `/app0/` (ASCII case-preserving), compute standard FNV-1a 64 over the
  relative bytes, binary-search the 166,728 × 32 B file records by (hash, exact path bytes), return
  `logical_size`, `first_chunk`, `chunk_count`.
- `Read(file, offset, len, out)`: for each 256 KiB chunk, decode the u64 word
  (`off = w & 0xffffffff`, `vol = (w >> 32) & 0xfff`, `stored = ((w >> 44) & 0x3ffff) + 1`,
  `codec = w >> 62`), `pread` the stored bytes from `ampr_zstd-%03u.pak` (lazy-open, 11 fds max),
  decompress with `ZSTD_decompressDCtx` (windowLogMax 18) when codec 1, copy the requested slice.
- One decoded scratch buffer (262,144 B) and one `ZSTD_DCtx` per reading worker; index stays mapped
  (~21 MiB total). `AMPRIDX3` is not parsed — the zstd index is sufficient for path lookups.
- Fall through to the real filesystem for loose files (eboot, sce_sys, *.prx/*.sprx, the indexes,
  the packs themselves) and for any path not found in the index.

### Mount hook (finalized)
Kyty already has a complete virtual-archive pipeline: any path shaped
`<archive>!<member>` is handled by `Common::File` (stat/open/read/dir-enum) and by the kernel FS.
Reuse it instead of inventing a VFS:
- **Backend:** add an `ArchiveReader` implementation (`src/common/archive.cpp`, next to
  `ZArchiveReaderBackend` at `:44-84`) over `ampr_zstd.index` + `ampr_zstd-*.pak`; register its
  extension in `Formats[]` (`:91-96`). `Find` = index lookup, `List` = prefix-derived directory
  entries, `Read` = chunk decode + zstd. The backend gets stat/open/read/dir-enum and the
  64 KiB-stack-safe `ArchiveIoThread` for free.
- **Load once at mount:** `MountPoints::Mount` (`src/kernel/fileSystem.cpp:260-273`, right after
  `p.archive = Common::OpenArchive(folder)` at `:270`) parses the index when present and stores it
  on `MountPair` (extend the struct at `:46-50`). `Umount` (`:275-288`) already handles lifetime.
  This happens at `src/emulator.cpp:196-197`, before `LoadElf` at `:205`, so `libAmpr`'s
  negative-resolve cache is populated after the index exists.
- **Single interception:** `MountPoints::ResolvePath` (`src/kernel/fileSystem.cpp:341-379`). After
  the normal host candidate is computed (`:363-374`), if it is missing and the index contains the
  case-insensitive relative path, return `Common::MakeArchivePath(pack_path, member)`. Otherwise
  keep today's result, including `{}` for unmounted paths, so ENOENT probing and `O_CREAT` are
  unchanged.
- **`libAmpr.cpp` needs no changes:** `ResolveOnePath` (`:285, 289-293`), `GetFileStat`/`GetFileSize`
  (`:217-257, 526-558`) and reads (`:2036-2140`) all go through `GetRealFilename` + `Common::File::*`,
  both archive-aware. Mutating ops already reject archive paths with EROFS/EACCES
  (`fileSystem.cpp:491-495` etc.).
- **Optional only if needed:** merge index-only children into real directory listings at
  `KernelOpen` (`:521`) / `KernelStat` (`:1214-1215`) when a real directory gains virtual files.
- **Inert by default:** no `ampr_zstd.index` → no backend, no behavior change; the working old-ROM
  path is untouched.

### Build note
`libzstd_static` is already linked into `kyty_emulator` (`CMakeLists.txt:568`) and `<zstd.h>` is
available there (`pipelineList.cpp:9`). If the backend is added to the `common` static library, add
`libzstd_static` to `src/common/CMakeLists.txt` for the include path; otherwise keep it in a file
compiled into `kyty_emulator` to need no CMake change. Prefer the latter for the smallest diff.

## Testing
- Unit test: parse the real index, look up a known file (`data/common/font/FuturaStd-Bold.otf`,
  28,280 B, chunk 587,370, volume 8), read first/last bytes and a cross-chunk range; compare against
  `ps5-dump-forge --zstd-unpack` output for that one file.
- Integration: boot the v01.019.000 dump at 1600×900 on the RX 580 and capture frames; success is the
  ASTRO BOT title/galaxy screen instead of the gray rectangle.
- Regression: boot the old `.zar` dump and confirm the title screen is unchanged.

## Left to the planner
- Thread-safety: one `ZSTD_DCtx` + 256 KiB scratch per `ArchiveIoThread`, volume fds cached per
  backend instance; decide locking at the backend rather than in the kernel FS.
- Directory listing: confirm whether the game needs index-only children in real directories; add
  the small merge at `fileSystem.cpp:521` / `:1214-1215` only if the boot test shows it is needed.
- Synthetic archive extension name and how the backend locates the real `ampr_zstd.index` beside it.

## Status
Design finalized from both investigations. Taking the user's "1" as approval of approach 1;
running the builder check next, then writing the implementation plan. No product code touched yet.

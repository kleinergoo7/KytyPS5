#!/usr/bin/env bash
# Linux release layout, as .github/workflows/build-linux.yml packages it.
#   ./package-linux.sh [package-name]   stage _Build/package/<name> from _Build/linux
# KYTY_BUILD_DIR overrides the build directory (default _Build/linux).
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
BUILD="${KYTY_BUILD_DIR:-$ROOT/_Build/linux}"
NAME="${1:-KytyPS5-linux-local}"
OUT="$ROOT/_Build/package/$NAME"

for f in kyty_emulator kyty_shader_precompile souls-launcher; do
	[ -x "$BUILD/$f" ] || { echo "Missing $BUILD/$f: build it first (./build-linux.sh $f)." >&2; exit 1; }
done

rm -rf "$OUT"
mkdir -p "$OUT/tools/local/static-precompile"
cp "$BUILD/kyty_emulator" "$BUILD/kyty_shader_precompile" "$BUILD/souls-launcher" "$OUT"/
cp "$ROOT/tools/local/run_ds.sh" "$ROOT/precompile-linux.sh" "$ROOT/tools/local/dlss-bridge/get-dlss-linux.sh" "$OUT"/
# The Steam Deck's launch script and its guide (docs/STEAM-DECK.md).
cp "$ROOT/tools/local/deck/run-deck.sh" "$ROOT/docs/STEAM-DECK.md" "$ROOT/docs/STEAM-DECK.zh-CN.md" "$OUT"/
# DLSS: the bridge's source only (MIT); get-dlss-linux.sh downloads NVIDIA's SDK and builds it on the user's PC,
# so the package carries none of NVIDIA's files.
mkdir -p "$OUT/dlss-bridge"
cp "$ROOT/tools/local/dlss-bridge/kyty_dlss_bridge.cpp" "$ROOT/tools/local/dlss-bridge/LICENSE.txt" \
	"$ROOT/src/local/kyty-dlss-bridge.h" "$OUT/dlss-bridge/"
cp "$ROOT/LICENSE" "$OUT"/
cp -r "$ROOT/LICENSES" "$OUT"/
# The render-pass states the precompile program makes the seed file with (the same files
# the Windows package carries): the first launch lists the game's shaders from its files.
cp "$ROOT/tools/local/static-precompile/pass-states.json" "$OUT/tools/local/static-precompile/"
cp "$ROOT"/tools/local/static-precompile/hints-*.hints "$OUT/tools/local/static-precompile/" 2>/dev/null || true
chmod +x "$OUT/kyty_emulator" "$OUT/kyty_shader_precompile" "$OUT/souls-launcher" "$OUT/run_ds.sh" \
	"$OUT/precompile-linux.sh" "$OUT/get-dlss-linux.sh" "$OUT/run-deck.sh"
cat > "$OUT/README.md" <<'EOF'
# KytyPS5 (Linux x64) for Demon's Souls (PPSA01341)

An experimental PS5 emulator. This package is the emulator and its shader precompile tool
for Linux x86-64 (glibc), built for CPUs with AVX2 (x86-64-v3).

## Requirements

- A 64-bit x86-64 CPU with AVX2 (Haswell/Excavator or newer)
- A Vulkan 1.3-capable GPU with current drivers
- A current Linux distribution (glibc 2.35 or newer)

## Run

The game is Demon's Souls 01.007.000 or 01.005.000, as a folder (the one with `eboot.bin`)
or as a ZArchive (`.zar`).

`./souls-launcher` opens the settings window (the Windows package's KytyLauncher.exe): choose the
game, the resolution, the upscaler (DLSS, FSR 3), frame generation, the graphics preset and the
rest, then PLAY. It saves them in `launcher-settings.json` and starts the game through `run_ds.sh`
in a terminal window with its log. The Shaders page runs the precompile, the Graphics page builds
DLSS (below). `./souls-launcher --play` starts the game with the saved settings without the window
(Steam: add it as a non-Steam game). It needs nothing installed: SDL2 and Dear ImGui are built in.

`run_ds.sh` starts the game from a terminal; point it at the game:

```
KYTY_GAME="/path/to/Demon's Souls.zar" ./run_ds.sh
```

The launcher prefers the `kyty_emulator` next to it (override with `KYTY_BIN`), opens a
1920x1080 window and shows MangoHud when it is installed (`MANGO=1`; `MANGO=0` hides it). The first
launch translates shaders in the background for a minute or two; later launches are faster.

If the unpacker dropped the files' execute permission ("Permission denied"): `chmod +x kyty_emulator
kyty_shader_precompile souls-launcher run_ds.sh precompile-linux.sh get-dlss-linux.sh run-deck.sh`.

On a Steam Deck, take the release's Steam Deck package (`KytyPS5-steamdeck-*.zip`, built for it), or start the
game from this one with `run-deck.sh` (the Deck's fastest settings): `STEAM-DECK.md` in this package tells how
to copy the game and the emulator over, add it to Steam and what frame rates to expect.

Keep the kernel's own CPU scheduler: the emulator places its threads itself (`KYTY_RENDER_CPUS`), and a
sched_ext scheduler works against that (`scx_lavd`, which CachyOS suggests for games: about 10% slower on an
i5-14400, issue #80). A power profile (balanced or performance) made no measurable difference there.

## Shader precompile (optional)

`./precompile-linux.sh --game <the game>` collects every shader and pipeline of the game into the static
pipeline cache ahead of time (hours the first time: run it overnight; an interrupted run resumes), so a new
area does not stall on its first pipelines. `--quick` compiles only what recorded play used, `--help` lists
the rest. See the repository's `tools/local/static-precompile/README.md`.

## DLSS (optional, NVIDIA RTX)

`./get-dlss-linux.sh` (once; it offers to install git and a C++ compiler when missing) downloads NVIDIA's DLSS
SDK and builds `libkyty_dlss.so` from `dlss-bridge/`, next to the emulator with NVIDIA's DLSS libraries:
then `KYTY_UPSCALER=dlss` and `KYTY_FRAMEGEN` work (the repository's `tools/local/dlss-bridge/README.md`).

## FSR 3 (built in, any graphics card)

AMD FSR 3 is part of this package (AMD's FidelityFX SDK, MIT license in `LICENSES/`): `KYTY_UPSCALER=fsr`
upscales the game's 3D on its PS5 Pro path in place of Sony's upscaler (sharpness `KYTY_FSR_SHARPNESS`, 0.0-1.0,
default 0.6), and `KYTY_FRAMEGEN_BACKEND=fsr` turns on FSR 3 frame generation (2x), for example
`KYTY_UPSCALER=fsr KYTY_FRAMEGEN_BACKEND=fsr ./run_ds.sh`.

Sizeable parts of this program are under the third-party licenses in `LICENSES/` and
`LICENSE`.
EOF
ls -l "$OUT"
echo "Staged $OUT"

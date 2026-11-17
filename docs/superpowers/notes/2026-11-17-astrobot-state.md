# Astro Bot state investigation (2026-11-19)

## Question
Does Astro Bot PPSA21564 v01.007.000 reach actual gameplay on the RX 580?

## Evidence (all from .zar runs with KYTY_LOOP_GUARD=64 KYTY_LOOP_GUARD_SHADERS=657ad04626bf9d55)

1. Screenshot after 45 s (`/tmp/opencode/astro-screen.ppm`, 3840x2160) = **Team Asobi splash screen**, rendered perfectly.
2. Screenshots at 20/35/50 s with virtual-pad attach + button 0 and button 1 presses
   (`/tmp/opencode/astro-s{1,2,3}.ppm`): all still the same splash. Background animates
   (hashes differ) but the logo never changes → the game is NOT advancing. Button presses
   acknowledged (`LIVE_VPAD ... ok=1`) but had no visible effect.
3. Frames run ~275 ms (3.6 fps) with `gpu_wait≈download_wait≈230 ms`, `record_wait≈40 ms`.
4. At the end of the 75 s run, `record_wait` jumps to **2124 ms** for 2 frames, coinciding with
   a new shader batch: `Shaders: VS 30 | PS 42 | CS 89` (was VS 28/PS 39/CS 80), then
   "Shader prefetch: 97 plans ... 97 programs translated", "88 pipelines (54 ms)",
   "Shader warmup: saved 157 inputs and 203 pipelines".
5. This matches Jetsku's documented "0.2-15 s freezes while ~100 pipelines compile at once"
   (RELEASE-NOTES / EXPERIMENTAL) — the transition out of the splash needs a shader batch that
   stalls, and the game never proceeds.

## Conclusion
The GPU-hang mitigation works (no device loss), but the game is **stuck at the splash screen**.
The "120 s survived" earlier was just the splash animating — not gameplay. The blocker is a
**shader-translation / pipeline-compile stall at the splash→game transition** (~97 programs
translated at once, 2.1 s record_wait), possibly made worse by VRAM pressure (6.4/8 GiB).

## Next steps (see plan 2026-11-17-astrobot-to-gameplay.md)
- G1b: run longer (180-300 s) with screenshots to see if the splash EVER resolves (the 2.1 s
  stall may be a one-time compile that finally completes — we only watched 75 s).
- G2: if it never resolves, investigate the shader/pipeline stall at the transition; test
  disabling warmup / forcing unoptimized-first pipelines, and VRAM relief.
- Do NOT mistake a static splash for gameplay. Verify with changing frames + input response.

## UPDATE (2026-11-19, second pass) — two crucial findings

1. NOT IN GAME: screenshots at 45/60/70/120 s, with and without virtual-pad button presses,
   ALL show the Team Asobi splash. The earlier "survived 120 s" was the splash animating.
   The game never advances past the splash.

2. LOOP GUARD IS UNRELIABLE (cache-dependent): the 180 s run had `Loop guard` = 0 hits and
   device-lost at tick 331719, while the earlier bg64 run had 177 hits and survived 120 s.
   Root cause: `Played pipelines: 38 made of earlier sessions' binaries, 0 compiled and kept`
   — the guard is baked into SPIR-V at translation time, but the pipeline cache reused a
   binary compiled WITHOUT the guard. So the mitigation silently stops working depending on
   cache state. To make it reliable the cache must be invalidated when the guard env changes
   (a code change), or the guard env must be part of the cache key.

3. The stall at the splash→game transition: `record_wait` 2124 ms when the VS 30/PS 42/CS 89
   batch appears (97 programs translated at once). This is the splash-unblocking shader batch.

VERDICT: Astro Bot reaches a rendered splash but NOT gameplay. The GPU hang is a secondary
symptom; the primary blocker is the splash→game transition (shader/pipeline stall), and the
loop-guard workaround is cache-fragile.

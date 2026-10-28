# AGENTS.md

Standing rules for AI agents. Task instructions come from the session prompt,
not from here.

## Scope

KytyPS5 — a PS5 emulator in C++20 using Vulkan, tuned for Demon's Souls
(PPSA01341), versions 01.007.000 and 01.005.000. This branch is a personal
fork of `chenxiao07/KytyPS5` (`experiment/perf-40fps-20260926`): hardware
support (Polaris/RDNA1 without shader barycentrics), built-in FSR 3 frame
generation, and local packaging. Upstream moves fast; rebases need a fresh
fetch.

## Hardware range (never narrow it to one machine)

- CPU: x86-64-v3 (AVX2, FMA, BMI2). Do not ship `-march=native`.
- GPU: any Vulkan 1.3 device with 8 GB of video memory or more — AMD (RDNA and
  older GCN/Polaris), NVIDIA (GTX and RTX), Intel Arc and integrated.
- Features that only one vendor offers (DLSS-G/NGX, Streamline, Reflex, HDR10,
  shader barycentrics) must be optional: detected at run time, logged when
  absent, and never required to start the game or draw a frame.
- A change that only works on the author's GPU is a regression, even if it is
  faster there. State which hardware you verified on and which you did not.

## C++ and style

- C++20. Match the surrounding file's indentation (tabs). Do not reformat
  untouched code.
- No new third-party dependencies without asking.
- Comments explain *why* (the bug, the issue number, the measurement), never
  what the line does. Keep existing comments unless the change makes them
  false — then fix them in the same commit.
- Small, purpose-shaped commits. Build fixes and runtime fixes stay separate.

## Correctness: fail loudly

Every failure path must log the reason or return an error the caller reports.
No silent fallbacks, no `(void)` on checked calls, no feature that claims to
work while off.

Existing violations are debt, not precedent: touching a file means draining
its silent failure paths in the same change, or leaving
`TODO(loud): <why not yet>` beside each one you leave. A file's debt count
may only go down.

## Testing

- Do not claim a build or run succeeded without the exit code and the log
  line.
- Frame generation proves itself with the `FSR 3 ... built in` log line, not
  with the backend-requested line alone.
- Test on at least one GPU different from the one a feature was written on
  when the change touches vendor-gated code.

## Agent behaviour

- Ask at most one question at a time, only when blocked.
- State what you skipped and the risk it leaves at the end of each task.
- Never add game files, firmware, keys, or extracted assets to the repo.
- Prefer minimal diffs; when a rebase conflicts, prefer upstream's version and
  re-apply only the fork's intent.

# Plan: Make Astro Bot boot to gameplay on RX 580 (POLARIS10)

## Goal
Astro Bot PPSA21567 v01.019.000 on jetsku-plus runs cleanly (18k draws/frame, 0 fatal) but is stuck
on a static gray notice/loading screen. Root cause: the new ROM's startup renders through NGG/mesh
draws; RX 580 has no VK_EXT_mesh_shader, so our graceful mesh-skip drops those draws and only a
fallback UI rectangle survives. The old ROM (v01.007.000) booted to a visible splash because it did
not use mesh draws at startup.

## Global constraints
- Hardware target: AMD RX 580 / RADV POLARIS10 (GCN4). No mesh shaders, no fragmentShaderBarycentric.
- No llvmpipe. Build with system packages only. ccache.
- Preserve all improvements; no reverts. Local commits + explicit pushes only.
- Subagents: opencode-go/hy4-flash-free.
- Window 1600x900. ROM used directly from /home/reng/disk/Downloads/[ASTRO BOT]-[PPSA21567]/.

## Tasks
1. Research existing NGG/mesh-shader software-fallback implementations in upstream KytyPS5 and all
   known forks; report concrete commits/PRs and a porting feasibility assessment.
2. Analyze jetsku-plus's mesh draw path end to end and design the smallest viable fallback that
   converts an unsupported NGG draw into something RADV can render; identify the exact functions.
3. Empirically identify the dropped startup mesh draws (shader hashes, draw params) and confirm
   they produce the splash; report what must be reproduced by a fallback.

## Spec
No written spec; the objective is behavioral: reach visible gameplay on RX 580. Rulings are provisional.

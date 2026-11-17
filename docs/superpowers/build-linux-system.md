# Building jetsku-plus with system packages only (no brew)

Prerequisites (Fedora): `sudo dnf install -y libXtst-devel` (the only package that was
missing; everything else — clang, lld, ninja, cmake, glslang, X11 dev libs, etc. — was present).

The `intrin.h` problem: this tree includes MSVC's `<intrin.h>` unconditionally. Clang 22's
`intrin.h` is a Windows-compat shim that does `#include_next <intrin.h>` and fails on Linux.
Fix with a tiny shim that maps to `<x86intrin.h>` (kept at /tmp/opencode/jetsku-intrin-shim/intrin.h):

```
#ifndef KYTY_INTRIN_SHIM_H
#define KYTY_INTRIN_SHIM_H
#include <x86intrin.h>
#endif
```

Configure + build (system-only PATH, ccache, shim first in include path):

```
cd /tmp/opencode/jetsku-plus
env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin \
  cmake -S . -B _Build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DKYTY_BUILD_LAUNCHER=OFF \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_FLAGS="-I/tmp/opencode/jetsku-intrin-shim"
env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin \
  cmake --build _Build/linux --target kyty_emulator --parallel
```

Result: `kyty_emulator` built (git e70f3d67 = Jetsku int21 + our 2 cherry-picks).

Note: the shim path is outside the repo; to make the build self-contained, copy intrin.h
into the repo and reference it via a relative -I in a future commit.

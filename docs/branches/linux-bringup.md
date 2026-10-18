# feature/linux-bringup

Based on `feature/present-diagnostics`.

## Why

Building and starting a generated project on Linux x86-64 failed in two
places:

- The SDK uses SSSE3/SSE4.1 intrinsics (byte swapping in
  `core/memory.cpp`, XMA and audio conversion). Its own presets pass
  `-march=x86-64-v2`, but projects from `rexglue init` set no `-march`, so
  building the SDK through `add_subdirectory()` targeted baseline SSE2 and
  failed with "always_inline function '_mm_shuffle_epi8' requires target
  feature 'ssse3'".
- Linux hosts get an `$ORIGIN` rpath, but the shared runtime libraries were
  only copied next to the executable on macOS. `librexruntime.so` stayed in
  the SDK's `out/` directory and the host failed with "cannot open shared
  object file".

## What

- The SDK's targets build with SSE4.1 on x86-64 whatever flags the consuming
  project sets.
- `rexglue_configure_target` stages the runtime libraries next to the
  executable on every non-Windows platform.

## Status

General build fixes, suitable for upstream.

# feature/present-diagnostics

Based on `feature/data-sections`.

## Why

Bringing up the Vulkan backend on macOS meant debugging frames that came out
black or never reached the screen, and hangs that neither lldb nor `sample`
could attach to. These are the tools used to find where output was lost.

## What

All debugging aids, off by default:

- `vulkan_log_resolves`: logs every resolve destination and the swap source.
  Every 60 frames it logs byte statistics of each resolve target, and every
  300 frames it dumps them raw to `run/` (needs `vulkan_readback_resolve`
  for data).
- `present_log_paints`: logs guest output refreshes, paint flows and
  results, and saves the presenter's guest output image to `run/` every 300
  frames.
- `vulkan_invalidate_textures_every_frame`: fires every shared memory watch
  at frame end so all textures reload from guest memory. Separates CPU write
  tracking bugs from bad source data.
- `dump_shaders` also writes the Vulkan backend's translated SPIR-V, as the
  D3D12 backend already did.
- macOS: `kill -USR1` makes every thread append its native backtrace to
  `run/thread_dump.txt`.

## Status

Debugging aids only; nothing changes unless a cvar is set or the signal is
sent. Useful upstream, but not required by any game.

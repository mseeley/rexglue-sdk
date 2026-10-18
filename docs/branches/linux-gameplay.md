# feature/linux-gameplay

Based on `feature/linux-bringup`.

## Why

Once Lara Croft and the Guardian of Light reached gameplay on Linux (Intel
UHD, Mesa ANV), three SDK bugs showed, plus a need to reproduce runs and
inspect GPU frames without someone at the keyboard:

- The Audio Worker thread used 70-80% of a core busy-waiting.
- Some surfaces rendered pure black (Lara, parts of the floor, cutscene
  backgrounds), on every render target path.
- Tests needed a person to press keys for about two minutes to reach
  gameplay each time.

## What

Fixes:

- **POSIX multi-object waits spun** (`threading_posix.cpp`). The remaining
  timeout was truncated to whole milliseconds, so a sub-millisecond
  remainder became `sleep_for(0)` and the loop spun until the deadline.
  Sleeps now keep full precision, and the alertable wait timeout rounds up.
- **SPIR-V texture exponent bias read the wrong word**
  (`spirv_translator_fetch.cpp`). The translator took `exp_adjust` from bits
  13:18 of fetch constant word 4, which are part of the LOD bias. Any
  texture with a negative LOD bias was scaled by up to 2^-32, so it came out
  near black. `exp_adjust` is in word 3, as `xenos.h` and the DXBC
  translator have it. Shader storage holds only guest microcode, so existing
  caches pick up the fix.

Debugging aids, off by default:

- `mnk_record=FILE`, `mnk_replay=FILE`, `mnk_replay_clock=polls|ms`: record
  key presses and replay them. Replay is timed by guest input polls (follows
  the game's frame rate) or wall-clock milliseconds, and ignores window
  focus so runs can go unattended.
- `vulkan_log_resolves` also logs each resolve's source render target and
  format, destination pitch and height, and MSAA sample settings.
- `gpu_log_texture_formats`: logs each distinct texture format, sign,
  swizzle, layout and exponent bias combination bound.
- `renderdoc_capture_trigger_file=PATH`: when `PATH` appears, captures the
  next guest frame (swap to swap) in RenderDoc and deletes the file. The
  RenderDoc API is looked up again at that point, since the instance looks
  before the Vulkan loader brings in the layer.
- `vulkan_shader_float_controls=false`: hides the device's shader float
  controls, so translated shaders don't declare `DenormFlushToZero` or
  `RoundingModeRTE`, which RenderDoc's shader debugger refuses.

## Status

The two fixes are general and suitable for upstream. The debugging aids are
optional.

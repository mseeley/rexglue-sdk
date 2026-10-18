# feature/ps5

Based on `feature/static-runtime`.

## Why

Lara Croft and the Guardian of Light is being ported to a homebrew PS5
(lcgol-mods' `docs/PS5.md`). The title is built with the open-source payload
SDK (PS5_PayloadSDK fork: FreeBSD-derived libc, libc++ 18) and links the
static RADV driver. The SDK did not compile for that target:

- `platform.h` stopped on any OS but glibc Linux, Android, macOS and Windows.
- SDL3, which has no PS5 port, was required for the entry point, windowing,
  input and audio.
- Several POSIX files used glibc or Linux-only interfaces.
- libc++ 18 lacks some C++20 library pieces.

## What

**Platform**

- The payload SDK's CMake toolchain sets `PS5`. The SDK then defines
  `REX_PLATFORM_PS5` with `REX_PLATFORM_LINUX`, as Android does with its own
  flag: the POSIX code paths apply, with PS5 exceptions.
- `platform.h` recognises `__PROSPERO__`.
- A PS5 build requires `REXGLUE_STATIC_RUNTIME` and
  `REXGLUE_VULKAN_LINKED_LOADER`.
- On the PS5 an empty `REXGLUE_VULKAN_LOADER_LIBRARY` means the game links
  the driver itself.

**SDL off: `REXGLUE_USE_SDL`.** The option defaults to `ON`, and to `OFF` on
the PS5. Without SDL:

- The SDL window, app context and key map, the SDL input driver, the SDL
  audio driver, and `windowed_app_main_sdl.cpp` are left out. The game
  provides `main`, its `WindowedAppContext` and `Window::Create`.
- The default audio backend is `NopAudioSystem`; the game can set its own in
  `OnPreSetup`.
- The X11 and Wayland pkg-config checks are skipped.

**Cross builds.** `CMAKE_CROSSCOMPILING` skips the `rexglue` tool and
`rexcodegen`. `rex::rexglue` is imported from `REXGLUE_HOST_TOOL`, a host
build that runs codegen.

**FreeBSD / payload SDK libc.** Each change is written for FreeBSD where it
applies there in general, and for the PS5 only where it doesn't:

| Area | Change |
|---|---|
| Large files (memory, mapped memory, file I/O) | Plain `mmap`, `ftruncate`, `fstat`, `fseeko`, `ftello`: `off_t` is 64-bit, as on macOS. |
| Threads | `pthread_getthreadid_np`; `pthread_set_name_np`/`pthread_get_name_np`; `cpuset_t` affinity; `pthread_kill` for user callbacks, as macOS does. |
| Robust mutexes | Off on the PS5, whose libc has none. The owner-death recovery path is not taken there. |
| Signal handler | Registers from FreeBSD's `mcontext_t` (`mc_rip`, `mc_rflags`, `mc_rax`…, `mc_err`; XMM from `mc_fpstate` as `struct savefpu`), both read and written back. SIGBUS is handled too, as on macOS. The PayloadSDK fork's `sys/_ucontext.h` corrects the console's context layout. |
| Clock | `CLOCK_MONOTONIC` (no `CLOCK_MONOTONIC_RAW`), with the tick frequency reported as 1 GHz, since ticks are nanoseconds. |
| `GetExecutablePath` | `/app0/eboot.bin` on the PS5 (no `/proc`). |
| `bit_scan_forward` | `__builtin_ffs` (no `ffs` export on the PS5). |
| STFS timestamps | `<chrono>`'s calendar on the PS5, which has no `timegm`. |
| libc++ 18 | The macOS workarounds now cover the PS5 too: `-fexperimental-library` for `std::jthread`, the `clock_cast` polyfill, floating-point `from_chars`. |
| RenderDoc | Stubbed on the PS5, as on macOS (the vendored header only knows Windows and Linux). This also guards `renderdoc_capture_trigger_file`, which didn't compile without the header. |
| FFmpeg | `thirdparty/ffmpeg-config-freebsd/config.h`, put ahead of FFmpeg's own `config.h` on FreeBSD targets. It is the Linux x86-64 config without memalign, sched_getaffinity, `<malloc.h>`, `<asm/types.h>`, perf events and iconv. The FFmpeg submodule is unchanged. |
| Link libraries | `rexcore` links no `pthread`/`rt`/`dl` on the PS5. |

## Status

- Builds the title on 2026-10-18: `librexruntime.a` and `librexgpu-xenos.a`
  compile with the payload SDK. lcgol-mods links and signs
  `dist/PPSA99030/eboot.bin` (M0 in its `docs/PS5.md`).
- The Linux static build of lcgol also compiles and links from this branch
  (not replayed). Its code paths only change in `bit_scan_forward`
  (`__builtin_ffs`) and the RenderDoc guard.
- Not run on a console yet.
- Still to do before the title can boot (M1):
  - The guest memory backend on the PS5: `ps5_shm` views in a reservation at
    `0x10_0000_0000`. The current `shm_open` path is charged to the 448 MiB
    flexible budget, and its base probe starts inside RADV's GPU window.
  - `QueryProtect` without `/proc/self/maps`.
  - The `VK_KHR_display` surface on VideoOut in the presenter.
  - The console log sink on `stderr` (the kernel log).
- The FreeBSD changes are general. The PS5 ones (`REX_PLATFORM_PS5`, SDL off)
  are specific to this port.

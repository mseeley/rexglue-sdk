# feature/static-runtime

Based on `feature/linux-gameplay`.

## Why

A generated project ships `librexruntime.so` and `librexgpu-<name>.so` next
to the executable. The SDK `dlopen`s the GPU plugin and the system Vulkan
loader. The PS5 refuses `dlopen` of any library a title brings, so the port
of Lara Croft and the Guardian of Light needs everything in one executable.
The same layout suits any platform that wants a self-contained binary.

## What

Two options, both off by default, so existing builds don't change.

**`REXGLUE_STATIC_RUNTIME`**

- **Static archives.** `rexruntime` and the GPU plugins build as static
  archives (`REX_STATIC_RUNTIME=1`).
- **Linked whole.** `rexglue_configure_target` links the runtime and the
  `GPU_PLUGINS` whole into the executable, through CMake's
  `LINK_LIBRARY_OVERRIDE_<lib> WHOLE_ARCHIVE`. That is needed because kernel
  exports and cvars register from static initializers that nothing references
  and a plain link would drop. The same helper,
  `rexglue_link_runtime_whole_archive`, covers the `rexglue` tool and the test
  executables.
- **Nothing staged.** No shared libraries are copied next to the executable.
- **Static plugin registry.**
  - A linked plugin registers its two entry points under its name
    (`RegisterStaticGpuPlugin`, called from a static initializer in
    `plugin_main.cpp`).
  - `LoadGpuPlugin` looks there first, so `--gpu_plugin=xenos` works unchanged
    and the game needs no code change.
  - The ABI check and construction are shared with the `dlopen` path.
- **Tracy.** Needs `REXGLUE_ENABLE_TRACY=OFF`, since the Tracy client is a
  shared library. Configure stops with a message otherwise.
- **SDK location.** Needs an in-tree SDK (`REXSDK_DIR`), since the installed
  package exports no static plugin targets.

**`REXGLUE_VULKAN_LINKED_LOADER`**

- `VulkanInstance` calls a `vkGetInstanceProcAddr` linked into the executable
  instead of `dlopen`ing the Vulkan loader. It then takes `vkDestroyInstance`
  from the created instance.
- `REXGLUE_VULKAN_LOADER_LIBRARY` names what provides that symbol. The default
  is the system loader (`find_library(vulkan)`), which links the Linux loader
  normally. On the PS5 it will be the static RADV driver, whose
  `vk_icdGetInstanceProcAddr` a small shim exposes as `vkGetInstanceProcAddr`.

The single-copy rule of `rexglue_link_audit.cmake` still holds: rexcore's
objects end up only in `librexruntime.a`.

## Status

Checked on 2026-10-18 with Lara Croft and the Guardian of Light on Linux
(Intel UHD, Mesa ANV), with both options on:

- It builds one 95 MB executable that loads no rexglue library.
- The system `libvulkan.so.1` is linked, not opened.
- The plugin comes from the static registry.
- The recorded key replay reaches Temple of Light and renders it correctly
  on FSI.

With both options off (the shared layout), the build from this branch is
the same size as before, and the replay also reaches Temple of Light.

General build options, suitable for upstream.

Not covered here: running `rexglue codegen` from a host build when
cross-compiling. That comes with the PS5 target on `feature/ps5`.

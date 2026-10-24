/**
 * @file        system/gpu_plugin_loader.cpp
 * @brief       Host-side loader for GPU emulation plugin DLLs
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/system/gpu_plugin.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/platform/dynlib.h>

namespace rex::system {

namespace {

#if !REX_STATIC_RUNTIME
// Plugin binaries follow the SDK's per-config postfix convention; this TU is
// part of rexruntime, so REXGLUE_BUILD_CONFIG matches the plugin's config.
std::string PluginFileName(std::string_view name) {
  constexpr std::string_view kConfig = REXGLUE_BUILD_CONFIG;
  std::string_view postfix = "";
  if (kConfig == "Debug") {
    postfix = "d";
  } else if (kConfig == "RelWithDebInfo") {
    postfix = "rd";
  }
#if REX_PLATFORM_WIN32
  return fmt::format("rexgpu-{}{}.dll", name, postfix);
#elif REX_PLATFORM_MAC
  return fmt::format("librexgpu-{}{}.dylib", name, postfix);
#else
  return fmt::format("librexgpu-{}{}.so", name, postfix);
#endif
}

// Plugins stay loaded for process lifetime: guest threads may still be in
// plugin code pages at shutdown.
std::vector<platform::DynamicLibrary>& LoadedPlugins() {
  static std::vector<platform::DynamicLibrary> plugins;
  return plugins;
}
#endif

struct StaticGpuPlugin {
  GpuAbiVersionFn abi_version_fn;
  GpuCreateFn create_fn;
};

std::unordered_map<std::string, StaticGpuPlugin>& StaticPlugins() {
  static std::unordered_map<std::string, StaticGpuPlugin> plugins;
  return plugins;
}

// Checks the ABI and constructs the graphics system; `source` names where the
// plugin came from in log lines.
std::unique_ptr<IGraphicsSystem> CreateFromPlugin(std::string_view name, std::string_view source,
                                                  GpuAbiVersionFn abi_version_fn,
                                                  GpuCreateFn create_fn, std::string_view backend) {
  uint32_t plugin_abi = abi_version_fn();
  if (plugin_abi != kGpuPluginAbiVersion) {
    REXSYS_ERROR("GPU plugin '{}' has ABI version {}, host expects {}: {}", name, plugin_abi,
                 kGpuPluginAbiVersion, source);
    return nullptr;
  }

  std::string backend_str(backend);
  GpuCreateInfo info{};
  info.struct_size = sizeof(GpuCreateInfo);
  info.backend = backend_str.c_str();

  IGraphicsSystem* graphics_system = create_fn(kGpuPluginAbiVersion, &info);
  if (!graphics_system) {
    REXSYS_ERROR("GPU plugin '{}' factory returned no graphics system (backend '{}')", name,
                 backend_str);
    return nullptr;
  }
  return std::unique_ptr<IGraphicsSystem>(graphics_system);
}

}  // namespace

void RegisterStaticGpuPlugin(const char* name, GpuAbiVersionFn abi_version_fn,
                             GpuCreateFn create_fn) {
  StaticPlugins()[name] = StaticGpuPlugin{abi_version_fn, create_fn};
}

std::unique_ptr<IGraphicsSystem> LoadGpuPlugin(std::string_view name, std::string_view backend) {
  auto static_plugin = StaticPlugins().find(std::string(name));
  if (static_plugin != StaticPlugins().end()) {
    auto graphics_system = CreateFromPlugin(name, "linked into the executable",
                                            static_plugin->second.abi_version_fn,
                                            static_plugin->second.create_fn, backend);
    if (graphics_system) {
      REXSYS_DEBUG("GPU plugin '{}' is linked into the executable", name);
    }
    return graphics_system;
  }

#if REX_STATIC_RUNTIME
  REXSYS_ERROR(
      "GPU plugin '{}' is not linked into this executable (GPU_PLUGINS {} in "
      "rexglue_configure_target).",
      name, name);
  return nullptr;
#else
  auto path = rex::filesystem::GetExecutableFolder() / PluginFileName(name);
  if (!std::filesystem::exists(path)) {
    REXSYS_ERROR(
        "GPU plugin '{}' not found at {}. Stage it next to the executable "
        "(GPU_PLUGINS {} in rexglue_configure_target).",
        name, path.string(), name);
    return nullptr;
  }

  platform::DynamicLibrary library;
  if (!library.Load(path, platform::SymbolResolution::kImmediate)) {
    REXSYS_ERROR("GPU plugin '{}' failed to load: {}", name, path.string());
    return nullptr;
  }

  auto abi_version_fn = library.GetSymbol<GpuAbiVersionFn>(kGpuAbiVersionSymbol);
  auto create_fn = library.GetSymbol<GpuCreateFn>(kGpuCreateSymbol);
  if (!abi_version_fn || !create_fn) {
    REXSYS_ERROR("GPU plugin '{}' is not a rexglue GPU plugin (missing {} / {} exports): {}", name,
                 kGpuAbiVersionSymbol, kGpuCreateSymbol, path.string());
    return nullptr;
  }

  auto graphics_system =
      CreateFromPlugin(name, path.string(), abi_version_fn, create_fn, backend);
  if (!graphics_system) {
    return nullptr;
  }

  LoadedPlugins().push_back(std::move(library));
  REXSYS_DEBUG("GPU plugin '{}' loaded ({})", name, path.filename().string());
  return graphics_system;
#endif
}

}  // namespace rex::system

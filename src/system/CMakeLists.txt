set(REXSYSTEM_SOURCES
    runtime.cpp
    embedded_metadata.cpp
    achievement_manager.cpp
    achievements.cpp
    kernel_state.cpp
    xmemory.cpp
    kernel_module.cpp
    user_module.cpp
    xthread.cpp
    xevent.cpp
    xfile.cpp
    xmutant.cpp
    xsemaphore.cpp
    xtimer.cpp
    xiocompletion.cpp
    xnotifylistener.cpp
    xenumerator.cpp
    xsocket.cpp
    xsymboliclink.cpp
    xobject.cpp
    xmodule.cpp
    guest_path.cpp

    # Utilities
    util/object_table.cpp
    util/native_list.cpp
    util/xdbf_utils.cpp
    util/gameinfo_utils.cpp

    # XAM services
    xam/app_manager.cpp
    xam/content_manager.cpp
    xam/user_profile.cpp

    ppc_types.cpp
    elf_module.cpp
    entry_table.cpp
    export_resolver.cpp
    lzx.cpp
    mmio_handler.cpp
    map_parser.cpp
    module.cpp
    function_dispatcher.cpp
    gpu_plugin_loader.cpp
    thread.cpp
    thread_state.cpp
    xex_module.cpp
)

if(REXGLUE_STATIC_RUNTIME)
    # Consumers link it whole (rexglue_link_runtime_whole_archive): kernel
    # exports and cvars register from static initializers that nothing else
    # references.
    add_library(rexruntime STATIC ${REXSYSTEM_SOURCES})
    target_compile_definitions(rexruntime PUBLIC REX_STATIC_RUNTIME=1)
else()
    add_library(rexruntime SHARED ${REXSYSTEM_SOURCES})
endif()
add_library(rex::runtime ALIAS rexruntime)
add_library(rex::system ALIAS rexruntime)
add_library(rex::kernel ALIAS rexruntime)

set_target_properties(rexruntime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

rex_target_pch(rexruntime
    <mutex>
    <unordered_map>
    <rex/math.h>
    <rex/string.h>
    <rex/memory.h>
    <rex/filesystem.h>
    <rex/thread.h>
    <rex/thread/mutex.h>
    <rex/ppc/context.h>
    <rex/system/kernel_state.h>
)

target_include_directories(rexruntime PUBLIC
    $<BUILD_INTERFACE:${REXGLUE_ROOT}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(rexruntime PRIVATE
    ${REXGLUE_ROOT}/thirdparty
)

target_link_libraries(rexruntime
    PUBLIC
        simde
        fmt::fmt
        spdlog::spdlog
        tomlplusplus
        $<$<BOOL:${REXGLUE_USE_SDL}>:SDL3::SDL3>
    PRIVATE
        aes128
        mspack
        xxHash::xxhash
)

if(REXGLUE_ENABLE_TRACY)
    # TARGET_NAME is what tells install(EXPORT) this text is a target to
    # namespace. Without it the plain name survives into the installed config
    # and consumers link a bare TracyClient they have no search path for.
    target_link_libraries(rexruntime PUBLIC
        $<$<NOT:$<CONFIG:Release>>:$<TARGET_NAME:TracyClient>>)
    target_compile_definitions(rexruntime PUBLIC
        $<$<NOT:$<CONFIG:Release>>:REXGLUE_ENABLE_PROFILING>)
endif()

if(REXGLUE_ENABLE_PERF_COUNTERS)
    target_compile_definitions(rexruntime PUBLIC
        $<$<NOT:$<CONFIG:Release>>:REXGLUE_ENABLE_PERF_COUNTERS>)
endif()

if(WIN32)
    target_link_libraries(rexruntime PRIVATE ws2_32)
    target_link_libraries(rexruntime PUBLIC dxgi dxguid)
    target_compile_definitions(rexruntime PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(REXGLUE_USE_D3D12)
    target_link_libraries(rexruntime PUBLIC dxc-headers)
    target_compile_definitions(rexruntime PUBLIC REX_HAS_D3D12=1)
endif()
# rex_app.cpp, compiled into the game, picks SDL audio by this; the
# directory-wide definition doesn't reach the game's target.
if(REXGLUE_USE_SDL)
    target_compile_definitions(rexruntime PUBLIC REX_HAS_SDL=1)
endif()
if(REXGLUE_USE_VULKAN)
    target_link_libraries(rexruntime PUBLIC
        Vulkan::Headers
        GPUOpen::VulkanMemoryAllocator
        spirv-tools-headers
    )
    target_compile_definitions(rexruntime PUBLIC REX_HAS_VULKAN=1)
    if(REXGLUE_VULKAN_LINKED_LOADER)
        # vulkan_instance.cpp (rexui) calls vkGetInstanceProcAddr directly;
        # whatever provides it is linked here.
        # On the PS5 the game links the driver (RADV and its
        # vkGetInstanceProcAddr shim) itself unless this names it.
        set(_rexglue_vulkan_loader "${REXGLUE_VULKAN_LOADER_LIBRARY}")
        if(NOT _rexglue_vulkan_loader AND NOT PS5)
            find_library(REXGLUE_SYSTEM_VULKAN_LOADER NAMES vulkan vulkan-1 REQUIRED)
            set(_rexglue_vulkan_loader "${REXGLUE_SYSTEM_VULKAN_LOADER}")
        endif()
        if(_rexglue_vulkan_loader)
            target_link_libraries(rexruntime PUBLIC ${_rexglue_vulkan_loader})
        endif()
    endif()
endif()

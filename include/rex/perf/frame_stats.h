/**
 * @file        perf/frame_stats.h
 * @brief       Per-second frame statistics log, built in every configuration
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <chrono>
#include <cstdint>

namespace rex::perf::frame_stats {

// Totals that only ever grow. The logger prints the difference since its last
// line, so any thread can add to them at any time.
enum class Stat : uint32_t {
  kCpSwaps,     // swaps executed by the GPU command processor
  kCpIdleUs,    // command processor waiting for guest commands
  kCpWaitUs,    // command processor in WAIT_REG_MEM, waiting on a register or memory
  kFenceWaits,  // blocking waits on a GPU submission fence
  kFenceWaitUs,
  kDraws,
  kPipelinesCreated,  // vkCreateGraphicsPipelines calls, any thread
  kPipelineCreateUs,
  kPipelineCreateCpuUs,  // thread CPU time of the above
  kPipelineBlockUs,      // command processor blocked on a pipeline cache miss
  kShadersTranslated,
  kShaderTranslateUs,
  kShaderTranslateCpuUs,  // thread CPU time of the above
  kCpTranslateUs,         // command processor translating shaders itself
  kShaderModuleUs,        // vkCreateShaderModule, part of kShaderTranslateUs
  kShaderModuleCpuUs,
  kShaderTranslateSysUs,  // kernel part of kShaderTranslateCpuUs (getrusage)
  kShaderTranslateFaults,
  kShaderTranslatePreempts,  // involuntary context switches
  kShaderTranslateBlocks,    // voluntary context switches: waits, such as on a lock
  kPixelShadersTranslated,   // the pixel shader part of kShadersTranslated
  kPixelShaderTranslateUs,
  kPixelShaderTranslateCpuUs,
  kTextureLoads,
  kTextureLoadUs,
  kFileReads,
  kFileReadBytes,
  kFileReadUs,
  kAccessFaults,  // guest write-watch access violations
  kAccessFaultUs,
  kXmaDrained,  // XMA context kicked with its output ring already empty
  kXmaStarved,  // ... and no valid input buffer to decode either
  kGuestSwapThreadCpuUs,  // CPU time of the thread calling VdSwap, between its swaps
  kGpuBusyUs,             // GPU time of command processor submissions (timestamps)
  kGpuSubmissions,        // submissions whose GPU time was read
  // The command processor thread: its CPU time between swaps, and the wall
  // time of the parts of a Vulkan draw and a submission (LapTimer).
  kCpCpuUs,
  kCpDrawSetupUs,         // submission, primitive processing, shaders, samplers
  kCpDrawTexturesUs,      // texture cache RequestTextures
  kCpDrawRenderTargetsUs, // render target cache Update
  kCpDrawPipelineConfigureUs,  // pipeline cache ConfigurePipeline
  kCpDrawPipelineUs,           // pipeline lookup by handle and binding
  kCpDrawStateUs,         // dynamic state, system constants, descriptor bindings
  kCpDrawMemoryUs,        // vertex buffer and memexport ranges in shared memory
  kCpDrawRecordUs,        // barriers, render pass, the draw command
  kCpReplayUs,            // deferred command buffer into the Vulkan one
  kCpQueueSubmitUs,       // vkQueueSubmit
  kCpOutputUs,            // presenter RefreshGuestOutput, including the frame's last submission
  kCpPackets,             // PM4 packets executed
  kCpRegisterWrites,      // registers written one at a time (WriteRegister)
  kCpBulkRegisterWrites,  // registers written as a block (WriteRegistersFromMem)
  kHostTickReads,         // Clock::QueryHostTickCount calls, any thread
  kHostSystemTimeReads,   // Clock::QueryHostSystemTime calls
  kGuestTickReads,        // Clock::QueryGuestTickCount calls (the guest's time base)
  kResolves,              // EDRAM resolves (VulkanRenderTargetCache::Resolve)
  kRenderTargetTransfers, // host render target ownership transfers
  kCount
};

bool enabled();
// log_frame_stats_draw_parts: time the parts of each draw as well.
bool draw_parts();
// log_shader_origins: physical allocations with their guest callers, file
// reads into guest memory, and each shader's first IM_LOAD - to find where and
// when the game creates its shaders, ahead of their first draw.
bool log_shader_origins();

// The write fault the calling thread is handling while it runs the memory
// watch callbacks: the host instruction and the host address written. Zero
// when a callback runs for another reason, such as a file read.
struct WriteFault {
  uintptr_t pc = 0;
  uintptr_t host_address = 0;
};
WriteFault& CurrentWriteFault();
void Add(Stat stat, uint64_t value);
uint64_t NowUs();
// CPU time used by the calling thread.
uint64_t ThreadCpuUs();

// The calling thread's resource usage, where getrusage(RUSAGE_THREAD) exists;
// zeros elsewhere.
struct ThreadUsage {
  uint64_t sys_us = 0;
  uint64_t faults = 0;
  uint64_t preempts = 0;
  uint64_t blocks = 0;
};
ThreadUsage GetThreadUsage();

// Called by VdSwap on the guest thread; logs once a second and for each slow
// frame.
void OnGuestSwap();

// Times consecutive parts of one function: each Lap adds the time since the
// previous Lap (or construction) to a stat. Inactive when constructed with
// false (by default, while log_frame_stats is off).
class LapTimer {
 public:
  explicit LapTimer(bool active = enabled()) : last_(active ? NowUs() : 0) {}
  void Lap(Stat stat) {
    if (!last_) {
      return;
    }
    uint64_t now = NowUs();
    Add(stat, now - last_);
    last_ = now;
  }

 private:
  uint64_t last_;
};

// Adds the elapsed microseconds to a stat, optionally the thread CPU time
// used meanwhile to another, and optionally counts one event.
class ScopedTimer {
 public:
  explicit ScopedTimer(Stat time_stat, Stat count_stat = Stat::kCount,
                       Stat cpu_time_stat = Stat::kCount)
      : time_stat_(time_stat),
        count_stat_(count_stat),
        cpu_time_stat_(cpu_time_stat),
        start_(enabled() ? NowUs() : 0),
        cpu_start_(start_ && cpu_time_stat != Stat::kCount ? ThreadCpuUs() : 0) {}
  ~ScopedTimer() {
    if (!start_) {
      return;
    }
    Add(time_stat_, NowUs() - start_);
    if (cpu_time_stat_ != Stat::kCount) {
      Add(cpu_time_stat_, ThreadCpuUs() - cpu_start_);
    }
    if (count_stat_ != Stat::kCount) {
      Add(count_stat_, 1);
    }
  }
  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

 private:
  Stat time_stat_;
  Stat count_stat_;
  Stat cpu_time_stat_;
  uint64_t start_;
  uint64_t cpu_start_;
};

}  // namespace rex::perf::frame_stats

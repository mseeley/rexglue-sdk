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
  kTextureLoads,
  kTextureLoadUs,
  kFileReads,
  kFileReadBytes,
  kFileReadUs,
  kAccessFaults,  // guest write-watch access violations
  kAccessFaultUs,
  kXmaDrained,  // XMA context kicked with its output ring already empty
  kXmaStarved,  // ... and no valid input buffer to decode either
  kCount
};

bool enabled();
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
};
ThreadUsage GetThreadUsage();

// Called by VdSwap on the guest thread; logs once a second and for each slow
// frame.
void OnGuestSwap();

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

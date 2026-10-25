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
  kPipelineBlockUs,  // command processor blocked on a pipeline cache miss
  kShadersTranslated,
  kShaderTranslateUs,
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

// Called by VdSwap on the guest thread; logs once a second and for each slow
// frame.
void OnGuestSwap();

// Adds the elapsed microseconds to a stat, and optionally counts one event.
class ScopedTimer {
 public:
  explicit ScopedTimer(Stat time_stat, Stat count_stat = Stat::kCount)
      : time_stat_(time_stat), count_stat_(count_stat), start_(enabled() ? NowUs() : 0) {}
  ~ScopedTimer() {
    if (!start_) {
      return;
    }
    Add(time_stat_, NowUs() - start_);
    if (count_stat_ != Stat::kCount) {
      Add(count_stat_, 1);
    }
  }
  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

 private:
  Stat time_stat_;
  Stat count_stat_;
  uint64_t start_;
};

}  // namespace rex::perf::frame_stats

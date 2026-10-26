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
  kGuestSwapThreadWaits,    // ... its guest kernel waits on objects (Ke/NtWaitFor*)
  kGuestSwapThreadWaitUs,   // ... and their time
  kGuestSwapThreadDelayUs,  // ... its KeDelayExecutionThread and NtYieldExecution time
  kGpuBusyUs,             // GPU time of command processor submissions (timestamps)
  kGpuSubmissions,        // submissions whose GPU time was read
  // The command processor thread: its CPU time between swaps, and the wall
  // time of the parts of a Vulkan draw and a submission (LapTimer).
  kCpCpuUs,
  kCpSysUs,               // ... of it in the kernel
  kCpPreempts,            // involuntary context switches of the command processor thread
  kCpBlocks,              // voluntary context switches (waits) of the command processor thread
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

// Whether this is the thread that last called VdSwap.
bool IsGuestSwapThread();

// log_frame_stats_swap_waits: with log_frame_stats, the swap thread's guest
// kernel waits by guest caller and object, and the threads that signal those
// objects, once a second.
bool swap_waits();

// What a guest kernel wait waits on, for log_frame_stats_swap_waits: the
// guest return address of the wait call and its caller's, the first object (the host
// XObject, any type, as a key), its guest address and handle, its type, and
// how many objects the wait names.
struct WaitTarget {
  uint32_t caller = 0;
  uint32_t caller2 = 0;
  const void* object = nullptr;
  uint32_t guest_object = 0;
  uint32_t handle = 0;
  const char* type = "";
  uint32_t count = 1;
};

// Whether a wait on this thread is counted per target: the stats and the
// swap waits are on, and this is the swap thread. The kernel fills a
// WaitTarget only then.
bool WantWaitTarget();

// A kernel object was signalled (event set or pulsed, semaphore or mutant
// released) by the calling thread. Cheap while log_frame_stats_swap_waits is
// off.
void NoteSignalSlow(const void* object);
inline void NoteSignal(const void* object) {
  if (swap_waits()) {
    NoteSignalSlow(object);
  }
}

// log_frame_stats_cp_waits: with log_frame_stats, the command processor's
// WAIT_REG_MEM waits by what they poll, once a second.
bool cp_waits();
// One finished WAIT_REG_MEM: a register (or physical memory address, with
// memory), the compare function, reference, mask and wait interval from the
// packet, the polls it took and the last value read.
struct CpWait {
  bool memory = false;
  uint32_t address = 0;
  uint32_t function = 0;
  uint32_t ref = 0;
  uint32_t mask = 0;
  uint32_t interval = 0;
};
void NoteCpWait(const CpWait& wait, uint64_t us, uint32_t polls, uint32_t sleeps,
                uint32_t last_value);

// log_frame_stats_timeline_ms: what happened when in each slow frame. The
// vsync thread, the command processor and the swap thread note events; the
// swap thread logs those of a frame at least that long at its swap.
enum class TimelineEvent : uint8_t {
  kVblank,          // the guest vblank (GraphicsSystem::MarkVblank)
  kCpInterrupt,     // the command processor dispatching a guest interrupt
  kCpWaitStart,     // a WAIT_REG_MEM that has to sleep, at its first sleep
  kCpWaitEnd,       // ... and when it is satisfied
  kCpSwap,          // the command processor executing the swap packet
  kGameWaitStart,   // a guest kernel wait on the swap thread
  kGameWaitEnd,
  kCount
};
bool timeline();
void NoteTimelineSlow(TimelineEvent event);
inline void NoteTimeline(TimelineEvent event) {
  if (timeline()) {
    NoteTimelineSlow(event);
  }
}

// Adds the time of a guest kernel wait or delay to the swap thread's stats,
// when on that thread; with a WaitTarget, to its per-target tally as well.
class SwapThreadWait {
 public:
  explicit SwapThreadWait(bool delay)
      : delay_(delay), start_(enabled() && IsGuestSwapThread() ? NowUs() : 0) {
    if (start_ && !delay_) {
      NoteTimeline(TimelineEvent::kGameWaitStart);
    }
  }
  ~SwapThreadWait() {
    if (!start_) {
      return;
    }
    if (!delay_) {
      NoteTimeline(TimelineEvent::kGameWaitEnd);
    }
    uint64_t us = NowUs() - start_;
    if (delay_) {
      Add(Stat::kGuestSwapThreadDelayUs, us);
    } else {
      Add(Stat::kGuestSwapThreadWaitUs, us);
      Add(Stat::kGuestSwapThreadWaits, 1);
      if (target_.object) {
        NoteSwapThreadWait(target_, us);
      }
    }
  }
  SwapThreadWait(const SwapThreadWait&) = delete;
  SwapThreadWait& operator=(const SwapThreadWait&) = delete;

  // What the wait is on, when WantWaitTarget().
  void set_target(const WaitTarget& target) { target_ = target; }

 private:
  static void NoteSwapThreadWait(const WaitTarget& target, uint64_t us);
  bool delay_;
  uint64_t start_;
  WaitTarget target_;
};

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

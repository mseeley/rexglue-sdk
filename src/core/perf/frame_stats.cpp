/**
 * @file        perf/frame_stats.cpp
 * @brief       Per-second frame statistics log, built in every configuration
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/perf/frame_stats.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#include <time.h>
#endif

#include <fmt/format.h>

REXCVAR_DEFINE_BOOL(log_frame_stats, false, "Perf",
                    "Log frame rate, GPU waits, pipeline, texture and file activity once a "
                    "second, and every frame slower than log_frame_stats_slow_ms");
REXCVAR_DEFINE_INT32(log_frame_stats_slow_ms, 100, "Perf",
                     "Frames slower than this get their own log_frame_stats line");

REXCVAR_DEFINE_BOOL(log_shader_origins, false, "Perf",
                    "Log physical allocations with their guest callers, file reads into guest "
                    "memory, and each shader's first IM_LOAD");

namespace rex::perf::frame_stats {

namespace {

constexpr size_t kStatCount = size_t(Stat::kCount);
using Totals = std::array<uint64_t, kStatCount>;

std::array<std::atomic<uint64_t>, kStatCount> g_totals{};

Totals Snapshot() {
  Totals totals;
  for (size_t i = 0; i < kStatCount; ++i) {
    totals[i] = g_totals[i].load(std::memory_order_relaxed);
  }
  return totals;
}

std::string FormatStats(const Totals& now, const Totals& before) {
  auto d = [&](Stat stat) { return now[size_t(stat)] - before[size_t(stat)]; };
  auto ms = [&](Stat stat) { return double(d(stat)) / 1000.0; };
  return fmt::format(
      "cp swaps {} idle {:.0f} ms wait {:.0f} ms | fence {} waits {:.1f} ms | draws {} | "
      "pipelines {} ({:.1f} ms, cpu {:.1f} ms, cp blocked {:.1f} ms) | shaders {} ({:.1f} ms, "
      "cpu {:.1f} ms, cp {:.1f} ms, module {:.1f} ms cpu {:.1f} ms, sys {:.1f} ms, "
      "faults {}, preempts {}, blocks {}) | "
      "textures {} ({:.1f} ms) | files {} {:.2f} MB ({:.1f} ms) | faults {} ({:.1f} ms) | xma "
      "drained {} starved {}",
      d(Stat::kCpSwaps), ms(Stat::kCpIdleUs), ms(Stat::kCpWaitUs), d(Stat::kFenceWaits),
      ms(Stat::kFenceWaitUs), d(Stat::kDraws), d(Stat::kPipelinesCreated),
      ms(Stat::kPipelineCreateUs), ms(Stat::kPipelineCreateCpuUs), ms(Stat::kPipelineBlockUs),
      d(Stat::kShadersTranslated), ms(Stat::kShaderTranslateUs), ms(Stat::kShaderTranslateCpuUs),
      ms(Stat::kCpTranslateUs), ms(Stat::kShaderModuleUs), ms(Stat::kShaderModuleCpuUs),
      ms(Stat::kShaderTranslateSysUs), d(Stat::kShaderTranslateFaults),
      d(Stat::kShaderTranslatePreempts), d(Stat::kShaderTranslateBlocks), d(Stat::kTextureLoads),
      ms(Stat::kTextureLoadUs), d(Stat::kFileReads),
      double(d(Stat::kFileReadBytes)) / (1024.0 * 1024.0), ms(Stat::kFileReadUs),
      d(Stat::kAccessFaults), ms(Stat::kAccessFaultUs), d(Stat::kXmaDrained), d(Stat::kXmaStarved));
}

}  // namespace

bool enabled() {
  return REXCVAR_GET(log_frame_stats);
}

bool log_shader_origins() {
  return REXCVAR_GET(log_shader_origins);
}

WriteFault& CurrentWriteFault() {
  thread_local WriteFault write_fault;
  return write_fault;
}

void Add(Stat stat, uint64_t value) {
  g_totals[size_t(stat)].fetch_add(value, std::memory_order_relaxed);
}

uint64_t NowUs() {
  // Never 0, which ScopedTimer uses for "not timing".
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count()) +
         1;
}

uint64_t ThreadCpuUs() {
#ifdef _WIN32
  FILETIME creation, exit, kernel, user;
  if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) {
    return 0;
  }
  auto to_100ns = [](const FILETIME& time) {
    return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
  };
  return (to_100ns(kernel) + to_100ns(user)) / 10;
#else
  timespec time;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time)) {
    return 0;
  }
  return uint64_t(time.tv_sec) * 1000000 + uint64_t(time.tv_nsec) / 1000;
#endif
}

ThreadUsage GetThreadUsage() {
  ThreadUsage usage;
#if defined(RUSAGE_THREAD)
  rusage thread_usage;
  if (!getrusage(RUSAGE_THREAD, &thread_usage)) {
    usage.sys_us =
        uint64_t(thread_usage.ru_stime.tv_sec) * 1000000 + uint64_t(thread_usage.ru_stime.tv_usec);
    usage.faults = uint64_t(thread_usage.ru_minflt) + uint64_t(thread_usage.ru_majflt);
    usage.preempts = uint64_t(thread_usage.ru_nivcsw);
    usage.blocks = uint64_t(thread_usage.ru_nvcsw);
  }
#endif
  return usage;
}

void OnGuestSwap() {
  if (!enabled()) {
    return;
  }
  static std::mutex mutex;
  static uint64_t last_swap_us = 0;
  static uint64_t window_start_us = 0;
  static Totals last_swap_totals{};
  static Totals window_totals{};
  static uint32_t frames = 0, frames_17 = 0, frames_34 = 0, frames_slower = 0;
  static uint64_t worst_us = 0;

  std::lock_guard<std::mutex> lock(mutex);
  uint64_t now_us = NowUs();
  Totals totals = Snapshot();
  if (!last_swap_us) {
    last_swap_us = window_start_us = now_us;
    last_swap_totals = window_totals = totals;
    return;
  }

  uint64_t frame_us = now_us - last_swap_us;
  ++frames;
  if (frame_us <= 17500) {
    ++frames_17;
  } else if (frame_us <= 34000) {
    ++frames_34;
  } else {
    ++frames_slower;
  }
  worst_us = std::max(worst_us, frame_us);
  if (frame_us >= uint64_t(std::max(REXCVAR_GET(log_frame_stats_slow_ms), 1)) * 1000) {
    REXLOG_INFO("frame_stats: slow frame {:.1f} ms | {}", double(frame_us) / 1000.0,
                FormatStats(totals, last_swap_totals));
  }
  last_swap_us = now_us;
  last_swap_totals = totals;

  uint64_t window_us = now_us - window_start_us;
  if (window_us < 1000000) {
    return;
  }
  REXLOG_INFO(
      "frame_stats: {:.1f} fps | frames <=17ms {} <=34ms {} slower {} | worst {:.1f} ms | {}",
      double(frames) * 1000000.0 / double(window_us), frames_17, frames_34, frames_slower,
      double(worst_us) / 1000.0, FormatStats(totals, window_totals));
  window_start_us = now_us;
  window_totals = totals;
  frames = frames_17 = frames_34 = frames_slower = 0;
  worst_us = 0;
}

}  // namespace rex::perf::frame_stats

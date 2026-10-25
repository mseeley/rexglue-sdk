/**
 * @file        perf/frame_stats.cpp
 * @brief       Per-second frame statistics log, built in every configuration
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/perf/frame_stats.h>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#include <time.h>
#endif

#include <fmt/format.h>

#if defined(__x86_64__) || defined(_M_X64)
#define REX_FRAME_STATS_TSC 1
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#else
#define REX_FRAME_STATS_TSC 0
#endif

REXCVAR_DEFINE_BOOL(log_frame_stats, false, "Perf",
                    "Log frame rate, GPU waits, pipeline, texture and file activity once a "
                    "second, and every frame slower than log_frame_stats_slow_ms");
REXCVAR_DEFINE_BOOL(log_frame_stats_draw_parts, false, "Perf",
                    "With log_frame_stats, also time the parts of each draw on the command "
                    "processor (eight timer reads a draw)");
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
      "faults {}, preempts {}, blocks {}; pixel {} ({:.1f} ms, cpu {:.1f} ms)) | "
      "textures {} ({:.1f} ms) | files {} {:.2f} MB ({:.1f} ms) | faults {} ({:.1f} ms) | xma "
      "drained {} starved {} | guest swap thread cpu {:.1f} ms | gpu {:.1f} ms ({} "
      "submissions) | cp cpu {:.1f} ms, packets {}, registers {} + {} in blocks | draw setup "
      "{:.1f} textures {:.1f} rts {:.1f} pipeline configure {:.1f} bind {:.1f} state {:.1f} "
      "memory {:.1f} record {:.1f} ms | replay {:.1f} submit {:.1f} output {:.1f} ms | clock "
      "reads host tick {} system {} guest tick {} | resolves {} rt transfers {}",
      d(Stat::kCpSwaps), ms(Stat::kCpIdleUs), ms(Stat::kCpWaitUs), d(Stat::kFenceWaits),
      ms(Stat::kFenceWaitUs), d(Stat::kDraws), d(Stat::kPipelinesCreated),
      ms(Stat::kPipelineCreateUs), ms(Stat::kPipelineCreateCpuUs), ms(Stat::kPipelineBlockUs),
      d(Stat::kShadersTranslated), ms(Stat::kShaderTranslateUs), ms(Stat::kShaderTranslateCpuUs),
      ms(Stat::kCpTranslateUs), ms(Stat::kShaderModuleUs), ms(Stat::kShaderModuleCpuUs),
      ms(Stat::kShaderTranslateSysUs), d(Stat::kShaderTranslateFaults),
      d(Stat::kShaderTranslatePreempts), d(Stat::kShaderTranslateBlocks),
      d(Stat::kPixelShadersTranslated), ms(Stat::kPixelShaderTranslateUs),
      ms(Stat::kPixelShaderTranslateCpuUs), d(Stat::kTextureLoads),
      ms(Stat::kTextureLoadUs), d(Stat::kFileReads),
      double(d(Stat::kFileReadBytes)) / (1024.0 * 1024.0), ms(Stat::kFileReadUs),
      d(Stat::kAccessFaults), ms(Stat::kAccessFaultUs), d(Stat::kXmaDrained), d(Stat::kXmaStarved),
      ms(Stat::kGuestSwapThreadCpuUs), ms(Stat::kGpuBusyUs), d(Stat::kGpuSubmissions),
      ms(Stat::kCpCpuUs), d(Stat::kCpPackets), d(Stat::kCpRegisterWrites),
      d(Stat::kCpBulkRegisterWrites), ms(Stat::kCpDrawSetupUs), ms(Stat::kCpDrawTexturesUs),
      ms(Stat::kCpDrawRenderTargetsUs), ms(Stat::kCpDrawPipelineConfigureUs),
      ms(Stat::kCpDrawPipelineUs), ms(Stat::kCpDrawStateUs),
      ms(Stat::kCpDrawMemoryUs), ms(Stat::kCpDrawRecordUs), ms(Stat::kCpReplayUs),
      ms(Stat::kCpQueueSubmitUs), ms(Stat::kCpOutputUs), d(Stat::kHostTickReads),
      d(Stat::kHostSystemTimeReads), d(Stat::kGuestTickReads), d(Stat::kResolves),
      d(Stat::kRenderTargetTransfers));
}

}  // namespace

bool enabled() {
  return REXCVAR_GET(log_frame_stats);
}

bool draw_parts() {
  return REXCVAR_GET(log_frame_stats) && REXCVAR_GET(log_frame_stats_draw_parts);
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

namespace {

uint64_t SteadyNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

#if REX_FRAME_STATS_TSC
// The timers read the time stamp counter: a few nanoseconds, where
// steady_clock can be a system call (about 0.75 us on the PS5, PS5 run 3).
// Calibrated against steady_clock over 20 ms the first time.
struct TscScale {
  uint64_t base_tsc;
  double us_per_tick;
};

const TscScale& GetTscScale() {
  static const TscScale scale = [] {
    uint64_t start_ns = SteadyNs();
    uint64_t start_tsc = __rdtsc();
    uint64_t end_ns;
    do {
      end_ns = SteadyNs();
    } while (end_ns - start_ns < 20000000);
    uint64_t end_tsc = __rdtsc();
    return TscScale{start_tsc, double(end_ns - start_ns) / 1000.0 / double(end_tsc - start_tsc)};
  }();
  return scale;
}
#endif

// Keeps the timed reads in LogClockCosts from being optimised away.
std::atomic<uint64_t> g_clock_cost_sink{0};

// Once, the first time the stats are on: what a timer read costs.
void LogClockCosts() {
  constexpr int kReads = 1000;
  uint64_t sink = 0;
  auto per_read_ns = [&](auto read) {
    uint64_t start = SteadyNs();
    for (int i = 0; i < kReads; ++i) {
      sink += read();
    }
    double ns = double(SteadyNs() - start) / kReads;
    g_clock_cost_sink.fetch_add(sink, std::memory_order_relaxed);
    return ns;
  };
  double steady_ns = per_read_ns([] { return SteadyNs(); });
  double now_us_ns = per_read_ns([] { return NowUs(); });
  double host_tick_ns = per_read_ns([] { return rex::chrono::Clock::QueryHostTickCount(); });
#if REX_FRAME_STATS_TSC
  double tsc_mhz = 1.0 / GetTscScale().us_per_tick;
#else
  double tsc_mhz = 0.0;
#endif
  REXLOG_INFO(
      "frame_stats: timer read costs: steady_clock {:.0f} ns, frame_stats timer {:.0f} ns "
      "(TSC {:.0f} MHz, 0 when not used), Clock::QueryHostTickCount {:.0f} ns",
      steady_ns, now_us_ns, tsc_mhz, host_tick_ns);
}

}  // namespace

uint64_t NowUs() {
  // Never 0, which ScopedTimer uses for "not timing".
#if REX_FRAME_STATS_TSC
  const TscScale& scale = GetTscScale();
  return uint64_t(double(__rdtsc() - scale.base_tsc) * scale.us_per_tick) + 1;
#else
  return SteadyNs() / 1000 + 1;
#endif
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
  static std::once_flag clock_costs_logged;
  std::call_once(clock_costs_logged, LogClockCosts);
  static std::mutex mutex;
  static uint64_t last_swap_us = 0;
  static uint64_t window_start_us = 0;
  static Totals last_swap_totals{};
  static Totals window_totals{};
  static uint32_t frames = 0, frames_17 = 0, frames_34 = 0, frames_slower = 0;
  static uint64_t worst_us = 0;

  std::lock_guard<std::mutex> lock(mutex);
  // The swapping thread's CPU time since its previous swap: the game's frame
  // work, without the time it waited (for a vblank, the GPU or ring space).
  static std::thread::id swap_thread;
  static uint64_t swap_thread_cpu_us = 0;
  uint64_t cpu_us = ThreadCpuUs();
  if (swap_thread == std::this_thread::get_id()) {
    Add(Stat::kGuestSwapThreadCpuUs, cpu_us - swap_thread_cpu_us);
  }
  swap_thread = std::this_thread::get_id();
  swap_thread_cpu_us = cpu_us;
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

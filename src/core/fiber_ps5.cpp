/**
 * @file        rex/core/fiber_ps5.cpp
 * @brief       PS5 backend for rex::thread::Fiber: thread fibers only
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/platform.h>
#if REX_PLATFORM_PS5

#include <rex/thread/fiber.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

// The console's getcontext writes more than the ucontext_t its headers
// declare, past the end of the Fiber that holds it, into the next heap chunk.
// Every guest thread converts itself (XThread::Execute), so the POSIX backend
// corrupted the heap on each thread start. Thread fibers need no saved
// context until something switches away from them; guest fibers (CreateFiber,
// SwitchToFiber) would need a context switch of our own and are not
// supported. holdmysocks/mcla-recomp's fiber_ps5.cpp does the same.

namespace rex::thread {

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  auto* f = new Fiber();
  f->is_thread_fiber_ = true;
  tls_current_ = f;
  return f;
}

Fiber* Fiber::Create(size_t, void (*)(void*), void*) {
  std::fputs("rex::thread::Fiber::Create: guest fibers are not supported on the PS5\n", stderr);
  return nullptr;
}

void Fiber::SwitchTo(Fiber*) {
  std::fputs("rex::thread::Fiber::SwitchTo: guest fibers are not supported on the PS5\n", stderr);
  std::abort();
}

void Fiber::Destroy() {
  if (is_thread_fiber_) {
    tls_current_ = nullptr;
  } else {
    assert(this != tls_current_ && "Destroy called on the currently running fiber");
  }
  delete this;
}

}  // namespace rex::thread

#endif  // REX_PLATFORM_PS5

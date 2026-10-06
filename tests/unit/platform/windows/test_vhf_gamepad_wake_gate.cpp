/**
 * @file tests/unit/platform/windows/test_vhf_gamepad_wake_gate.cpp
 * @brief Test src/platform/windows/vhf_gamepad_wake_gate.h's predicate/notify discipline.
 * @details `wake_gate` is deliberately free of Windows/libvirtualgamepad headers (unlike
 *          `vhf_gamepad_policy.h`), so unlike `test_vhf_gamepad_policy.cpp` these tests build and
 *          run on every platform, not just under `#ifdef _WIN32`.
 */
#include "../../../tests_common.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <src/platform/windows/vhf_gamepad_wake_gate.h>

using namespace std::chrono_literals;
using platf::vhf_gamepad::wake_gate;

namespace {
  // Bounds every wait below so a regression that reintroduces the lost-wakeup race fails this
  // test instead of hanging the whole suite.
  constexpr auto k_join_timeout = 5s;

  /**
   * @brief Waits for `done` to become `true`, then joins `thread`; fails instead of hanging if
   *        `done` never flips within `k_join_timeout`.
   * @param thread The thread under test, blocked in a bounded `wake_gate` wait.
   * @param done Set by `thread` immediately before it returns.
   */
  void join_or_fail(std::thread &thread, const std::atomic<bool> &done) {
    const auto deadline = std::chrono::steady_clock::now() + k_join_timeout;
    while (!done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(1ms);
    }
    if (!done.load(std::memory_order_acquire)) {
      // Detach rather than join: a genuine regression here means the thread is blocked forever,
      // and joining it would hang the test binary instead of just failing this test.
      thread.detach();
      FAIL() << "wake_gate wait() never observed the notified predicate (possible lost wakeup)";
      return;
    }
    thread.join();
  }
}  // namespace

/**
 * @brief Test fixture for `platf::vhf_gamepad::wake_gate`.
 */
class WakeGateTest: public BaseTest {};

// Mirrors vhf_gamepad_t::impl_t::feedback_loop()'s indefinite wait plus vhf_gamepad_t::alloc()'s
// active_count increment: a waiter idle on an empty predicate must wake once a slot is allocated.
TEST_F(WakeGateTest, IdleToAllocationWakesWaiter) {
  wake_gate gate;
  std::atomic<bool> started {false};
  std::atomic<bool> active {false};
  std::atomic<bool> done {false};

  std::thread waiter {[&] {
    gate.wait([&] {
      started.store(true, std::memory_order_release);
      return active.load(std::memory_order_acquire);
    });
    done.store(true, std::memory_order_release);
  }};

  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  // Mirrors alloc(): mutate the predicate state under the gate, then notify.
  gate.notify_all([&] {
    active.store(true, std::memory_order_release);
  });

  join_or_fail(waiter, done);
  EXPECT_TRUE(active.load(std::memory_order_acquire));
}

// Mirrors vhf_gamepad_t::impl_t::feedback_loop()'s indefinite wait plus ~vhf_gamepad_t()'s
// `stopping` store: a waiter idle on an empty predicate must wake on shutdown, not just on an
// allocation, so the destructor's join() cannot hang.
TEST_F(WakeGateTest, IdleToShutdownWakesWaiter) {
  wake_gate gate;
  std::atomic<bool> started {false};
  std::atomic<bool> stopping {false};
  std::atomic<bool> active {false};
  std::atomic<bool> done {false};

  std::thread waiter {[&] {
    gate.wait([&] {
      started.store(true, std::memory_order_release);
      return stopping.load(std::memory_order_acquire) || active.load(std::memory_order_acquire);
    });
    done.store(true, std::memory_order_release);
  }};

  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  // Mirrors ~vhf_gamepad_t(): mutate `stopping` under the gate, then notify.
  gate.notify_all([&] {
    stopping.store(true, std::memory_order_release);
  });

  join_or_fail(waiter, done);
  EXPECT_TRUE(stopping.load(std::memory_order_acquire));
  EXPECT_FALSE(active.load(std::memory_order_acquire));
}

// Mirrors feedback_loop()'s bounded poll-interval wait: no notification before the timeout means
// wait_for() reports the timeout, not a spurious success.
TEST_F(WakeGateTest, WaitForExpiresWhenPredicateStaysFalse) {
  wake_gate gate;
  const bool woke = gate.wait_for(20ms, [] {
    return false;
  });
  EXPECT_FALSE(woke);
}

// Mirrors feedback_loop()'s bounded poll-interval wait, but with shutdown landing inside the
// window: wait_for() must still report success instead of running out the full timeout.
TEST_F(WakeGateTest, WaitForReturnsTrueWhenNotifiedBeforeTimeout) {
  wake_gate gate;
  std::atomic<bool> started {false};
  std::atomic<bool> stopping {false};
  std::atomic<bool> woke {false};
  std::atomic<bool> done {false};

  std::thread waiter {[&] {
    const bool result = gate.wait_for(k_join_timeout, [&] {
      started.store(true, std::memory_order_release);
      return stopping.load(std::memory_order_acquire);
    });
    woke.store(result, std::memory_order_release);
    done.store(true, std::memory_order_release);
  }};

  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  gate.notify_all([&] {
    stopping.store(true, std::memory_order_release);
  });

  join_or_fail(waiter, done);
  EXPECT_TRUE(woke.load(std::memory_order_acquire));
}

// Mirrors free()'s fetch_sub(): notify_all() must forward whatever its mutate callback returns.
TEST_F(WakeGateTest, NotifyAllForwardsMutateReturnValue) {
  wake_gate gate;
  unsigned count = 0;
  const unsigned previous = gate.notify_all([&] {
    return count++;
  });
  EXPECT_EQ(previous, 0u);
  EXPECT_EQ(count, 1u);
}

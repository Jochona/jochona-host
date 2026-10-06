/**
 * @file src/platform/windows/vhf_gamepad_wake_gate.h
 * @brief A mutex/condition_variable wrapper that keeps predicate mutation and notification
 *        atomic with respect to waiters, used to wake the VHF feedback thread.
 * @details Deliberately free of any Windows or libvirtualgamepad header so it can be compiled and
 *          unit tested on any platform, even though its only production use is Windows-only
 *          (`vhf_gamepad.cpp`).
 */
#pragma once

// standard includes
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <type_traits>

namespace platf::vhf_gamepad {

  /**
   * @brief A mutex/condition_variable pair where every predicate mutation is inseparable from its
   *        notification.
   * @details `std::condition_variable::wait` only guarantees no lost wakeup when every thread that
   *          changes the state a predicate reads does so while holding the same mutex the waiter
   *          locks — holding that mutex is what makes "the waiter is still checking the predicate"
   *          and "the waiter has registered to be woken" mutually exclusive from the notifier's
   *          point of view. Mutating the predicate state with a plain atomic store and then calling
   *          `notify_all()` *without* that mutex does not provide this guarantee, even though the
   *          store itself is atomic: the notifier can complete its store and its notify entirely
   *          before the waiter finishes registering on the condition variable, so the notification
   *          has nothing to wake and is lost. The waiter then sleeps until the next spurious wakeup
   *          or, for an unbounded wait, forever. This type makes the correct sequence — lock,
   *          mutate, unlock, notify — the only way to signal, so that race cannot reappear.
   */
  class wake_gate {
  public:
    /**
     * @brief Blocks until `predicate` holds, re-checking it under the gate's lock on every wake.
     * @param predicate Returns `true` when the wait should end. Invoked under the gate's lock, so
     *                   it may read state that `notify_all()` mutates.
     */
    template<typename Predicate>
    void wait(Predicate predicate) {
      std::unique_lock lock {mutex_};
      cv_.wait(lock, std::move(predicate));
    }

    /**
     * @brief Blocks until `predicate` holds or `timeout` elapses, whichever comes first.
     * @param timeout Maximum time to wait.
     * @param predicate Returns `true` when the wait should end. Invoked under the gate's lock.
     * @return `true` if `predicate` returned `true` before the timeout elapsed.
     */
    template<typename Rep, typename Period, typename Predicate>
    bool wait_for(const std::chrono::duration<Rep, Period> &timeout, Predicate predicate) {
      std::unique_lock lock {mutex_};
      return cv_.wait_for(lock, timeout, std::move(predicate));
    }

    /**
     * @brief Mutates predicate state under the gate's lock, then wakes every waiter.
     * @details `mutate` runs with the lock held, so any concurrent waiter is either still
     *          evaluating its predicate under that same lock (and will observe the new state
     *          before deciding to wait) or has already released the lock to block on the
     *          condition variable (and will therefore receive this notification) — it can never be
     *          caught in between, which is exactly the window that loses a wakeup.
     * @param mutate Callback that updates the predicate state. Invoked under the gate's lock; may
     *               return a value, which is forwarded to the caller.
     * @return Whatever `mutate` returns.
     */
    template<typename Mutate>
    auto notify_all(Mutate mutate) -> decltype(mutate()) {
      std::unique_lock lock {mutex_};
      if constexpr (std::is_void_v<decltype(mutate())>) {
        mutate();
        lock.unlock();
        cv_.notify_all();
      } else {
        auto result = mutate();
        lock.unlock();
        cv_.notify_all();
        return result;
      }
    }

  private:
    std::mutex mutex_;  ///< Guards the predicate state `mutate`/`predicate` callbacks read and write.
    std::condition_variable cv_;  ///< Wakes `wait`/`wait_for` callers.
  };

}  // namespace platf::vhf_gamepad

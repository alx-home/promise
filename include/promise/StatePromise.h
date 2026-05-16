/*
MIT License

Copyright (c) 2025 Alexandre GARCIN

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "promise.h"

#include <stdexcept>

/**
 * @brief Coroutine-friendly state-transition primitive for ready/done workflows.
 *
 * StatePromise coordinates coroutines that need to wait for a resource to become "ready" and/or
 * "done". It exposes explicit Ready/Done transitions, and allows waiting for either state.
 *
 * Usage:
 *   - Call Ready() to signal the ready state (non-terminal).
 *   - Call Done() to signal the done state (terminal, unblocks all waiters).
 *   - WaitReady(), WaitDone(), Wait(), WaitWithReject() provide fine-grained awaitable handles.
 *   - Reset() returns the state to its initial (not ready, not done) form for reuse.
 *   - IsDone() checks if the done state has been reached.
 *
 * Destroying a StatePromise calls Done() so waiting coroutines are unblocked.
 */
class StatePromise {
public:
   /**
    * @brief Exception raised when a StatePromise is destroyed.
    *
    * This exception is used to reject pending promises when the StatePromise
    * is destroyed, ensuring that any waiting coroutines are awakened.
    */
   struct End : std::runtime_error {
      End()
         : std::runtime_error("Promise ended") {}
   };

   /**
    * @brief Constructs a new StatePromise with a fresh promise state.
    *
    * The initial state is neither ready nor done.
    */
   StatePromise();

   StatePromise(StatePromise const&)                = delete;
   StatePromise(StatePromise&&) noexcept            = delete;
   StatePromise& operator=(StatePromise const&)     = delete;
   StatePromise& operator=(StatePromise&&) noexcept = delete;

   virtual ~StatePromise();

   /**
    * @brief Waits for the promise to be ready.
    *
    * @return A promise that resolves when Ready() is called.
    */
   [[nodiscard]] WPromise<void> WaitReady() const;

   /**
    * @brief Waits for the promise to be done.
    *
    * @return A promise that resolves when Done() is called.
    */
   [[nodiscard]] WPromise<void> WaitDone() const;

   /**
    * @brief Waits for the promise to be done with rejection.
    *
    * @return A promise that rejects with End when Done() is called.
    */
   [[nodiscard]] WPromise<void> WaitDoneWithReject() const;

   /**
    * @brief Waits for the promise to be either ready or done.
    *
    * @return A promise that resolves when either Ready() or Done() is called.
    */
   [[nodiscard]] WPromise<void> Wait() const;

   /**
    * @brief Waits for the promise to be ready or rejects if done.
    *
    * @return A promise that resolves when Ready() is called, or rejects with End if Done() is
    * called first.
    */
   [[nodiscard]] WPromise<void> WaitWithReject() const;

   /**
    * @brief Marks the promise as ready (non-terminal state).
    *
    * Signals all waiters on WaitReady() and Wait().
    */
   void Ready();

   /**
    * @brief Marks the promise as done (terminal state).
    *
    * Signals all waiters on WaitDone(), Wait(), and WaitWithReject().
    * After Done(), IsDone() returns true and the state cannot be reused until Reset().
    */
   void Done();

   /**
    * @brief Resets the promise to its initial state, allowing it to be reused.
    *
    * After Reset(), the state is neither ready nor done.
    */
   void Reset();

   /**
    * @brief Checks if the promise is done.
    *
    * @return True if Done() has been called and the state is terminal.
    */
   [[nodiscard]] bool IsDone() const;

private:
   /** @brief Constructs a StatePromise from pre-created promise components.
    *
    * @param ready_promise Tuple containing the ready promise, resolve callback, and reject
    * callback.
    * @param done_promise Tuple containing the done promise, resolve callback, and reject callback
    */
   StatePromise(
     std::tuple<
       WPromise<void>,
       std::shared_ptr<promise::Resolve<void>>,
       std::shared_ptr<promise::Reject>>&& ready_promise,
     std::tuple<
       WPromise<void>,
       std::shared_ptr<promise::Resolve<void>>,
       std::shared_ptr<promise::Reject>>&& done_promise
   );

   WPromise<void>                          ready_promise_;
   std::shared_ptr<promise::Resolve<void>> ready_resolve_;
   std::shared_ptr<promise::Reject>        ready_reject_;

   WPromise<void>                          done_promise_;
   std::shared_ptr<promise::Resolve<void>> done_resolve_;
   std::shared_ptr<promise::Reject>        done_reject_;
};
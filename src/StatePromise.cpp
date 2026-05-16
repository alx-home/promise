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

#include "StatePromise.h"
#include <cassert>

/** @brief Constructs a new StatePromise with a fresh promise state. */
StatePromise::StatePromise()
   : StatePromise(
       [] {
          auto [promise, resolve, reject] = Promise<void>::Create();
          return std::make_tuple(std::move(promise), resolve, reject);
       }(),
       [] {
          auto [promise, resolve, reject] = Promise<void>::Create();
          return std::make_tuple(std::move(promise), resolve, reject);
       }()
     ) {}

/** @brief Constructs a StatePromise from pre-created promise components.
 *
 * @param ready_promise Tuple containing the ready promise, resolve callback, and reject callback.
 * @param done_promise Tuple containing the done promise, resolve callback, and reject callback
 */
StatePromise::StatePromise(
  std::tuple<
    WPromise<void>,
    std::shared_ptr<promise::Resolve<void>>,
    std::shared_ptr<promise::Reject>>&& ready_promise,
  std::tuple<
    WPromise<void>,
    std::shared_ptr<promise::Resolve<void>>,
    std::shared_ptr<promise::Reject>>&& done_promise
)
   : ready_promise_(std::move(std::get<0>(ready_promise)))
   , ready_resolve_(std::move(std::get<1>(ready_promise)))
   , ready_reject_(std::move(std::get<2>(ready_promise)))
   , done_promise_(std::move(std::get<0>(done_promise)))
   , done_resolve_(std::move(std::get<1>(done_promise)))
   , done_reject_(std::move(std::get<2>(done_promise))) {}

/** @brief Marks the state promise as done during destruction.
 *
 * This unblocks waiters by transitioning internal promises to their done state.
 */
StatePromise::~StatePromise() {
   assert(ready_resolve_);
   assert(done_resolve_);

   (*ready_resolve_)();
   (*done_resolve_)();
}

/** @brief Get the promise resolved when Ready() is called.
 *
 * @return Promise resolved when Ready() is called.
 */
WPromise<void>
StatePromise::WaitReady() const {
   return ready_promise_;
}

/** @brief Get the promise resolved when Done() is called.
 *
 * @return Promise resolved when Done() is called.
 */
WPromise<void>
StatePromise::WaitDone() const {
   return done_promise_;
}

/** @brief Get the promise resolved when either Ready() or Done() is called.
 *
 * @return Promise that completes when either WaitReady() or WaitDone() completes.
 */
WPromise<void>
StatePromise::Wait() const {
   return promise::Race(WaitDone(), WaitReady());
}

/** @brief Get the promise resolved when either Ready() or Done() is called.
 *
 * @return Promise that completes when either WaitReady() or WaitDone() completes.
 */
WPromise<void>
StatePromise::WaitWithReject() const {
   return promise::Race(WaitDoneWithReject(), WaitReady());
}

/** @brief Waits for the promise to be done with rejection.
 *
 * @return A promise that rejects when the state promise is done.
 */
[[nodiscard]] WPromise<void>
StatePromise::WaitDoneWithReject() const {
   return done_promise_.Then([] { throw End(); });
}

/** @brief Marks the state as ready.
 *
 * Resets the done promise and notifies the ready promise.
 */
void
StatePromise::Ready() {
   assert(ready_resolve_);
   assert(!ready_promise_.Done());
   assert(!done_promise_.Done());

   (*ready_resolve_)();
}

/** @brief Marks the state as done and rejects waiters with end-of-life semantics. */
void
StatePromise::Done() {
   assert(done_resolve_);

   auto const ready_was_done = ready_promise_.Done();
   // Resolve done before ready for WaitWithReject() to reject with End() instead of resolving with
   // void.
   (*done_resolve_)();

   if (!ready_was_done) {
      (*ready_resolve_)();
   }
}

/** @brief Reports whether both internal promises reached the done state.
 *
 * @return true when both ready and done promises are resolved.
 */
bool
StatePromise::IsDone() const {
   return done_promise_.Resolved() && ready_promise_.Resolved();
}

/** @brief Resets both internal promises to their initial pending state. */
void
StatePromise::Reset() {
   if (done_promise_.Resolved()) {
      assert(ready_promise_.Resolved());

      std::tie(ready_promise_, ready_resolve_, ready_reject_) = Promise<void>::Create();
      std::tie(done_promise_, done_resolve_, done_reject_)    = Promise<void>::Create();
   } else if (ready_promise_.Resolved()) {
      std::tie(ready_promise_, ready_resolve_, ready_reject_) = Promise<void>::Create();
   }
}

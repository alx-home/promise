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

/** @brief Marks the state promise as done during destruction.
 *
 * This unblocks waiters by transitioning internal promises to their done state.
 */
StatePromise::~StatePromise() { Done(); }

/** @brief Get the promise resolved when Ready() is called.
 *
 * @return Promise resolved when Ready() is called.
 */
WPromise<void>
StatePromise::WaitReady() const {
   return *ready_promise_;
}

/** @brief Get the promise resolved when Done() is called.
 *
 * @return Promise resolved or rejected when Done() is called.
 */
WPromise<void>
StatePromise::WaitDone() const {
   return *done_promise_;
}

/** @brief Get the promise resolved when either Ready() or Done() is called.
 *
 * @return Promise that completes when either WaitReady() or WaitDone() completes.
 */
WPromise<void>
StatePromise::Wait() const {
   return promise::Race(*done_promise_, *ready_promise_);
}

/** @brief Marks the state as ready.
 *
 * Resets the done promise and notifies the ready promise.
 */
void
StatePromise::Ready() {
   done_promise_.Reset();
   ready_promise_.Notify();
}

/** @brief Marks the state as done and rejects waiters with end-of-life semantics. */
void
StatePromise::Done() {
   done_promise_.Reject<CVPromise::End>();

   if (ready_promise_->Resolved()) {
      ready_promise_.Reset();
      ready_promise_.Reject<CVPromise::End>();
   } else {
      ready_promise_.Reject<CVPromise::End>();
   }
}

/** @brief Reports whether both internal promises reached the done state.
 *
 * @return true when both ready and done promises are rejected.
 */
bool
StatePromise::IsDone() const {
   return done_promise_->Rejected() && ready_promise_->Rejected();
}

/** @brief Resets both internal promises to their initial pending state. */
void
StatePromise::Reset() {
   ready_promise_.Reset();
   done_promise_.Reset();
}

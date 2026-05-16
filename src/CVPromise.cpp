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

#include "CVPromise.h"
#include <cassert>

/** @brief Constructs a new CVPromise with a fresh promise state. */
CVPromise::CVPromise()
   : CVPromise([] {
      auto [promise, resolve, reject] = Promise<void>::Create();
      return std::make_tuple(std::move(promise), resolve, reject);
   }()) {}

/** @brief Constructs a CVPromise from pre-created promise components.
 *
 * @param cv Tuple containing the promise, resolve callback, and reject callback.
 */
CVPromise::CVPromise(
  std::tuple<
    WPromise<void>,
    std::shared_ptr<promise::Resolve<void>>,
    std::shared_ptr<promise::Reject>>&& cv
)
   : promise_(std::move(std::get<0>(cv)))
   , resolve_(std::move(std::get<1>(cv)))
   , reject_(std::move(std::get<2>(cv))) {}

/** @brief Rejects the promise with End on destruction.
 *
 * This ensures pending waiters are released instead of blocking indefinitely.
 */
CVPromise::~CVPromise() {
   auto const reject = [this] {
      std::shared_lock lock{mutex_};
      assert(reject_);
      return reject_;
   }();

   assert(reject);
   reject->Apply<End>();
}

/** @brief Returns the current waitable promise.
 *
 * @return A copy of the underlying WPromise<void>.
 */
CVPromise::
operator WPromise<void>() const {
   std::shared_lock lock{mutex_};
   return promise_;
}

/** @brief Returns the current waitable promise.
 *
 * @return A copy of the underlying WPromise<void>.
 */
WPromise<void>
CVPromise::Wait() const {
   std::shared_lock lock{mutex_};
   return promise_;
}

/** @brief Dereferences to the underlying promise.
 *
 * @return A copy of the underlying WPromise<void>.
 */
WPromise<void>
CVPromise::operator*() const {
   std::shared_lock lock{mutex_};
   return promise_;
}

/** @brief Resolves the current promise state. */
void
CVPromise::Notify() {
   auto const [resolve, old_promise] = [this] {
      auto [new_promise, new_resolve, new_reject] = Promise<void>::Create();

      std::unique_lock lock{mutex_};
      assert(resolve_);

      auto old_promise  = promise_;
      auto old_resolver = resolve_;

      promise_ = std::move(new_promise);
      resolve_ = std::move(new_resolve);
      reject_  = std::move(new_reject);

      return std::make_pair(old_resolver, std::move(old_promise));
   }();

   assert(resolve);
   (*resolve)();
   assert(old_promise.Done());
}

/** @brief Checks if the promise has been resolved. */
bool
CVPromise::Resolved() const {
   std::shared_lock lock{mutex_};
   return promise_.Resolved();
}

/** @brief Checks if the promise has been rejected. */
bool
CVPromise::Rejected() const {
   std::shared_lock lock{mutex_};
   return promise_.Rejected();
}
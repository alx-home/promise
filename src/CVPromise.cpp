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

CVPromise::CVPromise()
   : CVPromise([] constexpr {
      auto [promise, resolve, reject] = Promise<void>::Create();
      return std::make_tuple(std::make_unique<WPromise<void>>(std::move(promise)), resolve, reject);
   }()) {}

CVPromise::~CVPromise() {
   // Reject the promise on destruction to unblock any waiting coroutines
   Reject<End>();
}

CVPromise::operator WPromise<void> const&() const { return *promise_; }
WPromise<void>
CVPromise::Wait() const {
   return *promise_;
}

WPromise<void> const&
CVPromise::operator*() const {
   return *promise_;
}

void
CVPromise::Notify() {
   auto const resolve = [this] constexpr {
      std::shared_lock lock{mutex_};
      assert(resolve_);
      return resolve_;
   }();
   (*resolve)();
}

void
CVPromise::Reset() {
   auto const [promise, resolve] = [this] constexpr {
      std::unique_lock lock{mutex_};
      auto             resolve = resolve_;
      // Resolve the promise after the lock is released to avoid deadlocks in callbacks
      // The promise is moved to the callback to ensure it is not destroyed (which could lead to
      // deadlocks if not resolved) until the callback is invoked
      auto promise                                = std::move(*promise_);
      auto [new_promise, new_resolve, new_reject] = Promise<void>::Create();
      promise_ = std::make_unique<WPromise<void>>(std::move(new_promise));
      resolve_ = std::move(new_resolve);
      reject_  = std::move(new_reject);
      return std::make_pair(std::move(promise), std::move(resolve));
   }();
   (*resolve)();
}

CVPromise::CVPromise(std::tuple<
                     std::unique_ptr<WPromise<void>>,
                     std::shared_ptr<promise::Resolve<void>>,
                     std::shared_ptr<promise::Reject>>&& cv)
   : promise_(std::move(std::get<0>(cv)))
   , resolve_(std::move(std::get<1>(cv)))
   , reject_(std::move(std::get<2>(cv))) {}
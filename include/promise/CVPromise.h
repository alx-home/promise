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
#include <type_traits>

struct CVPromise {
public:
   struct End : std::runtime_error {
      End()
         : std::runtime_error("Promise ended") {}
   };

   CVPromise();

   CVPromise(CVPromise const&)                = delete;
   CVPromise(CVPromise&&) noexcept            = delete;
   CVPromise& operator=(CVPromise const&)     = delete;
   CVPromise& operator=(CVPromise&&) noexcept = delete;

   virtual ~CVPromise();

   explicit(false) operator WPromise<void>() const;
   WPromise<void> Wait() const;

   void Notify() const;
   void Reset();

   template <class EXCEPTION, class... ARGS>
      requires(std::is_constructible_v<EXCEPTION, ARGS && ...>)
   void Reject(ARGS&&... args) {
      auto const [promise, reject] = [this] constexpr {
         std::unique_lock lock{mutex_};
         auto             reject = reject_;
         // Reject the promise after the lock is released to avoid deadlocks in callbacks
         // The promise is moved to the callback to ensure it is not destroyed (which could lead to
         // deadlocks if not resolved) until the callback is invoked
         auto promise                          = std::move(promise_);
         std::tie(promise_, resolve_, reject_) = Promise<void>::Create();
         return std::make_pair(std::move(promise), reject);
      }();
      reject->template Apply<EXCEPTION>(std::forward<ARGS>(args)...);
   }

private:
   CVPromise(std::tuple<
             WPromise<void>,
             std::shared_ptr<promise::Resolve<void>>,
             std::shared_ptr<promise::Reject>>&& cv);

   mutable std::shared_mutex               mutex_;
   WPromise<void>                          promise_;
   std::shared_ptr<promise::Resolve<void>> resolve_;
   std::shared_ptr<promise::Reject>        reject_;
};
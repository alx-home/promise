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

/**
 * @brief Coroutine-friendly condition-variable style primitive for async notification.
 *
 * CVPromise allows coroutines to wait for a signal (Notify), and supports resettable wait cycles.
 * - Notify() resolves all current waiters (one-shot signal).
 * - Wait() and operator*() provide awaitable handles for coroutines.
 * - Destroying or rejecting the notifier throws CVPromise::End in waiters.
 *
 * Usage:
 *   - Use Notify() to signal all current waiters.
 *   - Wait() or co_await *cv to await the signal.
 *   - Resolved()/Rejected() to inspect state.
 *   - Destroying the CVPromise rejects outstanding waiters with End.
 */
struct CVPromise {
public:
   /**
    * @brief Exception raised when a CVPromise is destroyed.
    *
    * This exception is used to reject pending promises when the CVPromise
    * is destroyed, ensuring that any waiting coroutines are awakened.
    */
   struct End : std::runtime_error {
      End()
         : std::runtime_error("Promise ended") {}
   };

   /**
    * @brief Constructs a new CVPromise with a fresh wait state.
    */
   CVPromise();

   CVPromise(CVPromise const&)                = delete;
   CVPromise(CVPromise&&) noexcept            = delete;
   CVPromise& operator=(CVPromise const&)     = delete;
   CVPromise& operator=(CVPromise&&) noexcept = delete;

   /**
    * @brief Destroys the condition-variable promise.
    *
    * The destructor rejects the underlying promise with @ref End to wake any
    * pending waiters and prevent coroutines from blocking indefinitely.
    */
   virtual ~CVPromise();

   /**
    * @brief Converts the CVPromise to a WPromise<void> for co_await.
    */
   [[nodiscard]] operator WPromise<void>() const;

   /**
    * @brief Gets a WPromise<void> that resolves when the CVPromise is resolved or rejected.
    *
    * @return A WPromise<void> that resolves when Notify() is called or rejects if
    *          @ref Reject() or the destructor is called.
    */
   [[nodiscard]] WPromise<void> Wait() const;

   /**
    * @brief Gets the underlying WPromise<void> for direct inspection or co_await.
    */
   [[nodiscard]] WPromise<void> operator*() const;

   /**
    * @brief Checks if the promise has been resolved (signaled).
    */
   bool Resolved() const;
   /**
    * @brief Checks if the promise has been rejected (rejected, or destroyed).
    */
   bool Rejected() const;

   /**
    * @brief Notifies all current waiters (one-shot signal).
    */
   void Notify();

   /** @brief Rejects the promise with a specific exception, rejecting it if it hasn't been resolved
    * or rejected.
    *
    * @tparam EXCEPTION The type of exception to reject the promise with.
    * @tparam ARGS The types of arguments to construct the exception with.
    * @param args The arguments to construct the exception with.
    */
   template <class EXCEPTION, class... ARGS>
      requires(std::is_constructible_v<EXCEPTION, ARGS && ...>)
   void Reject(ARGS&&... args) {
      auto const [reject, old_promise] = [this] {
         auto [new_promise, new_resolve, new_reject] = Promise<void>::Create();

         std::unique_lock lock{mutex_};
         assert(reject_);

         auto old_promise = promise_;
         auto old_reject  = reject_;

         promise_ = std::move(new_promise);
         resolve_ = std::move(new_resolve);
         reject_  = std::move(new_reject);

         return std::make_pair(old_reject, std::move(old_promise));
      }();

      assert(reject);
      reject->template Apply<EXCEPTION>(std::forward<ARGS>(args)...);
      assert(old_promise.Done());
   }

private:
   /** @brief Constructs a CVPromise from a tuple of promise components.
    *
    * @param cv A tuple containing the promise components.
    */
   CVPromise(
     std::tuple<
       WPromise<void>,
       std::shared_ptr<promise::Resolve<void>>,
       std::shared_ptr<promise::Reject>>&& cv
   );

   mutable std::shared_mutex               mutex_;
   WPromise<void>                          promise_;
   std::shared_ptr<promise::Resolve<void>> resolve_;
   std::shared_ptr<promise::Reject>        reject_;
};
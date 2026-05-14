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

/** @brief A promise that can be waited on, notified, and rejected. */
struct CVPromise {
public:
   struct End : std::runtime_error {
      End()
         : std::runtime_error("Promise ended") {}
   };

   /** @brief Constructs a new CVPromise. */
   CVPromise();

   CVPromise(CVPromise const&)                = delete;
   CVPromise(CVPromise&&) noexcept            = delete;
   CVPromise& operator=(CVPromise const&)     = delete;
   CVPromise& operator=(CVPromise&&) noexcept = delete;

   /** @brief Destroys the condition-variable promise.
    *
    * The destructor rejects the underlying promise with @ref End to wake any
    * pending waiters and prevent coroutines from blocking indefinitely.
    */
   virtual ~CVPromise();

   /** @brief Converts the CVPromise to a WPromise<void>. */
   [[nodiscard]] operator WPromise<void>() const;

   /** @brief Gets a WPromise<void> that resolves when the CVPromise is resolved or rejected.
    *
    * @return A WPromise<void> that resolves when the CVPromise is resolved or rejected.
    */
   [[nodiscard]] WPromise<void> Wait() const;

   /** @brief Gets the underlying WPromise<void>. */
   [[nodiscard]] WPromise<void> operator*() const;
   /** @brief Gets a pointer to the underlying WPromise<void>. */
   [[nodiscard]] WPromise<void> const* operator->() const;

   /** @brief Notifies the promise, resolving it if it hasn't been resolved or rejected. */
   void Notify();
   /** @brief Resets the promise, making it reusable. */
   void Reset();

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
      auto const reject = [this] constexpr {
         std::shared_lock lock{mutex_};
         assert(reject_);
         return reject_;
      }();

      reject->template Apply<EXCEPTION>(std::forward<ARGS>(args)...);
   }

private:
   /** @brief Constructs a CVPromise from a tuple of promise components.
    *
    * @param cv A tuple containing the promise components.
    */
   CVPromise(
     std::tuple<
       std::unique_ptr<WPromise<void>>,
       std::shared_ptr<promise::Resolve<void>>,
       std::shared_ptr<promise::Reject>>&& cv
   );

   mutable std::shared_mutex               mutex_;
   std::unique_ptr<WPromise<void>>         promise_;
   std::shared_ptr<promise::Resolve<void>> resolve_;
   std::shared_ptr<promise::Reject>        reject_;
};
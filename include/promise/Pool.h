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

#include <utils/Pool.h>
#include <optional>
#include <type_traits>
#include <utility>
#include <chrono>

namespace promise {

using namespace std::chrono;

/** @brief A thread pool that allows dispatching functions to be executed asynchronously, returning
 * promises for their results. */
template <std::size_t SIZE = 10>
class Pool : private ::Pool<false, SIZE> {
public:
   using duration   = typename ::Pool<false, SIZE>::duration;
   using time_point = typename ::Pool<false, SIZE>::time_point;

   explicit Pool(std::string_view thread_name)
      : ::Pool<false, SIZE>{thread_name} {}

   using ::Pool<false, SIZE>::Stop;
   using ::Pool<false, SIZE>::ThreadIds;

   /**
    * @brief Dispatch a function to be executed on the pool's thread, returning a promise for its
    * result.
    *
    * @tparam RETURN Return type of the function.
    * @param until Optional time point until which the function should be executed. If not
    * provided, executes as soon as possible.
    * @return Promise that resolves with the function's return value or rejects if the queue is
    * stopped.
    */
   [[nodiscard]] WPromise<void> Dispatch(
     std::optional<typename Pool::time_point> until = std::nullopt
   ) const noexcept {
      auto [promise, resolve, reject] = Promise<void>::Create();
      if (
        auto [dispatched, func] = ::Pool<false, SIZE>::Dispatch(
          [resolve = std::move(resolve), promise]() mutable {
             // Wait until the promise is actually awaited before resolving, to ensure the caller
             // is called from within this thread.
             promise.WaitAwaited(0);
             (*resolve)();
          },
          until
        );
        !dispatched
      ) {
         // Keep func alive until here to ensure the promise is rejected if the queue is stopped
         // before deletion.
         (void)func;
         reject->template Apply<QueueStopped>(this->GetName());
      }

      return std::move(promise);
   };

   /** @brief Dispatch a function to be executed on the pool's thread after a certain delay,
    * returning a promise for its result.
    *
    * @tparam RETURN Return type of the function.
    * @param delay Delay after which the function should be executed.
    * @return Promise that resolves with the function's return value or rejects if the queue is
    * stopped.
    */
   [[nodiscard]] WPromise<void> Dispatch(typename Pool::duration delay) const noexcept {
      return Dispatch(steady_clock::now() + delay);
   };

   /** @brief Dispatch a function to be executed on the pool's thread, returning a promise for its
    * result.
    *
    * @tparam RETURN Return type of the function.
    * @param func Function to be executed.
    * @param until Optional time point until which the function should be executed. If not provided,
    * executes as soon as possible.
    * @return Promise that resolves with the function's return value or rejects if the queue is
    * stopped.
    */
   template <class RETURN>
   [[nodiscard]] WPromise<RETURN> Dispatch(
     std::function<RETURN()>&&                func,
     std::optional<typename Pool::time_point> until = std::nullopt
   ) const noexcept {
      auto [promise, resolve, reject] = Promise<RETURN>::Create();
      if (
        auto [dispatched, func2] = ::Pool<false, SIZE>::Dispatch(
          [resolve = std::move(resolve), reject, func = std::move(func)]() mutable {
             try {
                if constexpr (std::is_void_v<RETURN>) {
                   func();
                   (*resolve)();
                } else {
                   (*resolve)(func());
                }
             } catch (...) {
                (*reject)(std::current_exception());
             }
          },
          until
        );
        !dispatched
      ) {
         // Keep func alive until here to ensure the promise is rejected if the queue is stopped
         // before deletion.
         (void)func2;
         reject->template Apply<QueueStopped>(this->GetName());
      }

      return std::move(promise);
   }

   /** @brief Dispatch a function to be executed on the pool's thread, returning a promise for its
    * result.
    *
    * @tparam RETURN Return type of the function.
    * @param func Function to be executed.
    * @param until Optional time point until which the function should be executed. If not provided,
    * executes as soon as possible.
    * @return Promise that resolves with the function's return value or rejects if the queue is
    * stopped.
    */
   template <class RETURN>
   [[nodiscard]] auto Dispatch(
     std::function<void(Resolve<RETURN> const&, Reject const&)>&& func,
     std::optional<typename Pool::time_point>                     until = std::nullopt
   ) const noexcept {
      auto [promise, resolve, reject] = Promise<RETURN>::Create();
      if (
        auto [dispatched, func2] = ::Pool<false, SIZE>::Dispatch(
          [resolve = std::move(resolve), reject, func = std::move(func)]() {
             try {
                func(*resolve, *reject);
             } catch (...) {
                (*reject)(std::current_exception());
             }
          },
          until
        );
        !dispatched
      ) {
         // Keep func alive until here to ensure the promise is rejected if the queue is stopped
         // before deletion.
         (void)func2;
         reject->template Apply<QueueStopped>(this->GetName());
      }

      return std::move(promise);
   }

   /** @brief Dispatch a function to be executed on the pool's thread, returning a promise for its
    * result.
    *
    * @tparam FUN Type of the function to be executed.
    * @param func Function to be executed.
    * @param until Optional time point until which the function should be executed. If not provided,
    * executes as soon as possible.
    * @return Promise that resolves with the function's return value or rejects if the queue is
    * stopped.
    */
   template <class FUN>
      requires(promise::function_constructible<FUN>)
   [[nodiscard]] auto Dispatch(
     FUN&&                                             func,
     typename std::optional<typename Pool::time_point> until = std::nullopt
   ) const noexcept {
      if constexpr (promise::IS_FUNCTION<FUN>) {
         return Dispatch(std::forward<FUN>(func), until);
      } else {
         return Dispatch(std::function{std::forward<FUN>(func)}, until);
      }
   }

   /** @brief Dispatch a function to be executed on the pool's thread after a certain delay,
    * returning a promise for its result.
    *
    * @tparam FUN Type of the function to be executed.
    * @param func Function to be executed.
    * @param delay Delay after which the function should be executed.
    * @return Promise that resolves with the function's return value or rejects if the queue is
    * stopped.
    */
   template <class FUN>
   [[nodiscard]] auto Dispatch(FUN&& func, typename Pool::duration delay) const noexcept {
      return Dispatch(std::forward<FUN>(func), steady_clock::now() + delay);
   }
};

}  // namespace promise
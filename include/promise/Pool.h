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
#include "core/concepts.inl"

#include <utils/Pool.h>
#include <optional>
#include <type_traits>

namespace promise {

template <std::size_t SIZE = 10>
class Pool : private ::Pool<false, SIZE> {
public:
   using duration   = typename ::Pool<false, SIZE>::duration;
   using time_point = typename ::Pool<false, SIZE>::time_point;

   explicit Pool(std::string_view thread_name)
      : ::Pool<false, SIZE>{thread_name} {}

   using ::Pool<false, SIZE>::Stop;
   using ::Pool<false, SIZE>::ThreadIds;

   [[nodiscard]] WPromise<void> Dispatch(
     std::optional<typename Pool::time_point> until = std::nullopt
   ) const noexcept {
      auto [promise, resolve, reject] = Promise<void>::Create();
      if (!::Pool<false, SIZE>::Dispatch(
            [resolve = std::move(resolve)]() mutable { (*resolve)(); }, until
          )) {
         reject->template Apply<QueueStopped>(this->GetName());
      }

      return std::move(promise);
   };

   [[nodiscard]] WPromise<void> Dispatch(typename Pool::duration delay) const noexcept {
      return Dispatch(std::chrono::steady_clock::now() + delay);
   };

   template <class RETURN>
   [[nodiscard]] WPromise<RETURN> Dispatch(
     std::function<RETURN()>&&                func,
     std::optional<typename Pool::time_point> until = std::nullopt
   ) const noexcept {
      auto [promise, resolve, reject] = Promise<RETURN>::Create();
      if (!::Pool<false, SIZE>::Dispatch(
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
          )) {
         reject->template Apply<QueueStopped>(this->GetName());
      }

      return std::move(promise);
   }

   template <class RETURN>
   [[nodiscard]] auto Dispatch(
     std::function<void(Resolve<RETURN> const&, Reject const&)>&& func,
     std::optional<typename Pool::time_point>                     until = std::nullopt
   ) const noexcept {
      auto [promise, resolve, reject] = Promise<RETURN>::Create();
      if (!::Pool<false, SIZE>::Dispatch(
            [resolve = std::move(resolve), reject, func = std::move(func)]() {
               try {
                  func(*resolve, *reject);
               } catch (...) {
                  (*reject)(std::current_exception());
               }
            },
            until
          )) {
         reject->template Apply<QueueStopped>(this->GetName());
      }

      return std::move(promise);
   }

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

   template <class FUN>
   [[nodiscard]] auto Dispatch(FUN&& func, typename Pool::duration delay) const noexcept {
      return Dispatch(std::forward<FUN>(func), std::chrono::steady_clock::now() + delay);
   }
};

}  // namespace promise
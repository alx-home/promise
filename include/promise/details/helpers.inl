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

#include "../core/core.inl"

#include <utils/Scoped.h>
#include <algorithm>
#include <cassert>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

/**
 * @brief Build a Promise from a generic callable.
 *
 * @param func Callable returning a Promise or value.
 * @param args Arguments forwarded to the callable.
 *
 * @return Constructed promise.
 */
template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN>)
static constexpr auto
MakePromise(FUN&& func, ARGS&&... args) {
   return MakePromise(std::function{std::forward<FUN>(func)}, std::forward<ARGS>(args)...);
}

/**
 * @brief Reject a promise by constructing an exception.
 *
 * @param reject Reject handle.
 * @param args Constructor args for EXCEPTION.
 *
 * @return True if rejected, false if already rejected.
 * @warning When RELAXED is false, a double reject throws.
 */
template <class EXCEPTION, bool RELAXED, class... ARGS>
bool
MakeReject(promise::Reject const& reject, ARGS&&... args) {
   if (!reject.Apply<EXCEPTION>(std::forward<ARGS>(args)...)) {
      if constexpr (!RELAXED) {
         throw promise::Exception("Promise Already rejected !");
      }
      return false;
   }

   return true;
}

namespace promise {
/**
 * @brief Await all promises and return a combined result.
 *
 * @param promise Promises to await.
 *
 * @return Tuple of resolved values (std::nullopt_t for void).
 */
template <class... PROMISE>
static constexpr auto
All(PROMISE&&... promise) {
   return MakePromise(
     [promise...]() mutable -> details::IPromise<std::tuple<std::conditional_t<
                              std::is_void_v<return_t<PROMISE>>,
                              std::nullopt_t,
                              return_t<PROMISE>>...>> {
        co_return std::make_tuple(([]<class... RESULT>(RESULT... result) constexpr {
           if constexpr (sizeof...(RESULT)) {
              return result...[0];
           } else {
              return std::nullopt;
           }
        }(co_await promise))...);
     }
   );
}

}  // namespace promise

/**
 * @brief Build a Promise from a std::function.
 *
 * @param func Callable returning a Promise or value.
 * @param args Arguments forwarded to the callable.
 *
 * @return Constructed promise.
 */
template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN>)
static constexpr auto
MakePromise(FUN&& func, ARGS&&... args) {
   using namespace promise;

   return details::Promise<return_t<return_t<FUN>>, WITH_RESOLVER<FUN>>::template Create<false>(
     std::forward<FUN>(func), std::forward<ARGS>(args)...
   );
}

/**
 * @brief Build a resolver-style Promise from a std::function.
 *
 * @param func Callable returning a resolver-style Promise.
 * @param args Arguments forwarded to the callable.
 *
 * @return Constructed resolver-style promise or tuple.
 */
template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
static constexpr auto
MakeRPromise(FUN&& func, ARGS&&... args) {
   using namespace promise;

   return details::Promise<return_t<return_t<FUN>>, WITH_RESOLVER<FUN>>::template Create<true>(
     std::forward<FUN>(func), std::forward<ARGS>(args)...
   );
}

/**
 * @brief Build a resolver-style Promise from a generic callable.
 *
 * @param func Callable returning a resolver-style Promise.
 * @param args Arguments forwarded to the callable.
 *
 * @return Constructed resolver-style promise or tuple.
 */
template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
static constexpr auto
MakeRPromise(FUN&& func, ARGS&&... args) {
   return MakeRPromise(std::function{std::forward<FUN>(func)}, std::forward<ARGS>(args)...);
}

/**
 * @brief Build a resolver and its resolve/reject handles.
 *
 * @return Tuple (resolver, resolve, reject).
 */
template <class T>
static constexpr auto
MakeResolver() {
   auto resolver = std::make_unique<promise::Resolver<T, true>>();
   return std::make_tuple(std::move(resolver), resolver->resolve_, resolver->reject_);
}

namespace promise {

/**
 * @brief Build a promise and its resolve/reject handles.
 *
 * @return Tuple (promise, resolve, reject).
 */
template <class T>
static constexpr auto
Pure() {
   auto [resolver, resolve, reject] = MakeResolver<T>();
   auto promise = ([](Resolve<T> const&, Reject const&) -> details::IPromise<T, true> {
      co_return;
   }(*resolve, *reject));

   return std::make_tuple(
     details::WPromise<T>{std::move(promise(std::move(resolver)))}, resolve, reject
   );
}

}  // namespace promise

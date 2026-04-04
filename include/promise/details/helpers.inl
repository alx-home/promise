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

#include <sys/stat.h>
#include <utils/Scoped.h>
#include <algorithm>
#include <cassert>
#include <exception>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

/**
 * @brief Build a Promise from a generic callable.
 *
 * @param func Callable returning a Promise or value.
 * @param args Arguments forwarded to the callable.
 *
 * @return Constructed promise.
 */
template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN> && promise::function_constructible<FUN>)
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
 * @return Tuple of resolved values (std::monostate for void).
 */
template <class... PROMISE>
static constexpr auto
All(PROMISE&&... promise) {
   return MakePromise(
     [promise...]() mutable -> details::IPromise<std::tuple<std::conditional_t<
                              std::is_void_v<return_t<PROMISE>>,
                              std::monostate,
                              return_t<PROMISE>>...>> {
        co_return std::make_tuple(([]<class... RESULT>(RESULT... result) constexpr {
           if constexpr (sizeof...(RESULT)) {
              return result...[0];
           } else {
              return std::monostate{};
           }
        }(co_await promise))...);
     }
   );
}

template <class V, class... TS>
struct UniqueVariantHelper;

template <class V, class T, class... TS>
struct UniqueVariantHelper<V, T, TS...> {
   template <typename T2, typename V2>
   struct add_if_missing;

   template <typename T2, typename... US>
   struct add_if_missing<T2, std::variant<US...>> {
      using type = std::conditional_t<
        (std::is_same_v<T2, US> || ...) || std::is_void_v<T2>,
        std::variant<US...>,
        std::variant<US..., T2>>;
   };

   using type = typename UniqueVariantHelper<typename add_if_missing<T, V>::type, TS...>::type;
};

template <typename V>
struct UniqueVariantHelper<V> {
   using type = V;
};

template <typename... TS>
using unique_variant = typename UniqueVariantHelper<std::variant<>, TS...>::type;

/**
 * @brief Await the first promise to resolve and return its result.
 *
 * @param promise Promises to await.
 *
 * @return Variant of resolved values (std::optional if any promise is void).
 */
template <class... PROMISES>
static constexpr auto
Race(PROMISES&&... promise) {
   static_assert(sizeof...(PROMISES) > 0, "Race cannot be called with zero promises !");

   static constexpr auto IS_VOID  = (std::is_void_v<return_t<PROMISES>> && ...);
   static constexpr auto HAS_VOID = (std::is_void_v<return_t<PROMISES>> || ...);

   using RaceReturn = std::remove_cvref_t<std::remove_pointer_t<decltype([] constexpr {
      if constexpr (IS_VOID) {
         return (void*)nullptr;
      } else {
         using RaceReturn1 = unique_variant<return_t<PROMISES>...>;
         static_assert(std::variant_size_v<RaceReturn1> != 0, "Race cannot have zero promises !");

         if constexpr (HAS_VOID) {
            return (std::optional<RaceReturn1>*)nullptr;
         } else {
            return (RaceReturn1*)nullptr;
         }
      }
   }())>>;

   auto [race_promise, resolve, reject] = Create<RaceReturn>();

   auto wrapper = [resolve, reject]<class PROMISE>(PROMISE&& promise) constexpr {
      using Return = return_t<PROMISE>;

      if constexpr (std::is_void_v<Return>) {
         std::forward<PROMISE>(promise)
           .Then([resolve = std::move(resolve)]() constexpr {
              if constexpr (IS_VOID) {
                 (*resolve)();
              } else {
                 // Delay type deduction of the variant to avoid compile errors
                 using Return = std::conditional_t<std::is_void_v<Return>, RaceReturn, RaceReturn>;
                 (*resolve)(Return{std::nullopt});
              }
           })
           .Catch([reject = std::move(reject)](std::exception_ptr exception) constexpr {
              (*reject)(std::move(exception));
           })
           .Detach();
      } else {
         std::forward<PROMISE>(promise)
           .Then([resolve = std::move(resolve)](Return const& result) constexpr {
              (*resolve)(result);
           })
           .Catch([reject = std::move(reject)](std::exception_ptr exception) constexpr {
              (*reject)(std::move(exception));
           })
           .Detach();
      }
   };

   (wrapper(std::forward<PROMISES>(promise)), ...);

   return std::move(race_promise);
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
   requires(promise::IS_FUNCTION<FUN> && promise::IS_PROMISE<FUN> && !promise::IS_WPROMISE<FUN>)
static constexpr auto
MakePromise(FUN&& func, ARGS&&... args) {
   using namespace promise;

   return details::Promise<return_t<return_t<FUN>>, WITH_RESOLVER<FUN>>::template Create<false>(
     std::forward<FUN>(func), std::forward<ARGS>(args)...
   );
}

/**
 * @brief Build a Create Promise from a std::function.
 *
 * @param func Callable returning a Promise or value.
 * @param args Arguments forwarded to the callable.
 *
 * @return Constructed promise.
 */
template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN> && (!promise::IS_PROMISE<FUN> || promise::IS_WPROMISE<FUN>))
static constexpr auto
MakePromise(FUN&& func, ARGS&&... args) {
   using namespace promise;
   using Return = std::remove_cvref_t<std::remove_pointer_t<decltype([] constexpr {
      if constexpr (std::tuple_size_v<all_args_t<FUN>> >= 1) {
         if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>) {
            static_assert(
              std::is_void_v<return_t<FUN>>, "Resolver-style promise cannot have a return value"
            );
            return static_cast<promise::RESOLVE_TYPE<std::tuple_element_t<0, all_args_t<FUN>>>*>(
              nullptr
            );
         } else {
            return static_cast<return_t<FUN>*>(nullptr);
         }
      } else {
         return static_cast<return_t<FUN>*>(nullptr);
      }
   }())>>;

   auto [promise, resolve, reject] = details::Promise<Return>::Create();

   try {
      if constexpr (std::tuple_size_v<all_args_t<FUN>> >= 2) {
         if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>
                       && IS_REJECTOR<std::tuple_element_t<1, all_args_t<FUN>>>) {
            func(*resolve, *reject, std::forward<ARGS>(args)...);
         } else if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>) {
            func(*resolve, std::forward<ARGS>(args)...);
         } else if constexpr (std::is_void_v<Return>) {
            func(std::forward<ARGS>(args)...);
            (*resolve)();
         } else {
            (*resolve)(func(std::forward<ARGS>(args)...));
         }
      } else if constexpr (std::tuple_size_v<all_args_t<FUN>> >= 1) {
         if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>) {
            func(*resolve, std::forward<ARGS>(args)...);
         } else if constexpr (std::is_void_v<Return>) {
            func(std::forward<ARGS>(args)...);
            (*resolve)();
         } else {
            (*resolve)(func(std::forward<ARGS>(args)...));
         }
      } else if constexpr (std::is_void_v<Return>) {
         func(std::forward<ARGS>(args)...);
         (*resolve)();
      } else {
         (*resolve)(func(std::forward<ARGS>(args)...));
      }
   } catch (...) {
      (*reject)(std::current_exception());
   }

   return promise;
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
   requires(!promise::IS_FUNCTION<FUN> && promise::function_constructible<FUN> && promise::WITH_RESOLVER<FUN>)
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
Create() {
   return details::Promise<T>::Create();
}

}  // namespace promise

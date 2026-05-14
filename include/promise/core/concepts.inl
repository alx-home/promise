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

#include <cassert>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace promise {

namespace details {

template <class T = void, bool WITH_RESOLVER = false>
class IPromise;

template <class T = void>
class WPromise;

}  // namespace details

/**
 * @brief Helper template to detect std::function types.
 *
 * Primary template is std::false_type.
 */
template <class>
struct IsFunction : std::false_type {};

/**
 * @brief Specialization that detects std::function types.
 *
 * Specializes IsFunction for std::function to inherit from std::true_type.
 */
template <class FUN>
struct IsFunction<std::function<FUN>> : std::true_type {};

/**
 * @brief Check if a type is a std::function.
 *
 * @tparam FUN Type to check.
 */
template <class FUN>
static constexpr bool IS_FUNCTION = IsFunction<std::remove_cvref_t<FUN>>::value;

/**
 * @brief Check if a type can be constructed into a std::function.
 *
 * @tparam FUN Type to check.
 */
template <class FUN>
concept function_constructible = requires(FUN fun) { std::function{fun}; };

template <class FUN>
struct return_;

/**
 * @brief Template specialization for non-std::function callables.
 *
 * Extracts the return type from a callable that can be converted to std::function.
 */
template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct return_<FUN> {
   using type = typename return_<decltype(std::function{std::declval<FUN>()})>::type;
};

/**
 * @brief Template specialization for std::function callables.
 *
 * Extracts the return type T from std::function<T(ARGS...)>.
 */
template <class T, class... ARGS>
struct return_<std::function<T(ARGS...)>> {
   using type = T;
};

/**
 * @brief Template specialization for types with return_type member.
 *
 * Extracts return_type from types that have a return_type member typedef.
 */
template <class T>
   requires(requires { typename T::return_type; })
struct return_<T> {
   using type = typename T::return_type;
};

/**
 * @brief Get the return type of a callable.
 * return_t<return_t<FUN>> is the return type of a callable that returns a promise.
 *
 * @tparam FUN Callable type.
 */
template <class FUN>
using return_t = typename return_<std::remove_cvref_t<FUN>>::type;

/**
 * @brief Concept that checks whether a type has a deducible return type.
 *
 * @tparam T Type to inspect.
 */
template <class T>
concept HasReturn = requires { typename return_<std::remove_cvref_t<T>>::type; };

/**
 * @brief Helper template to extract return type or void.
 *
 * Primary template for types without a deducible return type.
 */
template <class T>
struct ReturnOrVoid;

/**
 * @brief Specialization for types without a return type.
 *
 * For types that don't have a deducible return type, returns void.
 */
template <class T>
   requires(!HasReturn<T>)
struct ReturnOrVoid<T> {
   using type = void;
};

/**
 * @brief Specialization for types with a return type.
 *
 * For types that have a deducible return type, extracts it.
 */
template <class T>
   requires(HasReturn<T>)
struct ReturnOrVoid<T> {
   using type = return_t<T>;
};

/**
 * @brief Return type alias that falls back to void when no return type can be deduced.
 *
 * @tparam T Type to inspect.
 */
template <class T>
using return_or_void_t = typename ReturnOrVoid<T>::type;

/**
 * @brief Helper template to detect resolver types.
 *
 * Primary template is std::false_type.
 */
template <class T>
struct IsResolver : std::false_type {};

template <class T>
class Resolve;

class Reject;

/**
 * @brief Specialization that detects Resolve<T> types.
 *
 * Specializes IsResolver for Resolve<T> to inherit from std::true_type.
 */
template <class T>
struct IsResolver<promise::Resolve<T>> : std::true_type {};

/**
 * @brief Check if a type is a resolver.
 *
 * @tparam T Type to check.
 */
template <class T>
static constexpr bool IS_RESOLVER = IsResolver<std::remove_cvref_t<T>>::value;

/**
 * @brief Check if a type is a rejector.
 *
 * @tparam T Type to check.
 */
template <class T>
static constexpr bool IS_REJECTOR = std::is_same_v<std::remove_cvref_t<T>, promise::Reject>;

/**
 * @brief Helper template to detect resolver-style promises.
 *
 * Primary template is std::false_type.
 */
template <class FUN>
struct WithResolver : std::false_type {};

/**
 * @brief Specialization that detects resolver-style promises.
 *
 * Specializes WithResolver for IPromise<T, true> to inherit from std::true_type.
 */
template <class T>
struct WithResolver<details::IPromise<T, true>> : std::true_type {};

/**
 * @brief Check if a promise is resolver-style.
 *
 * @tparam FUN Callable type.
 */
template <class FUN>
static constexpr bool WITH_RESOLVER = WithResolver<return_t<FUN>>::value;

/**
 * @brief Helper template to extract the value type from a resolver.
 *
 * Primary template is empty.
 */
template <class T>
struct ResolveType;

/**
 * @brief Specialization that extracts value type from Resolve<T>.
 *
 * Extracts the type T from Resolve<T>.
 */
template <class T>
struct ResolveType<promise::Resolve<T>> {
   using type = T;
};

/**
 * @brief Get the type resolved by a resolver.
 *
 * @tparam T Resolver type.
 */
template <class T>
using RESOLVE_TYPE = typename ResolveType<std::remove_cvref_t<T>>::type;

/**
 * @brief Helper template to detect promise types.
 *
 * Primary template is std::false_type.
 */
template <class FUN>
struct IsPromise : std::false_type {};

/**
 * @brief Specialization that detects IPromise types.
 *
 * Specializes IsPromise for IPromise<T, WITH_RESOLVER> to inherit from std::true_type.
 */
template <class T, bool WITH_RESOLVER>
struct IsPromise<details::IPromise<T, WITH_RESOLVER>> : std::true_type {};

/**
 * @brief Specialization that detects WPromise types.
 *
 * Specializes IsPromise for WPromise<T> to inherit from std::true_type.
 */
template <class T>
struct IsPromise<details::WPromise<T>> : std::true_type {};

/**
 * @brief Helper template to detect promise wrapper types.
 *
 * Primary template is std::false_type.
 */
template <class FUN>
struct IsWPromise : std::false_type {};

/**
 * @brief Specialization that detects WPromise types.
 *
 * Specializes IsWPromise for WPromise<T> to inherit from std::true_type.
 */
template <class T>
struct IsWPromise<details::WPromise<T>> : std::true_type {};

/**
 * @brief Check if a type is a promise.
 *
 * @tparam FUN Type to check.
 */
template <class FUN>
static constexpr bool IS_PROMISE = IsPromise<return_or_void_t<FUN>>::value;

/**
 * @brief Check if a type is a promise wrapper.
 *
 * @tparam FUN Type to check.
 */
template <class FUN>
static constexpr bool IS_WPROMISE = IsWPromise<std::remove_cvref_t<FUN>>::value;

/**
 * @brief Check if a type is a promise function.
 *
 * @tparam FUN Type to check.
 */
template <class FUN>
static constexpr bool IS_PROMISE_FUNCTION =
  IsPromise<return_or_void_t<FUN>>::value && !IS_WPROMISE<return_or_void_t<FUN>>;

/**
 * @brief Helper template to extract argument types (excluding resolver/rejector if present).
 *
 * Primary template is empty, specialized for different function types.
 */
template <class FUN>
struct args_;

/**
 * @brief Specialization for non-std::function callables.
 *
 * Converts callable to std::function and extracts args from the result.
 */
template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct args_<FUN> {
   using type = typename args_<decltype(std::function{std::declval<FUN>()})>::type;
};

/**
 * @brief Specialization for std::function without resolver.
 *
 * Extracts all arguments from std::function<T(ARGS...)> for non-resolver promises.
 */
template <class T, class... ARGS>
   requires(!WithResolver<T>::value)
struct args_<std::function<T(ARGS...)>> {
   using type = std::tuple<ARGS...>;
};

/**
 * @brief Specialization for std::function with both resolver and rejector.
 *
 * Extracts remaining arguments after Resolve and Reject from resolver-style promises.
 */
template <class T, class RESOLVE, class REJECT, class... ARGS>
   requires(WithResolver<T>::value && IS_RESOLVER<RESOLVE> && IS_REJECTOR<REJECT>)
struct args_<std::function<T(RESOLVE, REJECT, ARGS...)>> {
   using type = std::tuple<ARGS...>;
};

/**
 * @brief Specialization for std::function with resolver and arguments.
 *
 * Extracts remaining arguments after Resolve from resolver-style promises.
 */
template <class T, class RESOLVE, class ARG1, class... ARGS>
   requires(WithResolver<T>::value && IS_RESOLVER<RESOLVE> && !IS_REJECTOR<ARG1>)
struct args_<std::function<T(RESOLVE, ARG1, ARGS...)>> {
   using type = std::tuple<ARG1, ARGS...>;
};

/**
 * @brief Specialization for std::function with only resolver.
 *
 * No arguments after Resolve for this resolver-style promise.
 */
template <class T, class RESOLVE>
   requires(WithResolver<T>::value && IS_RESOLVER<RESOLVE>)
struct args_<std::function<T(RESOLVE)>> {
   using type = std::tuple<>;
};

/**
 * @brief Specialization for std::function with resolver but no rejector.
 *
 * Extracts remaining arguments for resolver-style promises.
 */
template <class T, class ARG1, class... ARGS>
   requires(WithResolver<T>::value && !IS_RESOLVER<ARG1>)
struct args_<std::function<T(ARG1, ARGS...)>> {
   using type = std::tuple<ARG1, ARGS...>;
};

/**
 * @brief Specialization for std::function with no arguments.
 *
 * Empty argument tuple for no-argument callables.
 */
template <class T>
struct args_<std::function<T()>> {
   using type = std::tuple<>;
};

/**
 * @brief Get the argument types of a callable, excluding resolver/rejector if present.
 *
 * @tparam FUN Callable type.
 */
template <class FUN>
using args_t = typename args_<std::remove_cvref_t<FUN>>::type;

/**
 * @brief Helper template to extract all argument types including resolver/rejector.
 *
 * Primary template is empty, specialized for different function types.
 */
template <class FUN>
struct all_args_;

/**
 * @brief Specialization for non-std::function callables.
 *
 * Converts callable to std::function and extracts all args from the result.
 */
template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct all_args_<FUN> {
   using type = typename all_args_<decltype(std::function{std::declval<FUN>()})>::type;
};

/**
 * @brief Specialization for std::function.
 *
 * Extracts all arguments from std::function<T(ARGS...)>.
 */
template <class T, class... ARGS>
struct all_args_<std::function<T(ARGS...)>> {
   using type = std::tuple<ARGS...>;
};

/**
 * @brief Get all argument types of a callable, including resolver/rejector if present.
 *
 * @tparam FUN Callable type.
 */
template <class FUN>
using all_args_t = typename all_args_<std::remove_cvref_t<FUN>>::type;

/**
 * @brief Helper template to extract return type for wrapped promises.
 *
 * Primary template with empty specialization for SFINAE.
 */
template <class FUN, class = void>
struct WReturnHelper;

/**
 * @brief Specialization for callables with no arguments.
 *
 * Returns the direct return type for no-argument callables.
 */
template <class FUN>
struct WReturnHelper<FUN, std::enable_if_t<std::tuple_size_v<all_args_t<FUN>> == 0>> {
   using type = return_t<FUN>;
};

/**
 * @brief Specialization for callables with arguments.
 *
 * Extracts the value type from resolver if first argument is a resolver.
 */
template <class FUN>
struct WReturnHelper<FUN, std::enable_if_t<std::tuple_size_v<all_args_t<FUN>> >= 1>> {
   using type = std::conditional_t<
     IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>,
     promise::RESOLVE_TYPE<std::tuple_element_t<0, all_args_t<FUN>>>,
     return_t<FUN>>;
};

/**
 * @brief Return type of a callable adapted for promise wrappers.
 *
 * @tparam FUN Callable type.
 */
template <class FUN>
using WReturn = typename WReturnHelper<std::remove_cvref_t<FUN>>::type;

/**
 * @brief Return promise type produced by Then continuations.
 *
 * @tparam FUN Continuation callable type.
 */
template <class FUN>
using ThenReturn = details::WPromise<
  std::conditional_t<IS_PROMISE_FUNCTION<FUN>, return_or_void_t<return_t<FUN>>, return_t<FUN>>>;

/**
 * @brief Intermediate return promise type produced by Catch continuations.
 *
 * @tparam T Original promise value type.
 * @tparam T2 Catch continuation value type.
 */
template <class T, class T2>
using CatchReturn2 = std::conditional_t<
  std::is_void_v<T2> && std::is_void_v<T>,
  details::WPromise<void>,
  std::conditional_t<
    std::is_void_v<T2>,
    details::WPromise<std::optional<T>>,
    std::conditional_t<
      std::is_void_v<T>,
      details::WPromise<std::optional<T2>>,
      std::conditional_t<
        std::is_same_v<T, T2>,
        details::WPromise<T2>,
        details::WPromise<std::variant<T2, T>>>>>>;

/**
 * @brief Return promise type produced by Catch continuations.
 *
 * @tparam T Original promise value type.
 * @tparam FUN Catch continuation callable type.
 */
template <class T, class FUN>
using CatchReturn = CatchReturn2<
  T,
  std::conditional_t<IS_PROMISE_FUNCTION<FUN>, return_or_void_t<return_t<FUN>>, return_t<FUN>>>;

/**
 * @brief Return promise type produced by Finally continuations.
 *
 * @tparam T Original promise value type.
 */
template <class T>
using FinallyReturn = details::WPromise<T>;

/**
 * @brief Helper template to extract const reference or void.
 *
 * For non-void types, extracts T const&. For void types, returns void.
 */
template <class T>
struct CRefOrVoid {
   using type = T const&;
};

/**
 * @brief Specialization for void type.
 *
 * For void types, returns void instead of void const&.
 */
template <>
struct CRefOrVoid<void> {
   using type = void;
};

/**
 * @brief Alias to a const reference for non-void types, or void for void.
 *
 * @tparam T Value type.
 */
template <class T>
using cref_or_void_t = typename CRefOrVoid<T>::type;

}  // namespace promise

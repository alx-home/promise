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
#include <type_traits>
#include <utility>

namespace promise {

namespace details {

template <class T = void, bool WITH_RESOLVER = false>
class IPromise;

template <class T = void>
class WPromise;

}  // namespace details

template <class>
struct IsFunction : std::false_type {};
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

template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct return_<FUN> {
   using type = typename return_<decltype(std::function{std::declval<FUN>()})>::type;
};

template <class T, class... ARGS>
struct return_<std::function<T(ARGS...)>> {
   using type = T;
};

template <class T, bool WITH_RESOLVER>
struct return_<details::IPromise<T, WITH_RESOLVER>> {
   using type = T;
};

template <class T>
struct return_<details::WPromise<T>> {
   using type = T;
};

/**
 * @brief Get the return type of a callable.
 * return_t<return_t<FUN>> is the return type of a callable that returns a promise.
 *
 * @tparam FUN Callable type.
 */
template <class FUN>
using return_t = typename return_<std::remove_cvref_t<FUN>>::type;

template <class T>
struct IsResolver : std::false_type {};

template <class T>
struct Resolve;

struct Reject;

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

template <class FUN>
struct WithResolver : std::false_type {};

template <class T>
struct WithResolver<details::IPromise<T, true>> : std::true_type {};

/**
 * @brief Check if a promise is resolver-style.
 *
 * @tparam FUN Callable type.
 */
template <class FUN>
static constexpr bool WITH_RESOLVER = WithResolver<return_t<FUN>>::value;

template <class FUN>
struct IsPromise : std::false_type {};

template <class T, bool WITH_RESOLVER>
struct IsPromise<details::IPromise<T, WITH_RESOLVER>> : std::true_type {};

/**
 * @brief Check if a type is a promise.
 *
 * @tparam FUN Type to check.
 */
template <class FUN>
static constexpr bool IS_PROMISE = IsPromise<return_t<FUN>>::value;

template <class FUN>
struct args_;

template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct args_<FUN> {
   using type = typename args_<decltype(std::function{std::declval<FUN>()})>::type;
};

template <class T, class... ARGS>
   requires(!WithResolver<T>::value)
struct args_<std::function<T(ARGS...)>> {
   using type = std::tuple<ARGS...>;
};

template <class T, class RESOLVE, class REJECT, class... ARGS>
   requires(WithResolver<T>::value && IS_RESOLVER<RESOLVE> && IS_REJECTOR<REJECT>)
struct args_<std::function<T(RESOLVE, REJECT, ARGS...)>> {
   using type = std::tuple<ARGS...>;
};

template <class T, class RESOLVE, class ARG1, class... ARGS>
   requires(WithResolver<T>::value && IS_RESOLVER<RESOLVE> && !IS_REJECTOR<ARG1>)
struct args_<std::function<T(RESOLVE, ARG1, ARGS...)>> {
   using type = std::tuple<ARG1, ARGS...>;
};

template <class T, class RESOLVE>
   requires(WithResolver<T>::value && IS_RESOLVER<RESOLVE>)
struct args_<std::function<T(RESOLVE)>> {
   using type = std::tuple<>;
};

template <class T, class ARG1, class... ARGS>
   requires(WithResolver<T>::value && !IS_RESOLVER<ARG1>)
struct args_<std::function<T(ARG1, ARGS...)>> {
   using type = std::tuple<ARG1, ARGS...>;
};

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

template <class FUN>
struct all_args_;

template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct all_args_<FUN> {
   using type = typename all_args_<decltype(std::function{std::declval<FUN>()})>::type;
};

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

}  // namespace promise

/*
MIT License

Copyright (c) 2025 alx-home

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

#include <exception>
#include <functional>
#include <type_traits>

namespace promise {

template <class T = void> struct ResolveHelper {
   using type = std::function<void()>;
};

template <class T>
   requires(!std::is_void_v<T>)
struct ResolveHelper<T> {
   using type = std::function<void(T const&)>;
};

template <class T = void> using Resolve = ResolveHelper<T>::type;

using Reject = std::function<void(std::exception_ptr)>;

template <class T, bool WITH_RESOLVER = false> struct Promise;

template <class> struct IsFunction : std::false_type {};
template <class FUN> struct IsFunction<std::function<FUN>> : std::true_type {};

template <class FUN>
static constexpr bool IS_FUNCTION = IsFunction<std::remove_cvref_t<FUN>>::value;

template <class FUN>
concept function_constructible = requires(FUN fun) { std::function{fun}; };

template <class FUN> struct return_;

template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct return_<FUN> {
   using type = typename return_<decltype(std::function{std::declval<FUN>()})>::type;
};

template <class T, class... ARGS> struct return_<std::function<T(ARGS...)>> {
   using type = T;
};

template <class T, bool WITH_RESOLVER> struct return_<Promise<T, WITH_RESOLVER>> {
   using type = T;
};

template <class FUN> using return_t = typename return_<std::remove_cvref_t<FUN>>::type;

template <class T> struct IsResolver : std::false_type {};

template <class T> struct IsResolver<std::function<void(T const&)>> : std::true_type {};

template <> struct IsResolver<std::function<void()>> : std::true_type {};

template <class FUN> struct WithResolver : std::false_type {};

template <class T> struct WithResolver<Promise<T, true>> : std::true_type {};

template <class FUN> static constexpr bool WITH_RESOLVER = WithResolver<return_t<FUN>>::value;

template <class FUN> struct args_;

template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct args_<FUN> {
   using type = typename args_<decltype(std::function{std::declval<FUN>()})>::type;
};

template <class T, class RESOLVE, class REJECT, class... ARGS>
   requires(WithResolver<T>::value)
struct args_<std::function<T(RESOLVE, REJECT, ARGS...)>> {
   using type = std::tuple<ARGS...>;
};

template <class T, class... ARGS>
   requires(!WithResolver<T>::value)
struct args_<std::function<T(ARGS...)>> {
   using type = std::tuple<ARGS...>;
};

template <class FUN> using args_t = typename args_<std::remove_cvref_t<FUN>>::type;

}  // namespace promise

template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN>)
static constexpr auto MakePromise(FUN&& func, ARGS&&... args);

template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN>)
static constexpr auto MakePromise(FUN&& func, ARGS&&... args);

template <class EXCEPTION, class FUN, class... ARGS>
void MakeReject(FUN const& reject, ARGS&&... args);

namespace promise {
template <class... PROMISE> static constexpr auto All(PROMISE&&... promise);
}

#include "impl/Promise.inl"

template <class T = void> using Resolve = promise::Resolve<T>;
using Reject                            = promise::Reject;

template <class T = void, bool WITH_RESOLVER = false>
using Promise = promise::Promise<T, WITH_RESOLVER>;
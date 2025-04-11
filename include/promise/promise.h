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

template <class T = void>
struct resolve {
   using type = std::function<void()>;
};

template <class T>
   requires(!std::is_void_v<T>)
struct resolve<T> {
   using type = std::function<void(T const&)>;
};

template <class T = void>
using resolve_t = resolve<T>::type;

using reject_t = std::function<void(std::exception_ptr)>;

template <class T, bool with_resolver = false>
struct Promise;

template <class>
struct is_function : std::false_type {};
template <class FUN>
struct is_function<std::function<FUN>> : std::true_type {};

template <class FUN>
static constexpr bool is_function_v = is_function<std::remove_cvref_t<FUN>>::value;

template <class FUN>
concept function_constructible = requires(FUN fun) {
   std::function{fun};
};

template <class FUN>
struct return_;

template <class FUN>
   requires(!is_function_v<FUN> && function_constructible<FUN>)
struct return_<FUN> {
   using type = typename return_<decltype(std::function{std::declval<FUN>()})>::type;
};

template <class T, class... Args>
struct return_<std::function<T(Args...)>> {
   using type = T;
};

template <class T, bool with_resolver>
struct return_<Promise<T, with_resolver>> {
   using type = T;
};

template <class FUN>
using return_t = typename return_<std::remove_cvref_t<FUN>>::type;

template <class FUN>
struct args_;

template <class FUN>
   requires(!is_function_v<FUN> && function_constructible<FUN>)
struct args_<FUN> {
   using type = typename args_<decltype(std::function{std::declval<FUN>()})>::type;
};

template <class T, class... Args>
struct args_<std::function<T(Args...)>> {
   using type = std::tuple<Args...>;
};

template <class FUN>
using args_t = typename args_<std::remove_cvref_t<FUN>>::type;

template <class T>
struct is_resolver_t : std::false_type {};

template <class T>
struct is_resolver_t<std::function<void(T const&)>> : std::true_type {};

template <>
struct is_resolver_t<std::function<void()>> : std::true_type {};

template <class FUN>
struct with_resolver_t : std::false_type {};

template <class T>
struct with_resolver_t<Promise<T, true>> : std::true_type {};

template <class FUN>
static constexpr bool with_resolver_v = with_resolver_t<return_t<FUN>>::value;

}  // namespace promise

template <class FUN, class... Args>
   requires(promise::is_function_v<FUN>)
static constexpr auto
make_promise(FUN&& func, Args&&... args);

template <class FUN, class... Args>
   requires(!promise::is_function_v<FUN>)
static constexpr auto
make_promise(FUN&& func, Args&&... args);

template <class Exception, class FUN, class... Args>
void
make_reject(FUN const& reject, Args&&... args);

namespace promise {
template <class... Promise>
static constexpr auto
All(Promise&&... promise);
}

#include "impl/Promise.inl"

template <class T = void>
using resolve_t = promise::resolve_t<T>;
using reject_t  = promise::reject_t;

template <class T = void, bool with_resolver = false>
using Promise = promise::Promise<T, with_resolver>;
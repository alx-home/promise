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

#include "concepts.inl"

#include <cassert>

namespace promise {
struct Reject;
}

/**
 * @brief Build a Promise from a std::function.
 * @tparam FUN Function type (std::function).
 * @param func Callable that returns Promise<T> or T.
 * @param args Arguments forwarded to the callable.
 * @return Constructed promise.
 */
template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN>)
[[nodiscard("Either store this promise or call Detach()")]] static constexpr auto
MakePromise(FUN&& func, ARGS&&... args);

/**
 * @brief Build a Promise from a generic callable.
 * @tparam FUN Callable type.
 * @param func Callable that returns Promise<T> or T.
 * @param args Arguments forwarded to the callable.
 * @return Constructed promise.
 */
template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN>)
[[nodiscard("Either store this promise or call Detach()")]] static constexpr auto
MakePromise(FUN&& func, ARGS&&... args);

/**
 * @brief Build a resolver-style Promise from a std::function.
 * @tparam FUN Function type (std::function).
 * @param func Callable that returns Promise<T, true>.
 * @param args Arguments forwarded to the callable (after resolver args).
 * @return Constructed resolver-style promise.
 */
template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
[[nodiscard]] static constexpr auto MakeRPromise(FUN&& func, ARGS&&... args);

/**
 * @brief Build a resolver-style Promise from a generic callable.
 * @tparam FUN Callable type.
 * @param func Callable that returns Promise<T, true>.
 * @param args Arguments forwarded to the callable (after resolver args).
 * @return Constructed resolver-style promise.
 */
template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
[[nodiscard]] static constexpr auto MakeRPromise(FUN&& func, ARGS&&... args);

/**
 * @brief Reject a promise with a constructed exception.
 * @tparam EXCEPTION Exception type to construct.
 * @tparam RELAXED When true, ignore double-rejects.
 * @param reject Reject handle.
 * @param args Constructor args for EXCEPTION.
 * @return True if rejected, false if already rejected.
 * @warning When RELAXED is false, a double reject throws.
 */
template <class EXCEPTION, bool RELAXED = true, class... ARGS>
bool MakeReject(promise::Reject const& reject, ARGS&&... args);

namespace promise {
/**
 * @brief Build a promise and its resolve/reject handles.
 * @treturn Tuple-like result (promise, resolve, reject).
 * @return Tuple (promise, resolve, reject).
 */
template <class T>
static constexpr auto Pure();
}  // namespace promise

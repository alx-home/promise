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
#include <memory>
#include <type_traits>

namespace promise {

/**
 * @brief Resolver handle used to resolve a promise with a value.
 */
template <class T = void>
struct Resolve;

template <>
/**
 * @brief Resolver for Promise<void>.
 */
struct Resolve<void> : std::enable_shared_from_this<Resolve<void>> {
   /**
    * @brief Construct a void resolver from an implementation callback.
    * @param impl Callback invoked on resolve.
    */
   Resolve(std::function<void()> impl);

   /**
    * @brief Resolve the promise.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   bool operator()() const;
   /**
    * @brief Check whether this resolver can still resolve.
    * @return True if already resolved, false otherwise.
    */
   operator bool() const;

private:
   std::function<void()>     impl_;
   mutable std::atomic<bool> resolved_{false};
};

template <class T>
   requires(!std::is_void_v<T>)
/**
 * @brief Resolver for Promise<T>.
 */
struct Resolve<T> : std::enable_shared_from_this<Resolve<T>> {
   /**
    * @brief Construct a value resolver from an implementation callback.
    * @param impl Callback invoked on resolve.
    */
   Resolve(std::function<void(T const&)> impl);

   /**
    * @brief Resolve the promise with a value.
    * @param value Value to resolve with.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   bool operator()(T const& value) const;
   /**
    * @brief Check whether this resolver can still resolve.
    * @return True if already resolved, false otherwise.
    */
   operator bool() const;

private:
   std::function<void(T const&)> impl_;
   mutable std::atomic<bool>     resolved_{false};
};

}  // namespace promise

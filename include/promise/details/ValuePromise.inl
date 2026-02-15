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
#include <cassert>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <variant>

namespace promise {

using Lock = std::variant<
  std::reference_wrapper<std::shared_lock<std::shared_mutex>>,
  std::reference_wrapper<std::unique_lock<std::shared_mutex>>,
  std::reference_wrapper<std::lock_guard<std::shared_mutex>>>;

template <class T, bool VOID_TYPE>
/**
 * @brief Base class for promises that can hold a resolved value.
 *
 * This class provides common functionality for promises that may or may not have a resolved value.
 * The IS_VOID template parameter indicates whether the promise is of void type, which affects how
 * resolution is handled.
 *
 * @tparam T Type of the resolved value (ignored if VOID_TYPE is true).
 * @tparam VOID_TYPE Boolean flag indicating whether this is a void promise.
 *
 * @note This class is intended to be used as a base for promise implementations and should not be
 * used directly.
 */
struct ValuePromise : VPromise {
protected:
   static constexpr bool IS_VOID = VOID_TYPE;

public:
   /**
    * @brief Get the resolved value using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return Resolved value reference.
    */
   template <class SELF>
   auto const& GetValue(this SELF&& self, [[gnu::unused]] Lock lock) {
      assert(self.resolver_);
      assert(self.resolver_->value_);
      return *self.resolver_->value_;
   }

   /**
    * @brief Get the resolved value.
    *
    * @return Resolved value reference.
    */
   template <class SELF>
   auto const& GetValue(this SELF&& self) {
      std::shared_lock lock{self.mutex_};
      return self.GetValue(lock);
   }

   /**
    * @brief Check if a value is resolved using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if resolved.
    */
   template <class SELF>
   bool IsResolved(this SELF&& self, [[gnu::unused]] Lock lock) {
      assert(self.resolver_);
      return self.resolver_->value_ != nullptr;
   }
};

template <class T>
struct ValuePromise<T, true> : VPromise {
protected:
   static constexpr bool IS_VOID = true;

public:
   /**
    * @brief Check if the promise is resolved using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if resolved.
    */
   template <class SELF>
   bool IsResolved(this SELF&& self, [[gnu::unused]] Lock lock) {
      assert(self.resolver_);
      return *self.resolver_->resolved_;
   }
};

}  // namespace promise

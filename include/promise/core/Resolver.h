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

#include "core.h"

#include <utils/Scoped.h>
#include <cassert>
#include <exception>
#include <memory>
#include <type_traits>
#include <variant>

namespace promise {

template <class T, bool WITH_RESOLVER>
class Handle;

template <class T>
class ValuePromise;

template <class T>
struct ResolverValue {
   // unique_ptr to handle std::optional<std::optional>...
   std::unique_ptr<T> value_{};
};

template <>
struct ResolverValue<void> {
   bool value_is_set_{false};
};

template <class T>
class Resolver
   : public std::enable_shared_from_this<Resolver<T>>
   , private ResolverValue<T> {
public:
   using Promise = std::
     variant<std::weak_ptr<details::Promise<T, true>>, std::weak_ptr<details::Promise<T, false>>>;

private:
   Resolver() = default;

public:
   virtual ~Resolver() = default;

   static constexpr std::
     tuple<std::shared_ptr<Resolver<T>>, std::shared_ptr<Resolve<T>>, std::shared_ptr<Reject>>
     Create();

   /**
    * @brief Check if the resolver is already resolved.
    * @return True if already resolved, false otherwise.
    */
   [[nodiscard]] bool Done() const;

   /**
    * @brief Resolve the promise with a value.
    * @param value Value to resolve with.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   template <class TT>
      requires(!std::is_void_v<T> && std::is_convertible_v<TT, T>)
   bool Resolve(TT&& value);

   /**
    * @brief Resolve the promise with a value.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   template <class...>
      requires(std::is_void_v<T>)
   bool Resolve();

   /**
    * @brief Reject the promise with an exception and a shared resolved flag.
    * @param exception Exception to store.
    * @param resolved Shared resolved flag.
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   template <class EXCEPTION, class... ARGS>
   bool Reject(ARGS&&... args);

   /**
    * @brief Reject the promise with an exception and a shared resolved flag.
    * @param exception Exception to store.
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   bool Reject(std::exception_ptr exception);

private:
   Promise            promise_{std::weak_ptr<details::Promise<T, false>>{}};
   std::exception_ptr exception_{};
   std::atomic<bool>  resolved_{false};

   friend class details::Promise<T, true>;
   friend class promise::Handle<T, true>;
   friend class details::Promise<T, false>;
   friend class promise::Handle<T, false>;
   friend class promise::ValuePromise<T>;
};

}  // namespace promise

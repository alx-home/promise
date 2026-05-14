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
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>

namespace promise {

template <class T, bool WITH_RESOLVER>
class Handle;

template <class T>
class ValuePromise;

/**
 * @brief Construct a value resolver from an implementation callback.
 * @param impl Callback invoked on resolve.
 */
template <class T>
   requires(!std::is_void_v<T>)
Resolve<T>::Resolve(std::shared_ptr<Resolver<T>> resolver)
   : resolver_(std::move(resolver)) {}

/**
 * @brief Resolve the promise with a value.
 * @param value Value to resolve with.
 * @return True if this call resolved the promise, false if it was already resolved.
 */
template <class T>
   requires(!std::is_void_v<T>)
bool
Resolve<T>::operator()(T const& value) const {
   return resolver_->Resolve(value);
}

/**
 * @brief Check whether the resolver is still usable.
 * @return True if already resolved, false otherwise.
 */
template <class T>
   requires(!std::is_void_v<T>)
Resolve<T>::
operator bool() const {
   return resolver_->await_ready();
}

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

   static constexpr auto Create() {
      struct MakeSharedEnabler : public Resolver<T> {
      public:
         using Resolver<T>::Resolver;
      };

      auto resolver = std::make_shared<MakeSharedEnabler>();
      auto resolve  = promise::Resolve<T>::Create(resolver);
      auto reject   = promise::Reject::Create([resolver](std::exception_ptr exception) constexpr {
         resolver->Reject(std::move(exception));
      });
      return std::make_tuple(std::move(resolver), std::move(resolve), std::move(reject));
   }

   /**
    * @brief Check if the resolver is already resolved.
    * @return True if already resolved, false otherwise.
    */
   [[nodiscard]] bool Done() const {
      if constexpr (std::is_void_v<T>) {
         return this->value_is_set_;
      } else {
         return this->value_ != nullptr;
      }
   }

   /**
    * @brief Resolve the promise with a value.
    * @param value Value to resolve with.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   template <class TT>
      requires(!std::is_void_v<T> && std::is_convertible_v<TT, T>)
   bool Resolve(TT&& value) {
      return std::visit(
        [&](auto&& promise_) {
           if (!resolved_.exchange(true)) {
              auto const promise = promise_.lock();
              assert(promise);
              std::unique_lock lock{promise->mutex_};

              assert(!Done());
              assert(!exception_);
              ResolverValue<T>::value_ = std::make_unique<T>(std::forward<TT>(value));

              assert(promise);
              promise->OnResolved(lock);
              return true;
           }

           return false;
        },
        promise_
      );
   }

   /**
    * @brief Resolve the promise with a value.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   template <class...>
      requires(std::is_void_v<T>)
   bool Resolve() {
      return std::visit(
        [&](auto&& promise_) {
           if (!resolved_.exchange(true)) {
              auto const promise = promise_.lock();
              assert(promise);
              std::unique_lock lock{promise->mutex_};

              assert(!ResolverValue<T>::value_is_set_);
              assert(!exception_);
              ResolverValue<T>::value_is_set_ = true;

              assert(promise);
              promise->OnResolved(lock);
              return true;
           }

           return false;
        },
        promise_
      );
   }

   /**
    * @brief Reject the promise with an exception and a shared resolved flag.
    * @param exception Exception to store.
    * @param resolved Shared resolved flag.
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   template <class EXCEPTION, class... ARGS>
   bool Reject(ARGS&&... args) {
      return Reject(std::make_exception_ptr(EXCEPTION(std::forward<ARGS>(args)...)));
   }

   /**
    * @brief Reject the promise with an exception and a shared resolved flag.
    * @param exception Exception to store.
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   bool Reject(std::exception_ptr exception) {
      return std::visit(
        [&](auto&& promise_) {
           if (!resolved_.exchange(true)) {
              auto const promise = promise_.lock();
              assert(promise);
              std::unique_lock lock{promise->mutex_};

              assert(!Done());
              assert(!exception_);
              exception_ = std::move(exception);

              assert(promise);
              promise->OnResolved(lock);
              return true;
           }

           return false;
        },
        promise_
      );
   }

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

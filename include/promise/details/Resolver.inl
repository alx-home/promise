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
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace promise {

/**
 * @brief Construct a value resolver from an implementation callback.
 * @param impl Callback invoked on resolve.
 */
template <class T>
   requires(!std::is_void_v<T>)
Resolve<T>::Resolve(std::function<void(T const&)> impl)
   : impl_(std::move(impl)) {}

/**
 * @brief Resolve the promise with a value.
 * @param value Value to resolve with.
 * @return True if this call resolved the promise, false if it was already resolved.
 */
template <class T>
   requires(!std::is_void_v<T>)
bool
Resolve<T>::operator()(T const& value) const {
   if (!resolved_.exchange(true)) {
      impl_(value);
      return true;
   }

   return false;
}

/**
 * @brief Check whether the resolver is still usable.
 * @return True if already resolved, false otherwise.
 */
template <class T>
   requires(!std::is_void_v<T>)
Resolve<T>::operator bool() const {
   return resolved_;
}

template <class T, bool WITH_RESOLVER>
struct Resolver {
   details::Promise<T, WITH_RESOLVER>* promise_{nullptr};
   std::exception_ptr                  exception_{};
   std::shared_ptr<std::atomic<bool>>  resolved_{std::make_shared<std::atomic<bool>>(false)};

   // unique_ptr to handle std::optional<std::optional>...
   std::unique_ptr<T>          value_{};
   std::shared_ptr<Resolve<T>> resolve_{std::make_shared<promise::Resolve<T>>(
     [this, resolved = resolved_](T const& value) mutable { this->Resolve(value, *resolved); }
   )};
   std::shared_ptr<Reject>     reject_{
     std::make_shared<promise::Reject>([this, resolved = resolved_](std::exception_ptr exc
                                       ) mutable { this->Reject(std::move(exc), *resolved); })
   };

   /**
    * @brief Check if the resolver is already resolved.
    * @return True if already resolved.
    */
   bool await_ready() const { return *resolved_; }

   /**
    * @brief Resume the await (no value to return).
    */
   void await_resume() const {}

   /**
    * @brief Resolve the promise with a value.
    * @param value Value to resolve with.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   template <class TT>
      requires(std::is_convertible_v<TT, T>)
   bool Resolve(TT&& value) {
      return Resolve(value, *this->resolved_);
   }

   /**
    * @brief Resolve the promise with a value and a shared resolved flag.
    * @param value Value to resolve with.
    * @param resolved Shared resolved flag.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   template <class TT>
      requires(std::is_convertible_v<TT, T>)
   bool Resolve(TT&& value, std::atomic<bool>& resolved) {
      if (!resolved.exchange(true)) {
         std::unique_lock lock{promise_->mutex_};

         assert(!value_);
         assert(!exception_);
         value_ = std::make_unique<T>(std::forward<TT>(value));

         assert(promise_);
         promise_->OnResolved(lock);
         return true;
      }

      return false;
   }

   /**
    * @brief Reject the promise with an exception.
    * @param exception Exception to store.
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   bool Reject(std::exception_ptr exception) { return Reject(exception, *this->resolved_); }

   /**
    * @brief Reject the promise with an exception and a shared resolved flag.
    * @param exception Exception to store.
    * @param resolved Shared resolved flag.
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   bool Reject(std::exception_ptr exception, std::atomic<bool>& resolved) {
      if (!resolved.exchange(true)) {
         std::unique_lock lock{promise_->mutex_};

         assert(!value_);
         assert(!exception_);
         exception_ = std::move(exception);

         assert(promise_);
         promise_->OnResolved(lock);
         return true;
      }

      return false;
   }
};

template <bool WITH_RESOLVER>
struct Resolver<void, WITH_RESOLVER> {
   details::Promise<void, WITH_RESOLVER>* promise_{nullptr};

   std::shared_ptr<std::atomic<bool>> resolved_{std::make_shared<std::atomic<bool>>(false)};
   std::exception_ptr                 exception_{};
   std::shared_ptr<Resolve<void>>     resolve_{std::make_shared<promise::Resolve<void>>(
     [this, resolved = resolved_]() mutable { this->Resolve(*resolved); }
   )};
   std::shared_ptr<Reject>            reject_{std::make_shared<promise::Reject>(
     [this, resolved = resolved_](std::exception_ptr exc) mutable { this->Reject(exc, *resolved); }
   )};

   /**
    * @brief Check if the resolver is already resolved.
    * @return True if already resolved.
    */
   bool await_ready() const { return *resolved_; }

   /**
    * @brief Resume the await (no value to return).
    */
   void await_resume() const {}

   /**
    * @brief Resolve the promise.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   bool Resolve() { return Resolve(*resolved_); }
   /**
    * @brief Resolve the promise with a shared resolved flag.
    * @param resolved Shared resolved flag.
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   bool Resolve(std::atomic<bool>& resolved) {
      if (!resolved.exchange(true)) {
         std::unique_lock lock{promise_->mutex_};

         assert(!exception_);
         assert(promise_);
         promise_->OnResolved(lock);
         return true;
      }

      return false;
   }

   /**
    * @brief Reject the promise with an exception.
    * @param exception Exception to store.
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   bool Reject(std::exception_ptr exception) { return Reject(exception, *resolved_); }
   /**
    * @brief Reject the promise with an exception and a shared resolved flag.
    * @param exception Exception to store.
    * @param resolved Shared resolved flag.
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   bool Reject(std::exception_ptr exception, std::atomic<bool>& resolved) {
      if (!resolved.exchange(true)) {
         std::unique_lock lock{promise_->mutex_};

         assert(!exception_);
         exception_ = exception;

         assert(promise_);
         promise_->OnResolved(lock);
         return true;
      }

      return false;
   }
};

}  // namespace promise

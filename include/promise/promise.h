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
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

template <class T = void, bool WITH_RESOLVER = false>
class Promise;

namespace promise {

/**
 * @brief Base exception type used by the promise helpers.
 */
struct Exception : std::runtime_error {
   using std::runtime_error::runtime_error;
};

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
    */
   operator bool() const;

private:
   std::function<void(T const&)> impl_;
   mutable std::atomic<bool>     resolved_{false};
};

/**
 * @brief Rejector handle used to reject a promise with an exception.
 */
struct Reject : std::enable_shared_from_this<Reject> {
   /**
    * @brief Construct a rejector from an implementation callback.
    * @param impl Callback invoked on reject.
    */
   Reject(std::function<void(std::exception_ptr)> impl);

   /**
    * @brief Reject the promise with an exception.
    * @param exception Exception to store.
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   bool operator()(std::exception_ptr exception) const;
   /**
    * @brief Check whether this rejector can still reject.
    */
   operator bool() const;

private:
   std::function<void(std::exception_ptr)> impl_;
   mutable std::atomic<bool>               rejected_{false};
};

template <class>
struct IsFunction : std::false_type {};
template <class FUN>
struct IsFunction<std::function<FUN>> : std::true_type {};

template <class FUN>
static constexpr bool IS_FUNCTION = IsFunction<std::remove_cvref_t<FUN>>::value;

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
struct return_<::Promise<T, WITH_RESOLVER>> {
   using type = T;
};

template <class FUN>
using return_t = typename return_<std::remove_cvref_t<FUN>>::type;

template <class T>
struct IsResolver : std::false_type {};

template <class T>
struct IsResolver<std::function<void(T const&)>> : std::true_type {};

template <>
struct IsResolver<std::function<void()>> : std::true_type {};

template <class FUN>
struct WithResolver : std::false_type {};

template <class T>
struct WithResolver<::Promise<T, true>> : std::true_type {};

template <class FUN>
static constexpr bool WITH_RESOLVER = WithResolver<return_t<FUN>>::value;

template <class FUN>
struct IsPromise : std::false_type {};

template <class T, bool WITH_RESOLVER>
struct IsPromise<::Promise<T, WITH_RESOLVER>> : std::true_type {};

template <class FUN>
static constexpr bool IS_PROMISE = IsPromise<return_t<FUN>>::value;

template <class FUN>
struct args_;

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

template <class FUN>
using args_t = typename args_<std::remove_cvref_t<FUN>>::type;

/**
 * @brief Type-erased promise interface, useful for storing promises in pointers.
 */
struct VPromise {
   struct Awaitable {
      /**
       * @brief Virtual destructor.
       */
      virtual ~Awaitable() = default;
      /**
       * @brief Check if the await can complete synchronously.
       */
      virtual bool await_ready() = 0;
      /**
       * @brief Resume the await and return its result.
       */
      virtual void await_resume() = 0;
      /**
       * @brief Suspend the coroutine and register continuation.
       * @param h Awaiting coroutine handle.
       */
      virtual void await_suspend(std::coroutine_handle<> h) = 0;
   };

   /**
    * @brief Detach the promise from this handle.
    */
   virtual void VDetach() && = 0;

   /**
    * @brief Virtual destructor.
    */
   virtual ~VPromise() = default;

   /**
    * @brief Access a type-erased awaitable.
    */
   virtual Awaitable& VAwait() = 0;
};
/**
 * @brief Owning pointer to a type-erased promise.
 */
using Pointer = std::unique_ptr<VPromise>;

}  // namespace promise

/**
 * @brief Build a Promise from a std::function.
 * @tparam FUN Function type (std::function).
 * @param func Callable that returns Promise<T> or T.
 * @param args Arguments forwarded to the callable.
 */
template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN>)
static constexpr auto MakePromise(FUN&& func, ARGS&&... args);

/**
 * @brief Build a Promise from a generic callable.
 * @tparam FUN Callable type.
 * @param func Callable that returns Promise<T> or T.
 * @param args Arguments forwarded to the callable.
 */
template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN>)
static constexpr auto MakePromise(FUN&& func, ARGS&&... args);

/**
 * @brief Build a resolver-style Promise from a std::function.
 * @tparam FUN Function type (std::function).
 * @param func Callable that returns Promise<T, true>.
 * @param args Arguments forwarded to the callable (after resolver args).
 */
template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
static constexpr auto MakeRPromise(FUN&& func, ARGS&&... args);

/**
 * @brief Build a resolver-style Promise from a generic callable.
 * @tparam FUN Callable type.
 * @param func Callable that returns Promise<T, true>.
 * @param args Arguments forwarded to the callable (after resolver args).
 */
template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
static constexpr auto MakeRPromise(FUN&& func, ARGS&&... args);

/**
 * @brief Reject a promise with a constructed exception.
 * @tparam EXCEPTION Exception type to construct.
 * @tparam RELAXED When true, ignore double-rejects.
 * @param reject Reject handle.
 * @param args Constructor args for EXCEPTION.
 */
template <class EXCEPTION, bool RELAXED = true, class... ARGS>
bool MakeReject(promise::Reject const& reject, ARGS&&... args);
namespace promise {

/**
 * @brief Build a promise and its resolve/reject handles.
 * @treturn Tuple-like result (promise, resolve, reject).
 */
template <class T>
static constexpr auto Pure();

/**
 * @brief Await all promises and return a combined result.
 * @param promise Promises to await.
 */
template <class... PROMISE>
static constexpr auto All(PROMISE&&... promise);
namespace details {
template <class T, bool WITH_RESOLVER>
class Promise;
}

template <class T, bool WITH_RESOLVER = true>
struct Resolver;
}  // namespace promise

#include "impl/Promise.inl"

/**
 * @brief Promise handle that owns shared state and supports co_await.
 * @tparam T Value type (or void).
 * @tparam WITH_RESOLVER Whether the promise uses an external resolver.
 */
template <class T, bool WITH_RESOLVER>
class Promise : public promise::VPromise {
public:
   using Details      = promise::details::Promise<T, WITH_RESOLVER>;
   using promise_type = Details::promise_type;

   /**
    * @brief Check if the promise can resume immediately.
    */
   bool await_ready() {
      assert(details_);
      return details_->await_ready();
   }

   /**
    * @brief Suspend the coroutine and register continuation.
    * @param h Awaiting coroutine handle.
    */
   auto await_suspend(std::coroutine_handle<> h) {
      assert(details_);
      return details_->await_suspend(h);
   }

   /**
    * @brief Resume the await and return the resolved value or throw.
    */
   auto await_resume() noexcept(false) {
      assert(details_);
      return details_->await_resume();
   }

   template <class SELF>
      requires(!WITH_RESOLVER)
   /**
    * @brief Start a resolver-less promise.
    */
   auto&& operator()(this SELF&& self) {
      (*self.details_)();

      if constexpr (std::is_lvalue_reference_v<SELF>) {
         return self;
      } else {
         return std::move(self).Detach();
      }
   }

   template <class SELF>
      requires(WITH_RESOLVER)
   /**
    * @brief Start a resolver-style promise with a resolver.
    * @param resolver Resolver to drive the promise.
    */
   auto&&
   operator()(this SELF&& self, std::unique_ptr<promise::Resolver<T, WITH_RESOLVER>>&& resolver) {
      (*self.details_)(std::move(resolver));

      if constexpr (std::is_lvalue_reference_v<SELF>) {
         return self;
      } else {
         return std::move(self).Detach();
      }
   }

   /**
    * @brief Check if the promise is resolved or rejected.
    */
   bool Done() const noexcept(false) {
      assert(details_);
      std::shared_lock lock{details_->mutex_};
      return details_->IsDone(lock);
   }

   /**
    * @brief Get the resolved value (valid only when done and resolved).
    */
   auto Value() const noexcept(false) {
      assert(details_);
      std::shared_lock lock{details_->mutex_};

      assert(details_->IsDone(lock));
      return details_->GetValue(lock);
   }

   /**
    * @brief Get the stored exception (valid when rejected).
    */
   std::exception_ptr Exception() const noexcept(false) {
      assert(details_);
      std::shared_lock lock{details_->mutex_};
      return details_->GetException(lock);
   }

   template <class FUN, class SELF, class... ARGS>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr auto
   /**
    * @brief Chain a continuation to run on resolve.
    * @param func Continuation to invoke on resolve.
    * @param args Arguments forwarded to the continuation.
    */
   Then(this SELF&& self, FUN&& func, ARGS&&... args) {
      assert(self.details_);

      if constexpr (std::is_lvalue_reference_v<SELF>) {
         return self.details_->Then(std::forward<FUN>(func), std::forward<ARGS>(args)...);
      } else {
         // Transfer ownership to next promise
         return static_cast<Details&&>(*self.details_)
           .Then(std::move(self.details_), std::forward<FUN>(func), std::forward<ARGS>(args)...);
      }
   }

   template <class FUN, class SELF, class... ARGS>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr auto
   /**
    * @brief Chain a continuation to run on rejection.
    * @param func Continuation to invoke on rejection.
    * @param args Arguments forwarded to the continuation.
    */
   Catch(this SELF&& self, FUN&& func, ARGS&&... args) {
      assert(self.details_);

      if constexpr (std::is_lvalue_reference_v<SELF>) {
         return self.details_->Catch(std::forward<FUN>(func), std::forward<ARGS>(args)...);
      } else {
         // Transfer ownership to next promise
         return static_cast<Details&&>(*self.details_)
           .Catch(std::move(self.details_), std::forward<FUN>(func), std::forward<ARGS>(args)...);
      }
   }

   template <class FUN, class SELF>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr auto
   /**
    * @brief Chain a continuation that runs regardless of outcome.
    * @param func Continuation to invoke after resolve or reject.
    */
   Finally(this SELF&& self, FUN&& func) {
      assert(self.details_);

      if constexpr (std::is_lvalue_reference_v<SELF>) {
         return self.details_->Finally(std::forward<FUN>(func));

      } else {
         // Transfer ownership to next promise
         return static_cast<Details&&>(*self.details_)
           .Finally(std::move(self.details_), std::forward<FUN>(func));
      }
   }

   template <class... ARGS>
   /**
    * @brief Create a resolved promise without starting a coroutine.
    * @param args Constructor args for the resolved value (if any).
    */
   static constexpr auto Resolve(ARGS&&... args) {
      return Details::Promise::Resolve(std::forward<ARGS>(args)...);
   }

   template <class... ARGS>
   /**
    * @brief Create a rejected promise without starting a coroutine.
    * @param args Constructor args for the rejection value (if any).
    */
   static constexpr auto Reject(ARGS&&... args) {
      return Details::Promise::Reject(std::forward<ARGS>(args)...);
   }

   /**
    * @brief Detach so the promise can live independently of this handle.
    */
   auto& Detach() && {
      assert(details_);
      auto& details = *details_;
      return details.Detach(std::move(details_));
   }

   /**
    * @brief Type-erased detach for VPromise.
    */
   void VDetach() && override { static_cast<Promise&&>(*this).Detach(); }

   template <class TYPE = promise::VPromise>
   /**
    * @brief Convert to a shared pointer of a type-erased promise.
    */
   auto ToPointer() && {
      return std::shared_ptr<TYPE>(static_cast<TYPE*>(new Promise{std::move(details_)}));
   }

private:
   std::shared_ptr<Details> details_{};

   /**
    * @brief Type-erased awaitable for VPromise.
    */
   Awaitable& VAwait() final { return details_->VAwait(); }

   /**
    * @brief Construct from shared promise details.
    */
   Promise(std::shared_ptr<Details> details)
      : details_(std::move(details)) {}

   /**
    * @brief Construct from coroutine handle.
    */
   Promise(Details::handle_type handle)
      : details_{[&handle]() constexpr {
         struct MakeUniqueFriend : Details {
            MakeUniqueFriend(Details::handle_type handle)
               : Details(std::move(handle)) {}
         };

         return std::make_unique<MakeUniqueFriend>(std::move(handle));
      }()} {}

   friend Details;
   friend typename Details::PromiseType;
   template <class, bool>
   friend class ::promise::details::Promise;
};

/**
 * @brief Public resolve handle alias.
 */
template <class T = void>
using Resolve = promise::Resolve<T>;
/**
 * @brief Public reject handle alias.
 */
using Reject = promise::Reject;

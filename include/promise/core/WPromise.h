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

#include "Promise.h"

#include <memory>
#include <variant>

namespace promise::details {

/**
 * @brief Promise handle that owns shared state and supports co_await.
 *
 * This class is the public-facing handle over the shared promise details. It provides
 * awaitable behavior and exposes state inspection and chaining helpers (Then/Catch/Finally)
 * without revealing whether the underlying promise is resolver-less or resolver-based.
 *
 * @tparam T Value type (or void).
 * @tparam WITH_RESOLVER Whether the promise uses an external resolver.
 *
 * @note Resolver is attached on first start (MakePromise or coroutine initial_suspend).
 * @note State queries (Done/Value/Exception) assume the promise has been started.
 */
template <class T>
class WPromise : public promise::VPromise {
public:
   /** @brief Promise details type for non-resolver promises. */
   using Promise = promise::details::Promise<T, false>;
   /** @brief Promise details type for resolver-backed promises. */
   using RPromise = promise::details::Promise<T, true>;

   /** @brief Shared variant over non-resolver and resolver-backed details. */
   using Details = std::variant<std::shared_ptr<Promise>, std::shared_ptr<RPromise>>;
   /** @brief Promise resolved value type. */
   using return_type = T;

   /** @brief Copy constructor. */
   WPromise(WPromise const& other) = default;

   /** @brief Copy assignment. */
   WPromise& operator=(WPromise const& other) = default;

   /** @brief Move constructor. */
   WPromise(WPromise&& other) noexcept = default;

   /** @brief Move assignment. */
   WPromise& operator=(WPromise&& other) noexcept = default;

   /** @brief Destroy this promise handle. */
   ~WPromise();

   /**
    * @brief Construct a promise handle from a callable.
    *
    * @param fun Callable used to create/start the promise.
    */
   template <class FUN>
      requires(function_constructible<FUN>)
   WPromise(FUN&& fun);

   class VAwaitable : public VPromise::Awaitable {
   public:
      /**
       * @brief Construct a type-erased awaitable from promise details.
       *
       * @param details Shared promise details.
       */
      explicit VAwaitable(Details details);

      /**
       * @brief Check if the promise can resume immediately.
       *
       * @return True if ready to resume.
       */
      bool await_ready() const final;

      /**
       * @brief Suspend the coroutine and register continuation.
       *
       * @param h Awaiting coroutine handle.
       */
      bool await_suspend(std::coroutine_handle<> h) const final;

      /**
       * @brief Resume the await and return or throw.
       *
       * @warning Throws if the promise was rejected.
       */
      void await_resume() const noexcept(false) final;

   private:
      Details details_;
   };

   class Awaitable {
   public:
      /**
       * @brief Construct an awaitable from promise details.
       *
       * @param details Shared promise details.
       */
      explicit Awaitable(Details details);

      /**
       * @brief Check if the promise can resume immediately.
       *
       * @return True if ready to resume.
       */
      bool await_ready() const;

      /**
       * @brief Suspend the coroutine and register continuation.
       *
       * @param h Awaiting coroutine handle.
       */
      bool await_suspend(std::coroutine_handle<> h) const;

      /**
       * @brief Resume the await and return the resolved value or throw.
       *
       * @return Resolved value for non-void promises.
       * @warning Throws if the promise was rejected.
       */
      T await_resume() const noexcept(false);

   private:
      Details details_;
   };

   /** @brief Build an awaitable adapter for this promise. */
   Awaitable operator co_await();

   /**
    * @brief Free-function co_await adapter.
    *
    * @param promise Promise to await.
    *
    * @return Awaitable adapter over the same shared state.
    */
   friend Awaitable operator co_await(WPromise const& promise) {
      return Awaitable{promise.details_};
   }

   /**
    * @brief Check if the promise is resolved or rejected.
    *
    * @return True if resolved or rejected.
    */
   [[nodiscard]] bool Done() const noexcept;

   /**
    * @brief Check if the promise is rejected.
    *
    * @return True if rejected.
    */
   [[nodiscard]] bool Rejected() const noexcept;

   /**
    * @brief Check if the promise is resolved.
    *
    * @return True if resolved.
    */
   [[nodiscard]] bool Resolved() const noexcept;

   /**
    * @brief Get the resolved value (valid only when done and resolved).
    *
    * @return Resolved value.
    *
    * @warning Undefined if called before resolution.
    */
   [[nodiscard]] cref_or_void_t<T> Value() const noexcept;

   /**
    * @brief Get the stored exception (valid when rejected).
    *
    * @return Stored exception pointer.
    */
   [[nodiscard]] std::exception_ptr Exception() const noexcept;

   /**
    * @brief Get the number of awaiters currently waiting on this promise.
    *
    * @return Number of awaiters.
    */
   [[nodiscard]] std::size_t Awaiters() const noexcept;

   /**
    * @brief Get the total number of awaiter registrations on this promise.
    *
    * @return Total number of awaiter registrations.
    */
   [[nodiscard]] std::size_t UseCount() const noexcept;

   /**
    * @brief Wait for the promise to be awaited.
    *
    * @param current_count Optional current use count to wait from a specific point.
    */
   void WaitAwaited(std::optional<std::size_t> current_count = std::nullopt) const;

   /**
    * @brief Wait for the promise to be resolved or rejected.
    */
   void WaitDone() const;

   /**
    * @brief Chain a continuation to run on resolve.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param func Continuation to invoke on resolve.
    * @param args Arguments forwarded to the continuation.
    *
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
    */
   template <class FUN, class SELF, class... ARGS>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr ThenReturn<FUN>
   Then(this SELF&& self, FUN&& func, ARGS&&... args);

   /**
    * @brief Chain a continuation to run on rejection.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param func Continuation to invoke on rejection.
    * @param args Arguments forwarded to the continuation.
    *
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
    */
   template <class FUN, class SELF, class... ARGS>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr CatchReturn<T, FUN>
   Catch(this SELF&& self, FUN&& func, ARGS&&... args);

   /**
    * @brief Chain a continuation that runs regardless of outcome.
    *
    * @tparam FUN Type of the continuation function.
    *
    * @param func Continuation to invoke after resolve or reject.
    *
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
    */
   template <class FUN, class SELF>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr FinallyReturn<T>
   Finally(this SELF&& self, FUN&& func);

   /**
    * @brief Chain a continuation that runs regardless of outcome.
    *
    * @tparam FUN Type of the continuation function.
    *
    * @param func Continuation to invoke after resolve or reject.
    *
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
    */
   template <class T2, class SELF>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr WPromise<T2> Race(
     this SELF&&                         self,
     WPromise<T2>&&                      race_promise,
     std::shared_ptr<Resolve<T2>> const& resolve,
     std::shared_ptr<Reject> const&      reject
   );

   /**
    * @brief Create a resolved promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the resolved value constructor.
    *
    * @param args Constructor args for the resolved value (if any).
    *
    * @return Resolved promise.
    */
   template <class... ARGS>
   static constexpr WPromise<T> Resolve(ARGS&&... args);

   /**
    * @brief Create a rejected promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the rejection value constructor.
    *
    * @param args Constructor args for the rejection value (if any).
    *
    * @return Rejected promise.
    */
   template <class... ARGS>
   static constexpr WPromise<T> Reject(ARGS&&... args);

   /**
    * @brief Create a rejected promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the rejection value constructor.
    *
    * @param args Constructor args for the rejection value (if any).
    *
    * @return Rejected promise.
    */
   template <class EXCEPTION, class... ARGS>
   static constexpr WPromise<T> Reject(ARGS&&... args);

   /**
    * @brief Create a pending promise with resolve and reject handles.
    *
    * @return Tuple of promise handle, resolve handle, and reject handle.
    */
   static constexpr std::
     tuple<WPromise<T>, std::shared_ptr<promise::Resolve<T>>, std::shared_ptr<promise::Reject>>
     Create();

   /**
    * @brief Detach so the promise can live independently of this handle.
    *
    * @return Reference to promise details.
    * @note Best practice: detach only when the promise must outlive this handle.
    */
   void Detach() &&;

   /**
    * @brief Type-erased detach for VPromise.
    */
   void VDetach() && override;

   /**
    * @brief Convert to a shared pointer of a type-erased promise.
    *
    * @tparam TYPE Type of the type-erased promise (defaults to VPromise).
    *
    * @return Shared pointer to a type-erased promise.
    */
   template <class TYPE = promise::VPromise>
   [[nodiscard]] std::shared_ptr<TYPE> ToPointer() &&;

private:
   Details details_{};

   /**
    * @brief Type-erased awaitable for VPromise.
    *
    * @return Reference to a type-erased awaitable wrapper.
    */
   VPromise::Awaitable& VAwait() final;

   /**
    * @brief Construct from shared promise details.
    *
    * @tparam WITH_RESOLVER Whether the promise uses an external resolver.
    *
    * @param details Shared promise state.
    */
   template <bool WITH_RESOLVER>
   WPromise(std::shared_ptr<promise::details::Promise<T, WITH_RESOLVER>> details);

   /**
    * @brief Construct from shared promise details.
    *
    * @param details Shared promise state.
    */
   WPromise(Details details);

   template <class, bool>
   friend class IPromise;

   friend Promise;
   friend RPromise;
   template <class, bool>
   friend class ::promise::details::Promise;
};

/** @brief Deduction guide for promise-returning callables. */
template <class FUN>
   requires(function_constructible<FUN> && IS_PROMISE_FUNCTION<FUN>)
WPromise(FUN&& fun) -> WPromise<return_or_void_t<return_t<FUN>>>;

/** @brief Deduction guide for value-returning callables. */
template <class FUN>
   requires(function_constructible<FUN> && !IS_PROMISE_FUNCTION<FUN>)
WPromise(FUN&& fun) -> WPromise<WReturn<FUN>>;

/**
 * @brief Promise handle that owns shared state and supports co_await.
 *
 * @tparam T Value type (or void).
 * @tparam WITH_RESOLVER Whether the promise uses an external resolver.
 */
template <class T, bool WITH_RESOLVER>
class IPromise : public WPromise<T> {
public:
   /** @brief Parent public promise handle type. */
   using Parent = WPromise<T>;
   /** @brief Concrete details type for this resolver mode. */
   using Details = promise::details::Promise<T, WITH_RESOLVER>;
   /** @brief Coroutine promise_type associated with this handle. */
   using promise_type = typename Details::promise_type;
   /** @brief Promise resolved value type. */
   using return_type = T;

   /**
    * @brief co_await operator for resolver-less promises.
    */
   using WPromise<T>::operator co_await;

   /**
    * @brief Check if the promise is resolved or rejected.
    * @return True if resolved or rejected.
    */
   using WPromise<T>::Done;

   /**
    * @brief Get the resolved value (valid only when done and resolved).
    * @return Resolved value.
    * @warning Undefined if called before resolution.
    */
   using WPromise<T>::Value;

   /**
    * @brief Get the stored exception (valid when rejected).
    * @return Stored exception pointer.
    */
   using WPromise<T>::Exception;

   /**
    * @brief Get the number of awaiters currently waiting on this promise.
    * @return Number of awaiters.
    */
   using WPromise<T>::Awaiters;

   /**
    * @brief Chain a continuation to run on resolve.
    * @param func Continuation to invoke on resolve.
    * @param args Arguments forwarded to the continuation.
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
    */
   using WPromise<T>::Then;

   /**
    * @brief Chain a continuation to run on rejection.
    * @param func Continuation to invoke on rejection.
    * @param args Arguments forwarded to the continuation.
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
    */
   using WPromise<T>::Catch;

   /**
    * @brief Chain a continuation that runs regardless of outcome.
    * @param func Continuation to invoke after resolve or reject.
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
    */
   using WPromise<T>::Finally;

   /**
    * @brief Create a resolved promise without starting a coroutine.
    * @param args Constructor args for the resolved value (if any).
    * @return Resolved promise.
    */
   using WPromise<T>::Resolve;

   /**
    * @brief Create a rejected promise without starting a coroutine.
    * @param args Constructor args for the rejection value (if any).
    * @return Rejected promise.
    */
   using WPromise<T>::Reject;

   /**
    * @brief Detach so the promise can live independently of this handle.
    * @return Reference to promise details.
    * @note Best practice: detach only when the promise must outlive this handle.
    */
   Promise<T, WITH_RESOLVER>& Detach() &&;

   /**
    * @brief Type-erased detach for VPromise.
    */
   using WPromise<T>::VDetach;

   /**
    * @brief Convert to a shared pointer of a type-erased promise.
    * @return Shared pointer to a type-erased promise.
    */
   using WPromise<T>::ToPointer;

   /**
    * @brief Start a resolver-less promise.
    * @return This promise handle (lvalue) or detached handle (rvalue).
    * @note Best practice: keep or detach the handle immediately after starting.
    */
   template <class SELF>
      requires(!WITH_RESOLVER)
   Promise<T, WITH_RESOLVER>& operator()(this SELF&& self);

   /**
    * @brief Start a resolver-style promise with a resolver.
    * @param resolver Resolver to drive the promise.
    * @return This promise handle (lvalue) or detached handle (rvalue).
    * @note Best practice: keep or detach the handle immediately after starting.
    */
   template <class SELF>
      requires(WITH_RESOLVER)
   Promise<T, WITH_RESOLVER>&
   operator()(this SELF&& self, std::unique_ptr<promise::Resolver<T>>&& resolver);

private:
   /**
    * @brief Type-erased awaitable for VPromise.
    * @return Reference to a type-erased awaitable wrapper.
    */
   using WPromise<T>::VAwait;

   /**
    * @brief Construct from shared promise details.
    * @param details Shared promise state.
    */
   using WPromise<T>::WPromise;

   /**
    * @brief Construct from coroutine handle.
    * @param handle Coroutine handle.
    */
   IPromise(Details::handle_type handle);

   friend Details;
   friend typename Details::PromiseType;
   template <class, bool>
   friend class ::promise::details::Promise;
};

}  // namespace promise::details
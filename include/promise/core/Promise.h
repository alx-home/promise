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

#include "Handle.h"
#include "Memcheck.h"

#include <utils/Scoped.h>
#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <variant>

namespace promise::details {
template <class T>
class WPromise;

/**
 * @brief Promise coroutine implementation with optional resolver support.
 *
 * This class implements the promise_type for C++20 coroutines, providing awaitable
 * semantics and continuation chaining. It supports two modes:
 * - Resolver-less mode (WITH_RESOLVER=false): Promise completes via co_return
 * - Resolver mode (WITH_RESOLVER=true): Promise completes via external resolver
 *
 * @tparam T The value type that the promise resolves to. Can be void for promises
 *           that don't produce a value.
 * @tparam WITH_RESOLVER Boolean flag indicating whether the promise uses an external
 *                       resolver (true) or completes via co_return (false).
 *
 * The class provides:
 * - Coroutine awaitable interface (await_ready, await_suspend, await_resume)
 * - Promise chaining via Then(), Catch(), and Finally() methods
 * - Static factory methods for creating resolved/rejected promises
 * - Thread-safe access through internal locking mechanisms
 * - Exception handling and propagation through the promise chain
 *
 * @note Move operations are deleted; promises cannot be moved or copied.
 *       Move operations are handled in the WPromise wrapper, which manages shared ownership of the
 * promise details.
 */
template <class T, bool WITH_RESOLVER>
class Promise
   : public Handle<T, WITH_RESOLVER>
   , public std::enable_shared_from_this<Promise<T, WITH_RESOLVER>>
#ifdef PROMISE_MEMCHECK
   , public Refcount
#endif  // PROMISE_MEMCHECK
{
public:
   /** @brief Coroutine promise_type produced by this details implementation. */
   using promise_type = Handle<T, WITH_RESOLVER>::PromiseType;

protected:
   friend class details::IPromise<T, WITH_RESOLVER>;
   friend class details::WPromise<T>;

   using ValuePromise = Handle<T, WITH_RESOLVER>::ValuePromise;
   using handle_type  = Handle<T, WITH_RESOLVER>::handle_type;
   using Locker       = typename Handle<T, WITH_RESOLVER>::Locker;
   using Unlock       = typename Handle<T, WITH_RESOLVER>::Unlock;

   static constexpr bool IS_VOID = std::is_void_v<T>;

private:
   using Handle<T, WITH_RESOLVER>::Handle;

   /**
    * @brief Constructor for the Promise class.
    *
    * @param handle Coroutine handle to manage.
    */
   explicit Promise(handle_type handle);

public:
   /**
    * @brief Destructor for the Promise class.
    *
    * Ensures that the coroutine handle is properly cleaned up and that any unresolved promises are
    * detected in debug mode.
    */
   ~Promise() override;

   /** @brief Non-movable promise details type. */
   Promise(Promise&& rhs) noexcept = delete;

   /** @brief Non-movable promise details type. */
   Promise& operator=(Promise&& rhs) noexcept = delete;

   /** @brief Non-copyable promise details type. */
   Promise(Promise const&) = delete;

   /** @brief Non-copyable promise details type. */
   Promise& operator=(Promise const&) = delete;

private:
   /**
    * @brief Check if the promise can resume immediately.
    *
    * @return True if ready to resume.
    */
   bool await_ready();

   /**
    * @brief Suspend the coroutine and register continuation.
    *
    * @param h Awaiting coroutine handle.
    */
   bool await_suspend(std::coroutine_handle<> h);

   /**
    * @brief Resume the await and return the resolved value or throw.
    *
    * @return Resolved value for non-void promises.
    */
   cref_or_void_t<T> await_resume() noexcept(false);

   /**
    * @brief Start a resolver-less promise.
    */
   template <class...>
      requires(!WITH_RESOLVER)
   void operator()();

   /**
    * @brief Start a resolver-style promise with a resolver.
    *
    * @param resolver Resolver to drive the promise.
    */
   template <class...>
      requires(WITH_RESOLVER)
   void operator()(std::unique_ptr<Resolver<T>>&& resolver);

   /**
    * @brief Get a type-erased awaitable wrapper.
    *
    * @return Reference to a heap-allocated awaitable wrapper.
    * @warning The wrapper is deleted in await_resume.
    */
   VPromise::Awaitable& VAwait() final;

   /**
    * @brief Get the stored exception using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return Stored exception pointer.
    */
   [[nodiscard]] std::exception_ptr const& GetException(Lock lock) const;

   /**
    * @brief Register an awaiting coroutine.
    *
    * @param h Awaiting coroutine handle.
    * @param lock Active lock for thread-safe access.
    */
   void Await(std::coroutine_handle<> h, UniqueLock lock);

   /**
    * @brief Register an awaiting function.
    *
    * @param fun Awaiting function.
    * @param lock Active lock for thread-safe access.
    *
    * @return ID of the registered function.
    */
   std::size_t Await(std::function<void()> fun, UniqueLock lock);

   /**
    * @brief Unregister an awaiting function.
    *
    * @param id ID of the registered function.
    * @param lock Active lock for thread-safe access.
    *
    * @return True if the function was unregistered.
    */
   bool UnAwait(std::size_t id, UniqueLock lock);

   /**
    * @brief Get the number of awaiters currently waiting on this promise.
    *
    * @return Number of awaiters.
    */
   [[nodiscard]] std::size_t Awaiters() const;

   /**
    * @brief Get the total number of awaiter registrations on this promise.
    *
    * @return Total number of awaiter registrations.
    */
   [[nodiscard]] std::size_t UseCount() const noexcept;

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
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr ThenReturn<FUN> Then(FUN&& func, ARGS&&... args) &;

   /**
    * @brief Chain a continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param self Owning shared pointer to promise details.
    * @param func Continuation to invoke on resolve.
    * @param args Arguments forwarded to the continuation.
    *
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr ThenReturn<FUN>
   Then(std::shared_ptr<Promise>&& self, FUN&& func, ARGS&&... args) &&;

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
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr CatchReturn<T, FUN> Catch(FUN&& func, ARGS&&... args) &;

   /**
    * @brief Chain a rejection continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param self Owning shared pointer to promise details.
    * @param func Continuation to invoke on rejection.
    * @param args Arguments forwarded to the continuation.
    *
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr CatchReturn<T, FUN>
   Catch(std::shared_ptr<Promise>&& self, FUN&& func, ARGS&&... args) &&;

   /**
    * @brief Chain a continuation that runs regardless of outcome.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param func Continuation to invoke after resolve or reject.
    *
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr FinallyReturn<T> Finally(FUN&& func) &;

   /**
    * @brief Chain a finally continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    *
    * @param self Owning shared pointer to promise details.
    * @param func Continuation to invoke after resolve or reject.
    *
    * @return Chained promise.
    */
   template <class FUN>
   [[nodiscard]] constexpr FinallyReturn<T> Finally(std::shared_ptr<Promise>&& self, FUN&& func) &&;

   /**
    * @brief Chain a race continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    *
    * @param race_promise Promise to race against.
    * @param resolve Resolver for the race promise.
    * @param reject Rejecter for the race promise.
    *
    * @return Chained promise.
    */
   template <class T2>
   [[nodiscard]] constexpr WPromise<T2> Race(
     WPromise<T2>&&                      race_promise,
     std::shared_ptr<Resolve<T2>> const& resolve,
     std::shared_ptr<Reject> const&      reject
   ) &;

   /**
    * @brief Chain a race continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    *
    * @param self Owning shared pointer to promise details.
    * @param race_promise Promise to race against.
    * @param resolve Resolver for the race promise.
    * @param reject Rejecter for the race promise.
    *
    * @return Chained promise.
    */
   template <class T2>
   [[nodiscard]] constexpr WPromise<T2> Race(
     std::shared_ptr<Promise>&&          self,
     WPromise<T2>&&                      race_promise,
     std::shared_ptr<Resolve<T2>> const& resolve,
     std::shared_ptr<Reject> const&      reject
   ) &&;

public:
   /**
    * @brief Create a resolved promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the resolver.
    *
    * @param args Constructor args for the resolved value (if any).
    *
    * @return Resolved promise.
    */
   static constexpr std::
     tuple<details::WPromise<T>, std::shared_ptr<Resolve<T>>, std::shared_ptr<Reject>>
     Create();

   /**
    * @brief Create a resolved promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the resolver.
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
    * @tparam ARGS Types of arguments to forward to the resolver.
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
    * @tparam EXCEPTION Type of the exception to reject with.
    * @tparam ARGS Types of arguments to forward to the resolver.
    *
    * @param args Constructor args for the rejection value (if any).
    *
    * @return Rejected promise.
    */
   template <class EXCEPTION, class... ARGS>
   static constexpr WPromise<T> Reject(ARGS&&... args);

   /**
    * @brief Create a promise from a callable and optional resolver.
    **
    * @tparam RPROMISE Boolean flag indicating whether to return a tuple with resolver and
    *rejector.
    * @tparam FUN Type of the callable.
    * @tparam ARGS Types of arguments to forward to the callable.
    **
    * @param func Callable used to produce the promise.
    * @param args Arguments forwarded to the callable.
    *
    * @return Promise or tuple when RPROMISE is true.
    */
   template <bool RPROMISE, class FUN, class... ARGS>
#if defined(__clang__)
   __attribute__((no_sanitize("address")))
#endif
   static constexpr std::conditional_t<
     RPROMISE,
     std::tuple<
       details::WPromise<T>,
       std::shared_ptr<promise::Resolve<T>>,
       std::shared_ptr<promise::Reject>>,
     details::WPromise<T>> Create(FUN&& func, ARGS&&... args);

private:
   std::atomic<std::size_t> use_count_{0};
   struct AwaitFunction : std::function<void()> {
      std::size_t id_{0};
   };
   using Awaiter = std::variant<std::coroutine_handle<>, AwaitFunction>;

   std::unique_ptr<Function> function_{nullptr};
   std::vector<Awaiter>      awaiters_{};

   friend Handle<T, WITH_RESOLVER>;
};
}  // namespace promise::details

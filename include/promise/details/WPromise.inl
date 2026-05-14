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

#include "../core/WPromise.h"

namespace promise::details {

/**
 * @brief Promise handle that owns shared state and supports co_await.
 *
 * This class is the public-facing handle over the shared promise details. It
 * provides awaitable behavior and exposes state inspection and chaining helpers
 * (Then/Catch/Finally) without revealing whether the underlying promise is
 * resolver-less or resolver-based.
 *
 * @tparam T Value type (or void).
 * @tparam WITH_RESOLVER Whether the promise uses an external resolver.
 *
 * @note Resolver is attached on first start (MakePromise or coroutine
 * initial_suspend).
 * @note State queries (Done/Value/Exception) assume the promise has been
 * started.
 */
template <class T>

WPromise<T>::~WPromise() {
   std::visit(
     [](auto& details) constexpr {
        if (details && !details->IsDone()) {
           details->WaitDone();
        }
     },
     details_
   );
}

template <class T>
template <class FUN>
   requires(function_constructible<FUN>)
WPromise<T>::WPromise(FUN&& fun)
   : WPromise(MakePromise(std::forward<FUN>(fun))) {}

template <class T>
WPromise<T>::VAwaitable::VAwaitable(WPromise<T>::Details details)
   : details_(std::move(details)) {}

/**
 * @brief Check if the promise can resume immediately.
 *
 * @return True if ready to resume.
 */
template <class T>
bool
WPromise<T>::VAwaitable::await_ready() const {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        return details->await_ready();
     },
     details_
   );
}

/**
 * @brief Suspend the coroutine and register continuation.
 *
 * @param h Awaiting coroutine handle.
 */
template <class T>
bool
WPromise<T>::VAwaitable::await_suspend(std::coroutine_handle<> h) const {
   return std::visit(
     [h = std::move(h)](auto const& details) constexpr {
        assert(details);
        return details->await_suspend(std::move(h));
     },
     details_
   );
}

/**
 * @brief Resume the await and return or throw.
 *
 * @warning Throws if the promise was rejected.
 */
template <class T>
void
WPromise<T>::VAwaitable::await_resume() const noexcept(false) {
   std::unique_ptr<VAwaitable const> const self{this};  // ensure deletion after resume

   std::visit(
     [](auto const& details) constexpr {
        assert(details);
        details->await_resume();
     },
     details_
   );
}

template <class T>
WPromise<T>::Awaitable::Awaitable(Details details)
   : details_(std::move(details)) {}

/**
 * @brief Check if the promise can resume immediately.
 *
 * @return True if ready to resume.
 */
template <class T>
bool
WPromise<T>::Awaitable::await_ready() const {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        return details->await_ready();
     },
     details_
   );
}

/**
 * @brief Suspend the coroutine and register continuation.
 *
 * @param h Awaiting coroutine handle.
 */
template <class T>
bool
WPromise<T>::Awaitable::await_suspend(std::coroutine_handle<> h) const {
   return std::visit(
     [h = std::move(h)](auto const& details) constexpr {
        assert(details);
        return details->await_suspend(std::move(h));
     },
     details_
   );
}

/**
 * @brief Resume the await and return the resolved value or throw.
 *
 * @return Resolved value for non-void promises.
 * @warning Throws if the promise was rejected.
 */
template <class T>
T
WPromise<T>::Awaitable::await_resume() const noexcept(false) {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        return details->await_resume();
     },
     details_
   );
}

template <class T>
typename WPromise<T>::Awaitable
WPromise<T>::operator co_await() {
   return Awaitable{details_};
}

/**
 * @brief Check if the promise is resolved or rejected.
 *
 * @return True if resolved or rejected.
 */
template <class T>
[[nodiscard]] bool
WPromise<T>::Done() const noexcept {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        std::shared_lock lock{details->mutex_};
        return details->IsDone(lock);
     },
     details_
   );
}

/**
 * @brief Check if the promise is rejected.
 *
 * @return True if rejected.
 */
template <class T>
[[nodiscard]] bool
WPromise<T>::Rejected() const noexcept {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        std::shared_lock lock{details->mutex_};
        return details->IsRejected(lock);
     },
     details_
   );
}

/**
 * @brief Check if the promise is resolved.
 *
 * @return True if resolved.
 */
template <class T>
[[nodiscard]] bool
WPromise<T>::Resolved() const noexcept {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        std::shared_lock lock{details->mutex_};
        return details->IsResolved(lock);
     },
     details_
   );
}

/**
 * @brief Get the resolved value (valid only when done and resolved).
 *
 * @return Resolved value.
 *
 * @warning Undefined if called before resolution.
 */
template <class T>
[[nodiscard]] cref_or_void_t<T>
WPromise<T>::Value() const noexcept {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        std::shared_lock lock{details->mutex_};

        assert(details->IsDone(lock));
        return details->GetValue(lock);
     },
     details_
   );
}

/**
 * @brief Get the stored exception (valid when rejected).
 *
 * @return Stored exception pointer.
 */
template <class T>
[[nodiscard]] std::exception_ptr
WPromise<T>::Exception() const noexcept {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        std::shared_lock lock{details->mutex_};

        assert(details->IsDone(lock));
        return details->GetException(lock);
     },
     details_
   );
}

/**
 * @brief Get the number of awaiters currently waiting on this promise.
 *
 * @return Number of awaiters.
 */
template <class T>
[[nodiscard]] std::size_t
WPromise<T>::Awaiters() const noexcept {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        std::shared_lock lock{details->mutex_};
        return details->Awaiters();
     },
     details_
   );
}

/**
 * @brief Get the total number of awaiter registrations on this promise.
 *
 * @return Total number of awaiter registrations.
 */
template <class T>
[[nodiscard]] std::size_t
WPromise<T>::UseCount() const noexcept {
   return std::visit(
     [](auto const& details) constexpr {
        assert(details);
        return details->UseCount();
     },
     details_
   );
}

/**
 * @brief Wait for the promise to be awaited.
 *
 * @param current_count Optional current use count to wait from a specific
 * point.
 */
template <class T>
void
WPromise<T>::WaitAwaited(std::optional<std::size_t> current_count) const {
   std::visit(
     [current_count](auto const& details) constexpr {
        assert(details);
        details->WaitAwaited(current_count);
     },
     details_
   );
}

/**
 * @brief Wait for the promise to be resolved or rejected.
 */
template <class T>
void
WPromise<T>::WaitDone() const {
   std::visit(
     [](auto const& details) constexpr {
        assert(details);
        details->WaitDone();
     },
     details_
   );
}

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
template <class T>
template <class FUN, class SELF, class... ARGS>
[[nodiscard("Either store this promise or call Detach()")]] constexpr ThenReturn<FUN>
WPromise<T>::Then(this SELF&& self, FUN&& func, ARGS&&... args) {
   return std::visit(
     [&](auto&& details) constexpr {
        assert(details);

        if constexpr (std::is_lvalue_reference_v<SELF>) {
           return details->Then(
             std::function{std::forward<FUN>(func)}, std::forward<ARGS>(args)...
           );
        } else {
           // Transfer ownership to next promise
           return std::move(*details).Then(
             std::move(details), std::function{std::move(func)}, std::forward<ARGS>(args)...
           );
        }
     },
     self.details_
   );
}

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
template <class T>
template <class FUN, class SELF, class... ARGS>
[[nodiscard("Either store this promise or call Detach()")]] constexpr CatchReturn<T, FUN>
WPromise<T>::Catch(this SELF&& self, FUN&& func, ARGS&&... args) {
   return std::visit(
     [&](auto&& details) constexpr {
        assert(details);

        if constexpr (std::is_lvalue_reference_v<SELF>) {
           return details->Catch(
             std::function{std::forward<FUN>(func)}, std::forward<ARGS>(args)...
           );
        } else {
           // Transfer ownership to next promise
           return std::move(*details).Catch(
             std::move(details), std::function{std::move(func)}, std::forward<ARGS>(args)...
           );
        }
     },
     self.details_
   );
}

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
template <class T>
template <class FUN, class SELF>
[[nodiscard("Either store this promise or call Detach()")]] constexpr FinallyReturn<T>
WPromise<T>::Finally(this SELF&& self, FUN&& func) {
   return std::visit(
     [&](auto&& details) constexpr {
        assert(details);

        if constexpr (std::is_lvalue_reference_v<SELF>) {
           return details->Finally(std::function{std::forward<FUN>(func)});

        } else {
           // Transfer ownership to next promise
           return std::move(*details).Finally(std::move(details), std::function{std::move(func)});
        }
     },
     self.details_
   );
}

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
template <class T>
template <class T2, class SELF>
[[nodiscard("Either store this promise or call Detach()")]] constexpr WPromise<T2>
WPromise<T>::Race(
  this SELF&&                                  self,
  WPromise<T2>&&                               race_promise,
  std::shared_ptr<promise::Resolve<T2>> const& resolve,
  std::shared_ptr<promise::Reject> const&      reject
) {
   return std::visit(
     [&](auto&& details) constexpr {
        assert(details);

        if constexpr (std::is_lvalue_reference_v<SELF>) {
           return details->Race(std::move(race_promise), resolve, reject);

        } else {
           // Transfer ownership to next promise
           return std::move(*details).Race(
             std::move(details), std::move(race_promise), resolve, reject
           );
        }
     },
     self.details_
   );
}

/**
 * @brief Create a resolved promise without starting a coroutine.
 *
 * @tparam ARGS Types of arguments to forward to the resolved value constructor.
 *
 * @param args Constructor args for the resolved value (if any).
 *
 * @return Resolved promise.
 */
template <class T>
template <class... ARGS>
constexpr WPromise<T>
WPromise<T>::Resolve(ARGS&&... args) {
   return Promise::Resolve(std::forward<ARGS>(args)...);
}

/**
 * @brief Create a rejected promise without starting a coroutine.
 *
 * @tparam ARGS Types of arguments to forward to the rejection value
 * constructor.
 *
 * @param args Constructor args for the rejection value (if any).
 *
 * @return Rejected promise.
 */
template <class T>
template <class... ARGS>
constexpr WPromise<T>
WPromise<T>::Reject(ARGS&&... args) {
   return Promise::Reject(std::forward<ARGS>(args)...);
}

/**
 * @brief Create a rejected promise without starting a coroutine.
 *
 * @tparam ARGS Types of arguments to forward to the rejection value
 * constructor.
 *
 * @param args Constructor args for the rejection value (if any).
 *
 * @return Rejected promise.
 */
template <class T>
template <class EXCEPTION, class... ARGS>
constexpr WPromise<T>
WPromise<T>::Reject(ARGS&&... args) {
   return Promise::template Reject<EXCEPTION>(std::forward<ARGS>(args)...);
}

template <class T>
constexpr std::
  tuple<WPromise<T>, std::shared_ptr<promise::Resolve<T>>, std::shared_ptr<promise::Reject>>
  WPromise<T>::Create() {
   return RPromise::Create();
}

/**
 * @brief Detach so the promise can live independently of this handle.
 *
 * @return Reference to promise details.
 * @note Best practice: detach only when the promise must outlive this handle.
 */
template <class T>
void
WPromise<T>::Detach() && {
   return std::visit(
     [&](auto&& details) constexpr {
        assert(details);
        auto& details_ref = *details;
        std::move(details_ref).Detach(std::move(details));
     },
     details_
   );
}

/**
 * @brief Type-erased detach for VPromise.
 */
template <class T>
void
WPromise<T>::VDetach() && {
   static_cast<WPromise&&>(*this).Detach();
}

/**
 * @brief Convert to a shared pointer of a type-erased promise.
 *
 * @tparam TYPE Type of the type-erased promise (defaults to VPromise).
 *
 * @return Shared pointer to a type-erased promise.
 */
template <class T>
template <class TYPE>
[[nodiscard]] std::shared_ptr<TYPE>
WPromise<T>::ToPointer() && {
   return std::shared_ptr<TYPE>(static_cast<TYPE*>(new WPromise{std::move(details_)}));
}

/**
 * @brief Type-erased awaitable for VPromise.
 *
 * @return Reference to a type-erased awaitable wrapper.
 */
template <class T>
VPromise::Awaitable&
WPromise<T>::VAwait() {
   return *new VAwaitable{details_};
}

/**
 * @brief Construct from shared promise details.
 *
 * @tparam WITH_RESOLVER Whether the promise uses an external resolver.
 *
 * @param details Shared promise state.
 */
template <class T>
template <bool WITH_RESOLVER>
WPromise<T>::WPromise(std::shared_ptr<promise::details::Promise<T, WITH_RESOLVER>> details)
   : details_(std::move(details)) {}

/**
 * @brief Construct from shared promise details.
 *
 * @param details Shared promise state.
 */
template <class T>
WPromise<T>::WPromise(Details details)
   : details_(std::move(details)) {}

/**
 * @brief Detach so the promise can live independently of this handle.
 * @return Reference to promise details.
 * @note Best practice: detach only when the promise must outlive this handle.
 */
template <class T, bool WITH_RESOLVER>
Promise<T, WITH_RESOLVER>&
IPromise<T, WITH_RESOLVER>::Detach() && {
   using Promise = std::shared_ptr<promise::details::Promise<T, WITH_RESOLVER>>;

   auto& details_ptr = std::get<Promise>(this->details_);

   assert(details_ptr);
   auto& details = *details_ptr;
   return std::move(details).Detach(std::move(details_ptr));
}

/**
 * @brief Start a resolver-less promise.
 * @return This promise handle (lvalue) or detached handle (rvalue).
 * @note Best practice: keep or detach the handle immediately after starting.
 */
template <class T, bool WITH_RESOLVER>
template <class SELF>
   requires(!WITH_RESOLVER)
Promise<T, WITH_RESOLVER>&
IPromise<T, WITH_RESOLVER>::operator()(this SELF&& self) {
   using Promise = std::shared_ptr<promise::details::Promise<T, WITH_RESOLVER>>;

   assert(std::holds_alternative<Promise>(self.details_));
   (*std::get<Promise>(self.details_))();

   if constexpr (std::is_lvalue_reference_v<SELF>) {
      return self;
   } else {
      return std::move(self).Detach();
   }
}

/**
 * @brief Start a resolver-style promise with a resolver.
 * @param resolver Resolver to drive the promise.
 * @return This promise handle (lvalue) or detached handle (rvalue).
 * @note Best practice: keep or detach the handle immediately after starting.
 */
template <class T, bool WITH_RESOLVER>
template <class SELF>
   requires(WITH_RESOLVER)
Promise<T, WITH_RESOLVER>&
IPromise<T, WITH_RESOLVER>::operator()(
  this SELF&&                             self,
  std::unique_ptr<promise::Resolver<T>>&& resolver
) {
   using Promise = std::shared_ptr<promise::details::Promise<T, WITH_RESOLVER>>;
   assert(std::holds_alternative<Promise>(self.details_));
   (*std::get<Promise>(self.details_))(std::move(resolver));

   if constexpr (std::is_lvalue_reference_v<SELF>) {
      return self;
   } else {
      return std::move(self).Detach();
   }
}

/**
 * @brief Construct from coroutine handle.
 * @param handle Coroutine handle.
 */
template <class T, bool WITH_RESOLVER>
IPromise<T, WITH_RESOLVER>::IPromise(Details::handle_type handle)
   : WPromise<T>{[&handle]() constexpr {
      struct MakeUniqueFriend : Details {
         MakeUniqueFriend(Details::handle_type handle)
            : Details(std::move(handle)) {}
      };

      return std::make_unique<MakeUniqueFriend>(std::move(handle));
   }()} {}

/**
 * @brief Get a type-erased awaitable wrapper.
 *
 * @return Reference to a heap-allocated awaitable wrapper.
 * @warning The wrapper is deleted in await_resume.
 */
template <class T, bool WITH_RESOLVER>
VPromise::Awaitable&
Promise<T, WITH_RESOLVER>::VAwait() {
   return *new WPromise<T>::VAwaitable{this->shared_from_this()};
}

}  // namespace promise::details
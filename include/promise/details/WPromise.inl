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

#include "Promise.inl"

#include <memory>
#include <shared_mutex>
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
   using Promise  = promise::details::Promise<T, false>;
   using RPromise = promise::details::Promise<T, true>;

   using Details = std::variant<std::shared_ptr<Promise>, std::shared_ptr<RPromise>>;

   /**
    * @brief Check if the promise can resume immediately.
    *
    * @return True if ready to resume.
    */
   bool await_ready() const {
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
   auto await_suspend(std::coroutine_handle<> h) const {
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
   auto await_resume() const noexcept(false) {
      return std::visit(
        [](auto const& details) constexpr {
           assert(details);
           return details->await_resume();
        },
        details_
      );
   }

   /**
    * @brief Check if the promise is resolved or rejected.
    *
    * @return True if resolved or rejected.
    */
   bool Done() const noexcept {
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
    * @brief Get the resolved value (valid only when done and resolved).
    *
    * @return Resolved value.
    *
    * @warning Undefined if called before resolution.
    */
   auto Value() const noexcept {
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
   std::exception_ptr Exception() const noexcept {
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

   template <class FUN, class SELF, class... ARGS>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr auto
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
   Then(this SELF&& self, FUN&& func, ARGS&&... args) {
      return std::visit(
        [&](auto&& details) constexpr {
           assert(details);

           if constexpr (std::is_lvalue_reference_v<SELF>) {
              return details->Then(std::forward<FUN>(func), std::forward<ARGS>(args)...);
           } else {
              // Transfer ownership to next promise
              return std::move(*details).Then(
                std::move(details), std::forward<FUN>(func), std::forward<ARGS>(args)...
              );
           }
        },
        self.details_
      );
   }

   template <class FUN, class SELF, class... ARGS>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr auto
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
   Catch(this SELF&& self, FUN&& func, ARGS&&... args) {
      return std::visit(
        [&](auto&& details) constexpr {
           assert(details);

           if constexpr (std::is_lvalue_reference_v<SELF>) {
              return details->Catch(std::forward<FUN>(func), std::forward<ARGS>(args)...);
           } else {
              // Transfer ownership to next promise
              return std::move(*details).Catch(
                std::move(details), std::forward<FUN>(func), std::forward<ARGS>(args)...
              );
           }
        },
        self.details_
      );
   }

   template <class FUN, class SELF>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr auto
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
   Finally(this SELF&& self, FUN&& func) {
      return std::visit(
        [&](auto const& details) constexpr {
           assert(details);

           if constexpr (std::is_lvalue_reference_v<SELF>) {
              return details->Finally(std::forward<FUN>(func));

           } else {
              // Transfer ownership to next promise
              return std::move(*details).Finally(std::move(details), std::forward<FUN>(func));
           }
        },
        self.details_
      );
   }

   template <class... ARGS>
   /**
    * @brief Create a resolved promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the resolved value constructor.
    *
    * @param args Constructor args for the resolved value (if any).
    *
    * @return Resolved promise.
    */
   static constexpr auto Resolve(ARGS&&... args) {
      return Promise::Resolve(std::forward<ARGS>(args)...);
   }

   template <class... ARGS>
   /**
    * @brief Create a rejected promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the rejection value constructor.
    *
    * @param args Constructor args for the rejection value (if any).
    *
    * @return Rejected promise.
    */
   static constexpr auto Reject(ARGS&&... args) {
      return Promise::Reject(std::forward<ARGS>(args)...);
   }

   /**
    * @brief Detach so the promise can live independently of this handle.
    *
    * @return Reference to promise details.
    * @note Best practice: detach only when the promise must outlive this handle.
    */
   void Detach() && {
      return std::visit(
        [&](auto&& details) constexpr {
           assert(details);
           auto& details_ref = *details;
           details_ref.Detach(std::move(details));
        },
        details_
      );
   }

   /**
    * @brief Type-erased detach for VPromise.
    */
   void VDetach() && override { static_cast<WPromise&&>(*this).Detach(); }

   template <class TYPE = promise::VPromise>
   /**
    * @brief Convert to a shared pointer of a type-erased promise.
    *
    * @tparam TYPE Type of the type-erased promise (defaults to VPromise).
    *
    * @return Shared pointer to a type-erased promise.
    */
   auto ToPointer() && {
      return std::shared_ptr<TYPE>(static_cast<TYPE*>(new WPromise{std::move(details_)}));
   }

private:
   Details details_{std::shared_ptr<Promise>{nullptr}};

   /**
    * @brief Type-erased awaitable for VPromise.
    *
    * @return Reference to a type-erased awaitable wrapper.
    */
   Awaitable& VAwait() final {
      return std::visit(
        [&](auto const& details) constexpr -> auto& {
           assert(details);
           return details->VAwait();
        },
        details_
      );
   }

   /**
    * @brief Construct from shared promise details.
    *
    * @tparam WITH_RESOLVER Whether the promise uses an external resolver.
    *
    * @param details Shared promise state.
    */
   template <bool WITH_RESOLVER>
   WPromise(std::shared_ptr<promise::details::Promise<T, WITH_RESOLVER>> details)
      : details_(std::move(details)) {}

   /**
    * @brief Construct from shared promise details.
    *
    * @param details Shared promise state.
    */
   WPromise(Details details)
      : details_(std::move(details)) {}

   template <class, bool>
   friend class IPromise;

   friend Promise;
   friend RPromise;
   template <class, bool>
   friend class ::promise::details::Promise;
};

/**
 * @brief Promise handle that owns shared state and supports co_await.
 *
 * @tparam T Value type (or void).
 * @tparam WITH_RESOLVER Whether the promise uses an external resolver.
 */
template <class T, bool WITH_RESOLVER>
class IPromise : public WPromise<T> {
public:
   using Parent       = WPromise<T>;
   using Details      = promise::details::Promise<T, WITH_RESOLVER>;
   using promise_type = Details::promise_type;

   /**
    * @brief Check if the promise can resume immediately.
    * @return True if ready to resume.
    */
   using WPromise<T>::await_ready;

   /**
    * @brief Suspend the coroutine and register continuation.
    * @param h Awaiting coroutine handle.
    */
   using WPromise<T>::await_suspend;

   /**
    * @brief Resume the await and return the resolved value or throw.
    * @return Resolved value for non-void promises.
    * @warning Throws if the promise was rejected.
    */
   using WPromise<T>::await_resume;

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

   using WPromise<T>::Exception;

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
   auto& Detach() && {
      using Promise = std::shared_ptr<promise::details::Promise<T, WITH_RESOLVER>>;

      auto& details_ptr = std::get<Promise>(this->details_);

      assert(details_ptr);
      auto& details = *details_ptr;
      return details.Detach(std::move(details_ptr));
   }

   /**
    * @brief Type-erased detach for VPromise.
    */
   using WPromise<T>::VDetach;

   /**
    * @brief Convert to a shared pointer of a type-erased promise.
    * @return Shared pointer to a type-erased promise.
    */
   using WPromise<T>::ToPointer;

   template <class SELF>
      requires(!WITH_RESOLVER)
   /**
    * @brief Start a resolver-less promise.
    * @return This promise handle (lvalue) or detached handle (rvalue).
    * @note Best practice: keep or detach the handle immediately after starting.
    */
   auto&& operator()(this SELF&& self) {
      using Promise = std::shared_ptr<promise::details::Promise<T, WITH_RESOLVER>>;

      assert(std::holds_alternative<Promise>(self.details_));
      (*std::get<Promise>(self.details_))();

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
    * @return This promise handle (lvalue) or detached handle (rvalue).
    * @note Best practice: keep or detach the handle immediately after starting.
    */
   auto&&
   operator()(this SELF&& self, std::unique_ptr<promise::Resolver<T, WITH_RESOLVER>>&& resolver) {
      using Promise = std::shared_ptr<promise::details::Promise<T, WITH_RESOLVER>>;
      assert(std::holds_alternative<Promise>(self.details_));
      (*std::get<Promise>(self.details_))(std::move(resolver));

      if constexpr (std::is_lvalue_reference_v<SELF>) {
         return self;
      } else {
         return std::move(self).Detach();
      }
   }

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
   IPromise(Details::handle_type handle)
      : WPromise<T>{[&handle]() constexpr {
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

}  // namespace promise::details
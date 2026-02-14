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

#include <shared_mutex>

namespace promise::details {

/**
 * @brief Promise handle that owns shared state and supports co_await.
 * @tparam T Value type (or void).
 * @tparam WITH_RESOLVER Whether the promise uses an external resolver.
 */
template <class T, bool WITH_RESOLVER>
class WPromise : public promise::VPromise {
public:
   using Details      = promise::details::Promise<T, WITH_RESOLVER>;
   using promise_type = Details::promise_type;

   /**
    * @brief Check if the promise can resume immediately.
    * @return True if ready to resume.
    */
   bool await_ready() const {
      assert(details_);
      return details_->await_ready();
   }

   /**
    * @brief Suspend the coroutine and register continuation.
    * @param h Awaiting coroutine handle.
    */
   auto await_suspend(std::coroutine_handle<> h) const {
      assert(details_);
      return details_->await_suspend(h);
   }

   /**
    * @brief Resume the await and return the resolved value or throw.
    * @return Resolved value for non-void promises.
    * @warning Throws if the promise was rejected.
    */
   auto await_resume() const noexcept(false) {
      assert(details_);
      return details_->await_resume();
   }

   template <class SELF>
      requires(!WITH_RESOLVER)
   /**
    * @brief Start a resolver-less promise.
    * @return This promise handle (lvalue) or detached handle (rvalue).
    * @note Best practice: keep or detach the handle immediately after starting.
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
    * @return This promise handle (lvalue) or detached handle (rvalue).
    * @note Best practice: keep or detach the handle immediately after starting.
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
    * @return True if resolved or rejected.
    */
   bool Done() const noexcept(false) {
      assert(details_);
      std::shared_lock lock{details_->mutex_};
      return details_->IsDone(lock);
   }

   /**
    * @brief Get the resolved value (valid only when done and resolved).
    * @return Resolved value.
    * @warning Undefined if called before resolution.
    */
   auto Value() const noexcept(false) {
      assert(details_);
      std::shared_lock lock{details_->mutex_};

      assert(details_->IsDone(lock));
      return details_->GetValue(lock);
   }

   /**
    * @brief Get the stored exception (valid when rejected).
    * @return Stored exception pointer.
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
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
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
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
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
    * @return Chained promise.
    * @note Best practice: store the returned promise or call Detach().
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
    * @return Resolved promise.
    */
   static constexpr auto Resolve(ARGS&&... args) {
      return Details::Promise::Resolve(std::forward<ARGS>(args)...);
   }

   template <class... ARGS>
   /**
    * @brief Create a rejected promise without starting a coroutine.
    * @param args Constructor args for the rejection value (if any).
    * @return Rejected promise.
    */
   static constexpr auto Reject(ARGS&&... args) {
      return Details::Promise::Reject(std::forward<ARGS>(args)...);
   }

   /**
    * @brief Detach so the promise can live independently of this handle.
    * @return Reference to promise details.
    * @note Best practice: detach only when the promise must outlive this handle.
    */
   auto& Detach() && {
      assert(details_);
      auto& details = *details_;
      return details.Detach(std::move(details_));
   }

   /**
    * @brief Type-erased detach for VPromise.
    */
   void VDetach() && override { static_cast<WPromise&&>(*this).Detach(); }

   template <class TYPE = promise::VPromise>
   /**
    * @brief Convert to a shared pointer of a type-erased promise.
    * @return Shared pointer to a type-erased promise.
    */
   auto ToPointer() && {
      return std::shared_ptr<TYPE>(static_cast<TYPE*>(new WPromise{std::move(details_)}));
   }

private:
   std::shared_ptr<Details> details_{};

   /**
    * @brief Type-erased awaitable for VPromise.
    * @return Reference to a type-erased awaitable wrapper.
    */
   Awaitable& VAwait() final { return details_->VAwait(); }

   /**
    * @brief Construct from shared promise details.
    * @param details Shared promise state.
    */
   WPromise(std::shared_ptr<Details> details)
      : details_(std::move(details)) {}

   /**
    * @brief Construct from coroutine handle.
    * @param handle Coroutine handle.
    */
   WPromise(Details::handle_type handle)
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

}  // namespace promise::details
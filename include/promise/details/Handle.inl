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

#include "ValuePromise.inl"
#include "Resolver.inl"

#include <utils/Scoped.h>
#include <algorithm>
#include <cassert>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace promise {

template <class T, bool WITH_RESOLVER>
struct Handle : public ValuePromise<T, std::is_void_v<T>> {
protected:
   struct PromiseType;
   using handle_type  = std::coroutine_handle<PromiseType>;
   using Promise      = details::IPromise<T, WITH_RESOLVER>;
   using ValuePromise = ValuePromise<T, std::is_void_v<T>>;
   using Locker       = std::unique_lock<std::shared_mutex>;
   /**
    * @brief Unlock helper for lock guards.
    */
   struct Unlock {
      Locker& lock_;

      /**
       * @brief Unlock the lock on destruction.
       */
      ~Unlock() {
         if (lock_) {
            lock_.unlock();
         }
      }
   };

   /**
    * @brief Returned promise type for this handle.
    */
   static constexpr bool IS_VOID = WITH_RESOLVER || std::is_void_v<T>;

   struct VoidPromiseType {
   public:
      /**
       * @brief Return void from the coroutine.
       *
       * @param self Promise type context.
       */
      template <class SELF>
      void return_void(this SELF&& self) {
         self.ReturnImpl();
      }
   };

   struct ValuePromiseType {
   public:
      /**
       * @brief Return a value from the coroutine.
       *
       * @param self Promise type context.
       * @param value Value to return.
       */
      template <class FROM, class SELF>
         requires(std::is_convertible_v<FROM, T>)
      void return_value(this SELF&& self, FROM&& value) {
         self.ReturnImpl(std::forward<FROM>(value));
      }
   };

   struct PromiseType : std::conditional_t<IS_VOID, VoidPromiseType, ValuePromiseType> {
      using Parent = details::Promise<T, WITH_RESOLVER>;

   private:
      Parent* parent_{};

   public:
      PromiseType() = default;

      PromiseType(PromiseType const&)               = delete;
      PromiseType(PromiseType&&) noexcept           = delete;
      PromiseType operator=(PromiseType const&)     = delete;
      PromiseType operator=(PromiseType&&) noexcept = delete;

      ~PromiseType() = default;

      /**
       * @brief Get the return object for this promise.
       *
       * @return Promise details reference.
       */
      details::IPromise<T, WITH_RESOLVER> get_return_object() {
         return details::IPromise<T, WITH_RESOLVER>{handle_type::from_promise(*this)};
      }

      /**
       * @brief Get the parent promise details.
       *
       * @return Parent promise details pointer.
       */
      Parent* GetParent() const { return parent_; }

      struct InitSuspend {
         Parent& self_;

         [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }

         /**
          * @brief Suspend the coroutine at initial suspension.
          *
          * @param h Coroutine handle.
          */
         constexpr void await_suspend(std::coroutine_handle<> h) const noexcept { (void)h; }

         /**
          * @brief Complete initial suspend and attach a resolver.
          * @note Best practice: create resolver promises via MakePromise/MakeRPromise.
          */
         constexpr void await_resume() const noexcept(false) {
            if (!self_.resolver_) {
               // Promise has been created without MakePromise

               if (WITH_RESOLVER) {
                  throw std::runtime_error("Promise with resolver must be created with MakePromise"
                  );
               }

               self_.resolver_ = std::make_unique<Resolver<T, WITH_RESOLVER>>();
            }

            self_.resolver_->promise_ = &self_;
         }
      };
      using FinalSuspend = std::suspend_never;
      /**
       * @brief Initial suspend hook.
       *
       * @return Awaitable initial suspend object.
       */
      InitSuspend initial_suspend() {
         assert(this->parent_);
         return {.self_ = *this->parent_};
      }

      /**
       * @brief Final suspend hook.
       *
       * @return Final suspend awaitable.
       */
      FinalSuspend final_suspend() noexcept {
         auto const parent = this->parent_;

         std::unique_lock lock{parent->mutex_};
         assert(parent);
         parent->handle_ = nullptr;

         if (parent->IsDone(lock)) {
            parent->OnResolved(lock);
         } else {
            // Will be done by the resolver
            assert(WITH_RESOLVER);
         }

         return {};
      }

      /**
       * @brief Handle an unhandled exception in the coroutine.
       */
      void unhandled_exception() {
         auto const parent = this->parent_;

         assert(parent);
         assert(parent->resolver_);
         parent->resolver_->Reject(std::current_exception());
      }

      /**
       * @brief Forward return values to the promise implementation.
       *
       * @param value Values to return.
       */
      template <class... FROM>
         requires(
           (std::is_convertible_v<FROM, T> && ...)
           && (sizeof...(FROM) == ((IS_VOID || WITH_RESOLVER) ? 0 : 1))
         )
      void ReturnImpl(FROM&&... value) {
         this->parent_->ReturnImpl(std::forward<FROM>(value)...);
      }

      friend Promise;
      friend Handle;
   };

   /**
    * @brief Construct a promise handle from a coroutine handle.
    *
    * @param handle Coroutine handle to manage.
    */
   explicit Handle(handle_type handle)
      : handle_{std::move(handle)} {
      if (handle_) {
         handle_.promise().parent_ = static_cast<details::Promise<T, WITH_RESOLVER>*>(this);
      }
      // handle_ can be null in case of Catch through
   }

public:
   /**
    * @brief Check if resolved using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if resolved.
    */
   template <class SELF>
   bool IsResolved(this SELF&& self, Lock lock) {
      return self.ValuePromise::IsResolved(lock);
   }

   /**
    * @brief Check if done using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if resolved or rejected.
    */
   template <class SELF>
   bool IsDone(this SELF&& self, Lock lock) {
      return self.ValuePromise::IsResolved(lock) || self.resolver_->exception_;
   }

   /**
    * @brief Check if done.
    *
    * @return True if resolved or rejected.
    */
   template <class SELF>
   bool IsDone(this SELF&& self) {
      std::shared_lock lock{self.mutex_};
      return self.IsDone(lock);
   }

   /**
    * @brief Resume awaiters once resolved.
    *
    * @param lock Active lock for thread-safe access.
    */
   template <class SELF>
   void OnResolved(this SELF&& self, std::unique_lock<std::shared_mutex>& lock) {
      // If not Done, will be done on final_suspends
      if (self.Done(lock)) {
         std::vector<std::coroutine_handle<>> awaiters{};

         self.awaiters_.swap(awaiters);
         assert(!self.awaiters_.size());

         auto const save_self = std::move(self.self_owned_);
         lock.unlock();

         // We must not use self anymore !
         for (auto const& awaiter : awaiters) {
            assert(awaiter);
            awaiter.resume();
         }
      }
   }

   /**
    * @brief Detach the promise details from this handle.
    *
    * @param self Shared ownership of the promise details.
    *
    * @return Reference to promise details.
    */
   details::Promise<T, WITH_RESOLVER>& Detach(
     std::shared_ptr<details::Promise<T, WITH_RESOLVER>>&& self
   ) {
      assert(self);
      std::unique_lock lock{self->mutex_};
      assert(!self->self_owned_);

      if (!ValuePromise::IsResolved(lock)) {
         auto& result       = *self;
         result.self_owned_ = std::move(self);

         return result;
      }

      return *self;
   }

   void VDetach() && override { assert(false); }

   ~Handle() { assert(!this->handle_); }

   Handle(Handle&& rhs) noexcept = delete;
   Handle(Handle const& rhs)     = delete;

   Handle& operator=(Handle&& rhs) noexcept = delete;
   Handle& operator=(Handle const& rhs)     = delete;

   /**
    * @brief Check if the coroutine handle is cleared.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if the coroutine is complete.
    */
   bool Done(Lock lock) const {
      (void)lock;
      return handle_ == nullptr;
   }

protected:
   std::shared_mutex mutex_{};
   Locker            lock_{this->mutex_, std::defer_lock};

   handle_type                                         handle_{nullptr};
   std::shared_ptr<details::Promise<T, WITH_RESOLVER>> self_owned_{nullptr};
   std::unique_ptr<Resolver<T, WITH_RESOLVER>>         resolver_{nullptr};

public:
   friend class details::IPromise<T, WITH_RESOLVER>;
   friend ValuePromise;
   friend struct Resolver<T, WITH_RESOLVER>;
};

}  // namespace promise

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
#include <variant>

namespace promise {
/**
 * @brief Type-erased awaitable wrapper for promises.
 *
 * This class allows promises to be awaited without knowing their concrete type.
 */
struct Function {
   virtual ~Function() = default;
};

template <class T, bool WITH_RESOLVER>
class Handle : public ValuePromise<T> {
protected:
   struct PromiseType;
   using handle_type  = std::coroutine_handle<PromiseType>;
   using Promise      = details::IPromise<T, WITH_RESOLVER>;
   using ValuePromise = ValuePromise<T>;
   using Locker       = std::unique_lock<std::shared_mutex>;
   /**
    * @brief Unlock helper for lock guards.
    */
   struct Unlock {
      Locker& lock_;

      /**
       * @brief Unlock the lock on destruction.
       */
#if defined(__clang__)
      __attribute__((no_sanitize("address")))
#endif
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
         (void)self;
         assert(!self.delayed_return_);
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
         assert(!self.delayed_return_);
         self.delayed_return_ = std::make_unique<T>(std::forward<FROM>(value));
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
               throw std::runtime_error("Promise with resolver must be created with MakePromise");
            }
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
         {
            std::unique_lock lock{parent_->mutex_};
            assert(parent_);
            assert(parent_->resolver_);

            parent_->handle_   = nullptr;
            parent_->function_ = nullptr;

            if constexpr (WITH_RESOLVER) {
               if (parent_->IsDone(lock)) {
                  parent_->OnResolved(lock);
                  return {};
               }
            }
         }

         if (delayed_return_) {
            auto const delayed_return = std::move(*delayed_return_);
            delayed_return_           = std::nullopt;

            if constexpr (IS_VOID) {
               parent_->resolver_->Reject(std::move(delayed_return));
            } else {
               if (std::holds_alternative<std::exception_ptr>(delayed_return)) {
                  parent_->resolver_->Reject(std::get<std::exception_ptr>(delayed_return));
               } else {
                  assert(!WITH_RESOLVER && "Resolver promises must not return values");
                  parent_->resolver_->Resolve(std::move(*std::get<std::unique_ptr<T>>(delayed_return
                  )));
               }
            }
         } else if constexpr (IS_VOID && !WITH_RESOLVER) {
            parent_->resolver_->Resolve();
         } else {
            assert(IS_VOID && "Non-void promises must return a value or throw");
         }

         return {};
      }

      /**
       * @brief Handle an unhandled exception in the coroutine.
       */
      void unhandled_exception() {
         assert(!delayed_return_);

         delayed_return_ = std::current_exception();
      }

      friend Promise;
      friend Handle;

   private:
      using ReturnValue = std::conditional_t<
        IS_VOID,
        std::exception_ptr,
        std::variant<std::exception_ptr, std::unique_ptr<T>>>;

      std::optional<ReturnValue> delayed_return_{};
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
      assert(self.resolver_);
      return self.ValuePromise::IsResolved(lock) || (self.resolver_->exception_ != nullptr);
   }

   /**
    * @brief Check if rejected using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if rejected.
    */
   template <class SELF>
   bool IsRejected(this SELF&& self, Lock) {
      assert(self.resolver_);
      return (self.resolver_->exception_ != nullptr);
   }

   /**
    * @brief Check if rejected using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if rejected.
    */
   template <class SELF>
   bool IsRejected(this SELF&& self) {
      std::shared_lock lock{self.mutex_};
      return self.IsRejected(lock);
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
         std::vector<typename std::remove_cvref_t<SELF>::Awaiter> awaiters{};

         self.awaiters_.swap(awaiters);
         assert(!self.awaiters_.size());

         auto const save_self = std::move(self.self_owned_);
         lock.unlock();

         // We must not use self anymore !
         for (auto const& awaiter : awaiters) {
            if (std::holds_alternative<std::coroutine_handle<>>(awaiter)) {
               assert(std::get<std::coroutine_handle<>>(awaiter));
               std::get<std::coroutine_handle<>>(awaiter).resume();
            } else {
               using AwaitFunction = typename details::Promise<T, WITH_RESOLVER>::AwaitFunction;
               assert(std::holds_alternative<AwaitFunction>(awaiter));
               std::get<AwaitFunction>(awaiter)();
            }
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
   ) && {
      assert(self);
      std::unique_lock lock{self->mutex_};
      // assert(!self->self_owned_); @TODO implement a refcount to detach only when no more
      // references to the promise details exist

      if (!IsDone(lock)) {
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
   mutable std::shared_mutex mutex_{};

   handle_type                                         handle_{nullptr};
   std::shared_ptr<details::Promise<T, WITH_RESOLVER>> self_owned_{nullptr};
   std::shared_ptr<Resolver<T>>                        resolver_{nullptr};

public:
   friend class details::IPromise<T, WITH_RESOLVER>;
   friend ValuePromise;
   friend class Resolver<T>;
};

}  // namespace promise

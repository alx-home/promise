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

#include "ValuePromise.h"
#include "Resolver.h"

#include <utils/Scoped.h>
#include <cassert>
#include <condition_variable>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
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
      details::IPromise<T, WITH_RESOLVER> get_return_object();

      /**
       * @brief Get the parent promise details.
       *
       * @return Parent promise details pointer.
       */
      Parent* GetParent() const;

      struct InitSuspend {
         Parent& self_;

         [[nodiscard]] constexpr bool await_ready() const noexcept;

         /**
          * @brief Suspend the coroutine at initial suspension.
          *
          * @param h Coroutine handle.
          */
         constexpr void await_suspend(std::coroutine_handle<> h) const noexcept;

         /**
          * @brief Complete initial suspend and attach a resolver.
          * @note Best practice: create resolver promises via MakePromise/MakeRPromise.
          */
         constexpr void await_resume() const noexcept(false);
      };
      using FinalSuspend = std::suspend_never;

      /**
       * @brief Initial suspend hook.
       *
       * @return Awaitable initial suspend object.
       */
      InitSuspend initial_suspend();

      /**
       * @brief Final suspend hook.
       *
       * @return Final suspend awaitable.
       */
      FinalSuspend final_suspend() noexcept;

      /**
       * @brief Handle an unhandled exception in the coroutine.
       */
      void unhandled_exception();

      template <class FUN>
         requires(IS_PROMISE_FUNCTION<FUN>)
      details::WPromise<return_t<return_t<FUN>>> await_transform(FUN&& fun);

      template <class FUN>
         requires(!IS_PROMISE_FUNCTION<FUN>)
      FUN&& await_transform(FUN&& fun);

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
   explicit Handle(handle_type handle);

public:
   /**
    * @brief Check if resolved using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if resolved.
    */
   template <class SELF>
   [[nodiscard]] bool IsResolved(this SELF&& self, Lock lock);

   /**
    * @brief Check if done using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if resolved or rejected.
    */
   template <class SELF>
   [[nodiscard]] bool IsDone(this SELF&& self, Lock lock);

   /**
    * @brief Wait for the promise to be resolved or rejected.
    */
   void WaitDone();

   /**
    * @brief Wait for the promise to be awaited.
    *
    * @param current_count Optional current use count to wait from a specific point.
    */
   template <class SELF>
   void WaitAwaited(this SELF const& self, std::optional<std::size_t> current_count = std::nullopt);

   /**
    * @brief Check if rejected using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if rejected.
    */
   template <class SELF>
   [[nodiscard]] bool IsRejected(this SELF&& self, Lock);

   /**
    * @brief Check if rejected using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if rejected.
    */
   template <class SELF>
   [[nodiscard]] bool IsRejected(this SELF&& self);

   /**
    * @brief Check if done.
    *
    * @return True if resolved or rejected.
    */
   template <class SELF>
   [[nodiscard]] bool IsDone(this SELF&& self);

   /**
    * @brief Resume awaiters once resolved.
    *
    * @param lock Active lock for thread-safe access.
    */
   template <class SELF>
   void OnResolved(this SELF&& self, std::unique_lock<std::shared_mutex>& lock);

   /**
    * @brief Detach the promise details from this handle.
    *
    * @param self Shared ownership of the promise details.
    *
    * @return Reference to promise details.
    */
   details::Promise<T, WITH_RESOLVER>& Detach(
     std::shared_ptr<details::Promise<T, WITH_RESOLVER>>&& self
   ) &&;

   void VDetach() && override;

   ~Handle();

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
   [[nodiscard]] bool Done(Lock lock) const;

protected:
   mutable std::shared_mutex           mutex_{};
   mutable std::condition_variable_any cv_{};

   handle_type                                         handle_{nullptr};
   std::shared_ptr<details::Promise<T, WITH_RESOLVER>> self_owned_{nullptr};
   std::shared_ptr<Resolver<T>>                        resolver_{nullptr};

public:
   friend class details::IPromise<T, WITH_RESOLVER>;
   friend ValuePromise;
   friend class Resolver<T>;
};

}  // namespace promise

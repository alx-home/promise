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
#include <coroutine>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

/**
 * @brief MSVC-specific helper to extract typed exceptions from std::exception_ptr.
 * @warning This relies on MSVC internal layout and is only tested on MSVC 2019/2022.
 * @note Best practice: prefer std::exception_ptr unless you control the toolchain.
 */
template <class T>
   requires(std::is_class_v<T>)
struct ExceptionWrapper : std::exception_ptr {
   using std::exception_ptr::exception_ptr;
   using std::exception_ptr::operator=;

   explicit(false) operator T&() {
      static_assert(
        _MSC_VER == 1943 || _MSC_VER == 1929,
        "Only tested on msvc 2022/2019, with other "
        "compiler use it at your own risk!"
      );

      assert(*this);

      struct ExceptionPtr {
         std::byte* data1_;
         std::byte* data2_;
      };
      static_assert(sizeof(ExceptionPtr) == 8 * 2);

      ExceptionPtr data;
      std::ranges::copy_n(
        reinterpret_cast<std::byte*>(this),
        sizeof(ExceptionPtr),
        reinterpret_cast<std::byte*>(&data)
      );

      std::byte* addr;
      std::ranges::copy_n(
        reinterpret_cast<std::byte*>(data.data2_) + 0x38,
        sizeof(addr),
        reinterpret_cast<std::byte*>(&addr)
      );

      auto ptr = reinterpret_cast<T*>(addr);
      return *ptr;
   }
};

/**
 * @brief Build a Promise from a generic callable.
 * @param func Callable returning a Promise or value.
 * @param args Arguments forwarded to the callable.
 * @return Constructed promise.
 */
template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN>)
static constexpr auto
MakePromise(FUN&& func, ARGS&&... args) {
   return MakePromise(std::function{std::forward<FUN>(func)}, std::forward<ARGS>(args)...);
}

/**
 * @brief Reject a promise by constructing an exception.
 * @param reject Reject handle.
 * @param args Constructor args for EXCEPTION.
 * @return True if rejected, false if already rejected.
 * @warning When RELAXED is false, a double reject throws.
 */
template <class EXCEPTION, bool RELAXED, class... ARGS>
bool
MakeReject(promise::Reject const& reject, ARGS&&... args) {
   if (!reject(std::make_exception_ptr(EXCEPTION{std::forward<ARGS>(args)...}))) {
      if constexpr (!RELAXED) {
         throw promise::Exception("Promise Already rejected !");
      }
      return false;
   }

   return true;
}

/**
 * @brief Create a rejected promise by constructing an exception.
 * @param args Constructor args for EXCEPTION.
 * @return Rejected promise instance.
 */
template <class PROMISE, class EXCEPTION, class... ARGS>
auto
MakeReject(ARGS&&... args) {
   return PROMISE::Reject(std::make_exception_ptr(EXCEPTION{std::forward<ARGS>(args)...}));
}

namespace promise {
struct Terminate : std::runtime_error {
   using std::runtime_error::runtime_error;
};

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

using Lock = std::variant<
  std::reference_wrapper<std::shared_lock<std::shared_mutex>>,
  std::reference_wrapper<std::unique_lock<std::shared_mutex>>,
  std::reference_wrapper<std::lock_guard<std::shared_mutex>>>;

template <class T, bool VOID_TYPE>
struct ValuePromise : VPromise {
protected:
   static constexpr bool IS_VOID = VOID_TYPE;

public:
   /**
    * @brief Get the resolved value using an existing lock.
    * @param lock Active lock for thread-safe access.
    * @return Resolved value reference.
    */
   template <class SELF>
   auto const& GetValue(this SELF&& self, Lock) {
      assert(self.resolver_);
      assert(self.resolver_->value_);
      return *self.resolver_->value_;
   }

   /**
    * @brief Get the resolved value.
    * @return Resolved value reference.
    */
   template <class SELF>
   auto const& GetValue(this SELF&& self) {
      std::shared_lock lock{self.mutex_};
      return self.GetValue(lock);
   }

   /**
    * @brief Check if a value is resolved using an existing lock.
    * @param lock Active lock for thread-safe access.
    * @return True if resolved.
    */
   template <class SELF>
   bool IsResolved(this SELF&& self, Lock) {
      assert(self.resolver_);
      return self.resolver_->value_ != nullptr;
   }
};

template <class T>
struct ValuePromise<T, true> : VPromise {
protected:
   static constexpr bool IS_VOID = true;

public:
   /**
    * @brief Check if the promise is resolved using an existing lock.
    * @param lock Active lock for thread-safe access.
    * @return True if resolved.
    */
   template <class SELF>
   bool IsResolved(this SELF&& self, Lock) {
      assert(self.resolver_);
      return *self.resolver_->resolved_;
   }
};

#ifdef PROMISE_MEMCHECK
struct Refcount {
   static std::atomic<std::size_t> counter;
#   ifdef PROMISE_MEMCHECK_FULL
   static std::unordered_set<VPromise const*> ptrs;
   static std::mutex                          mutex;
   VPromise*                                  ptr_;
#   endif

   explicit Refcount(VPromise* ptr) {
      ++counter;
#   ifdef PROMISE_MEMCHECK_FULL
      std::lock_guard lock{mutex};
      ptrs.emplace(ptr);
      ptr_ = ptr;
#   endif
   }

   Refcount(Refcount&&) noexcept                 = delete;
   Refcount(Refcount const&) noexcept            = delete;
   Refcount& operator=(Refcount&&) noexcept      = delete;
   Refcount& operator=(Refcount const&) noexcept = delete;

   ~Refcount() {
      --counter;
#   ifdef PROMISE_MEMCHECK_FULL
      std::lock_guard lock{mutex};
      ptrs.erase(ptr_);
#   endif
   }
};
#endif  // PROMISE_MEMCHECK

template <class T, bool WITH_RESOLVER>
struct Handle : public ValuePromise<T, std::is_void_v<T>> {
protected:
   friend class details::WPromise<T, WITH_RESOLVER>;

   struct PromiseType;
   using handle_type  = std::coroutine_handle<PromiseType>;
   using Promise      = details::WPromise<T, WITH_RESOLVER>;
   using ValuePromise = ValuePromise<T, std::is_void_v<T>>;
   using Locker       = std::unique_lock<std::shared_mutex>;
   struct Unlock {
      Locker& lock_;
      ~Unlock() {
         if (lock_) {
            lock_.unlock();
         }
      }
   };

   static constexpr bool IS_VOID = WITH_RESOLVER || std::is_void_v<T>;

   struct VoidPromiseType {
   public:
      /**
       * @brief Return void from the coroutine.
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

      details::WPromise<T, WITH_RESOLVER> get_return_object() {
         return details::WPromise<T, WITH_RESOLVER>{handle_type::from_promise(*this)};
      }

      /**
       * @brief Get the parent promise details.
       * @return Parent promise details pointer.
       */
      Parent* GetParent() const { return parent_; }

      struct InitSuspend {
         Parent& self_;

         [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }

         /**
          * @brief Suspend the coroutine at initial suspension.
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
       * @return Awaitable initial suspend object.
       */
      InitSuspend initial_suspend() {
         assert(this->parent_);
         return {.self_ = *this->parent_};
      }

      /**
       * @brief Final suspend hook.
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
      template <bool>
      friend struct AwaitTransform;
      friend Handle;
   };

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
    * @param lock Active lock for thread-safe access.
    * @return True if resolved.
    */
   template <class SELF>
   bool IsResolved(this SELF&& self, Lock lock) {
      return self.ValuePromise::IsResolved(lock);
   }

   /**
    * @brief Check if done using an existing lock.
    * @param lock Active lock for thread-safe access.
    * @return True if resolved or rejected.
    */
   template <class SELF>
   bool IsDone(this SELF&& self, Lock lock) {
      return self.ValuePromise::IsResolved(lock) || self.resolver_->exception_;
   }

   /**
    * @brief Check if done.
    * @return True if resolved or rejected.
    */
   template <class SELF>
   bool IsDone(this SELF&& self) {
      std::shared_lock lock{self.mutex_};
      return self.IsDone(lock);
   }

   /**
    * @brief Resume awaiters once resolved.
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
    * @param self Shared ownership of the promise details.
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
    * @param lock Active lock for thread-safe access.
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
   friend ValuePromise;
   friend struct Resolver<T, WITH_RESOLVER>;
};

struct Function {
   virtual ~Function() = default;
};

namespace details {
template <class T, bool WITH_RESOLVER>
class Promise
   : public Handle<T, WITH_RESOLVER>
#ifdef PROMISE_MEMCHECK
   , public Refcount
#endif  // PROMISE_MEMCHECK
{
protected:
   friend class details::WPromise<T, WITH_RESOLVER>;

   using ValuePromise = Handle<T, WITH_RESOLVER>::ValuePromise;
   using handle_type  = Handle<T, WITH_RESOLVER>::handle_type;
   using promise_type = Handle<T, WITH_RESOLVER>::PromiseType;
   using Locker       = typename Handle<T, WITH_RESOLVER>::Locker;
   using Unlock       = typename Handle<T, WITH_RESOLVER>::Unlock;

   static constexpr bool IS_VOID = std::is_void_v<T>;

private:
   using Handle<T, WITH_RESOLVER>::Handle;

   explicit Promise(handle_type handle)
      : Handle<T, WITH_RESOLVER>(std::move(handle))
   //
#ifdef PROMISE_MEMCHECK
      , Refcount(this)
#endif
   {
   }

public:
   ~Promise() {
      if (this->handle_) {
         assert(this->self_owned_);
      }

      assert(WITH_RESOLVER || this->IsDone(this->lock_));
   };

   Promise(Promise&& rhs) noexcept            = delete;
   Promise& operator=(Promise&& rhs) noexcept = delete;
   Promise(Promise const&)                    = delete;
   Promise operator=(Promise const&)          = delete;

   /**
    * @brief Check if the promise can resume immediately.
    * @return True if ready to resume.
    */
   bool await_ready() {
      this->lock_.lock();
      return Ready(this->lock_);
   }

   /**
    * @brief Suspend the coroutine and register continuation.
    * @param h Awaiting coroutine handle.
    */
   void await_suspend(std::coroutine_handle<> h) {
      Unlock _{this->lock_};

      if constexpr (!WITH_RESOLVER) {
         assert(this->handle_);
      }

      Await(h, this->lock_);
   }

   /**
    * @brief Resume the await and return the resolved value or throw.
    * @return Resolved value for non-void promises.
    */
   auto await_resume() noexcept(false) {
      if (!this->lock_) {
         // await_suspend has released the lock
         this->lock_.lock();
      }

      Unlock _{this->lock_};

      if (auto const& exception = this->GetException(this->lock_); exception) {
         std::rethrow_exception(exception);
      }

      if constexpr (IS_VOID) {
         assert(this->IsResolved(this->lock_));
      } else {
         return this->GetValue(this->lock_);
      }
   }

private:
   template <class...>
      requires(!WITH_RESOLVER)
   /**
    * @brief Start a resolver-less promise.
    * @return Coroutine handle result.
    */
   auto operator()() {
      return this->handle_();
   }

   template <class...>
      requires(WITH_RESOLVER)
   /**
    * @brief Start a resolver-style promise with a resolver.
    * @param resolver Resolver to drive the promise.
    * @return Coroutine handle result.
    */
   auto operator()(std::unique_ptr<Resolver<T, WITH_RESOLVER>>&& resolver) {
      assert(!this->resolver_);
      this->resolver_ = std::move(resolver);
      return this->handle_();
   }

   /**
    * @brief Get a type-erased awaitable wrapper.
    * @return Reference to a heap-allocated awaitable wrapper.
    * @warning The wrapper is deleted in await_resume.
    */
   VPromise::Awaitable& VAwait() final {
      struct Awaitable
         : VPromise::Awaitable
#ifdef PROMISE_MEMCHECK
         , Refcount
#endif
      {
         /**
          * @brief Construct a type-erased awaitable.
          * @param self Promise details to await.
          */
         Awaitable(details::Promise<T, WITH_RESOLVER>& self)
            :  //
#ifdef PROMISE_MEMCHECK
            Refcount(static_cast<VPromise*>(&self))
            ,
#endif
            self_(self) {
         }

         /**
          * @brief Check if the await can complete synchronously.
          * @return True if ready to resume.
          */
         bool await_ready() final { return self_.await_ready(); }

         /**
          * @brief Resume the await and destroy the wrapper.
          */
         void await_resume() final {
            // Delete this After resume
            std::unique_ptr<Awaitable> self_ptr(this);
            self_.await_resume();
         }

         /**
          * @brief Suspend the coroutine and register continuation.
          * @param h Awaiting coroutine handle.
          */
         void await_suspend(std::coroutine_handle<> h) final { self_.await_suspend(h); }

      private:
         details::Promise<T, WITH_RESOLVER>& self_;
      };

      return *new Awaitable{*this};
   }

   /**
    * @brief Get the stored exception using an existing lock.
    * @param lock Active lock for thread-safe access.
    * @return Stored exception pointer.
    */
   auto const& GetException(Lock lock) {
      (void)lock;
      assert(this->resolver_);
      return this->resolver_->exception_;
   }

   /**
    * @brief Register an awaiting coroutine.
    * @param h Awaiting coroutine handle.
    * @param lock Active lock for thread-safe access.
    * @return True if registered.
    */
   bool Await(std::coroutine_handle<> h, Lock lock) {
      (void)lock;
      awaiters_.emplace_back(h);
      return true;
   }

   /**
    * @brief Check whether the promise is ready.
    * @param lock Active lock for thread-safe access.
    * @return True if ready to resume.
    */
   bool Ready(Lock lock) {
      if (this->GetException(lock)) {
         return true;
      }

      return this->IsResolved(lock);
   }

   /**
    * @brief Forward return values to the resolver.
    * @param value Values to resolve with.
    */
   template <class... FROM>
      requires(
        (std::is_convertible_v<FROM, T> && ...)
        && (sizeof...(FROM) == ((IS_VOID || WITH_RESOLVER) ? 0 : 1))
      )
   void ReturnImpl(FROM&&... value) {
      assert(this->resolver_);

      if constexpr (!WITH_RESOLVER) {
         if constexpr (IS_VOID) {
            this->resolver_->Resolve();
         } else {
            (this->resolver_->Resolve(value), ...);
         }
      }
   }

   /**
    * @brief Chain a continuation to run on resolve.
    * @param func Continuation to invoke on resolve.
    * @param args Arguments forwarded to the continuation.
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto Then(FUN&& func, ARGS&&... args) & {

      if constexpr (!promise::WITH_RESOLVER<FUN>) {
         // Optimisation skip coroutine frame creation

         if (std::shared_lock lock{this->mutex_}; this->IsResolved(lock)) {
            if constexpr (!IS_PROMISE<FUN>) {
               // Function return type
               using T2 = return_t<FUN>;

               try {
                  if constexpr (IS_VOID) {
                     if constexpr (std::is_void_v<T2>) {
                        func(std::forward<ARGS>(args)...);
                        return details::WPromise<T2, true>::Resolve();
                     } else {
                        return details::WPromise<T2, true>::Resolve(func(std::forward<ARGS>(args)...
                        ));
                     }
                  } else {
                     if constexpr (std::is_void_v<T2>) {
                        func(this->GetValue(lock), std::forward<ARGS>(args)...);
                        return details::WPromise<T2, true>::Resolve();
                     } else {
                        return details::WPromise<T2, true>::Resolve(
                          func(this->GetValue(lock), std::forward<ARGS>(args)...)
                        );
                     }
                  }
               } catch (...) {
                  return details::WPromise<T2, true>::Reject(std::current_exception());
               }
            } else if constexpr (IS_VOID) {
               return ::MakePromise(std::move(func), std::forward<ARGS>(args)...);
            } else {
               return ::MakePromise(
                 std::move(func), this->GetValue(lock), std::forward<ARGS>(args)...
               );
            }
         }
      }  // else @todo

      if constexpr (IS_PROMISE<FUN>) {
         // Promise return type
         using T2 = return_t<return_t<FUN>>;

         return ::MakePromise(
           [func = std::forward<FUN>(func)](
             details::Promise<T, WITH_RESOLVER>& self, ARGS&&... args
           ) -> details::WPromise<T2, false> {
              if constexpr (IS_VOID) {
                 co_await self;
                 co_return co_await ::MakePromise(std::move(func), std::forward<ARGS>(args)...);
              } else {
                 co_return co_await ::MakePromise(
                   std::move(func), co_await self, std::forward<ARGS>(args)...
                 );
              }
           },
           *this,
           std::forward<ARGS>(args)...
         );
      } else {
         // Function return type
         using T2                        = return_t<FUN>;
         auto [promise, resolve, reject] = promise::Pure<T2>();

         ::MakePromise(
           [func = std::forward<FUN>(func), resolve, reject](
             details::WPromise<T, WITH_RESOLVER>& self, ARGS&&... args
           ) -> details::WPromise<std::conditional_t<IS_VOID, void, void>, false> {
              // std::conditional_t<IS_VOID, void, void> is used to delay the return type deduction
              // to avoid Promise<void> undefined type compilation error
              try {
                 if constexpr (IS_VOID) {
                    co_await self;
                    if constexpr (std::is_void_v<T2>) {
                       func(std::forward<ARGS>(args)...);
                       (*resolve)();
                    } else {
                       (*resolve)(func(std::forward<ARGS>(args)...));
                    }
                 } else {
                    if constexpr (std::is_void_v<T2>) {
                       func(co_await self, std::forward<ARGS>(args)...);
                       (*resolve)();
                    } else {
                       (*resolve)(func(co_await self, std::forward<ARGS>(args)...));
                    }
                 }
              } catch (...) {
                 (*reject)(std::current_exception());
              }

              co_return;
           },
           *this,
           std::forward<ARGS>(args)...
         )
           .Detach();

         return std::move(promise);
      }
   }

   /**
    * @brief Chain a continuation on an rvalue promise handle.
    * @param self Owning shared pointer to promise details.
    * @param func Continuation to invoke on resolve.
    * @param args Arguments forwarded to the continuation.
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto
   Then(std::shared_ptr<Promise>&& self, FUN&& func, ARGS&&... args) && {
      assert(self);
      ScopeExit _{[&]() { this->Detach(std::move(self)); }};
      return self->Then(std::forward<FUN>(func), std::forward<ARGS>(args)...);
   }

   /**
    * @brief Chain a continuation to run on rejection.
    * @param func Continuation to invoke on rejection.
    * @param args Arguments forwarded to the continuation.
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto Catch(FUN&& func, ARGS&&... args) & {
      // Promise return type
      using promise_t = return_t<decltype(std::function{func})>;
      using T2        = std::remove_pointer_t<decltype([]() constexpr {
         if constexpr (IS_PROMISE<FUN>) {
            return static_cast<return_t<promise_t>*>(nullptr);
         } else {
            return static_cast<promise_t*>(nullptr);
         }
      }())>;
      using FArgs     = args_t<decltype(std::function{func})>;
      static_assert(std::tuple_size_v<FArgs> == 1, "Catch promise must have exactly one argument!");
      using Exception = std::tuple_element_t<0, FArgs>;

      static constexpr bool IS_EXC_PTR =
        std::is_same_v<std::remove_cvref_t<Exception>, std::exception_ptr>;
      static constexpr bool IS_VALID_V =
        IS_EXC_PTR
        || (std::is_lvalue_reference_v<Exception> && std::is_const_v<std::remove_reference_t<Exception>>);
      static_assert(
        IS_VALID_V,
        "Catch promise argument must be : std::exception_ptr or a "
        "const reference to an exception "
        "!"
      );

      using return_t = std::remove_pointer_t<decltype([]() constexpr {
         if constexpr (std::is_void_v<T2> && std::is_void_v<T>) {
            return static_cast<details::WPromise<void, false>*>(nullptr);
         } else if constexpr (std::is_void_v<T2>) {
            return static_cast<details::WPromise<std::optional<T>, false>*>(nullptr);
         } else if constexpr (std::is_void_v<T>) {
            return static_cast<details::WPromise<std::optional<T2>, false>*>(nullptr);
         } else if constexpr (std::is_same_v<T, T2>) {
            return static_cast<details::WPromise<T2, false>*>(nullptr);
         } else {
            return static_cast<details::WPromise<std::variant<T2, T>, false>*>(nullptr);
         }
      }())>;

      if (this->IsDone()) {
         // Optimisation skip coroutine frame creation

         if (std::shared_lock lock{this->mutex_}; !this->GetException(lock)) {
            if constexpr (std::is_void_v<T>) {
               if constexpr (std::is_void_v<T2>) {
                  return return_t::Resolve();
               } else {
                  return return_t::Resolve(std::optional<T2>{});
               }
            } else {
               return return_t::Resolve(this->GetValue(lock));
            }
         }  // @todo else
      }

      return MakePromise(
        [func =
           std::forward<FUN>(func)](Promise<T, WITH_RESOLVER>& self, ARGS&&... args) -> return_t {
           using exception_t = std::remove_cvref_t<Exception>;
           ExceptionWrapper<exception_t> exc{};

           try {
              if constexpr (std::is_void_v<T>) {
                 co_await self;
                 if constexpr (std::is_void_v<T2>) {
                    co_return;
                 } else {
                    co_return std::optional<T2>{};
                 }
              } else {
                 co_return co_await self;
              }
           } catch (std::remove_cvref_t<Exception> const& e) {
              exc = std::current_exception();
           } catch (...) {
              if constexpr (IS_EXC_PTR) {
                 exc = std::current_exception();
              } else {
                 throw;
              }
           }

           assert(exc);

           if constexpr (std::is_void_v<T2>) {
              if constexpr (IS_PROMISE<FUN>) {
                 co_await MakePromise(std::move(func), exc, std::forward<ARGS>(args)...);
              } else {
                 func(exc, std::forward<ARGS>(args)...);
              }

              if constexpr (std::is_void_v<T>) {
                 co_return;
              } else {
                 co_return std::optional<T>{};
              }
           } else {
              if constexpr (IS_PROMISE<FUN>) {
                 co_return co_await MakePromise(std::move(func), exc, std::forward<ARGS>(args)...);
              } else {
                 co_return func(exc, std::forward<ARGS>(args)...);
              }
           }
        },
        *this,
        std::forward<ARGS>(args)...
      );
   }

   /**
    * @brief Chain a rejection continuation on an rvalue promise handle.
    * @param self Owning shared pointer to promise details.
    * @param func Continuation to invoke on rejection.
    * @param args Arguments forwarded to the continuation.
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto
   Catch(std::shared_ptr<Promise>&& self, FUN&& func, ARGS&&... args) && {
      assert(self);
      ScopeExit _{[&]() { this->Detach(std::move(self)); }};
      return self->Catch(std::forward<FUN>(func), std::forward<ARGS>(args)...);
   }

   /**
    * @brief Chain a continuation that runs regardless of outcome.
    * @param func Continuation to invoke after resolve or reject.
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto Finally(FUN&& func) & {
      if constexpr (!promise::WITH_RESOLVER<FUN>) {
         // Optimisation skip coroutine frame creation

         if (std::shared_lock lock{this->mutex_}; this->IsResolved(lock)) {
            if constexpr (!IS_PROMISE<FUN>) {
               try {
                  func();

                  if constexpr (IS_VOID) {
                     return Promise<T, true>::Resolve();
                  } else {
                     return Promise<T, true>::Resolve(this->GetValue(lock));
                  }
               } catch (...) {
                  return Promise<T, true>::Reject(std::current_exception());
               }
            } else if constexpr (IS_VOID) {
               using promise_t = return_t<decltype(std::function{func})>;
               using T2        = return_t<promise_t>;
               if constexpr (std::is_void_v<T2>) {
                  return ::MakePromise(std::move(func)).Then([]() constexpr {});
               } else {
                  return ::MakePromise(std::move(func)).Then([](T2 const&) constexpr {});
               }
            } else {
               using promise_t = return_t<decltype(std::function{func})>;
               using T2        = return_t<promise_t>;
               if constexpr (std::is_void_v<T2>) {
                  return ::MakePromise(std::move(func))
                    .Then([value = this->GetValue(lock)]() constexpr { return value; });
               } else {
                  return ::MakePromise(std::move(func))
                    .Then([value = this->GetValue(lock)](T2 const&) constexpr { return value; });
               }
            }
         }
      }  // else @todo

      if constexpr (IS_PROMISE<FUN>) {
         return ::MakePromise(
           [func = std::forward<FUN>(func)](
             promise::Resolve<T> const&           resolve,
             promise::Reject const&               reject,
             details::WPromise<T, WITH_RESOLVER>& self
           ) -> details::WPromise<T, true> {
              if constexpr (IS_VOID) {
                 std::exception_ptr exception;
                 try {
                    co_await self;
                 } catch (...) {
                    exception = std::current_exception();
                 }
                 co_await ::MakePromise(std::move(func));

                 if (exception) {
                    reject(exception);
                 } else {
                    resolve();
                 }
                 co_return;
              } else {
                 std::exception_ptr exception;
                 bool               prom_exception = true;

                 try {
                    auto result    = co_await self;
                    prom_exception = false;
                    co_await ::MakePromise(std::move(func));
                    resolve(result);
                    co_return;
                 } catch (...) {
                    exception = std::current_exception();
                 }

                 assert(exception);
                 if (prom_exception) {
                    // If the exception come from the promise, we want to call func before
                    // rethrowing it
                    co_await ::MakePromise(std::move(func));
                 }

                 reject(exception);
                 co_return;
              }
           },
           *this
         );
      } else {
         return ::MakePromise(
           [func = std::forward<FUN>(func)](
             promise::Resolve<T> const&           resolve,
             promise::Reject const&               reject,
             details::WPromise<T, WITH_RESOLVER>& self
           ) -> details::WPromise<T, true> {
              std::exception_ptr exception;

              if constexpr (IS_VOID) {
                 try {
                    co_await self;
                 } catch (...) {
                    exception = std::current_exception();
                 }

                 func();

                 if (exception) {
                    reject(exception);
                 } else {
                    resolve();
                 }
                 co_return;
              } else {
                 bool prom_exception = true;
                 try {
                    auto result    = co_await self;
                    prom_exception = false;
                    func();

                    resolve(result);
                    co_return;
                 } catch (...) {
                    exception = std::current_exception();
                 }

                 if (prom_exception) {
                    // If the exception come from the promise, we want to call func before
                    // rethrowing it
                    func();
                 }

                 reject(exception);
              }
           },
           *this
         );
      }
   }

   /**
    * @brief Chain a finally continuation on an rvalue promise handle.
    * @param self Owning shared pointer to promise details.
    * @param func Continuation to invoke after resolve or reject.
    * @return Chained promise.
    */
   template <class FUN>
   [[nodiscard]] constexpr auto Finally(std::shared_ptr<Promise>&& self, FUN&& func) && {
      assert(self);
      ScopeExit _{[&]() { this->Detach(std::move(self)); }};
      return self->Finally(std::forward<FUN>(func));
   }

public:
   /**
    * @brief Create a resolved promise without starting a coroutine.
    * @param args Constructor args for the resolved value (if any).
    * @return Resolved promise.
    */
   template <class... ARGS>
   static constexpr auto Resolve(ARGS&&... args) {
      details::WPromise<T, WITH_RESOLVER> promise{handle_type{}};
      auto                                resolver = std::make_unique<Resolver<T, WITH_RESOLVER>>();

      auto const details = promise.details_.get();
      resolver->promise_ = details;
      details->resolver_ = std::move(resolver);

      details->resolver_->Resolve(std::forward<ARGS>(args)...);

      return promise;
   }

   /**
    * @brief Create a rejected promise without starting a coroutine.
    * @param args Constructor args for the rejection value (if any).
    * @return Rejected promise.
    */
   template <class... ARGS>
   static constexpr auto Reject(ARGS&&... args) {
      details::WPromise<T, WITH_RESOLVER> promise{handle_type{}};
      auto                                resolver = std::make_unique<Resolver<T, WITH_RESOLVER>>();

      auto const details = promise.details_.get();
      resolver->promise_ = details;
      details->resolver_ = std::move(resolver);

      details->resolver_->Reject(std::forward<ARGS>(args)...);

      return promise;
   }

   /**
    * @brief Create a promise from a callable and optional resolver.
    * @param func Callable used to produce the promise.
    * @param args Arguments forwarded to the callable.
    * @return Promise or tuple when RPROMISE is true.
    */
   template <bool RPROMISE, class FUN, class... ARGS>
   static constexpr auto Create(FUN&& func, ARGS&&... args) {

      struct FunctionImpl : Function {
         /**
          * @brief Construct a function holder from an rvalue callable.
          * @param value Callable to store.
          */
         explicit FunctionImpl(std::remove_cvref_t<FUN>&& value)
            : func_(std::move(value)) {}

         /**
          * @brief Construct a function holder from an lvalue callable.
          * @param value Callable to store.
          */
         explicit FunctionImpl(std::remove_cvref_t<FUN> const& value)
            : func_(value) {}

         std::remove_cvref_t<FUN> func_;
      };
      auto holder   = std::make_unique<FunctionImpl>(std::forward<FUN>(func));
      auto resolver = std::make_unique<Resolver<T, WITH_RESOLVER>>();

      auto& resolve = *resolver->resolve_;
      auto& reject  = *resolver->reject_;

      auto promise = [&]() constexpr {
         if constexpr (WITH_RESOLVER) {
            return holder->func_(resolve, reject, std::forward<ARGS>(args)...);
         } else {
            return holder->func_(std::forward<ARGS>(args)...);
         }
      }();

      auto const details = promise.details_.get();
      details->resolver_ = std::move(resolver);
      details->function_ = std::move(holder);

      promise.details_->handle_();

      if constexpr (RPROMISE) {
         return std::make_tuple(promise, &resolve, &reject);
      } else {
         return promise;
      }
   }

private:
   std::unique_ptr<Function>            function_{nullptr};
   std::vector<std::coroutine_handle<>> awaiters_{};

   friend Handle<T, WITH_RESOLVER>;
   template <bool>
   friend struct AwaitTransform;
};
}  // namespace details

/**
 * @brief Await all promises and return a combined result.
 * @param promise Promises to await.
 * @return Tuple of resolved values (std::nullopt_t for void).
 */
template <class... PROMISE>
static constexpr auto
All(PROMISE&&... promise) {
   return MakePromise(
     [promise...]() mutable -> details::WPromise<std::tuple<std::conditional_t<
                              std::is_void_v<return_t<PROMISE>>,
                              std::nullopt_t,
                              return_t<PROMISE>>...>> {
        co_return std::make_tuple(([]<class... RESULT>(RESULT... result) constexpr {
           if constexpr (sizeof...(RESULT)) {
              return result...[0];
           } else {
              return std::nullopt;
           }
        }(co_await promise))...);
     }
   );
}

#ifdef PROMISE_MEMCHECK
constexpr auto
Memcheck() {
   struct Check {
      Check() = default;

      Check(Check&&) noexcept            = delete;
      Check(Check const&)                = delete;
      Check& operator=(Check&&) noexcept = delete;
      Check& operator=(Check const&)     = delete;

      ~Check() {
         auto const refcount = Refcount::counter.load();

         if (refcount) {
            std::cerr << "Promise: Leak memory detected (" << refcount << " unterminated promises)"
                      << std::endl;
#   ifdef PROMISE_MEMCHECK_FULL
            auto const& ptrs = Refcount::ptrs;
            for (auto const& ptr : ptrs) {
               std::cout << "At addr: " << ptr << std::endl;
            }
#   endif
            assert(false);
         }
      }
   };

   return Check{};
}
#endif  // PROMISE_MEMCHECK

}  // namespace promise

/**
 * @brief Build a Promise from a std::function.
 * @param func Callable returning a Promise or value.
 * @param args Arguments forwarded to the callable.
 * @return Constructed promise.
 */
template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN>)
static constexpr auto
MakePromise(FUN&& func, ARGS&&... args) {
   using namespace promise;

   return details::Promise<return_t<return_t<FUN>>, WITH_RESOLVER<FUN>>::template Create<false>(
     std::forward<FUN>(func), std::forward<ARGS>(args)...
   );
}

/**
 * @brief Build a resolver-style Promise from a std::function.
 * @param func Callable returning a resolver-style Promise.
 * @param args Arguments forwarded to the callable.
 * @return Constructed resolver-style promise or tuple.
 */
template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
static constexpr auto
MakeRPromise(FUN&& func, ARGS&&... args) {
   using namespace promise;

   return details::Promise<return_t<return_t<FUN>>, WITH_RESOLVER<FUN>>::template Create<true>(
     std::forward<FUN>(func), std::forward<ARGS>(args)...
   );
}

/**
 * @brief Build a resolver-style Promise from a generic callable.
 * @param func Callable returning a resolver-style Promise.
 * @param args Arguments forwarded to the callable.
 * @return Constructed resolver-style promise or tuple.
 */
template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
static constexpr auto
MakeRPromise(FUN&& func, ARGS&&... args) {
   return MakeRPromise(std::function{std::forward<FUN>(func)}, std::forward<ARGS>(args)...);
}

/**
 * @brief Build a resolver and its resolve/reject handles.
 * @return Tuple (resolver, resolve, reject).
 */
template <class T>
static constexpr auto
MakeResolver() {
   auto resolver = std::make_unique<promise::Resolver<T, true>>();
   return std::make_tuple(std::move(resolver), resolver->resolve_, resolver->reject_);
}

namespace promise {

/**
 * @brief Build a promise and its resolve/reject handles.
 * @return Tuple (promise, resolve, reject).
 */
template <class T>
static constexpr auto
Pure() {
   auto [resolver, resolve, reject] = MakeResolver<T>();
   auto promise = ([](Resolve<T> const&, Reject const&) -> details::WPromise<T, true> {
      co_return;
   }(*resolve, *reject));

   return std::make_tuple(std::move(promise(std::move(resolver))), resolve, reject);
}

}  // namespace promise

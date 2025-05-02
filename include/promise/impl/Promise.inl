/*
MIT License

Copyright (c) 2025 alx-home

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

#include "../promise.h"

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
#include <type_traits>
#include <utility>
#include <variant>

// Add a cast operator to an exception pointer (Reverse engineering of MSVC22 must not works if
// compiled with other compiler)
template <class T>
   requires(std::is_class_v<T>)
struct ExceptionWrapper : std::exception_ptr {
   using std::exception_ptr::exception_ptr;
   using std::exception_ptr::operator=;

   explicit(false) operator T&() {
      static_assert(
         _MSC_VER == 1943, "Only tested on msvc 2022, with other compiler use it at your own risk!"
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

template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN>)
static constexpr auto
MakePromise(FUN&& func, ARGS&&... args) {
   return MakePromise(std::function{std::forward<FUN>(func)}, std::forward<ARGS>(args)...);
}

template <class EXCEPTION, class FUN, class... ARGS>
void
MakeReject(FUN const& reject, ARGS&&... args) {
   reject(std::make_exception_ptr(EXCEPTION{std::forward<ARGS>(args)...}));
}

namespace promise {
struct Terminate : std::runtime_error {
   using std::runtime_error::runtime_error;
};

template <class T, bool WITH_RESOLVER> struct Resolver {
   details::Promise<T, WITH_RESOLVER>* promise_{nullptr};
   std::exception_ptr                  exception_{};
   std::optional<T>                    value_{};
   Resolve<T>                          resolve_{[this](T const& value) { this->Resolve(value); }};
   Reject reject_{[this](std::exception_ptr exc) { this->Reject(exc); }};

   bool await_ready() const { return value_.has_value(); }

   void await_resume() const {}

   template <class TT>
      requires(std::is_convertible_v<TT, T>)
   void Resolve(TT&& value) {
      std::unique_lock lock{promise_->mutex_};

      assert(!value_.has_value());
      assert(!exception_);
      value_ = std::forward<TT>(value);

      assert(promise_);
      promise_->OnResolved(lock);
   }

   void Reject(std::exception_ptr exception) {
      std::unique_lock lock{promise_->mutex_};

      assert(!value_.has_value());
      assert(!exception_);
      exception_ = exception;

      assert(promise_);
      promise_->OnResolved(lock);
   }
};

template <bool WITH_RESOLVER> struct Resolver<void, WITH_RESOLVER> {
   details::Promise<void, WITH_RESOLVER>* promise_{nullptr};

   bool               resolved_{false};
   std::exception_ptr exception_{};
   Resolve<void>      resolve_{[this]() { this->Resolve(); }};
   Reject             reject_{[this](std::exception_ptr exc) { this->Reject(exc); }};

   bool await_ready() const { return resolved_; }

   void await_resume() const {}

   void Resolve() {
      std::unique_lock lock{promise_->mutex_};

      assert(!resolved_);
      assert(!exception_);
      resolved_ = true;

      assert(promise_);
      promise_->OnResolved(lock);
   }

   void Reject(std::exception_ptr exception) {
      std::unique_lock lock{promise_->mutex_};

      assert(!resolved_);
      assert(!exception_);
      exception_ = exception;

      assert(promise_);
      promise_->OnResolved(lock);
   }
};

using Lock = std::variant<
   std::reference_wrapper<std::shared_lock<std::shared_mutex>>,
   std::reference_wrapper<std::unique_lock<std::shared_mutex>>,
   std::reference_wrapper<std::lock_guard<std::shared_mutex>>>;

template <class T, bool VOID_TYPE> struct ValuePromise : VPromise {
protected:
   static constexpr bool IS_VOID = VOID_TYPE;

public:
   template <class SELF> auto const& GetValue(this SELF&& self, Lock) {
      assert(self.resolver_);
      assert(self.resolver_->value_.has_value());
      return self.resolver_->value_.value();
   }

   template <class SELF> auto const& GetValue(this SELF&& self) {
      std::shared_lock lock{*self.mutex_};
      return self.GetValue(lock);
   }

   template <class SELF> bool IsResolved(this SELF&& self, Lock) {
      assert(self.resolver_);
      return self.resolver_->value_.has_value();
   }
};

template <class T> struct ValuePromise<T, true> : VPromise {
protected:
   static constexpr bool IS_VOID = true;

public:
   template <class SELF> bool IsResolved(this SELF&& self, Lock) {
      assert(self.resolver_);
      return self.resolver_->resolved_;
   }
};

#ifdef PROMISE_MEMCHECK
struct Refcount {
   static std::atomic<std::size_t> counter;

   constexpr Refcount() { ++counter; }

   Refcount(Refcount&&) noexcept                 = delete;  //{ ++counter; };
   Refcount(Refcount const&) noexcept            = delete;
   Refcount& operator=(Refcount&&) noexcept      = delete;
   Refcount& operator=(Refcount const&) noexcept = delete;

   ~Refcount() { --counter; }
};
#endif  // PROMISE_MEMCHECK

template <class T, bool WITH_RESOLVER> struct Handle : public ValuePromise<T, std::is_void_v<T>> {
protected:
   friend class ::Promise<T, WITH_RESOLVER>;

   struct PromiseType;
   using handle_type  = std::coroutine_handle<PromiseType>;
   using Promise      = ::Promise<T, WITH_RESOLVER>;
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
      template <class SELF> void return_void(this SELF&& self) { self.ReturnImpl(); }
   };

   struct ValuePromiseType {
   public:
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

      ::Promise<T, WITH_RESOLVER> get_return_object() {
         return ::Promise<T, WITH_RESOLVER>{handle_type::from_promise(*this)};
      }

      Parent* GetParent() const { return parent_; }

      using init_suspend_t  = std::suspend_always;
      using final_suspend_t = std::suspend_never;
      init_suspend_t  initial_suspend() { return {}; }
      final_suspend_t final_suspend() noexcept {
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

      void unhandled_exception() {
         auto const parent = this->parent_;

         std::lock_guard lock{parent->mutex_};
         assert(parent);
         assert(parent->resolver_);
         parent->resolver_->exception_ = std::current_exception();
      }

      template <class... FROM>
         requires(
            (std::is_convertible_v<FROM, T> && ...)
            && (sizeof...(FROM) == ((IS_VOID || WITH_RESOLVER) ? 0 : 1))
         )
      void ReturnImpl(FROM&&... value) {
         this->parent_->ReturnImpl(std::forward<FROM>(value)...);
      }

      friend Promise;
      template <bool> friend struct AwaitTransform;
      friend Handle;
   };

   explicit Handle(handle_type handle)
      : handle_{std::move(handle)} {
      assert(handle_);
      handle_.promise().parent_ = static_cast<details::Promise<T, WITH_RESOLVER>*>(this);
   }

public:
   template <class SELF> bool IsResolved(this SELF&& self, Lock lock) {
      return self.ValuePromise::IsResolved(lock);
   }

   template <class SELF> bool IsDone(this SELF&& self, Lock lock) {
      return self.ValuePromise::IsResolved(lock) || self.resolver_->reject_;
   }

   template <class SELF> bool IsDone(this SELF&& self) {
      std::shared_lock lock{*self.mutex_};
      return self.IsDone(lock);
   }

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

   details::Promise<T, WITH_RESOLVER>& Detach(
      std::unique_ptr<details::Promise<T, WITH_RESOLVER>>&& self
   ) {
      std::unique_lock lock{self->mutex_};
      assert(!self->self_owned_);

      if (!ValuePromise::IsResolved(lock)) {
         auto& result       = *self;
         result.self_owned_ = std::move(self);

         return result;
      }

      return *self;
   }

   ~Handle() { assert(!this->handle_); }

   Handle(Handle&& rhs) noexcept = delete;
   Handle(Handle const& rhs)     = delete;

   Handle& operator=(Handle&& rhs) noexcept = delete;
   Handle& operator=(Handle const& rhs)     = delete;

   bool Done(Lock) const { return handle_ == nullptr; }

protected:
   std::shared_mutex mutex_{};
   Locker            lock_{this->mutex_, std::defer_lock};

   handle_type                                         handle_{nullptr};
   std::unique_ptr<details::Promise<T, WITH_RESOLVER>> self_owned_{nullptr};
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
   friend class ::Promise<T, WITH_RESOLVER>;

   using ValuePromise = Handle<T, WITH_RESOLVER>::ValuePromise;
   using handle_type  = Handle<T, WITH_RESOLVER>::handle_type;
   using promise_type = Handle<T, WITH_RESOLVER>::PromiseType;
   using Locker       = typename Handle<T, WITH_RESOLVER>::Locker;
   using Unlock       = typename Handle<T, WITH_RESOLVER>::Unlock;

   static constexpr bool IS_VOID = std::is_void_v<T>;

private:
   using Handle<T, WITH_RESOLVER>::Handle;

   explicit Promise(handle_type handle)
      : Handle<T, WITH_RESOLVER>(std::move(handle)) {}

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

   bool await_ready() {
      this->lock_.lock();
      return Ready(this->lock_);
   }

   void await_suspend(std::coroutine_handle<> h) {
      Unlock _{this->lock_};

      if constexpr (!WITH_RESOLVER) {
         assert(this->handle_);
      }

      Await(h, this->lock_);
   }

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
   VPromise::Awaitable& Await() final {
      struct Awaitable
         : VPromise::Awaitable
#ifdef PROMISE_MEMCHECK
         , Refcount
#endif
      {

         Awaitable(details::Promise<T, WITH_RESOLVER>& self)
            : self_(self) {}

         virtual ~Awaitable() = default;

         bool await_ready() final { return self_.await_ready(); }

         void await_resume() final {
            // Delete this After resume
            std::unique_ptr<Awaitable> self_ptr(this);
            self_.await_resume();
         }

         void await_suspend(std::coroutine_handle<> h) final { self_.await_suspend(h); }

      private:
         details::Promise<T, WITH_RESOLVER>& self_;
      };

      return *new Awaitable{*this};
   }

   auto const& GetException(Lock) {
      assert(this->resolver_);
      return this->resolver_->exception_;
   }

   bool Await(std::coroutine_handle<> h, Lock) {
      awaiters_.emplace_back(h);
      return true;
   }

   bool Ready(Lock lock) {
      if (this->GetException(lock)) {
         return true;
      }

      return this->IsResolved(lock);
   }

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

   template <class FUN, class... ARGS> constexpr auto Then(FUN&& func, ARGS&&... args) & {
      // Promise return type
      using T2 = return_t<return_t<FUN>>;

      return ::MakePromise(
         [func = std::forward<FUN>(func
          )](Promise<T, WITH_RESOLVER>& self, ARGS&&... args) -> ::Promise<T2, false> {
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
   }

   template <class FUN, class... ARGS>
   constexpr auto Then(std::unique_ptr<Promise>&& self, FUN&& func, ARGS&&... args) && {
      return this->Detach(std::move(self))
         .Then(std::forward<FUN>(func), std::forward<ARGS>(args)...);
   }

   template <class FUN, class... ARGS> constexpr auto Catch(FUN&& func, ARGS&&... args) & {
      // Promise return type
      using promise_t = return_t<decltype(std::function{func})>;
      using T2        = return_t<promise_t>;
      using FArgs     = args_t<decltype(std::function{func})>;
      static_assert(std::tuple_size_v<FArgs> == 1, "Catch promise must have exactly one argument!");
      using Exception = std::tuple_element_t<0, FArgs>;

      static constexpr bool IS_EXC_PTR = std::is_same_v<Exception, std::exception_ptr>;
      static constexpr bool IS_VALID_V =
         IS_EXC_PTR
         || (std::is_lvalue_reference_v<Exception> && std::is_const_v<std::remove_reference_t<Exception>>);
      static_assert(
         IS_VALID_V,
         "Catch promise argument must be : std::exception_ptr or a const reference to an exception "
         "!"
      );

      using return_t = std::remove_pointer_t<decltype([]() constexpr {
         if constexpr (std::is_void_v<T2> && std::is_void_v<T>) {
            return static_cast<::Promise<void, false>*>(nullptr);
         } else if constexpr (std::is_void_v<T2>) {
            return static_cast<::Promise<std::optional<T>, false>*>(nullptr);
         } else if constexpr (std::is_void_v<T>) {
            return static_cast<::Promise<std::optional<T2>, false>*>(nullptr);
         } else if constexpr (std::is_same_v<T, T2>) {
            return static_cast<::Promise<T2, false>*>(nullptr);
         } else {
            return static_cast<::Promise<std::variant<T2, T>, false>*>(nullptr);
         }
      }())>;

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
               if constexpr (!IS_EXC_PTR) {
                  exc = std::current_exception();
               } else {
                  throw;
               }
            } catch (...) {
               if constexpr (IS_EXC_PTR) {
                  exc = std::current_exception();
               } else {
                  throw;
               }
            }

            assert(exc);

            if constexpr (std::is_void_v<T2>) {
               co_await MakePromise(std::move(func), exc, std::forward<ARGS>(args)...);

               if constexpr (std::is_void_v<T>) {
                  co_return;
               } else {
                  co_return std::optional<T>{};
               }
            } else {
               co_return co_await MakePromise(std::move(func), exc, std::forward<ARGS>(args)...);
            }
         },
         *this,
         std::forward<ARGS>(args)...
      );
   }

   template <class FUN, class... ARGS>
   constexpr auto Catch(std::unique_ptr<Promise>&& self, FUN&& func, ARGS&&... args) && {
      return this->Detach(std::move(self))
         .Catch(std::forward<FUN>(func), std::forward<ARGS>(args)...);
   }

public:
   template <class FUN, class... ARGS> static constexpr auto Create(FUN&& func, ARGS&&... args) {

      struct FunctionImpl : Function {
         explicit FunctionImpl(std::remove_cvref_t<FUN>&& value)
            : func_(std::move(value)) {}

         explicit FunctionImpl(std::remove_cvref_t<FUN> const& value)
            : func_(value) {}

         std::remove_cvref_t<FUN> func_;
      };
      auto holder   = std::make_unique<FunctionImpl>(std::forward<FUN>(func));
      auto resolver = std::make_unique<Resolver<T, WITH_RESOLVER>>();

      auto promise = [&]() constexpr {
         if constexpr (WITH_RESOLVER) {
            return holder->func_(
               resolver->resolve_, resolver->reject_, std::forward<ARGS>(args)...
            );
         } else {
            return holder->func_(std::forward<ARGS>(args)...);
         }
      }();

      auto const details = promise.details_.get();
      resolver->promise_ = details;
      details->resolver_ = std::move(resolver);
      details->function_ = std::move(holder);

      promise.details_->handle_();
      return promise;
   }

private:
   std::unique_ptr<Function>            function_{nullptr};
   std::vector<std::coroutine_handle<>> awaiters_{};

   friend Handle<T, WITH_RESOLVER>;
   template <bool> friend struct AwaitTransform;
};
}  // namespace details

template <class... PROMISE>
static constexpr auto
All(PROMISE&&... promise) {
   return MakePromise(
      [](PROMISE&&... promise) -> ::Promise<std::tuple<std::conditional_t<
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
      },
      std::forward<PROMISE>(promise)...
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
            std::cerr << "Promise: Leak memory detected (" << refcount << " > 0)" << std::endl;
            assert(false);
         }
      }
   };

   return Check{};
}
#endif  // PROMISE_MEMCHECK

}  // namespace promise

template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN>)
static constexpr auto
MakePromise(FUN&& func, ARGS&&... args) {
   using namespace promise;

   return details::Promise<return_t<return_t<FUN>>, WITH_RESOLVER<FUN>>::Create(
      std::forward<FUN>(func), std::forward<ARGS>(args)...
   );
}
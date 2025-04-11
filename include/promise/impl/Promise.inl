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
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <variant>

template <class T> struct exception_helper;

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

template <class T, bool WITH_RESOLVER> struct Resolver {
   Promise<T, WITH_RESOLVER>*    promise_{nullptr};
   std::exception_ptr            exception_{};
   std::optional<T>              value_{};
   std::unique_ptr<resolve_t<T>> resolve_{};
   std::unique_ptr<reject_t>     reject_{};

   bool await_ready() const { return value_.has_value(); }

   void await_resume() const {}

   template <class TT>
      requires(std::is_convertible_v<TT, T>)
   void Resolve(TT&& value) {
      assert(!value_.has_value());
      assert(!exception_);
      value_ = std::forward<TT>(value);

      if constexpr (WITH_RESOLVER) {
         assert(promise_);
         if (promise_->Done()) {
            promise_->ResumeAwaiters();
         }
      }
   }

   void Reject(std::exception_ptr exception) {
      assert(!value_.has_value());
      assert(!exception_);
      exception_ = exception;

      if constexpr (WITH_RESOLVER) {
         assert(promise_);
         if (promise_->Done()) {
            promise_->ResumeAwaiters();
         }
      }
   }
};

template <bool WITH_RESOLVER> struct Resolver<void, WITH_RESOLVER> {
   Promise<void, WITH_RESOLVER>* promise_{nullptr};

   bool                             resolved_{false};
   std::exception_ptr               exception_{};
   std::unique_ptr<resolve_t<void>> resolve_{};
   std::unique_ptr<reject_t>        reject_{};

   bool await_ready() const { return resolved_; }

   void await_resume() const {}

   void Resolve() {
      assert(!resolved_);
      assert(!exception_);
      resolved_ = true;

      if constexpr (WITH_RESOLVER) {
         assert(promise_);
         if (promise_->Done()) {
            promise_->ResumeAwaiters();
         }
      }
   }

   void Reject(std::exception_ptr exception) {
      assert(!resolved_);
      assert(!exception_);
      exception_ = exception;

      if constexpr (WITH_RESOLVER) {
         assert(promise_);
         if (promise_->Done()) {
            promise_->ResumeAwaiters();
         }
      }
   }
};

template <class T, bool VOID_TYPE> struct ValuePromise {
protected:
   static constexpr bool IS_VOID = VOID_TYPE;

public:
   template <class SELF> auto const& GetValue(this SELF&& self) {
      std::shared_lock lock{*self.mutex_};
      assert(self.resolver_);
      assert(self.resolver_->value_.has_value());
      return self.resolver_->value_.value();
   }

   template <class SELF> bool HasValue(this SELF&& self) {
      std::shared_lock lock{*self.mutex_};
      assert(self.resolver_);
      return self.resolver_->value_.has_value();
   }
};

template <class T> struct ValuePromise<T, true> {
protected:
   static constexpr bool IS_VOID = true;

public:
   template <class SELF> bool IsResolved(this SELF&& self) {
      std::shared_lock lock{*self.mutex_};
      assert(self.resolver_);
      return self.resolver_->resolved_;
   }
};

#ifdef PROMISE_MEMCHECK
struct Refcount {
   static std::atomic<std::size_t> counter;

   constexpr Refcount() { ++counter; }

   Refcount(Refcount&&) noexcept { ++counter; };
   Refcount(Refcount const&) noexcept            = delete;
   Refcount& operator=(Refcount&&) noexcept      = delete;
   Refcount& operator=(Refcount const&) noexcept = delete;

   ~Refcount() { --counter; }
};
#endif  // PROMISE_MEMCHECK

template <class T, bool WITH_RESOLVER> struct Handle {
protected:
   struct PromiseType;
   using handle_type             = std::coroutine_handle<PromiseType>;
   using Promise                 = Promise<T, WITH_RESOLVER>;
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
   private:
      Promise* parent_{};

   public:
      PromiseType() = default;

      PromiseType(PromiseType const&)               = delete;
      PromiseType(PromiseType&&) noexcept           = delete;
      PromiseType operator=(PromiseType const&)     = delete;
      PromiseType operator=(PromiseType&&) noexcept = delete;

      ~PromiseType() = default;

      Promise get_return_object() { return Promise{handle_type::from_promise(*this)}; }

      Promise* GetParent() const { return parent_; }

      using init_suspend_t  = std::suspend_always;
      using final_suspend_t = std::suspend_never;
      init_suspend_t  initial_suspend() { return {}; }
      final_suspend_t final_suspend() noexcept {
         auto ptr               = std::move(this->parent_->self_owned_);
         this->parent_->handle_ = nullptr;

         this->parent_->ResumeAwaiters();
         ptr = nullptr;
         return {};
      }
      void unhandled_exception() { this->parent_->unhandled_exception(); }

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
      : handle_{handle} {
      assert(handle_);
      handle_.promise().parent_ = static_cast<Promise*>(this);
   }

   Handle(Handle&& rhs) noexcept
      : self_owned_{std::move(rhs.self_owned_)} {

      if (rhs.handle_) {
         std::swap(rhs.handle_, handle_);
         handle_.promise().parent_ = static_cast<Promise*>(this);
      }

      std::swap(rhs.resolver_, resolver_);
      assert(resolver_);
      resolver_->promise_ = static_cast<Promise*>(this);
   }

public:
   Promise& Detach() && {
      assert(!self_owned_);

      if (this->handle_) {
         auto  ptr          = std::unique_ptr<Promise>(new Promise(static_cast<Promise&&>(*this)));
         auto& result       = *ptr;
         result.self_owned_ = std::move(ptr);

         return result;
      } else {
         return static_cast<Promise&>(*this);
      }
   }

   ~Handle() { assert(!this->handle_); }

   Handle(Handle const& rhs) = delete;

   Handle& operator=(Handle&& rhs) noexcept = delete;
   Handle& operator=(Handle const& rhs)     = delete;

   bool Done() const { return handle_ == nullptr; }

protected:
   handle_type                                 handle_{nullptr};
   std::unique_ptr<Promise>                    self_owned_{nullptr};
   std::unique_ptr<Resolver<T, WITH_RESOLVER>> resolver_{nullptr};

   template <class, bool> friend struct Then;
};

struct Function {
   virtual ~Function() = default;
};

template <class T, bool WITH_RESOLVER>
struct Promise
   : Handle<T, WITH_RESOLVER>
   , ValuePromise<T, std::is_void_v<T>>
#ifdef PROMISE_MEMCHECK
   , Refcount
#endif  // PROMISE_MEMCHECK
{
   using handle_type  = Handle<T, WITH_RESOLVER>::handle_type;
   using promise_type = Handle<T, WITH_RESOLVER>::PromiseType;
   using ValuePromise = ValuePromise<T, std::is_void_v<T>>;

   static constexpr bool IS_VOID = std::is_void_v<T>;

private:
   using Handle<T, WITH_RESOLVER>::Handle;

   Promise(Promise&& rhs) noexcept = default;

public:
   ~Promise() {
      if (this->handle_) {
         assert(!this->handle_.done());
         assert(!this->self_owned_);
      }
   };

   Promise& operator=(Promise&& rhs) noexcept = delete;
   Promise(Promise const&)                    = delete;
   Promise operator=(Promise const&)          = delete;

   auto const& GetException() {
      std::shared_lock lock{*mutex_};
      assert(this->resolver_);
      return this->resolver_->exception_;
   }

   void Lock() {
      // wait for lock to be released (await resume operation done)
      std::shared_lock lock{*this->mutex_};

      if constexpr (!WITH_RESOLVER) {
         if constexpr (IS_VOID) {
            assert(this->IsResolved());
         } else {
            assert(this->HasValue());
         }
      }
   }

   bool Await(std::coroutine_handle<> h) {
      std::unique_lock lock{*mutex_, std::defer_lock};
      if (!lock.try_lock()) {  // @todo correctly...
         // awaiter resume in progress
         return false;
      }

      awaiters_.emplace_back(h);
      return true;
   }

   bool Ready() {
      if (this->GetException()) {
         return true;
      }

      if constexpr (IS_VOID) {
         std::shared_lock lock{*this->mutex_};
         return this->IsResolved();
      } else {
         return this->HasValue();
      }
   }

   bool await_ready() {
      if (auto exc = GetException()) {
         assert(!this->handle_);
         std::rethrow_exception(exc);
      }

      return Ready();
   }

   auto await_suspend(std::coroutine_handle<> h) {
      if constexpr (!WITH_RESOLVER) {
         assert(this->handle_);
      }

      if (!Await(h)) {
         Lock();
         h.resume();
      }
   }

   auto await_resume() noexcept(false) {
      auto const& exception = this->GetException();
      if (exception) {
         std::rethrow_exception(exception);
      }

      if constexpr (IS_VOID) {
         assert(this->IsResolved());
      } else {
         return this->GetValue();
      }
   }

   template <class... FROM>
      requires(
         (std::is_convertible_v<FROM, T> && ...)
         && (sizeof...(FROM) == ((IS_VOID || WITH_RESOLVER) ? 0 : 1))
      )
   void ReturnImpl(FROM&&... value) {
      std::lock_guard lock{*mutex_};

      if constexpr (!WITH_RESOLVER) {
         if constexpr (IS_VOID) {
            assert(!this->resolver_->resolved_);
            this->resolver_->resolved_ = true;
         } else {
            assert(!this->resolver_->value_.has_value());
            ((this->resolver_->value_ = value), ...);
         }
      }
   }

   void unhandled_exception() {
      std::lock_guard lock{*mutex_};
      assert(this->resolver_);
      this->resolver_->exception_ = std::current_exception();
   }

   void ResumeAwaiters() noexcept {
      std::vector<std::coroutine_handle<>> awaiters{};

      {
         std::lock_guard lock{*mutex_};

         awaiters_.swap(awaiters);
         assert(!awaiters_.size());
      }

      for (auto const& awaiter : awaiters) {
         assert(awaiter);
         awaiter.resume();
      }
   }

   template <class FUN, class... ARGS> static constexpr auto Create(FUN&& func, ARGS&&... args) {

      struct FunctionImpl : Function {
         explicit FunctionImpl(std::remove_cvref_t<FUN>&& value)
            : func_(std::move(value)) {}

         explicit FunctionImpl(std::remove_cvref_t<FUN> const& value)
            : func_(value) {}

         std::remove_cvref_t<FUN> func_;
      };
      auto holder = std::make_unique<FunctionImpl>(std::forward<FUN>(func));

      auto promise{[&]() constexpr {
         auto resolver = std::make_unique<Resolver<T, WITH_RESOLVER>>();

         auto resolve = std::make_unique<resolve_t<T>>([&resolver = *resolver]() constexpr {
            if constexpr (IS_VOID) {
               return [&resolver]() { resolver.Resolve(); };
            } else {
               return [&resolver](T const& value) { resolver.Resolve(value); };
            }
         }());

         auto reject = std::make_unique<reject_t>([&resolver = *resolver](std::exception_ptr exc) {
            resolver.Reject(exc);
         });

         auto promise = [&]() constexpr {
            if constexpr (WITH_RESOLVER) {
               return holder->func_(*resolve, *reject, std::forward<ARGS>(args)...);
            } else {
               return holder->func_(std::forward<ARGS>(args)...);
            }
         }();

         resolver->resolve_ = std::move(resolve);
         resolver->reject_  = std::move(reject);
         resolver->promise_ = &promise;
         promise.resolver_  = std::move(resolver);
         promise.function_  = std::move(holder);

         promise.handle_();
         return promise;
      }()};

      return promise;
   }

   template <class FUN, class SELF, class... ARGS>
   constexpr auto Then(this SELF&& self, FUN&& func, ARGS&&... args) {
      // Promise return type
      using T2 = return_t<return_t<decltype(std::function{func})>>;

      auto* self2 = [&]() constexpr {
         if constexpr (!std::is_lvalue_reference_v<SELF>) {
            return &std::forward<SELF>(self).Detach();
         } else {
            return &self;
         }
      }();

      return ::MakePromise(
         [func = std::forward<FUN>(func
          )](std::remove_reference_t<SELF>* self, ARGS&&... args) -> Promise<T2, false> {
            if constexpr (IS_VOID) {
               co_await *self;
               co_return co_await ::MakePromise(std::move(func), std::forward<ARGS>(args)...);
            } else {
               co_return co_await ::MakePromise(
                  std::move(func), co_await *self, std::forward<ARGS>(args)...
               );
            }
         },
         self2,
         std::forward<ARGS>(args)...
      );
   }

   template <class FUN, class SELF, class... ARGS>
   constexpr auto Catch(this SELF&& self, FUN&& func, ARGS&&... args) {
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

      auto* self2 = [&]() constexpr {
         if constexpr (!std::is_lvalue_reference_v<SELF>) {
            return &std::forward<SELF>(self).Detach();
         } else {
            return &self;
         }
      }();

      using return_t = std::remove_cvref_t<decltype([]() constexpr {
         if constexpr (std::is_void_v<T2> && std::is_void_v<T>) {
            return static_cast<Promise<void, false>>(nullptr);
         } else if constexpr (std::is_void_v<T2>) {
            return static_cast<Promise<std::optional<T>, false>>(nullptr);
         } else if constexpr (std::is_void_v<T> || std::is_same_v<T, T2>) {
            return static_cast<Promise<std::optional<T2>, false>>(nullptr);
         } else {
            return static_cast<Promise<std::variant<T2, T>, false>>(nullptr);
         }
      }())>;

      return MakePromise(
         [func = std::forward<FUN>(func
          )](std::remove_reference_t<SELF>* self, ARGS&&... args) -> return_t {
            using exception_t = std::remove_cvref_t<Exception>;
            ExceptionWrapper<exception_t> exc{};

            try {
               if constexpr (std::is_void_v<T>) {
                  co_await *self;
                  if constexpr (std::is_void_v<T2>) {
                     co_return;
                  } else {
                     co_return std::optional<T2>{};
                  }
               } else {
                  co_return co_await *self;
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
         self2,
         std::forward<ARGS>(args)...
      );
   }

private:
   std::unique_ptr<Function>            function_{nullptr};
   std::vector<std::coroutine_handle<>> awaiters_{};
   // unique_ptr for move operation
   std::unique_ptr<std::shared_mutex> mutex_{std::make_unique<std::shared_mutex>()};

   template <class, bool> friend struct Promise;
   friend ValuePromise;
   friend Handle<T, WITH_RESOLVER>;
   template <bool> friend struct AwaitTransform;
};

template <class... PROMISE>
static constexpr auto
All(PROMISE&&... promise) {
   return MakePromise(
      [](PROMISE&&... promise) -> promise::Promise<std::tuple<std::conditional_t<
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

      ~Check() { assert(Refcount::counter == 0); }
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

   return promise::Promise<return_t<return_t<FUN>>, WITH_RESOLVER<FUN>>::Create(
      std::forward<FUN>(func), std::forward<ARGS>(args)...
   );
}
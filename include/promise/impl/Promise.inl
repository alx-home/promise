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
#include <bit>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

template <class T>
struct exception_helper;

// Add a cast operator to an exception pointer (Reverse engineering of MSVC22 must not works if compiled with other compiler)
template <class T>
   requires(std::is_class_v<T>)
struct exception_wrapper_t : std::exception_ptr {
   using std::exception_ptr::exception_ptr;
   using std::exception_ptr::operator=;

   explicit(false) operator T&() {
      static_assert(_MSC_VER == 1943, "Only tested on msvc 2022, with other compiler use it at your own risk!");

      assert(*this);

      struct exception_ptr {
         std::byte* data1_;
         std::byte* data2_;
      };
      static_assert(sizeof(exception_ptr) == 8 * 2);

      exception_ptr data;
      std::ranges::copy_n(reinterpret_cast<std::byte*>(this), sizeof(exception_ptr), reinterpret_cast<std::byte*>(&data));

      std::byte* addr;
      std::ranges::copy_n(reinterpret_cast<std::byte*>(data.data2_) + 0x38, sizeof(addr), reinterpret_cast<std::byte*>(&addr));

      auto ptr = reinterpret_cast<T*>(addr);
      return *ptr;
   }
};

template <class FUN, class... Args>
   requires(!promise::is_function_v<FUN>)
static constexpr auto
make_promise(FUN&& func, Args&&... args) {
   return make_promise(std::function{std::forward<FUN>(func)}, std::forward<Args>(args)...);
}

template <class Exception, class FUN, class... Args>
void
make_reject(FUN const& reject, Args&&... args) {
   reject(std::make_exception_ptr(Exception{std::forward<Args>(args)...}));
}

namespace promise {

template <class T, bool with_resolver>
struct resolver_t {
   Promise<T, with_resolver>*    promise_{nullptr};
   std::exception_ptr            exception_{};
   std::optional<T>              value_{};
   std::unique_ptr<resolve_t<T>> resolve_{};
   std::unique_ptr<reject_t>     reject_{};

   bool await_ready() const {
      return value_.has_value();
   }

   void await_resume() const {}

   template <class TT>
      requires(std::is_convertible_v<TT, T>)
   void resolve(TT&& value) {
      assert(!value_.has_value());
      assert(!exception_);
      value_ = std::forward<TT>(value);

      if constexpr (with_resolver) {
         assert(promise_);
         if (promise_->done()) {
            promise_->resume_awaiters();
         }
      }
   }

   void reject(std::exception_ptr exception) {
      assert(!value_.has_value());
      assert(!exception_);
      exception_ = exception;

      if constexpr (with_resolver) {
         assert(promise_);
         if (promise_->done()) {
            promise_->resume_awaiters();
         }
      }
   }
};

template <bool with_resolver>
struct resolver_t<void, with_resolver> {
   Promise<void, with_resolver>* promise_{nullptr};

   bool                             resolved_{false};
   std::exception_ptr               exception_{};
   std::unique_ptr<resolve_t<void>> resolve_{};
   std::unique_ptr<reject_t>        reject_{};

   bool await_ready() const {
      return resolved_;
   }

   void await_resume() const {}

   void resolve() {
      assert(!resolved_);
      assert(!exception_);
      resolved_ = true;

      if constexpr (with_resolver) {
         assert(promise_);
         if (promise_->done()) {
            promise_->resume_awaiters();
         }
      }
   }

   void reject(std::exception_ptr exception) {
      assert(!resolved_);
      assert(!exception_);
      exception_ = exception;

      if constexpr (with_resolver) {
         assert(promise_);
         if (promise_->done()) {
            promise_->resume_awaiters();
         }
      }
   }
};

template <class T, bool void_type_>
struct ValuePromise {
protected:
   static constexpr bool void_type = void_type_;

public:
   template <class Self>
   auto const& getValue(this Self&& self) {
      std::shared_lock lock{*self.mutex_};
      assert(self.resolver_);
      assert(self.resolver_->value_.has_value());
      return self.resolver_->value_.value();
   }

   template <class Self>
   bool hasValue(this Self&& self) {
      std::shared_lock lock{*self.mutex_};
      assert(self.resolver_);
      return self.resolver_->value_.has_value();
   }
};

template <class T>
struct ValuePromise<T, true> {
protected:
   static constexpr bool void_type = true;

public:
   template <class Self>
   bool isResolved(this Self&& self) {
      std::shared_lock lock{*self.mutex_};
      assert(self.resolver_);
      return self.resolver_->resolved_;
   }
};

#ifdef PROMISE_MEMCHECK
struct refcount_t {
   static std::atomic<std::size_t> counter;

   constexpr refcount_t() {
      ++counter;
   }

   refcount_t(refcount_t&&) noexcept {
      ++counter;
   };
   refcount_t(refcount_t const&) noexcept            = delete;
   refcount_t& operator=(refcount_t&&) noexcept      = delete;
   refcount_t& operator=(refcount_t const&) noexcept = delete;

   ~refcount_t() {
      --counter;
   }
};
#endif  // PROMISE_MEMCHECK

template <class T, bool with_resolver>
struct Handle {
protected:
   struct promise_type;
   using handle_type               = std::coroutine_handle<promise_type>;
   using Promise                   = Promise<T, with_resolver>;
   static constexpr bool void_type = with_resolver || std::is_void_v<T>;

   struct void_promise_type {
   public:
      template <class Self>
      void return_void(this Self&& self) {
         self.return_impl();
      }
   };

   struct value_promise_type {
   public:
      template <class FROM, class Self>
         requires(std::is_convertible_v<FROM, T>)
      void return_value(this Self&& self, FROM&& value) {
         self.return_impl(std::forward<FROM>(value));
      }
   };

   struct promise_type : std::conditional_t<void_type, void_promise_type, value_promise_type> {
   private:
      Promise* parent_{};

   public:
      promise_type() = default;

      promise_type(promise_type const&)               = delete;
      promise_type(promise_type&&) noexcept           = delete;
      promise_type operator=(promise_type const&)     = delete;
      promise_type operator=(promise_type&&) noexcept = delete;

      ~promise_type() = default;

      Promise get_return_object() {
         return Promise{handle_type::from_promise(*this)};
      }

      Promise* getParent() const {
         return parent_;
      }

      using init_suspend_t  = std::suspend_always;
      using final_suspend_t = std::suspend_never;
      init_suspend_t  initial_suspend() { return {}; }
      final_suspend_t final_suspend() noexcept {
         auto ptr               = std::move(this->parent_->self_owned_);
         this->parent_->handle_ = nullptr;

         this->parent_->resume_awaiters();
         ptr = nullptr;
         return {};
      }
      void unhandled_exception() {
         this->parent_->unhandled_exception();
      }

      template <class... FROM>
         requires((std::is_convertible_v<FROM, T> && ...) && (sizeof...(FROM) == ((void_type || with_resolver) ? 0 : 1)))
      void return_impl(FROM&&... value) {
         this->parent_->return_impl(std::forward<FROM>(value)...);
      }

      friend Promise;
      template <bool>
      friend struct AwaitTransform;
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
   Promise& detach() && {
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

   ~Handle() {
      assert(!this->handle_);
   }

   Handle(Handle const& rhs) = delete;

   Handle& operator=(Handle&& rhs) noexcept = delete;
   Handle& operator=(Handle const& rhs)     = delete;

   bool done() const {
      return handle_ == nullptr;
   }

protected:
   handle_type                                   handle_{nullptr};
   std::unique_ptr<Promise>                      self_owned_{nullptr};
   std::unique_ptr<resolver_t<T, with_resolver>> resolver_{nullptr};

   template <class, bool>
   friend struct Then;
};

struct function_t {
   virtual ~function_t() = default;
};

template <class T, bool with_resolver>
struct Promise : Handle<T, with_resolver>
   , ValuePromise<T, std::is_void_v<T>>
#ifdef PROMISE_MEMCHECK
   , refcount_t
#endif  // PROMISE_MEMCHECK
{
   using handle_type  = Handle<T, with_resolver>::handle_type;
   using promise_type = Handle<T, with_resolver>::promise_type;
   using ValuePromise = ValuePromise<T, std::is_void_v<T>>;

   static constexpr bool void_type = std::is_void_v<T>;

private:
   using Handle<T, with_resolver>::Handle;

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

   auto const& getException() {
      std::shared_lock lock{*mutex_};
      assert(this->resolver_);
      return this->resolver_->exception_;
   }

   void lock() {
      // wait for lock to be released (await resume operation done)
      std::shared_lock lock{*this->mutex_};

      if constexpr (!with_resolver) {
         if constexpr (void_type) {
            assert(this->isResolved());
         } else {
            assert(this->hasValue());
         }
      }
   }

   bool await(std::coroutine_handle<> h) {
      std::unique_lock lock{*mutex_, std::defer_lock};
      if (!lock.try_lock()) {  // @todo correctly...
         // awaiter resume in progress
         return false;
      }

      awaiters_.emplace_back(h);
      return true;
   }

   bool ready() {
      if (this->getException()) {
         return true;
      }

      if constexpr (void_type) {
         std::shared_lock lock{*this->mutex_};
         return this->isResolved();
      } else {
         return this->hasValue();
      }
   }

   bool await_ready() {
      if (auto exc = getException()) {
         assert(!this->handle_);
         std::rethrow_exception(exc);
      }

      return ready();
   }

   auto await_suspend(std::coroutine_handle<> h) {
      if constexpr (!with_resolver) {
         assert(this->handle_);
      }

      if (!await(h)) {
         lock();
         h.resume();
      }
   }

   auto await_resume() noexcept(false) {
      auto const& exception = this->getException();
      if (exception) {
         std::rethrow_exception(exception);
      }

      if constexpr (void_type) {
         assert(this->isResolved());
      } else {
         return this->getValue();
      }
   }

   template <class... FROM>
      requires((std::is_convertible_v<FROM, T> && ...) && (sizeof...(FROM) == ((void_type || with_resolver) ? 0 : 1)))
   void return_impl(FROM&&... value) {
      std::lock_guard lock{*mutex_};

      if constexpr (!with_resolver) {
         if constexpr (void_type) {
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

   void resume_awaiters() noexcept {
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

   template <class FUN, class... Args>
   static constexpr auto create(FUN&& func, Args&&... args) {

      struct _function_t : function_t {
         explicit _function_t(std::remove_cvref_t<FUN>&& value)
            : func_(std::move(value)) {}

         explicit _function_t(std::remove_cvref_t<FUN> const& value)
            : func_(value) {}

         std::remove_cvref_t<FUN> func_;
      };
      auto holder = std::make_unique<_function_t>(std::forward<FUN>(func));

      auto promise{[&]() constexpr {
         auto resolver = std::make_unique<resolver_t<T, with_resolver>>();

         auto resolve = std::make_unique<resolve_t<T>>([&resolver = *resolver]() constexpr {
            if constexpr (void_type) {
               return [&resolver]() {
                  resolver.resolve();
               };
            } else {
               return [&resolver](T const& value) {
                  resolver.resolve(value);
               };
            }
         }());

         auto reject = std::make_unique<reject_t>([&resolver = *resolver](std::exception_ptr exc) {
            resolver.reject(exc);
         });

         auto promise = [&]() constexpr {
            if constexpr (with_resolver) {
               return holder->func_(*resolve, *reject, std::forward<Args>(args)...);
            } else {
               return holder->func_(std::forward<Args>(args)...);
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

   template <class FUN, class Self, class... Args>
   constexpr auto then(this Self&& self, FUN&& func, Args&&... args) {
      // Promise return type
      using T2 = return_t<return_t<decltype(std::function{func})>>;

      auto* self_ = [&]() constexpr {
         if constexpr (!std::is_lvalue_reference_v<Self>) {
            return &std::forward<Self>(self).detach();
         } else {
            return &self;
         }
      }();

      return ::make_promise([func = std::forward<FUN>(func)](std::remove_reference_t<Self>* self, Args&&... args) -> Promise<T2, false> {
         if constexpr (void_type) {
            co_await *self;
            co_return co_await ::make_promise(std::move(func), std::forward<Args>(args)...);
         } else {
            co_return co_await ::make_promise(std::move(func), co_await *self, std::forward<Args>(args)...);
         }
      },
                            self_,
                            std::forward<Args>(args)...);
   }

   template <class FUN, class Self, class... Args>
   constexpr auto catch_(this Self&& self, FUN&& func, Args&&... args) {
      // Promise return type
      using promise_t = return_t<decltype(std::function{func})>;
      using T2        = return_t<promise_t>;
      using FArgs     = args_t<decltype(std::function{func})>;
      static_assert(std::tuple_size_v<FArgs> == 1, "Catch promise must have exactly one argument!");
      using Exception = std::tuple_element_t<0, FArgs>;

      static constexpr bool is_exc_ptr = std::is_same_v<Exception, std::exception_ptr>;
      static constexpr bool is_valid_v = is_exc_ptr || (std::is_lvalue_reference_v<Exception> && std::is_const_v<std::remove_reference_t<Exception>>);
      static_assert(is_valid_v, "Catch promise argument must be : std::exception_ptr or a const reference to an exception !");

      auto* self_ = [&]() constexpr {
         if constexpr (!std::is_lvalue_reference_v<Self>) {
            return &std::forward<Self>(self).detach();
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

      return make_promise([func = std::forward<FUN>(func)](std::remove_reference_t<Self>* self, Args&&... args) -> return_t {
         using exception_t = std::remove_cvref_t<Exception>;
         exception_wrapper_t<exception_t> exc{};

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
            if constexpr (!is_exc_ptr) {
               exc = std::current_exception();
            } else {
               throw;
            }
         } catch (...) {
            if constexpr (is_exc_ptr) {
               exc = std::current_exception();
            } else {
               throw;
            }
         }

         assert(exc);

         if constexpr (std::is_void_v<T2>) {
            co_await make_promise(std::move(func), exc, std::forward<Args>(args)...);

            if constexpr (std::is_void_v<T>) {
               co_return;
            } else {
               co_return std::optional<T>{};
            }
         } else {
            co_return co_await make_promise(std::move(func), exc, std::forward<Args>(args)...);
         }
      },
                          self_,
                          std::forward<Args>(args)...);
   }

private:
   std::unique_ptr<function_t>          function_{nullptr};
   std::vector<std::coroutine_handle<>> awaiters_{};
   // unique_ptr for move operation
   std::unique_ptr<std::shared_mutex> mutex_{std::make_unique<std::shared_mutex>()};

   template <class, bool>
   friend struct Promise;
   friend ValuePromise;
   friend Handle<T, with_resolver>;
   template <bool>
   friend struct AwaitTransform;
};

template <class... Promise>
static constexpr auto
All(Promise&&... promise) {
   return make_promise([](Promise&&... promise) -> promise::Promise<std::tuple<std::conditional_t<std::is_void_v<return_t<Promise>>, std::nullopt_t, return_t<Promise>>...>> {
      co_return std::make_tuple(([]<class... Result>(Result... result) constexpr {
         if constexpr (sizeof...(Result)) {
            return result...[0];
         } else {
            return std::nullopt;
         }
      }(co_await promise))...);
   },
                       std::forward<Promise>(promise)...);
}

#ifdef PROMISE_MEMCHECK
constexpr auto
memcheck() {
   struct check_t {
      check_t() = default;

      check_t(check_t&&) noexcept            = delete;
      check_t(check_t const&)                = delete;
      check_t& operator=(check_t&&) noexcept = delete;
      check_t& operator=(check_t const&)     = delete;

      ~check_t() {
         assert(refcount_t::counter == 0);
      }
   };

   return check_t{};
}
#endif  // PROMISE_MEMCHECK

}  // namespace promise

template <class FUN, class... Args>
   requires(promise::is_function_v<FUN>)
static constexpr auto
make_promise(FUN&& func, Args&&... args) {
   using namespace promise;

   return promise::Promise<return_t<return_t<FUN>>, with_resolver_v<FUN>>::create(std::forward<FUN>(func), std::forward<Args>(args)...);
}
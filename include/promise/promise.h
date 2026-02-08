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

#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

template <class T = void, bool WITH_RESOLVER = false>
class Promise;

namespace promise {

struct Exception : std::runtime_error {
   using std::runtime_error::runtime_error;
};

template <class T = void>
struct Resolve;

template <>
struct Resolve<void> : std::enable_shared_from_this<Resolve<void>> {
   Resolve(std::function<void()> impl);

   bool operator()() const;
   operator bool() const;

private:
   std::function<void()>     impl_;
   mutable std::atomic<bool> resolved_{false};
};

template <class T>
   requires(!std::is_void_v<T>)
struct Resolve<T> : std::enable_shared_from_this<Resolve<T>> {
   Resolve(std::function<void(T const&)> impl);

   bool operator()(T const&) const;
   operator bool() const;

private:
   std::function<void(T const&)> impl_;
   mutable std::atomic<bool>     resolved_{false};
};

struct Reject : std::enable_shared_from_this<Reject> {
   Reject(std::function<void(std::exception_ptr)> impl);

   bool operator()(std::exception_ptr exception) const;
   operator bool() const;

private:
   std::function<void(std::exception_ptr)> impl_;
   mutable std::atomic<bool>               rejected_{false};
};

template <class>
struct IsFunction : std::false_type {};
template <class FUN>
struct IsFunction<std::function<FUN>> : std::true_type {};

template <class FUN>
static constexpr bool IS_FUNCTION = IsFunction<std::remove_cvref_t<FUN>>::value;

template <class FUN>
concept function_constructible = requires(FUN fun) { std::function{fun}; };

template <class FUN>
struct return_;

template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct return_<FUN> {
   using type = typename return_<decltype(std::function{std::declval<FUN>()})>::type;
};

template <class T, class... ARGS>
struct return_<std::function<T(ARGS...)>> {
   using type = T;
};

template <class T, bool WITH_RESOLVER>
struct return_<::Promise<T, WITH_RESOLVER>> {
   using type = T;
};

template <class FUN>
using return_t = typename return_<std::remove_cvref_t<FUN>>::type;

template <class T>
struct IsResolver : std::false_type {};

template <class T>
struct IsResolver<std::function<void(T const&)>> : std::true_type {};

template <>
struct IsResolver<std::function<void()>> : std::true_type {};

template <class FUN>
struct WithResolver : std::false_type {};

template <class T>
struct WithResolver<::Promise<T, true>> : std::true_type {};

template <class FUN>
static constexpr bool WITH_RESOLVER = WithResolver<return_t<FUN>>::value;

template <class FUN>
struct IsPromise : std::false_type {};

template <class T, bool WITH_RESOLVER>
struct IsPromise<::Promise<T, WITH_RESOLVER>> : std::true_type {};

template <class FUN>
static constexpr bool IS_PROMISE = IsPromise<return_t<FUN>>::value;

template <class FUN>
struct args_;

template <class FUN>
   requires(!IS_FUNCTION<FUN> && function_constructible<FUN>)
struct args_<FUN> {
   using type = typename args_<decltype(std::function{std::declval<FUN>()})>::type;
};

template <class T, class RESOLVE, class REJECT, class... ARGS>
   requires(WithResolver<T>::value)
struct args_<std::function<T(RESOLVE, REJECT, ARGS...)>> {
   using type = std::tuple<ARGS...>;
};

template <class T, class... ARGS>
   requires(!WithResolver<T>::value)
struct args_<std::function<T(ARGS...)>> {
   using type = std::tuple<ARGS...>;
};

template <class FUN>
using args_t = typename args_<std::remove_cvref_t<FUN>>::type;

// For Handling a promise in a pointer
struct VPromise {
   struct Awaitable {
      virtual ~Awaitable()                                = default;
      virtual bool await_ready()                          = 0;
      virtual void await_resume()                         = 0;
      virtual void await_suspend(std::coroutine_handle<>) = 0;
   };

   virtual void VDetach() && = 0;

   virtual ~VPromise() = default;

   virtual Awaitable& VAwait() = 0;
};
using Pointer = std::unique_ptr<VPromise>;

}  // namespace promise

template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN>)
static constexpr auto MakePromise(FUN&& func, ARGS&&... args);

template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN>)
static constexpr auto MakePromise(FUN&& func, ARGS&&... args);

template <class FUN, class... ARGS>
   requires(promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
static constexpr auto MakeRPromise(FUN&& func, ARGS&&... args);

template <class FUN, class... ARGS>
   requires(!promise::IS_FUNCTION<FUN> && promise::WITH_RESOLVER<FUN>)
static constexpr auto MakeRPromise(FUN&& func, ARGS&&... args);

template <class EXCEPTION, bool RELAXED = true, class... ARGS>
bool MakeReject(promise::Reject const& reject, ARGS&&... args);
namespace promise {

template <class T>
static constexpr auto Pure();

template <class... PROMISE>
static constexpr auto All(PROMISE&&... promise);
namespace details {
template <class T, bool WITH_RESOLVER>
class Promise;
}

template <class T, bool WITH_RESOLVER = true>
struct Resolver;
}  // namespace promise

#include "impl/Promise.inl"

template <class T, bool WITH_RESOLVER>
class Promise : public promise::VPromise {
public:
   using Details      = promise::details::Promise<T, WITH_RESOLVER>;
   using promise_type = Details::promise_type;

   bool await_ready() {
      assert(details_);
      return details_->await_ready();
   }

   auto await_suspend(std::coroutine_handle<> h) {
      assert(details_);
      return details_->await_suspend(h);
   }

   auto await_resume() noexcept(false) {
      assert(details_);
      return details_->await_resume();
   }

   template <class SELF>
      requires(!WITH_RESOLVER)
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
   auto&&
   operator()(this SELF&& self, std::unique_ptr<promise::Resolver<T, WITH_RESOLVER>>&& resolver) {
      (*self.details_)(std::move(resolver));

      if constexpr (std::is_lvalue_reference_v<SELF>) {
         return self;
      } else {
         return std::move(self).Detach();
      }
   }

   bool Done() const noexcept(false) {
      assert(details_);
      std::shared_lock lock{details_->mutex_};
      return details_->IsDone(lock);
   }

   std::exception_ptr Exception() {
      assert(details_);
      std::shared_lock lock{details_->mutex_};
      return details_->GetException(lock);
   }

   template <class FUN, class SELF, class... ARGS>
   [[nodiscard("Either store this promise or call Detach()")]] constexpr auto
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

   template <class... ARGS>
   static constexpr auto Resolve(ARGS&&... args) {
      return Details::Promise::Resolve(std::forward<ARGS>(args)...);
   }

   template <class... ARGS>
   static constexpr auto Reject(ARGS&&... args) {
      return Details::Promise::Reject(std::forward<ARGS>(args)...);
   }

   auto& Detach() && {
      assert(details_);
      auto& details = *details_;
      return details.Detach(std::move(details_));
   }

   void VDetach() && override { static_cast<Promise&&>(*this).Detach(); }

   template <class TYPE = promise::VPromise>
   auto ToPointer() && {
      return std::shared_ptr<TYPE>(static_cast<TYPE*>(new Promise{std::move(details_)}));
   }

private:
   std::shared_ptr<Details> details_{};

   Awaitable& VAwait() final { return details_->VAwait(); }

   Promise(std::shared_ptr<Details> details)
      : details_(std::move(details)) {}

   Promise(Details::handle_type handle)
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

template <class T = void>
using Resolve = promise::Resolve<T>;
using Reject  = promise::Reject;

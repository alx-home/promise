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

#include "Handle.inl"
#include "Memcheck.inl"
#include "ExceptionWrapper.inl"

#include <utils/Scoped.h>
#include <algorithm>
#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace promise {
struct Function {
   virtual ~Function() = default;
};
}  // namespace promise

namespace promise::details {

template <class T>
class WPromise;

template <class T, bool WITH_RESOLVER>
class Promise
   : public Handle<T, WITH_RESOLVER>
#ifdef PROMISE_MEMCHECK
   , public Refcount
#endif  // PROMISE_MEMCHECK
{
public:
   using promise_type = Handle<T, WITH_RESOLVER>::PromiseType;

protected:
   friend class details::IPromise<T, WITH_RESOLVER>;
   friend details::IPromise<T, WITH_RESOLVER>::Parent;

   using ValuePromise = Handle<T, WITH_RESOLVER>::ValuePromise;
   using handle_type  = Handle<T, WITH_RESOLVER>::handle_type;
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
      {
         // Optimisation skip coroutine frame creation

         if (std::shared_lock lock{this->mutex_}; this->IsResolved(lock)) {
            if constexpr (!IS_PROMISE<FUN>) {
               // Function return type
               using T2 = return_t<FUN>;

               try {
                  if constexpr (IS_VOID) {
                     if constexpr (std::is_void_v<T2>) {
                        func(std::forward<ARGS>(args)...);
                        return details::WPromise<T2>::Resolve();
                     } else {
                        return details::WPromise<T2>::Resolve(func(std::forward<ARGS>(args)...));
                     }
                  } else {
                     if constexpr (std::is_void_v<T2>) {
                        func(this->GetValue(lock), std::forward<ARGS>(args)...);
                        return details::WPromise<T2>::Resolve();
                     } else {
                        return details::WPromise<T2>::Resolve(
                          func(this->GetValue(lock), std::forward<ARGS>(args)...)
                        );
                     }
                  }
               } catch (...) {
                  return details::WPromise<T2>::Reject(std::current_exception());
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
           ) -> details::IPromise<T2, false> {
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
             promise::details::Promise<T, WITH_RESOLVER>& self, ARGS&&... args
           ) -> details::IPromise<std::conditional_t<IS_VOID, void, void>, false> {
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

      using Return = std::remove_pointer_t<decltype([]() constexpr {
         if constexpr (std::is_void_v<T2> && std::is_void_v<T>) {
            return static_cast<details::IPromise<void, false>*>(nullptr);
         } else if constexpr (std::is_void_v<T2>) {
            return static_cast<details::IPromise<std::optional<T>, false>*>(nullptr);
         } else if constexpr (std::is_void_v<T>) {
            return static_cast<details::IPromise<std::optional<T2>, false>*>(nullptr);
         } else if constexpr (std::is_same_v<T, T2>) {
            return static_cast<details::IPromise<T2, false>*>(nullptr);
         } else {
            return static_cast<details::IPromise<std::variant<T2, T>, false>*>(nullptr);
         }
      }())>;

      if (this->IsDone()) {
         // Optimisation skip coroutine frame creation

         if (std::shared_lock lock{this->mutex_}; !this->GetException(lock)) {
            if constexpr (std::is_void_v<T>) {
               if constexpr (std::is_void_v<T2>) {
                  return WPromise<return_t<Return>>::Resolve();
               } else {
                  return WPromise<return_t<Return>>::Resolve(std::optional<T2>{});
               }
            } else {
               return WPromise<return_t<Return>>::Resolve(this->GetValue(lock));
            }
         }  // @todo else
      }

      return MakePromise(
        [func =
           std::forward<FUN>(func)](Promise<T, WITH_RESOLVER>& self, ARGS&&... args) -> Return {
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
      {
         // Optimisation skip coroutine frame creation

         if (std::shared_lock lock{this->mutex_}; this->IsResolved(lock)) {
            if constexpr (!IS_PROMISE<FUN>) {
               try {
                  func();

                  if constexpr (IS_VOID) {
                     return WPromise<T>::Resolve();
                  } else {
                     return WPromise<T>::Resolve(this->GetValue(lock));
                  }
               } catch (...) {
                  return WPromise<T>::Reject(std::current_exception());
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
             details::IPromise<T, WITH_RESOLVER>& self
           ) -> details::IPromise<T, true> {
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
             details::IPromise<T, WITH_RESOLVER>& self
           ) -> details::IPromise<T, true> {
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
      details::IPromise<T, WITH_RESOLVER> promise{handle_type{}};
      auto                                resolver = std::make_unique<Resolver<T, WITH_RESOLVER>>();

      auto const details =
        std::get<std::shared_ptr<Promise<T, WITH_RESOLVER>>>(promise.details_).get();
      resolver->promise_ = details;
      details->resolver_ = std::move(resolver);

      details->resolver_->Resolve(std::forward<ARGS>(args)...);

      return WPromise<T>{std::move(promise)};
   }

   /**
    * @brief Create a rejected promise without starting a coroutine.
    * @param args Constructor args for the rejection value (if any).
    * @return Rejected promise.
    */
   template <class... ARGS>
   static constexpr auto Reject(ARGS&&... args) {
      details::IPromise<T, WITH_RESOLVER> promise{handle_type{}};
      auto                                resolver = std::make_unique<Resolver<T, WITH_RESOLVER>>();

      using Promise = std::shared_ptr<Promise<T, WITH_RESOLVER>>;

      assert(std::holds_alternative<Promise>(promise.details_));
      auto const details = std::get<Promise>(promise.details_).get();
      resolver->promise_ = details;
      details->resolver_ = std::move(resolver);

      details->resolver_->Reject(std::forward<ARGS>(args)...);

      return WPromise<T>{std::move(promise)};
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
         if constexpr (std::tuple_size_v<all_args_t<FUN>> >= 2) {
            if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>
                          && IS_REJECTOR<std::tuple_element_t<1, all_args_t<FUN>>>) {
               return holder->func_(resolve, reject, std::forward<ARGS>(args)...);
            } else if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>) {
               return holder->func_(resolve, std::forward<ARGS>(args)...);
            } else {
               return holder->func_(std::forward<ARGS>(args)...);
            }
         } else if constexpr (std::tuple_size_v<all_args_t<FUN>> >= 1) {
            if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>) {
               return holder->func_(resolve, std::forward<ARGS>(args)...);
            } else {
               return holder->func_(std::forward<ARGS>(args)...);
            }
         } else {
            return holder->func_(std::forward<ARGS>(args)...);
         }
      }();

      {
         auto const details =
           std::get<std::shared_ptr<Promise<T, WITH_RESOLVER>>>(promise.details_).get();
         details->resolver_ = std::move(resolver);
         details->function_ = std::move(holder);

         details->handle_();
      }

      if constexpr (RPROMISE) {
         return std::make_tuple(WPromise<T>{std::move(promise)}, &resolve, &reject);
      } else {
         return WPromise<T>{std::move(promise)};
      }
   }

private:
   std::unique_ptr<Function>            function_{nullptr};
   std::vector<std::coroutine_handle<>> awaiters_{};

   friend Handle<T, WITH_RESOLVER>;
};
}  // namespace promise::details

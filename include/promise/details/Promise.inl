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

#include <utils/Scoped.h>
#include <algorithm>
#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace promise::details {
template <class T>
class WPromise;

/**
 * @brief Promise coroutine implementation with optional resolver support.
 *
 * This class implements the promise_type for C++20 coroutines, providing awaitable
 * semantics and continuation chaining. It supports two modes:
 * - Resolver-less mode (WITH_RESOLVER=false): Promise completes via co_return
 * - Resolver mode (WITH_RESOLVER=true): Promise completes via external resolver
 *
 * @tparam T The value type that the promise resolves to. Can be void for promises
 *           that don't produce a value.
 * @tparam WITH_RESOLVER Boolean flag indicating whether the promise uses an external
 *                       resolver (true) or completes via co_return (false).
 *
 * The class provides:
 * - Coroutine awaitable interface (await_ready, await_suspend, await_resume)
 * - Promise chaining via Then(), Catch(), and Finally() methods
 * - Static factory methods for creating resolved/rejected promises
 * - Thread-safe access through internal locking mechanisms
 * - Exception handling and propagation through the promise chain
 *
 * @note Move operations are deleted; promises cannot be moved or copied.
 *       Move operations are handled in the WPromise wrapper, which manages shared ownership of the
 * promise details.
 */
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
   friend class details::WPromise<T>;

   using ValuePromise = Handle<T, WITH_RESOLVER>::ValuePromise;
   using handle_type  = Handle<T, WITH_RESOLVER>::handle_type;
   using Locker       = typename Handle<T, WITH_RESOLVER>::Locker;
   using Unlock       = typename Handle<T, WITH_RESOLVER>::Unlock;

   static constexpr bool IS_VOID = std::is_void_v<T>;

private:
   using Handle<T, WITH_RESOLVER>::Handle;

   /**
    * @brief Constructor for the Promise class.
    *
    * @param handle Coroutine handle to manage.
    */
   explicit Promise(handle_type handle)
      : Handle<T, WITH_RESOLVER>(std::move(handle))
   //
#ifdef PROMISE_MEMCHECK
      , Refcount(this)
#endif
   {
   }

public:
   /**
    * @brief Destructor for the Promise class.
    *
    * Ensures that the coroutine handle is properly cleaned up and that any unresolved promises are
    * detected in debug mode.
    */
   ~Promise() override {
      if (this->handle_) {
         assert(this->self_owned_);
      }

      assert(WITH_RESOLVER || [this] constexpr {
         std::shared_lock lock{this->mutex_};
         return this->IsDone(lock);
      }());
   };

   Promise(Promise&& rhs) noexcept            = delete;
   Promise& operator=(Promise&& rhs) noexcept = delete;
   Promise(Promise const&)                    = delete;
   Promise& operator=(Promise const&)         = delete;

   /**
    * @brief Check if the promise can resume immediately.
    *
    * @return True if ready to resume.
    */
   bool await_ready() {
      std::shared_lock lock{this->mutex_};
      return Ready(lock);
   }

   /**
    * @brief Suspend the coroutine and register continuation.
    *
    * @param h Awaiting coroutine handle.
    */
   bool await_suspend(std::coroutine_handle<> h) {
      std::unique_lock lock{this->mutex_};

      if (Ready(lock)) {
         return false;
      }

      if constexpr (!WITH_RESOLVER) {
         assert(this->handle_);
      }

      Await(h, lock);

      return true;
   }

   /**
    * @brief Resume the await and return the resolved value or throw.
    *
    * @return Resolved value for non-void promises.
    */
   auto await_resume() noexcept(false) {
      std::shared_lock lock{this->mutex_};

      if (auto const& exception = this->GetException(lock); exception) {
         std::rethrow_exception(exception);
      }

      if constexpr (IS_VOID) {
         assert(this->IsResolved(lock));
      } else {
         return this->GetValue(lock);
      }
   }

private:
   template <class...>
      requires(!WITH_RESOLVER)
   /**
    * @brief Start a resolver-less promise.
    *
    * @return Coroutine handle result.
    */
   auto operator()() {
      return this->handle_();
   }

   template <class...>
      requires(WITH_RESOLVER)
   /**
    * @brief Start a resolver-style promise with a resolver.
    *
    * @param resolver Resolver to drive the promise.
    *
    * @return Coroutine handle result.
    */
   auto operator()(std::unique_ptr<Resolver<T>>&& resolver) {
      assert(!this->resolver_);
      this->resolver_ = std::move(resolver);

      if (this->handle_) {
         return this->handle_();
      }
   }

   /**
    * @brief Get a type-erased awaitable wrapper.
    *
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
          *
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

         Awaitable(Awaitable&&) noexcept            = default;
         Awaitable& operator=(Awaitable&&) noexcept = default;
         Awaitable(Awaitable const&)                = delete;
         Awaitable& operator=(Awaitable const&)     = delete;

         /**
          * @brief Check if the await can complete synchronously.
          *
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
          *
          * @param h Awaiting coroutine handle.
          */
         bool await_suspend(std::coroutine_handle<> h) final {
            self_.await_suspend(h);
            return true;
         }

      private:
         details::Promise<T, WITH_RESOLVER>& self_;
      };

      return *new Awaitable{*this};
   }

   /**
    * @brief Get the stored exception using an existing lock.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return Stored exception pointer.
    */
   auto const& GetException(Lock lock) {
      (void)lock;
      assert(this->resolver_);
      return this->resolver_->exception_;
   }

   /**
    * @brief Register an awaiting coroutine.
    *
    * @param h Awaiting coroutine handle.
    * @param lock Active lock for thread-safe access.
    */
   void Await(std::coroutine_handle<> h, UniqueLock lock) {
      (void)lock;
      ++use_count_;
      this->cv_.notify_all();

      awaiters_.emplace_back(h);
   }

   /**
    * @brief Register an awaiting function.
    *
    * @param fun Awaiting function.
    * @param lock Active lock for thread-safe access.
    *
    * @return ID of the registered function.
    */
   std::size_t Await(std::function<void()> fun, UniqueLock lock) {
      (void)lock;
      auto const id = ++use_count_;
      this->cv_.notify_all();

      awaiters_.emplace_back(AwaitFunction{std::move(fun), id});
      return id;
   }

   /**
    * @brief Unregister an awaiting function.
    *
    * @param id ID of the registered function.
    * @param lock Active lock for thread-safe access.
    *
    * @return True if the function was unregistered.
    */
   bool UnAwait(std::size_t id, UniqueLock lock) {
      (void)lock;
      auto const it = std::ranges::find_if(awaiters_, [id](auto const& f) constexpr {
         if (std::holds_alternative<AwaitFunction>(f)) {
            return std::get<AwaitFunction>(f).id_ == id;
         }
         return false;
      });
      if (it != awaiters_.end()) {
         awaiters_.erase(it);
         return true;
      }

      return false;
   }

   /**
    * @brief Get the number of awaiters currently waiting on this promise.
    *
    * @return Number of awaiters.
    */
   std::size_t Awaiters() const {
      std::shared_lock lock{this->mutex_};
      return this->awaiters_.size();
   }

   /**
    * @brief Get the total number of awaiter registrations on this promise.
    *
    * @return Total number of awaiter registrations.
    */
   std::size_t UseCount() const noexcept { return use_count_; }

   /**
    * @brief Check whether the promise is ready.
    *
    * @param lock Active lock for thread-safe access.
    *
    * @return True if ready to resume.
    */
   bool Ready(Lock lock) {
      if (this->GetException(lock)) {
         return true;
      }

      return this->IsResolved(lock);
   }

   /**
    * @brief Chain a continuation to run on resolve.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param func Continuation to invoke on resolve.
    * @param args Arguments forwarded to the continuation.
    *
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto Then(FUN&& func, ARGS&&... args) & {
      static_assert(!IS_WPROMISE<FUN>, "Then does not support promise wrapper!");

      if (std::unique_lock ulock{this->mutex_}; this->IsDone(ulock)) {
         ulock.unlock();
         std::shared_lock lock{this->mutex_};

         if constexpr (!IS_PROMISE<FUN>) {
            // Function return type
            using T2 = return_t<FUN>;

            auto const& exception = this->GetException(lock);
            if (exception) {
               lock.unlock();
               return details::WPromise<T2>::Reject(exception);
            }

            try {
               if constexpr (IS_VOID) {
                  lock.unlock();

                  if constexpr (std::is_void_v<T2>) {
                     func(std::forward<ARGS>(args)...);
                     return details::WPromise<T2>::Resolve();
                  } else {
                     return details::WPromise<T2>::Resolve(func(std::forward<ARGS>(args)...));
                  }
               } else {
                  auto const& value = this->GetValue(lock);
                  lock.unlock();

                  if constexpr (std::is_void_v<T2>) {
                     func(value, std::forward<ARGS>(args)...);
                     return details::WPromise<T2>::Resolve();
                  } else {
                     return details::WPromise<T2>::Resolve(func(value, std::forward<ARGS>(args)...)
                     );
                  }
               }
            } catch (...) {
               return details::WPromise<T2>::Reject(std::current_exception());
            }
         } else if constexpr (IS_VOID) {
            using T2 = return_t<return_t<FUN>>;

            auto const& exception = this->GetException(lock);
            if (exception) {
               lock.unlock();
               return details::WPromise<T2>::Reject(exception);
            }

            return ::MakePromise(std::move(func), std::forward<ARGS>(args)...);
         } else {
            using T2 = return_t<return_t<FUN>>;

            auto const& exception = this->GetException(lock);
            if (exception) {
               lock.unlock();
               return details::WPromise<T2>::Reject(exception);
            }

            auto const& value = this->GetValue(lock);
            lock.unlock();

            return ::MakePromise(std::move(func), value, std::forward<ARGS>(args)...);
         }
      } else if constexpr (IS_PROMISE<FUN>) {
         // Promise return type
         using T2 = return_t<return_t<FUN>>;

         auto [promise, resolve, reject] = promise::Create<T2>();
         Await(
           [this,
            func     = std::forward<FUN>(func),
            resolve  = std::move(resolve),
            reject   = std::move(reject),
            ... args = std::forward<ARGS>(args)] mutable {
              auto promise = [this,
                              func     = std::move(func),
                              ... args = std::forward<ARGS>(args)] constexpr mutable {
                 std::shared_lock lock{this->mutex_};
                 auto const&      exception = this->GetException(lock);
                 if (exception) {
                    lock.unlock();
                    return details::WPromise<T2>::Reject(exception);
                 } else {
                    if constexpr (IS_VOID) {
                       lock.unlock();
                       return ::MakePromise(std::move(func), std::forward<ARGS>(args)...);
                    } else {
                       auto const& value = this->GetValue(lock);
                       lock.unlock();
                       return ::MakePromise(std::move(func), value, std::forward<ARGS>(args)...);
                    }
                 }
              }();

              [promise = std::move(promise), resolve = std::move(resolve)] constexpr mutable {
                 if constexpr (std::is_void_v<T2>) {
                    return std::move(promise).Then([resolve = std::move(resolve)]() constexpr {
                       (*resolve)();
                    });
                 } else {
                    return std::move(promise).Then([resolve = std::move(resolve)](T2 const& value
                                                   ) constexpr { (*resolve)(value); });
                 }
              }()
                .Catch([reject = std::move(reject)](std::exception_ptr exception) constexpr {
                   (*reject)(std::move(exception));
                })
                .Detach();
           },
           ulock
         );

         return std::move(promise);
      } else {
         // Function return type
         using T2 = return_t<FUN>;

         auto [promise, resolve, reject] = promise::Create<T2>();
         Await(
           [this,
            func     = std::forward<FUN>(func),
            resolve  = std::move(resolve),
            reject   = std::move(reject),
            ... args = std::forward<ARGS>(args)] constexpr {
              try {
                 std::shared_lock lock{this->mutex_};
                 auto const&      exception = this->GetException(lock);

                 if (exception) {
                    lock.unlock();
                    (*reject)(exception);
                 } else {
                    if constexpr (IS_VOID) {
                       lock.unlock();

                       if constexpr (std::is_void_v<T2>) {
                          func(std::forward<ARGS>(args)...);
                          (*resolve)();
                       } else {
                          (*resolve)(func(std::forward<ARGS>(args)...));
                       }
                    } else {
                       auto const& value = this->GetValue(lock);
                       lock.unlock();

                       if constexpr (std::is_void_v<T2>) {
                          func(value, std::forward<ARGS>(args)...);
                          (*resolve)();
                       } else {
                          (*resolve)(func(value, std::forward<ARGS>(args)...));
                       }
                    }
                 }
              } catch (...) {
                 (*reject)(std::current_exception());
              }
           },
           ulock
         );

         return std::move(promise);
      }
   }

   /**
    * @brief Chain a continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param self Owning shared pointer to promise details.
    * @param func Continuation to invoke on resolve.
    * @param args Arguments forwarded to the continuation.
    *
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto
   Then(std::shared_ptr<Promise>&& self, FUN&& func, ARGS&&... args) && {
      assert(self);
      ScopeExit _{[&]() { std::move(*this).Detach(std::move(self)); }};
      return self->Then(std::forward<FUN>(func), std::forward<ARGS>(args)...);
   }

   /**
    * @brief Chain a continuation to run on rejection.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param func Continuation to invoke on rejection.
    * @param args Arguments forwarded to the continuation.
    *
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto Catch(FUN&& func, ARGS&&... args) & {
      static_assert(!IS_WPROMISE<FUN>, "Catch does not support promise wrapper!");

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

      using ReturnType = return_t<Return>;

      auto apply_value = [this](auto& lock) constexpr {
         if constexpr (std::is_void_v<T>) {
            (void)this;
            lock.unlock();

            if constexpr (!std::is_void_v<T2>) {
               return std::optional<T2>{};
            }
         } else {
            auto const& value = this->GetValue(lock);
            lock.unlock();
            return value;
         }
      };

      auto apply_exception = [func = std::forward<FUN>(func),
                              ... args =
                                std::forward<ARGS>(args)](auto const& exception) constexpr mutable {
         auto exception_wrapper = [&exception](auto&& invoke) constexpr {
            if constexpr (IS_EXC_PTR) {
               return invoke(exception);
            } else {
               try {
                  std::rethrow_exception(exception);
               } catch (Exception const& ex) {
                  if constexpr (IS_PROMISE<FUN> && std::is_lvalue_reference_v<Exception>) {
                     // Copy exception to avoid dangling reference if func captures it by
                     // reference and is called after this scope
                     using invoke_promise_t =
                       std::invoke_result_t<decltype(invoke)&, Exception const&>;
                     using invoke_value_t = return_t<invoke_promise_t>;

                     return MakePromise(
                       [ex, invoke = std::move(invoke)](
                       ) mutable -> details::IPromise<invoke_value_t, false> {
                          co_return co_await invoke(ex);
                       }
                     );
                  } else {
                     return invoke(ex);
                  }
               }
            }
         };

         return exception_wrapper([func = std::forward<FUN>(func),
                                   ... args =
                                     std::forward<ARGS>(args)](auto const& ex) constexpr mutable {
            if constexpr (IS_PROMISE<FUN>) {
               return MakePromise(std::move(func), ex, std::forward<ARGS>(args)...);
            } else if constexpr (std::is_void_v<T2>) {
               func(ex, std::forward<ARGS>(args)...);
               if constexpr (!std::is_void_v<T>) {
                  return std::optional<T>{};
               }
            } else {
               return func(ex, std::forward<ARGS>(args)...);
            }
         });
      };

      auto resolve_wrapper = [](auto&& promise, auto&& resolve) constexpr {
         if constexpr (std::is_void_v<T2>) {
            if constexpr (std::is_void_v<T>) {
               return std::move(promise).Then([resolve = std::move(resolve)]() constexpr {
                  (*resolve)();
               });
            } else {
               return std::move(promise).Then([resolve = std::move(resolve)]() constexpr {
                  (*resolve)(std::nullopt);
               });
            }
         } else {
            return std::move(promise).Then([resolve = std::move(resolve)](T2 const& value
                                           ) constexpr { (*resolve)(value); });
         }
      };

      auto [promise, resolve, reject] = promise::Create<ReturnType>();
      try {
         if (std::unique_lock ulock{this->mutex_}; this->IsDone(ulock)) {
            ulock.unlock();
            std::shared_lock lock{this->mutex_};

            auto const& exception = this->GetException(lock);
            if (exception) {
               lock.unlock();

               if constexpr (IS_PROMISE<FUN>) {
                  resolve_wrapper(apply_exception(exception), std::move(resolve))
                    .Catch([reject](std::exception_ptr exception) constexpr {
                       (*reject)(std::move(exception));
                    })
                    .Detach();
               } else {
                  if constexpr (std::is_void_v<std::invoke_result_t<
                                  decltype(apply_exception),
                                  decltype((exception))>>) {
                     apply_exception(exception);
                     (*resolve)();
                  } else {
                     (*resolve)(apply_exception(exception));
                  }
               }
            } else if constexpr (std::is_void_v<
                                   std::invoke_result_t<decltype(apply_value), decltype((lock))>>) {
               apply_value(lock);
               (*resolve)();
            } else {
               (*resolve)(apply_value(lock));
            }
         } else {
            Await(
              [this,
               resolve         = std::move(resolve),
               reject          = std::move(reject),
               apply_exception = std::move(apply_exception),
               apply_value     = std::move(apply_value),
               resolve_wrapper = std::move(resolve_wrapper),
               ... args        = std::forward<ARGS>(args)] constexpr mutable {
                 std::shared_lock lock{this->mutex_};
                 auto const&      exception = this->GetException(lock);

                 try {
                    if (exception) {
                       lock.unlock();

                       if constexpr (IS_PROMISE<FUN>) {
                          resolve_wrapper(apply_exception(exception), std::move(resolve))
                            .Catch([reject = std::move(reject)](std::exception_ptr exception
                                   ) constexpr { (*reject)(std::move(exception)); })
                            .Detach();
                       } else {
                          (void)resolve_wrapper;

                          if constexpr (std::is_void_v<std::invoke_result_t<
                                          decltype(apply_exception),
                                          decltype(exception)>>) {
                             apply_exception(exception);
                             (*resolve)();
                          } else {
                             (*resolve)(apply_exception(exception));
                          }
                       }
                    } else {
                       if constexpr (std::is_void_v<std::invoke_result_t<
                                       decltype(apply_value),
                                       decltype((lock))>>) {
                          apply_value(lock);
                          (*resolve)();
                       } else {
                          (*resolve)(apply_value(lock));
                       }
                    }
                 } catch (...) {
                    if (lock.owns_lock()) {
                       lock.unlock();
                    }

                    (*reject)(std::current_exception());
                 }
              },
              ulock
            );
         }
      } catch (...) {
         (*reject)(std::current_exception());
      }

      return std::move(promise);
   }

   /**
    * @brief Chain a rejection continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param self Owning shared pointer to promise details.
    * @param func Continuation to invoke on rejection.
    * @param args Arguments forwarded to the continuation.
    *
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto
   Catch(std::shared_ptr<Promise>&& self, FUN&& func, ARGS&&... args) && {
      assert(self);
      ScopeExit _{[&]() { std::move(*this).Detach(std::move(self)); }};
      return self->Catch(std::forward<FUN>(func), std::forward<ARGS>(args)...);
   }

   /**
    * @brief Chain a continuation that runs regardless of outcome.
    *
    * @tparam FUN Type of the continuation function.
    * @tparam ARGS Types of arguments to forward to the continuation.
    *
    * @param func Continuation to invoke after resolve or reject.
    *
    * @return Chained promise.
    */
   template <class FUN, class... ARGS>
   [[nodiscard]] constexpr auto Finally(FUN&& func) & {
      static_assert(!IS_WPROMISE<FUN>, "Finally does not support promise wrapper!");

      auto apply_value = [](auto&& resolve, auto&& reject, auto&& func, auto&&... value) constexpr {
         static_assert(sizeof...(value) == (IS_VOID ? 0 : 1));

         if constexpr (IS_PROMISE<FUN>) {
            if constexpr (std::is_void_v<T>) {
               MakePromise(std::forward<FUN>(func))
                 .Then([resolve = std::move(resolve)]() constexpr { (*resolve)(); })
                 .Catch([reject](std::exception_ptr exception) constexpr {
                    (*reject)(std::move(exception));
                 })
                 .Detach();
            } else {
               MakePromise(std::forward<FUN>(func))
                 .Then([resolve = std::move(resolve), value = value...[0]]() constexpr {
                    (*resolve)(value);
                 })
                 .Catch([reject](std::exception_ptr exception) constexpr {
                    (*reject)(std::move(exception));
                 })
                 .Detach();
            }
         } else {
            try {
               if constexpr (std::is_void_v<T>) {
                  func();
                  (*resolve)();
               } else {
                  func();
                  (*resolve)(value...[0]);
               }
            } catch (...) {
               (*reject)(std::current_exception());
            }
         }
      };

      auto apply_exception =
        [](auto&& reject, auto&& func, std::exception_ptr exception) constexpr {
           if constexpr (IS_PROMISE<FUN>) {
              MakePromise(std::forward<FUN>(func))
                .Then([reject, exception = std::move(exception)]() constexpr {
                   (*reject)(exception);
                })
                .Catch([reject](std::exception_ptr exception) constexpr {
                   (*reject)(std::move(exception));
                })
                .Detach();
           } else {
              try {
                 func();
                 (*reject)(std::move(exception));
              } catch (...) {
                 (*reject)(std::current_exception());
              }
           }
        };

      auto [promise, resolve, reject] = promise::Create<T>();
      if (std::unique_lock<std::shared_mutex> ulock{this->mutex_}; this->IsDone(ulock)) {
         ulock.unlock();
         std::shared_lock lock{this->mutex_};

         auto const& exception = this->GetException(lock);
         if (exception) {
            lock.unlock();

            if constexpr (IS_PROMISE<FUN>) {
               apply_exception(reject, std::forward<FUN>(func), exception);
            } else {
               apply_exception(reject, std::forward<FUN>(func), exception);
            }
         } else if constexpr (IS_VOID) {
            apply_value(std::move(resolve), std::move(reject), std::forward<FUN>(func));
         } else {
            auto const& value = this->GetValue(lock);
            lock.unlock();

            apply_value(std::move(resolve), reject, std::forward<FUN>(func), value);
         }
      } else {
         Await(
           [this,
            resolve         = std::move(resolve),
            reject          = std::move(reject),
            func            = std::forward<FUN>(func),
            apply_exception = std::move(apply_exception),
            apply_value     = std::move(apply_value)] constexpr mutable {
              std::shared_lock lock{this->mutex_};
              auto const&      exception = this->GetException(lock);

              try {
                 if (exception) {
                    lock.unlock();
                    apply_exception(reject, std::move(func), exception);
                 } else if constexpr (IS_VOID) {
                    lock.unlock();
                    apply_value(std::move(resolve), reject, std::move(func));
                 } else {
                    auto const& value = this->GetValue(lock);
                    lock.unlock();

                    apply_value(std::move(resolve), reject, std::move(func), value);
                 }
              } catch (...) {
                 if (lock.owns_lock()) {
                    lock.unlock();
                 }

                 (*reject)(std::current_exception());
              }
           },
           ulock
         );
      }

      return std::move(promise);
   }

   /**
    * @brief Chain a finally continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    *
    * @param self Owning shared pointer to promise details.
    * @param func Continuation to invoke after resolve or reject.
    *
    * @return Chained promise.
    */
   template <class FUN>
   [[nodiscard]] constexpr auto Finally(std::shared_ptr<Promise>&& self, FUN&& func) && {
      assert(self);
      ScopeExit _{[&]() { std::move(*this).Detach(std::move(self)); }};
      return self->Finally(std::forward<FUN>(func));
   }

   /**
    * @brief Chain a race continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    *
    * @param race_promise Promise to race against.
    * @param resolve Resolver for the race promise.
    * @param reject Rejecter for the race promise.
    *
    * @return Chained promise.
    */
   template <class T2>
   [[nodiscard]] constexpr auto Race(
     WPromise<T2>&&                      race_promise,
     std::shared_ptr<Resolve<T2>> const& resolve,
     std::shared_ptr<Reject> const&      reject
   ) & {
      if (race_promise.Done()) {
         {
            std::unique_lock lock{this->mutex_};
            ++use_count_;
            this->cv_.notify_all();
         }
         // If the race promise is already done, we can skip registering a continuation and just
         // return it directly
         return std::move(race_promise);
      }

      auto const handle = [this, resolve, reject](std::shared_lock<std::shared_mutex>& lock
                          ) constexpr {
         auto const& exception = this->GetException(lock);
         if (exception) {
            lock.unlock();
            (*reject)(exception);
         } else if constexpr (std::is_void_v<T2>) {
            lock.unlock();
            (*resolve)();
         } else if constexpr (IS_VOID) {
            lock.unlock();
            (*resolve)(std::nullopt);
         } else {
            auto const& value = this->GetValue(lock);
            lock.unlock();

            (*resolve)(value);
         }
      };
      if (std::unique_lock ulock{this->mutex_}; this->IsDone(ulock)) {
         ulock.unlock();
         std::shared_lock lock{this->mutex_};
         handle(lock);
         return std::move(race_promise);
      } else {
         auto const id = Await(
           [this, handle = std::move(handle)]() mutable {
              std::shared_lock lock{this->mutex_};
              handle(lock);
           },
           ulock
         );
         ulock.unlock();

         return std::move(race_promise).Finally([this, id]() constexpr {
            std::lock_guard lock{this->mutex_};
            this->UnAwait(id, lock);
         });
      }
   }
   /**
    * @brief Chain a race continuation on an rvalue promise handle.
    *
    * @tparam FUN Type of the continuation function.
    *
    * @param self Owning shared pointer to promise details.
    * @param race_promise Promise to race against.
    * @param resolve Resolver for the race promise.
    * @param reject Rejecter for the race promise.
    *
    * @return Chained promise.
    */
   template <class T2>
   [[nodiscard]] constexpr auto Race(
     std::shared_ptr<Promise>&&          self,
     WPromise<T2>&&                      race_promise,
     std::shared_ptr<Resolve<T2>> const& resolve,
     std::shared_ptr<Reject> const&      reject
   ) && {
      assert(self);
      ScopeExit _{[&]() { std::move(*this).Detach(std::move(self)); }};
      return self->Race(std::move(race_promise), resolve, reject);
   }

public:
   /**
    * @brief Create a resolved promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the resolver.
    *
    * @param args Constructor args for the resolved value (if any).
    *
    * @return Resolved promise.
    */
   static constexpr auto Create() {
      if constexpr (WITH_RESOLVER) {
         using PromisePtr = std::shared_ptr<Promise<T, WITH_RESOLVER>>;

         details::IPromise<T, WITH_RESOLVER> promise{handle_type{}};
         assert(std::holds_alternative<PromisePtr>(promise.details_));

         auto [resolver, resolve, reject] = promise::Resolver<T>::Create();

         auto const details = std::get<PromisePtr>(promise.details_);
         resolver->promise_ = details;
         details->resolver_ = std::move(resolver);

         return std::make_tuple(
           details::WPromise<T>{std::move(promise)}, std::move(resolve), std::move(reject)
         );
      } else {
         return Promise<T, true>::Create();
      }
   }

   /**
    * @brief Create a resolved promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the resolver.
    *
    * @param args Constructor args for the resolved value (if any).
    *
    * @return Resolved promise.
    */
   template <class... ARGS>
   static constexpr auto Resolve(ARGS&&... args) {
      auto [promise, resolve, _] = Promise<T, true>::Create();
      (*resolve)(std::forward<ARGS>(args)...);
      return std::move(promise);
   }

   /**
    * @brief Create a rejected promise without starting a coroutine.
    *
    * @tparam ARGS Types of arguments to forward to the resolver.
    *
    * @param args Constructor args for the rejection value (if any).
    *
    * @return Rejected promise.
    */
   template <class... ARGS>
   static constexpr auto Reject(ARGS&&... args) {
      auto [promise, _, reject] = Promise<T, true>::Create();
      (*reject)(std::forward<ARGS>(args)...);
      return std::move(promise);
   }

   /**
    * @brief Create a rejected promise without starting a coroutine.
    *
    * @tparam EXCEPTION Type of the exception to reject with.
    * @tparam ARGS Types of arguments to forward to the resolver.
    *
    * @param args Constructor args for the rejection value (if any).
    *
    * @return Rejected promise.
    */
   template <class EXCEPTION, class... ARGS>
   static constexpr auto Reject(ARGS&&... args) {
      auto [promise, _, reject] = Promise<T, true>::Create();
      (*reject)(std::make_exception_ptr(EXCEPTION(std::forward<ARGS>(args)...)));
      return std::move(promise);
   }

   /**
    * @brief Create a promise from a callable and optional resolver.
    **
    * @tparam RPROMISE Boolean flag indicating whether to return a tuple with resolver and
    *rejector.
    * @tparam FUN Type of the callable.
    * @tparam ARGS Types of arguments to forward to the callable.
    **
    * @param func Callable used to produce the promise.
    * @param args Arguments forwarded to the callable.
    *
    * @return Promise or tuple when RPROMISE is true.
    */
   template <bool RPROMISE, class FUN, class... ARGS>
#if defined(__clang__)
   __attribute__((no_sanitize("address")))
#endif
   static constexpr auto
   Create(FUN&& func, ARGS&&... args) {

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

         std::remove_cvref_t<FUN>             func_;
         std::shared_ptr<promise::Resolve<T>> resolve_;
         std::shared_ptr<promise::Reject>     reject_;
      };
      auto holder                      = std::make_unique<FunctionImpl>(std::forward<FUN>(func));
      auto [resolver, resolve, reject] = promise::Resolver<T>::Create();

      holder->resolve_ = resolve;
      holder->reject_  = reject;

      auto promise = [&]() constexpr {
         if constexpr (std::tuple_size_v<all_args_t<FUN>> >= 2) {
            if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>
                          && IS_REJECTOR<std::tuple_element_t<1, all_args_t<FUN>>>) {
               return holder->func_(*resolve, *reject, std::forward<ARGS>(args)...);
            } else if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>) {
               return holder->func_(*resolve, std::forward<ARGS>(args)...);
            } else {
               return holder->func_(std::forward<ARGS>(args)...);
            }
         } else if constexpr (std::tuple_size_v<all_args_t<FUN>> >= 1) {
            if constexpr (IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>) {
               return holder->func_(*resolve, std::forward<ARGS>(args)...);
            } else {
               return holder->func_(std::forward<ARGS>(args)...);
            }
         } else {
            return holder->func_(std::forward<ARGS>(args)...);
         }
      }();

      {
         using PromisePtr = std::shared_ptr<Promise<T, WITH_RESOLVER>>;
         assert(std::holds_alternative<PromisePtr>(promise.details_));
         assert(std::get<PromisePtr>(promise.details_));

         auto const details = std::get<PromisePtr>(promise.details_);
         resolver->promise_ = details;

         details->resolver_ = std::move(resolver);
         details->function_ = std::move(holder);

         details->handle_();
      }

      if constexpr (RPROMISE) {
         return std::make_tuple(
           WPromise<T>{std::move(promise)}, std::move(resolve), std::move(reject)
         );
      } else {
         return WPromise<T>{std::move(promise)};
      }
   }

private:
   std::atomic<std::size_t> use_count_{0};
   struct AwaitFunction : std::function<void()> {
      std::size_t id_{0};
   };
   using Awaiter = std::variant<std::coroutine_handle<>, AwaitFunction>;

   std::unique_ptr<Function> function_{nullptr};
   std::vector<Awaiter>      awaiters_{};

   friend Handle<T, WITH_RESOLVER>;
};
}  // namespace promise::details

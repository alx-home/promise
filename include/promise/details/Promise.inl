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

#include "../core/Promise.h"

#include <cassert>
#include <exception>
#include <utils/Scoped.h>

namespace promise::details {

/**
 * @brief Constructor for the Promise class.
 *
 * @param handle Coroutine handle to manage.
 */

template <class T, bool WITH_RESOLVER>
Promise<T, WITH_RESOLVER>::Promise(handle_type handle)
   : Handle<T, WITH_RESOLVER>(std::move(handle))
//
#ifdef PROMISE_MEMCHECK
   , Refcount(this)
#endif
{
}

/**
 * @brief Destructor for the Promise class.
 *
 * Ensures that the coroutine handle is properly cleaned up and that any
 * unresolved promises are detected in debug mode.
 */
template <class T, bool WITH_RESOLVER>
Promise<T, WITH_RESOLVER>::~Promise() {
   if (this->handle_) {
      assert(this->self_owned_);
   }

   assert(WITH_RESOLVER || [this] constexpr {
      std::shared_lock lock{this->mutex_};
      return this->IsDone(lock);
   }());
};

/**
 * @brief Check if the promise can resume immediately.
 *
 * @return True if ready to resume.
 */
template <class T, bool WITH_RESOLVER>
bool
Promise<T, WITH_RESOLVER>::await_ready() {
   std::shared_lock lock{this->mutex_};
   return this->IsDone(lock);
}

/**
 * @brief Suspend the coroutine and register continuation.
 *
 * @param h Awaiting coroutine handle.
 */

template <class T, bool WITH_RESOLVER>
bool
Promise<T, WITH_RESOLVER>::await_suspend(std::coroutine_handle<> h) {
   std::unique_lock lock{this->mutex_};

   if (this->IsDone(lock)) {
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
template <class T, bool WITH_RESOLVER>
cref_or_void_t<T>
Promise<T, WITH_RESOLVER>::await_resume() noexcept(false) {
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

/**
 * @brief Start a resolver-less promise.
 */
template <class T, bool WITH_RESOLVER>
template <class...>
   requires(!WITH_RESOLVER)
void
Promise<T, WITH_RESOLVER>::operator()() {
   return this->handle_();
}

/**
 * @brief Start a resolver-style promise with a resolver.
 *
 * @param resolver Resolver to drive the promise.
 */
template <class T, bool WITH_RESOLVER>
template <class...>
   requires(WITH_RESOLVER)
void
Promise<T, WITH_RESOLVER>::operator()(std::unique_ptr<Resolver<T>>&& resolver) {
   assert(!this->resolver_);
   this->resolver_ = std::move(resolver);

   if (this->handle_) {
      return this->handle_();
   }
}

/**
 * @brief Get the stored exception using an existing lock.
 *
 * @param lock Active lock for thread-safe access.
 *
 * @return Stored exception pointer.
 */

template <class T, bool WITH_RESOLVER>
[[nodiscard]] std::exception_ptr const&
Promise<T, WITH_RESOLVER>::GetException(Lock lock) const {
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
template <class T, bool WITH_RESOLVER>
void
Promise<T, WITH_RESOLVER>::Await(std::coroutine_handle<> h, UniqueLock lock) {
   (void)lock;
   ++use_count_;
   this->cv_.notify_all();

   awaiters_.emplace_back(h);
   std::visit(
     [this]([[maybe_unused]] auto& lock) {
        (void)this;
        assert(!this->IsDone(lock));
     },
     lock
   );
}

/**
 * @brief Register an awaiting function.
 *
 * @param fun Awaiting function.
 * @param lock Active lock for thread-safe access.
 *
 * @return ID of the registered function.
 */
template <class T, bool WITH_RESOLVER>
std::size_t
Promise<T, WITH_RESOLVER>::Await(std::function<void()> fun, UniqueLock lock) {
   (void)lock;
   auto const id = ++use_count_;
   this->cv_.notify_all();

   awaiters_.emplace_back(AwaitFunction{std::move(fun), id});
   std::visit(
     [this]([[maybe_unused]] auto& lock) {
        (void)this;
        assert(!this->IsDone(lock));
     },
     lock
   );
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
template <class T, bool WITH_RESOLVER>
bool
Promise<T, WITH_RESOLVER>::UnAwait(std::size_t id, UniqueLock lock) {
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
template <class T, bool WITH_RESOLVER>
[[nodiscard]] std::size_t
Promise<T, WITH_RESOLVER>::Awaiters() const {
   std::shared_lock lock{this->mutex_};
   return this->awaiters_.size();
}

/**
 * @brief Get the total number of awaiter registrations on this promise.
 *
 * @return Total number of awaiter registrations.
 */
template <class T, bool WITH_RESOLVER>
[[nodiscard]] std::size_t
Promise<T, WITH_RESOLVER>::UseCount() const noexcept {
   return use_count_;
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
template <class T, bool WITH_RESOLVER>
template <class FUN, class... ARGS>
[[nodiscard]] constexpr ThenReturn<FUN>
Promise<T, WITH_RESOLVER>::Then(FUN&& func, ARGS&&... args) & {
   if (std::unique_lock ulock{this->mutex_}; this->IsDone(ulock)) {
      ulock.unlock();
      std::shared_lock lock{this->mutex_};

      if constexpr (!IS_PROMISE_FUNCTION<FUN>) {
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
                  return details::WPromise<T2>::Resolve(func(value, std::forward<ARGS>(args)...));
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
   } else if constexpr (IS_PROMISE_FUNCTION<FUN>) {
      // Promise return type
      using T2 = return_t<return_t<FUN>>;

      auto [promise, resolve, reject] = promise::Create<T2>();
      Await(
        [this,
         func     = std::forward<FUN>(func),
         resolve  = std::move(resolve),
         reject   = std::move(reject),
         ... args = std::forward<ARGS>(args)] mutable {
           auto promise =
             [this, func = std::move(func), ... args = std::forward<ARGS>(args)] constexpr mutable {
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
                 return std::move(promise).Then(
                   [resolve = std::move(resolve)](T2 const& value) constexpr { (*resolve)(value); }
                 );
              }
           }()
             .Catch([reject = std::move(reject)](std::exception_ptr exception) {
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
template <class T, bool WITH_RESOLVER>
template <class FUN, class... ARGS>
[[nodiscard]] constexpr ThenReturn<FUN>
Promise<T, WITH_RESOLVER>::Then(std::shared_ptr<Promise>&& self, FUN&& func, ARGS&&... args) && {
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
template <class T, bool WITH_RESOLVER>
template <class FUN, class... ARGS>
[[nodiscard]] constexpr CatchReturn<T, FUN>
Promise<T, WITH_RESOLVER>::Catch(FUN&& func, ARGS&&... args) & {
   // Promise return type
   using promise_t = return_t<decltype(std::function{func})>;
   using T2 = std::conditional_t<IS_PROMISE_FUNCTION<FUN>, return_or_void_t<promise_t>, promise_t>;
   using FArgs = args_t<decltype(std::function{func})>;
   static_assert(std::tuple_size_v<FArgs> == 1, "Catch promise must have exactly one argument!");
   using Exception = std::tuple_element_t<0, FArgs>;

   static constexpr bool IS_EXC_PTR =
     std::is_same_v<std::remove_cvref_t<Exception>, std::exception_ptr>;

   using ReturnType = return_t<CatchReturn<T, FUN>>;

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
               if constexpr (IS_PROMISE_FUNCTION<FUN> && std::is_lvalue_reference_v<Exception>) {
                  // Copy exception to avoid dangling reference if func captures it by
                  // reference and is called after this scope
                  using invoke_promise_t =
                    std::invoke_result_t<decltype(invoke)&, Exception const&>;
                  using invoke_value_t = return_t<invoke_promise_t>;

                  return MakePromise(
                    [ex, invoke = std::move(invoke)]() mutable
                      -> details::IPromise<invoke_value_t, false> { co_return co_await invoke(ex); }
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
         if constexpr (IS_PROMISE_FUNCTION<FUN>) {
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
         return std::move(promise).Then([resolve = std::move(resolve)](T2 const& value) constexpr {
            (*resolve)(value);
         });
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

            if constexpr (IS_PROMISE_FUNCTION<FUN>) {
               resolve_wrapper(apply_exception(exception), std::move(resolve))
                 .Catch([reject](std::exception_ptr exception) { (*reject)(std::move(exception)); })
                 .Detach();
            } else {
               if constexpr (
                 std::is_void_v<
                   std::invoke_result_t<decltype(apply_exception), decltype((exception))>>
               ) {
                  apply_exception(exception);
                  (*resolve)();
               } else {
                  (*resolve)(apply_exception(exception));
               }
            }
         } else if constexpr (
           std::is_void_v<std::invoke_result_t<decltype(apply_value), decltype((lock))>>
         ) {
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

                    if constexpr (IS_PROMISE_FUNCTION<FUN>) {
                       resolve_wrapper(apply_exception(exception), std::move(resolve))
                         .Catch([reject = std::move(reject)](std::exception_ptr exception) {
                            (*reject)(std::move(exception));
                         })
                         .Detach();
                    } else {
                       (void)resolve_wrapper;

                       if constexpr (
                         std::is_void_v<
                           std::invoke_result_t<decltype(apply_exception), decltype(exception)>>
                       ) {
                          apply_exception(exception);
                          (*resolve)();
                       } else {
                          (*resolve)(apply_exception(exception));
                       }
                    }
                 } else {
                    if constexpr (
                      std::is_void_v<std::invoke_result_t<decltype(apply_value), decltype((lock))>>
                    ) {
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
template <class T, bool WITH_RESOLVER>
template <class FUN, class... ARGS>
[[nodiscard]] constexpr CatchReturn<T, FUN>
Promise<T, WITH_RESOLVER>::Catch(std::shared_ptr<Promise>&& self, FUN&& func, ARGS&&... args) && {
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
template <class T, bool WITH_RESOLVER>
template <class FUN, class... ARGS>
[[nodiscard]] constexpr FinallyReturn<T>
Promise<T, WITH_RESOLVER>::Finally(FUN&& func) & {
   auto apply_value = [](auto&& resolve, auto&& reject, auto&& func, auto&&... value) constexpr {
      static_assert(sizeof...(value) == (IS_VOID ? 0 : 1));

      if constexpr (IS_PROMISE_FUNCTION<FUN>) {
         if constexpr (std::is_void_v<T>) {
            MakePromise(std::forward<FUN>(func))
              .Then([resolve = std::move(resolve)]() constexpr { (*resolve)(); })
              .Catch([reject =
                        std::forward<decltype(reject)>(reject)](std::exception_ptr exception) {
                 (*reject)(std::move(exception));
              })
              .Detach();
         } else {
            MakePromise(std::forward<FUN>(func))
              .Then([resolve = std::move(resolve),
                     value   = std::tuple{std::forward<decltype(value)>(value)...}]() constexpr {
                 (*resolve)(std::get<0>(value));
              })
              .Catch([reject =
                        std::forward<decltype(reject)>(reject)](std::exception_ptr exception) {
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
               (*resolve)(std::get<0>(std::tuple{std::forward<decltype(value)>(value)...}));
            }
         } catch (...) {
            (*reject)(std::current_exception());
         }
      }
   };

   auto apply_exception = [](auto&& reject, auto&& func, std::exception_ptr exception) {
      if constexpr (IS_PROMISE_FUNCTION<FUN>) {
         MakePromise(std::forward<FUN>(func))
           .Then([reject    = std::forward<decltype(reject)>(reject),
                  exception = std::move(exception)]() constexpr {
              (*reject)(std::move(exception));
           })
           .Catch([reject = std::forward<decltype(reject)>(reject)](std::exception_ptr exception) {
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

         if constexpr (IS_PROMISE_FUNCTION<FUN>) {
            apply_exception(std::move(reject), std::forward<FUN>(func), exception);
         } else {
            apply_exception(std::move(reject), std::forward<FUN>(func), exception);
         }
      } else if constexpr (IS_VOID) {
         apply_value(std::move(resolve), std::move(reject), std::forward<FUN>(func));
      } else {
         auto const& value = this->GetValue(lock);
         lock.unlock();

         apply_value(std::move(resolve), std::move(reject), std::forward<FUN>(func), value);
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
              assert(
                false
                && "This shall not throw since we're already handling "
                   "exceptions, but just in "
                   "case..."
              );

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
template <class T, bool WITH_RESOLVER>
template <class FUN>
[[nodiscard]] constexpr FinallyReturn<T>
Promise<T, WITH_RESOLVER>::Finally(std::shared_ptr<Promise>&& self, FUN&& func) && {
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
template <class T, bool WITH_RESOLVER>
template <class T2>
[[nodiscard]] constexpr WPromise<T2>
Promise<T, WITH_RESOLVER>::Race(
  WPromise<T2>&&                               race_promise,
  std::shared_ptr<promise::Resolve<T2>> const& resolve,
  std::shared_ptr<promise::Reject> const&      reject
) & {
   if (race_promise.Done()) {
      {
         std::unique_lock lock{this->mutex_};
         ++use_count_;
         this->cv_.notify_all();
      }
      // If the race promise is already done, we can skip registering a
      // continuation and just return it directly
      return std::move(race_promise);
   }

   auto const handle =
     [this, resolve, reject](std::shared_lock<std::shared_mutex>& lock) constexpr {
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
template <class T, bool WITH_RESOLVER>
template <class T2>
[[nodiscard]] constexpr WPromise<T2>
Promise<T, WITH_RESOLVER>::Race(
  std::shared_ptr<Promise>&&                   self,
  WPromise<T2>&&                               race_promise,
  std::shared_ptr<promise::Resolve<T2>> const& resolve,
  std::shared_ptr<promise::Reject> const&      reject
) && {
   assert(self);
   ScopeExit _{[&]() { std::move(*this).Detach(std::move(self)); }};
   return self->Race(std::move(race_promise), resolve, reject);
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
template <class T, bool WITH_RESOLVER>
constexpr std::tuple<details::WPromise<T>, std::shared_ptr<Resolve<T>>, std::shared_ptr<Reject>>
Promise<T, WITH_RESOLVER>::Create() {
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
template <class T, bool WITH_RESOLVER>
template <class... ARGS>
constexpr WPromise<T>
Promise<T, WITH_RESOLVER>::Resolve(ARGS&&... args) {
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
template <class T, bool WITH_RESOLVER>
template <class... ARGS>
constexpr WPromise<T>
Promise<T, WITH_RESOLVER>::Reject(ARGS&&... args) {
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
template <class T, bool WITH_RESOLVER>
template <class EXCEPTION, class... ARGS>
constexpr WPromise<T>
Promise<T, WITH_RESOLVER>::Reject(ARGS&&... args) {
   auto [promise, _, reject] = Promise<T, true>::Create();
   (*reject)(std::make_exception_ptr(EXCEPTION(std::forward<ARGS>(args)...)));
   return std::move(promise);
}

/**
 * @brief Create a promise from a callable and optional resolver.
 **
 * @tparam RPROMISE Boolean flag indicating whether to return a tuple with
 *resolver and rejector.
 * @tparam FUN Type of the callable.
 * @tparam ARGS Types of arguments to forward to the callable.
 **
 * @param func Callable used to produce the promise.
 * @param args Arguments forwarded to the callable.
 *
 * @return Promise or tuple when RPROMISE is true.
 */
template <class T, bool WITH_RESOLVER>
template <bool RPROMISE, class FUN, class... ARGS>
#if defined(__clang__)
__attribute__((no_sanitize("address")))
#endif
constexpr std::conditional_t<
  RPROMISE,
  std::tuple<
    details::WPromise<T>,
    std::shared_ptr<promise::Resolve<T>>,
    std::shared_ptr<promise::Reject>>,
  details::WPromise<T>>
Promise<T, WITH_RESOLVER>::Create(FUN&& func, ARGS&&... args) {
   static_assert(
     IS_PROMISE_FUNCTION<FUN>,
     "Create only supports callables that return promises, not "
     "promise wrappers!"
   );
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

   WPromise promise = [&]() constexpr {
      if constexpr (std::tuple_size_v<all_args_t<FUN>> >= 2) {
         if constexpr (
           IS_RESOLVER<std::tuple_element_t<0, all_args_t<FUN>>>
           && IS_REJECTOR<std::tuple_element_t<1, all_args_t<FUN>>>
         ) {
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
      return std::make_tuple(std::move(promise), std::move(resolve), std::move(reject));
   } else {
      return promise;
   }
}

}  // namespace promise::details

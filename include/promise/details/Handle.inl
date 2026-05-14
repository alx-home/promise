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

#include "core/Handle.h"

#include <utils/Scoped.h>
#include <cassert>

namespace promise {

/**
 * @brief Get the return object for this promise.
 *
 * @return Promise details reference.
 */
template <class T, bool WITH_RESOLVER>
details::IPromise<T, WITH_RESOLVER>
Handle<T, WITH_RESOLVER>::PromiseType::get_return_object() {
   return details::IPromise<T, WITH_RESOLVER>{handle_type::from_promise(*this)};
}

/**
 * @brief Get the parent promise details.
 *
 * @return Parent promise details pointer.
 */
template <class T, bool WITH_RESOLVER>
typename Handle<T, WITH_RESOLVER>::PromiseType::Parent*
Handle<T, WITH_RESOLVER>::PromiseType::GetParent() const {
   return parent_;
}

template <class T, bool WITH_RESOLVER>
[[nodiscard]] constexpr bool
Handle<T, WITH_RESOLVER>::PromiseType::InitSuspend::await_ready() const noexcept {
   return false;
}

/**
 * @brief Suspend the coroutine at initial suspension.
 *
 * @param h Coroutine handle.
 */
template <class T, bool WITH_RESOLVER>
constexpr void
Handle<T, WITH_RESOLVER>::PromiseType::InitSuspend::await_suspend(
  std::coroutine_handle<> h
) const noexcept {
   (void)h;
}

/**
 * @brief Complete initial suspend and attach a resolver.
 * @note Best practice: create resolver promises via MakePromise/MakeRPromise.
 */
template <class T, bool WITH_RESOLVER>
constexpr void
Handle<T, WITH_RESOLVER>::PromiseType::InitSuspend::await_resume() const noexcept(false) {
   if (!self_.resolver_) {
      throw std::runtime_error("Promise with resolver must be created with MakePromise");
   }
}

/**
 * @brief Initial suspend hook.
 *
 * @return Awaitable initial suspend object.
 */
template <class T, bool WITH_RESOLVER>
typename Handle<T, WITH_RESOLVER>::PromiseType::InitSuspend
Handle<T, WITH_RESOLVER>::PromiseType::initial_suspend() {
   assert(this->parent_);
   return {.self_ = *this->parent_};
}

/**
 * @brief Final suspend hook.
 *
 * @return Final suspend awaitable.
 */
template <class T, bool WITH_RESOLVER>
typename Handle<T, WITH_RESOLVER>::PromiseType::FinalSuspend
Handle<T, WITH_RESOLVER>::PromiseType::final_suspend() noexcept {
   ScopeExit _{[this] constexpr {
      std::unique_lock lock{parent_->mutex_};
      assert(parent_);
      parent_->handle_   = nullptr;
      parent_->function_ = nullptr;
      parent_->cv_.notify_all();

      if (parent_->IsDone(lock)) {
         parent_->OnResolved(lock);
      }
   }};

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
            parent_->resolver_->Resolve(std::move(*std::get<std::unique_ptr<T>>(delayed_return)));
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
template <class T, bool WITH_RESOLVER>
void
Handle<T, WITH_RESOLVER>::PromiseType::unhandled_exception() {
   assert(!delayed_return_);

   delayed_return_ = std::current_exception();
}

template <class T, bool WITH_RESOLVER>
template <class FUN>
   requires(IS_PROMISE_FUNCTION<FUN>)
details::WPromise<return_t<return_t<FUN>>>
Handle<T, WITH_RESOLVER>::PromiseType::await_transform(FUN&& fun) {
   // If the awaitable is a promise function, we create a new promise for it and return it as
   // an awaitable.
   return std::forward<FUN>(fun);
}

template <class T, bool WITH_RESOLVER>
template <class FUN>
   requires(!IS_PROMISE_FUNCTION<FUN>)
FUN&&
Handle<T, WITH_RESOLVER>::PromiseType::await_transform(FUN&& fun) {
   // If the await_transform is not a promise function, we just forward it as a normal
   // awaitable.
   return std::forward<FUN>(fun);
}

/**
 * @brief Construct a promise handle from a coroutine handle.
 *
 * @param handle Coroutine handle to manage.
 */
template <class T, bool WITH_RESOLVER>
Handle<T, WITH_RESOLVER>::Handle(handle_type handle)
   : handle_{std::move(handle)} {
   if (handle_) {
      handle_.promise().parent_ = static_cast<details::Promise<T, WITH_RESOLVER>*>(this);
   }
   // handle_ can be null in case of Catch through
}

/**
 * @brief Check if resolved using an existing lock.
 *
 * @param lock Active lock for thread-safe access.
 *
 * @return True if resolved.
 */
template <class T, bool WITH_RESOLVER>
template <class SELF>
[[nodiscard]] bool
Handle<T, WITH_RESOLVER>::IsResolved(this SELF&& self, Lock lock) {
   return self.ValuePromise::IsResolved(lock);
}

/**
 * @brief Check if done using an existing lock.
 *
 * @param lock Active lock for thread-safe access.
 *
 * @return True if resolved or rejected.
 */
template <class T, bool WITH_RESOLVER>
template <class SELF>
[[nodiscard]] bool
Handle<T, WITH_RESOLVER>::IsDone(this SELF&& self, Lock lock) {
   assert(self.resolver_);
   return self.ValuePromise::IsResolved(lock) || (self.resolver_->exception_ != nullptr);
}

/**
 * @brief Wait for the promise to be resolved or rejected.
 */
template <class T, bool WITH_RESOLVER>
void
Handle<T, WITH_RESOLVER>::WaitDone() {
   std::shared_lock lock{mutex_};
   cv_.wait(lock, [this, &lock]() constexpr { return this->IsDone(lock) && this->Done(lock); });
}

/**
 * @brief Wait for the promise to be awaited.
 *
 * @param current_count Optional current use count to wait from a specific point.
 */
template <class T, bool WITH_RESOLVER>
template <class SELF>
void
Handle<T, WITH_RESOLVER>::WaitAwaited(
  this SELF const&           self,
  std::optional<std::size_t> current_count
) {
   std::shared_lock lock{self.mutex_};

   auto const last_count = current_count ? *current_count : self.use_count_.load();

   self.cv_.wait(lock, [&self, last_count]() constexpr { return self.use_count_ > last_count; });
}

/**
 * @brief Check if rejected using an existing lock.
 *
 * @param lock Active lock for thread-safe access.
 *
 * @return True if rejected.
 */
template <class T, bool WITH_RESOLVER>
template <class SELF>
[[nodiscard]] bool
Handle<T, WITH_RESOLVER>::IsRejected(this SELF&& self, Lock) {
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
template <class T, bool WITH_RESOLVER>
template <class SELF>
[[nodiscard]] bool
Handle<T, WITH_RESOLVER>::IsRejected(this SELF&& self) {
   std::shared_lock lock{self.mutex_};
   return self.IsRejected(lock);
}

/**
 * @brief Check if done.
 *
 * @return True if resolved or rejected.
 */
template <class T, bool WITH_RESOLVER>
template <class SELF>
[[nodiscard]] bool
Handle<T, WITH_RESOLVER>::IsDone(this SELF&& self) {
   std::shared_lock lock{self.mutex_};
   return self.IsDone(lock);
}

/**
 * @brief Resume awaiters once resolved.
 *
 * @param lock Active lock for thread-safe access.
 */
template <class T, bool WITH_RESOLVER>
template <class SELF>
void
Handle<T, WITH_RESOLVER>::OnResolved(this SELF&& self, std::unique_lock<std::shared_mutex>& lock) {
   // If not Done, will be done on final_suspends
   if (self.Done(lock)) {
      self.cv_.notify_all();
      std::vector<typename std::remove_cvref_t<SELF>::Awaiter> awaiters{};

      self.awaiters_.swap(awaiters);
      assert(self.awaiters_.empty());

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
template <class T, bool WITH_RESOLVER>
details::Promise<T, WITH_RESOLVER>&
Handle<T, WITH_RESOLVER>::Detach(std::shared_ptr<details::Promise<T, WITH_RESOLVER>>&& self) && {
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

template <class T, bool WITH_RESOLVER>
void
Handle<T, WITH_RESOLVER>::VDetach() && {
   assert(false);
}

template <class T, bool WITH_RESOLVER>
Handle<T, WITH_RESOLVER>::~Handle() {
   assert(!this->handle_);
}

/**
 * @brief Check if the coroutine handle is cleared.
 *
 * @param lock Active lock for thread-safe access.
 *
 * @return True if the coroutine is complete.
 */
template <class T, bool WITH_RESOLVER>
[[nodiscard]] bool
Handle<T, WITH_RESOLVER>::Done(Lock lock) const {
   (void)lock;
   return handle_ == nullptr;
}

}  // namespace promise

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
#include <memory>

namespace promise {

/**
 * @brief Type-erased promise interface, useful for storing promises in pointers.
 */
class VPromise {
public:
   VPromise()                               = default;
   VPromise(VPromise const&)                = default;
   VPromise(VPromise&&) noexcept            = default;
   VPromise& operator=(VPromise const&)     = default;
   VPromise& operator=(VPromise&&) noexcept = default;

   virtual ~VPromise() = default;

   class Awaitable {
   public:
      Awaitable()          = default;
      virtual ~Awaitable() = default;

      Awaitable(Awaitable const&)                = default;
      Awaitable& operator=(Awaitable const&)     = default;
      Awaitable(Awaitable&&) noexcept            = default;
      Awaitable& operator=(Awaitable&&) noexcept = default;

      /**
       * @brief Check if the await can complete synchronously.
       *
       * @return True if ready to resume.
       */
      virtual bool await_ready() const = 0;
      /**
       * @brief Resume the await.
       */
      virtual void await_resume() const = 0;
      /**
       * @brief Suspend the coroutine and register continuation.
       *
       * @param h Awaiting coroutine handle.
       */
      virtual bool await_suspend(std::coroutine_handle<> h) const = 0;
   };

   /**
    * @brief Detach the promise from this handle.
    */
   virtual void VDetach() && = 0;

   /**
    * @brief Access a type-erased awaitable.
    */
   virtual Awaitable& VAwait() = 0;
};

/**
 * @brief Owning pointer to a type-erased promise.
 */
using Pointer = std::unique_ptr<VPromise>;

}  // namespace promise

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

#include "Pool.h"

namespace promise {

/** @brief A message queue that executes functions on a single thread, returning promises for their
 * results. */
class MessageQueue : public Pool<1> {
public:
   using Pool<1>::Pool;
   ~MessageQueue() override = default;

   using Pool<1>::Dispatch;
   using Pool<1>::Stop;

   /** @brief Dispatch a function to be executed on the pool's thread, returning a promise for its
    * result.
    *
    * @tparam ARGS Types of the arguments to be passed to the function.
    * @param args Arguments to be passed to the function.
    * @return Promise that resolves with the function's return value or rejects if the queue is
    * stopped.
    */
   template <class... ARGS>
   [[nodiscard]] WPromise<void> Ensure(ARGS&&... args) const noexcept {
      return Dispatch(std::forward<ARGS>(args)...);
   }

   /** @brief Gets the ID of the thread running the message queue.
    *
    * @return The ID of the thread running the message queue.
    */
   [[nodiscard]] std::thread::id ThreadId() const;
};

}  // namespace promise
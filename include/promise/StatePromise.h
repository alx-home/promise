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

#include "CVPromise.h"

/** @brief A promise that represents the state of an operation, allowing waiting for readiness or
 * completion. */
class StatePromise {
public:
   StatePromise() = default;

   StatePromise(StatePromise const&)                = delete;
   StatePromise(StatePromise&&) noexcept            = delete;
   StatePromise& operator=(StatePromise const&)     = delete;
   StatePromise& operator=(StatePromise&&) noexcept = delete;

   virtual ~StatePromise();

   /** @brief Waits for the promise to be ready.
    *
    * @return A promise that resolves when the state promise is ready.
    */
   [[nodiscard]] WPromise<void> WaitReady() const;

   /** @brief Waits for the promise to be done.
    *
    * @return A promise that resolves when the state promise is done.
    */
   [[nodiscard]] WPromise<void> WaitDone() const;

   /** @brief Waits for the promise to be either ready or done.
    *
    * @return A promise that resolves when the state promise is either ready or done.
    */
   [[nodiscard]] WPromise<void> Wait() const;

   /** @brief Marks the promise as ready. */
   void Ready();

   /** @brief Marks the promise as done. */
   void Done();

   /** @brief Resets the promise to its initial state, allowing it to be reused. */
   void Reset();

   /** @brief Checks if the promise is done.
    *
    * @return True if the promise is done.
    */
   [[nodiscard]] bool IsDone() const;

private:
   CVPromise done_promise_;
   CVPromise ready_promise_;
};
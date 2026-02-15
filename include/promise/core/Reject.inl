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
#include <exception>
#include <functional>
#include <memory>

namespace promise {

/**
 * @brief Rejector handle used to reject a promise with an exception.
 */
struct Reject : std::enable_shared_from_this<Reject> {
   /**
    * @brief Construct a rejector from an implementation callback.
    *
    * @param impl Callback invoked on reject.
    */
   Reject(std::function<void(std::exception_ptr)> impl);

   /**
    * @brief Reject the promise with an exception.
    *
    * @param exception Exception to store.
    *
    * @return True if this call rejected the promise, false if it was already rejected.
    */
   bool operator()(std::exception_ptr exception) const;
   /**
    * @brief Check whether this rejector can still reject.
    *
    * @return True if already rejected, false otherwise.
    */
   operator bool() const;

private:
   std::function<void(std::exception_ptr)> impl_;
   mutable std::atomic<bool>               rejected_{false};
};

}  // namespace promise

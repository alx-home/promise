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

#include "core/Memcheck.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <mutex>
#include <unordered_set>
#include <utils/Scoped.h>

namespace promise {

#ifdef PROMISE_MEMCHECK

/**
 * @brief Construct a Refcount for a promise pointer.
 *
 * @param ptr Pointer to the promise being tracked.
 */
Refcount::Refcount(VPromise* ptr) {
   ++counter;
#   ifdef PROMISE_MEMCHECK_FULL
   std::lock_guard lock{mutex};
   ptrs.emplace(ptr);
   ptr_ = ptr;
#   endif
}

/**
 * @brief Destructor that decrements the counter and removes the pointer from
 * tracking.
 */
Refcount::~Refcount() {
   --counter;
#   ifdef PROMISE_MEMCHECK_FULL
   std::lock_guard lock{mutex};
   ptrs.erase(ptr_);
#   endif
}

namespace mmchk {
Check::~Check() {
   auto const refcount = Refcount::counter.load();

   if (refcount) {
      std::cerr << "Promise: Leak memory detected (" << refcount << " unterminated promises)"
                << std::endl;
#   ifdef PROMISE_MEMCHECK_FULL
      auto const& ptrs = Refcount::ptrs;
      for (auto const& ptr : ptrs) {
         std::cout << "At addr: " << ptr << std::endl;
      }
#   endif
      assert(false);
   }
}
}  // namespace mmchk

/**
 * @brief Helper for memory leak detection in debug mode.
 * When enabled, this function returns a scope guard that checks for active
 * promises on destruction. If any active promises are detected, it reports the
 * number of leaks and their addresses.
 *
 * @note This should be used at the beginning of the main function to ensure it
 * runs for the entire program duration.
 *
 * @return Scope guard that checks for memory leaks on destruction.
 */
[[nodiscard]] mmchk::Check
Memcheck() {
   return mmchk::Check{};
}
#endif  // PROMISE_MEMCHECK

}  // namespace promise

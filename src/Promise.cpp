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

#include "details/Promise.inl"

namespace promise {

#ifdef PROMISE_MEMCHECK
std::atomic<std::size_t> Refcount::counter{0};
#   ifdef PROMISE_MEMCHECK_FULL
std::unordered_set<VPromise const*> Refcount::ptrs{};
std::mutex                          Refcount::mutex{};
#   endif
#endif  // PROMISE_MEMCHECK

Reject::Reject(std::function<void(std::exception_ptr)> impl)
   : impl_(std::move(impl)) {}

bool
Reject::operator()(std::exception_ptr exception) const {
   if (!rejected_.exchange(true)) {
      impl_(std::move(exception));
      return true;
   }

   return false;
}

Reject::operator bool() const { return rejected_; }

Resolve<void>::Resolve(std::function<void()> impl)
   : impl_(std::move(impl)) {}

bool
Resolve<void>::operator()() const {
   if (!resolved_.exchange(true)) {
      impl_();
      return true;
   }

   return false;
}

Resolve<void>::operator bool() const { return resolved_; }

}  // namespace promise

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

#include "../core/Reject.h"

#include <cassert>

namespace promise {

/**
 * @brief Reject the promise with an exception of type EXCEPTION constructed with ARGS.
 *
 * @tparam EXCEPTION Exception type to construct.
 * @tparam ARGS Constructor arguments for the exception.
 *
 * @param args Arguments forwarded to the exception constructor.
 *
 * @return True if this call rejected the promise, false if it was already rejected.
 */
template <class EXCEPTION>
bool
Reject::operator()(EXCEPTION&& exception) const {
   return (*this)(std::make_exception_ptr(std::forward<EXCEPTION>(exception)));
}

/**
 * @brief Reject the promise with an exception of type EXCEPTION constructed with ARGS.
 *
 * @tparam EXCEPTION Exception type to construct.
 * @tparam ARGS Constructor arguments for the exception.
 *
 * @param args Arguments forwarded to the exception constructor.
 *
 * @return True if this call rejected the promise, false if it was already rejected.
 */
template <class EXCEPTION, class... ARGS>
bool
Reject::Apply(ARGS&&... args) const {
   return (*this)(std::make_exception_ptr(EXCEPTION(std::forward<ARGS>(args)...)));
}

}  // namespace promise

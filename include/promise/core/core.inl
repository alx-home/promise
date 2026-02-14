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

#include "concepts.inl"
#include "helpers.inl"
#include "Resolve.inl"
#include "Reject.inl"
#include "VPromise.inl"

#include <cassert>
#include <stdexcept>

namespace promise {

/**
 * @brief Base exception type used by the promise helpers.
 */
struct Exception : std::runtime_error {
   using std::runtime_error::runtime_error;
};

/**
 * @brief Await all promises and return a combined result.
 * @param promise Promises to await.
 * @return Tuple of resolved values (std::nullopt_t for void).
 */
template <class... PROMISE>
static constexpr auto All(PROMISE&&... promise);
namespace details {
template <class T, bool WITH_RESOLVER>
class Promise;
}

template <class T, bool WITH_RESOLVER = true>
struct Resolver;
}  // namespace promise

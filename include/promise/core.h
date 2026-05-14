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

#include "core/WPromise.h"
#include "core/helpers.h"

/**
 * @brief Type alias for promise implementation details.
 *
 * @tparam T The value type that the promise resolves to (defaults to void).
 * @tparam WITH_RESOLVER Whether the promise uses an external resolver (defaults to false).
 *
 * This is an alias to the internal promise implementation details class.
 */
template <class T = void, bool WITH_RESOLVER = false>
using Promise = promise::details::IPromise<T, WITH_RESOLVER>;

/**
 * @brief Type alias for promise implementation with ownership.
 *
 * @tparam T The value type that the promise resolves to (defaults to void).
 *
 * This is a wrapper around the internal promise implementation that manages
 * shared ownership and provides awaitable semantics.
 */
template <class T = void>
using WPromise = promise::details::WPromise<T>;

/**
 * @brief Public resolve handle alias.
 */
template <class T = void>
using Resolve = promise::Resolve<T>;
/**
 * @brief Public reject handle alias.
 */
using Reject = promise::Reject;

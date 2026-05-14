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

#include "../core/Resolve.h"

#include <cassert>

namespace promise {

/**
 * @brief Construct a value resolver from an implementation callback.
 * @param resolver Shared resolver state.
 */
template <class T>
   requires(!std::is_void_v<T>)
Resolve<T>::Resolve(std::shared_ptr<Resolver<T>> resolver)
   : resolver_(std::move(resolver)) {}

/**
 * @brief Resolve the promise with a value.
 * @param value Value to resolve with.
 * @return True if this call resolved the promise, false if it was already resolved.
 */
template <class T>
   requires(!std::is_void_v<T>)
bool
Resolve<T>::operator()(T const& value) const {
   return resolver_->Resolve(value);
}

/**
 * @brief Check whether the resolver is still usable.
 * @return True if already resolved, false otherwise.
 */
template <class T>
   requires(!std::is_void_v<T>)
Resolve<T>::
operator bool() const {
   return resolver_->await_ready();
}

template <class T>
   requires(!std::is_void_v<T>)
std::shared_ptr<Resolve<T>>
Resolve<T>::Create(std::shared_ptr<Resolver<T>> resolver) {
   struct MakeSharedEnabler : public Resolve<T> {
      MakeSharedEnabler(std::shared_ptr<Resolver<T>> resolver)
         : Resolve<T>(std::move(resolver)) {}
   };
   return std::make_shared<MakeSharedEnabler>(std::move(resolver));
}

}  // namespace promise

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

#include "details/Resolve.inl"
#include "details/Promise.inl"
#include "details/Resolver.inl"

namespace promise {

/** @brief Creates a void resolver callback wrapper.
 *
 * @param resolver Shared resolver state used to resolve the promise.
 */
Resolve<void>::Resolve(std::shared_ptr<Resolver<void>> resolver)
   : resolver_(std::move(resolver)) {}

/** @brief Resolves the underlying void promise.
 *
 * @return true when resolution succeeds.
 */
bool
Resolve<void>::operator()() const {
   return resolver_->Resolve();
}

/** @brief Indicates whether the underlying resolver is done.
 *
 * @return true when the resolver reached a terminal state.
 */
Resolve<void>::
operator bool() const {
   return resolver_->Done();
}

std::shared_ptr<Resolve<void>>
Resolve<void>::Create(std::shared_ptr<Resolver<void>> resolver) {
   struct MakeSharedEnabler : public Resolve<void> {
      MakeSharedEnabler(std::shared_ptr<Resolver<void>> resolver)
         : Resolve<void>(std::move(resolver)) {}
   };
   return std::make_shared<MakeSharedEnabler>(std::move(resolver));
}

}  // namespace promise
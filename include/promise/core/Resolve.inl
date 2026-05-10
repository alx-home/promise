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
#include <functional>
#include <memory>
#include <type_traits>
#include <variant>

namespace promise {

template <class T>
class Resolver;

/**
 * @brief Resolver handle used to resolve a promise with a value.
 */
template <class T = void>
class Resolve;

template <>
/**
 * @brief Resolver for Promise<void>.
 */
class Resolve<void> : public std::enable_shared_from_this<Resolve<void>> {
private:
   /**
    * @brief Construct a void resolver from an implementation callback.
    *
    * @param impl Callback invoked on resolve.
    */
   Resolve(std::shared_ptr<Resolver<void>> resolver);

public:
   static std::shared_ptr<Resolve<void>> Create(std::shared_ptr<Resolver<void>> resolver) {
      struct MakeSharedEnabler : public Resolve<void> {
         MakeSharedEnabler(std::shared_ptr<Resolver<void>> resolver)
            : Resolve<void>(std::move(resolver)) {}
      };
      return std::make_shared<MakeSharedEnabler>(std::move(resolver));
   }

   /**
    * @brief Resolve the promise.
    *
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   bool operator()() const;

   /**
    * @brief Check whether this resolver can still resolve.
    *
    * @return True if already resolved, false otherwise.
    */
   operator bool() const;

private:
   std::shared_ptr<Resolver<void>> resolver_;
};

template <class T>
   requires(!std::is_void_v<T>)
/**
 * @brief Resolver for Promise<T>.
 */
class Resolve<T> : public std::enable_shared_from_this<Resolve<T>> {
private:
   /**
    * @brief Construct a value resolver from an implementation callback.
    *
    * @param impl Callback invoked on resolve.
    */
   Resolve(std::shared_ptr<Resolver<T>> resolver);

public:
   static constexpr std::shared_ptr<Resolve<T>> Create(std::shared_ptr<Resolver<T>> resolver) {
      struct MakeSharedEnabler : public Resolve<T> {
         MakeSharedEnabler(std::shared_ptr<Resolver<T>> resolver)
            : Resolve<T>(std::move(resolver)) {}
      };
      return std::make_shared<MakeSharedEnabler>(std::move(resolver));
   }

   /**
    * @brief Resolve the promise with a value.
    *
    * @param value Value to resolve with.
    *
    * @return True if this call resolved the promise, false if it was already resolved.
    */
   bool operator()(T const& value) const;

   /**
    * @brief Check whether this resolver can still resolve.
    *
    * @return True if already resolved, false otherwise.
    */
   operator bool() const;

private:
   std::shared_ptr<Resolver<T>> resolver_;
};

}  // namespace promise

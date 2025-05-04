/*
MIT License

Copyright (c) 2025 alx-home

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

#include "promise.h"

#include <Windows.h>
#include <WinUser.h>
#include <Windows.h>
#include <dwmapi.h>
#include <errhandlingapi.h>
#include <intsafe.h>
#include <minwindef.h>
#include <windef.h>
#include <winnt.h>
#include <winreg.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
int WINAPI
WinMain(HINSTANCE /*hInst*/, HINSTANCE /*hPrevInst*/, LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
#else
int
main() {
#endif
#ifdef PROMISE_MEMCHECK
   auto const _{promise::Memcheck()};
#endif

   // try {
#ifndef NDEBUG
   if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
      FILE* old = nullptr;
      freopen_s(&old, "CONOUT$", "w", stdout);
      freopen_s(&old, "CONOUT$", "w", stderr);
   }
#endif  // DEBUG
   {
      auto main_prom{MakePromise([]() -> Promise<void> {
         try {
            Resolve<int> const* resolver{};
            Reject const*       rejecter{};

            auto prom{MakePromise(
              [&resolver,
               &rejecter](Resolve<int> const& resolve, Reject const& reject) -> Promise<int, true> {
                 resolver = &resolve;
                 rejecter = &reject;
                 //   MakeReject<std::runtime_error>(*rejecter, "tutu");
                 co_return;
              }
            )};

            auto prom2{MakePromise([&]() -> Promise<int> {
               auto const result = ((co_await prom) + 1);
               // throw std::runtime_error("test");
               co_return result;
            })};

            auto prom_catch_through{
              MakePromise([&]() -> Promise<int> { co_return 0; })
                .Then([](int value) -> Promise<int> { co_return value + 3; })
                .Catch([](std::exception_ptr) -> Promise<void> { co_return; })
                .Then([](std::optional<int>) -> Promise<int> { co_return 0; })
                .Catch([](std::exception_ptr) -> Promise<int> { co_return 0; })
                .Then([](int) -> Promise<void> { co_return; })
                .Catch([](std::exception_ptr) -> Promise<int> { co_return 0; })
                .Then([](std::optional<int>) -> Promise<void> { co_return; })
                .Catch([](std::exception_ptr) -> Promise<void> { co_return; })
                .Then([]() -> Promise<void> { co_return; })
                .Then([]() -> Promise<int> { co_return 800; }),
            };

            auto prom_int{prom2
                            .Then([](int value) -> Promise<double> {
                               throw std::runtime_error("test");
                               co_return value + 3;
                            })
                            .Then([](double value) -> Promise<double> {
                               std::cout << "not evaluted" << std::endl;
                               co_return value;
                            })
                            .Catch([](std::exception_ptr) -> Promise<int> {
                               // throw std::runtime_error("test2");
                               std::cout << "test caught" << std::endl;
                               co_return 300;
                            })
                            .Then([](std::variant<int, double> value) -> Promise<double> {
                               std::cout << "test2 uncaught" << std::endl;
                               throw std::runtime_error("test3");
                               co_return std::holds_alternative<int>(value)
                                 ? std::get<int>(value) + 3
                                 : std::get<double>(value) + 8788;
                            })
                            .Catch([](std::exception_ptr) -> Promise<double> {
                               // throw std::runtime_error("test2");
                               std::cout << "test3 caught" << std::endl;
                               co_return 300;
                            })
                            .Then(
                              [](
                                Resolve<int> const& resolve, Reject const&, double value
                              ) -> Promise<int, true> {
                                 auto result = resolve(static_cast<int>(value) + 3);
                                 assert(result);
                                 co_return;
                              }
                            )};

            auto prom3{
              MakePromise([&](Resolve<int> const& resolve, Reject const&) -> Promise<int, true> {
                 try {
                    auto result = resolve((co_await prom2) + 5);
                    assert(result);
                 } catch (std::runtime_error const& e) {
                    std::cout << "PP3 " << e.what() << std::endl;
                    throw;
                 }
                 co_return;
              })
            };

            auto prom4{
              MakePromise([]() -> Promise<int> { co_return 999; })
                .Then([](int value) -> Promise<void> {
                   std::cout << value << std::endl;
                   co_return;
                })
                .Then([]() -> Promise<void> { co_return; })
                .Then([](Resolve<int> const& resolve, Reject const&) -> Promise<int, true> {
                   auto result = resolve(111);
                   assert(result);
                   co_return;
                })
                .Then(
                  [](
                    Resolve<void> const& resolve, Reject const&, int value
                  ) -> Promise<void, true> {
                     std::cout << value << std::endl;
                     auto result = resolve();
                     assert(result);
                     co_return;
                  }
                )
                .Then([]() -> Promise<int> { co_return 888; })
            };

            auto prom_ptr = MakePromise([&]() -> Promise<void> {
                               co_await prom4;
                               std::cout << "ok prom_ptr" << std::endl;
                               co_return;
                            }).ToPointer();

            auto promall{MakePromise([&]() -> Promise<void> {
               auto const [res1, res2, res3, res4, int_, _] =
                 co_await promise::All(prom, prom2, prom3, prom4, prom_int, prom_catch_through);
               co_await prom_ptr->VAwait();

               std::cout << res1 << " " << res2 << " " << res3 << " " << res4 << " " << int_
                         << std::endl;

               co_return;
            })};

            MakePromise([&]() -> Promise<void> {
               co_await promall;
               std::cout << "ok" << std::endl;
               co_return;
            }).Detach();

            std::this_thread::sleep_for(std::chrono::seconds(1));
            // MakeReject<std::runtime_error>(*rejecter, "titi");
            auto result = (*resolver)(5);
            assert(result);
            co_await promall;
         } catch (std::exception const& e) {
            std::cout << "exc? " << e.what() << std::endl;
         }

         co_return;
      })};
   }

   return 0;
}

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
#include <exception>

WPromise<void>
Test() {
   return MakePromise([]() -> Promise<void> { throw std::runtime_error("TEST_EXCEPTION"); });
}

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
            co_await Test();
         } catch (std::exception const& e) {
            std::cout << "TEST " << e.what() << std::endl;
         }

         try {
            std::shared_ptr<Resolve<int> const> resolver{};
            std::shared_ptr<Reject const>       rejecter{};

            auto prom{MakePromise(
              [&resolver,
               &rejecter](Resolve<int> const& resolve, Reject const& reject) -> Promise<int, true> {
                 resolver = resolve.shared_from_this();
                 rejecter = reject.shared_from_this();
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
                .Then([](std::optional<int> const&) -> Promise<int> { co_return 0; })
                .Catch([](std::exception_ptr) -> Promise<int> { co_return 0; })
                .Then([](int) -> Promise<void> { co_return; })
                .Catch([](std::exception_ptr) -> Promise<int> { co_return 0; })
                .Then([](std::optional<int> const&) -> Promise<void> { co_return; })
                .Catch([](std::exception_ptr) -> Promise<void> { co_return; })
                .Then([]() -> Promise<void> { co_return; })
                .Then([]() -> Promise<int> { co_return 800; }),
            };
            auto prom_catch_through2{
              MakePromise([&]() -> Promise<int> { co_return 0; })
                .Then([](int value) { return value + 3; })
                .Catch([](std::exception_ptr) {})
                .Then([](std::optional<int> const&) { return 0; })
                .Catch([](std::exception_ptr) { return 0; })
                .Then([](int) {})
                .Catch([](std::exception_ptr) { return 0; })
                .Then([](std::optional<int> const&) {})
                .Catch([](std::exception_ptr) {})
                .Then([]() {})
                .Then([]() { return 800; }),
            };

            auto [prom_Create, resolve, reject] = promise::Create<int>();

            auto prom_create_wait{MakePromise([&prom_Create]() -> Promise<int> {
               co_return co_await prom_Create;
            })};
            (*resolve)(888);

            std::cout << "Create " << co_await prom_create_wait << std::endl;

            struct FinalyExc : public std::exception {
               using std::exception::exception;
            };

            auto prom_int{
              prom2
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
                .Finally([]() { std::cout << "test finally" << std::endl; })
                .Then([](std::variant<int, double> const& value) -> Promise<double> {
                   std::cout << "test2 uncaught" << std::endl;
                   throw std::runtime_error("test3");
                   co_return std::holds_alternative<int>(value) ? std::get<int>(value) + 3
                                                                : std::get<double>(value) + 8788;
                })
                .Finally([]() { std::cout << "test3 finally" << std::endl; })
                .Catch([](std::exception_ptr) -> Promise<double> {
                   // throw std::runtime_error("test2");
                   std::cout << "test3 caught" << std::endl;
                   co_return 300;
                })
                .Finally([]() { std::cout << "test3 finally" << std::endl; })
                .Finally([]() {
                   std::cout << "test3 finally" << std::endl;
                   throw FinalyExc();
                })
                .Finally([]() { std::cout << "test4 finally" << std::endl; })
                .Catch([](FinalyExc const&) -> Promise<double> {
                   // throw std::runtime_error("test2");
                   std::cout << "test4 caught" << std::endl;
                   co_return 300;
                })
                .Then([](std::variant<int, double> const& value) -> Promise<double> {
                   std::cout << "test5 uncaught" << std::endl;
                   throw std::runtime_error("test5");
                   co_return std::holds_alternative<int>(value) ? std::get<int>(value) + 3
                                                                : std::get<double>(value) + 8788;
                })
                .Finally([]() {
                   std::cout << "test5 finally" << std::endl;
                   throw FinalyExc("test55");
                })
                .Catch([](FinalyExc const&) -> Promise<double> {
                   // throw std::runtime_error("test2");
                   std::cout << "test55 caught" << std::endl;
                   co_return 300;
                })
                .Then([](Resolve<int> const& resolve, double value) -> Promise<int, true> {
                   [[maybe_unused]] auto result = resolve(static_cast<int>(value) + 3);
                   assert(result);
                   co_return;
                })
            };

            auto prom3{
              MakePromise([&](Resolve<int> const& resolve, Reject const&) -> Promise<int, true> {
                 try {
                    [[maybe_unused]] auto result = resolve((co_await prom2) + 5);
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
                   [[maybe_unused]] auto result = resolve(111);
                   assert(result);
                   co_return;
                })
                .Then(
                  [](
                    Resolve<void> const& resolve, Reject const&, int value
                  ) -> Promise<void, true> {
                     std::cout << value << std::endl;
                     [[maybe_unused]] auto result = resolve();
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
               auto const [res1, res2, res3, res4, int_, catch_through, catch_through2] =
                 co_await promise::All(
                   prom, prom2, prom3, prom4, prom_int, prom_catch_through, prom_catch_through2
                 );
               co_await prom_ptr->VAwait();

               std::cout << res1 << " " << res2 << " " << res3 << " " << res4 << " " << int_ << " "
                         << catch_through << " " << catch_through2 << std::endl;

               co_return;
            })};

            MakePromise([&]() -> Promise<void> {
               co_await promall;
               std::cout << "ok" << std::endl;
               co_return;
            }).Detach();

            auto const prom_catch_asyn{
              MakePromise([&]() -> Promise<void> {
                 throw std::runtime_error("test async");
                 co_return;
              }).Catch([&](std::runtime_error const& exc) -> Promise<std::string> {
                 co_await promall;
                 co_return exc.what();
              })
            };

            std::this_thread::sleep_for(std::chrono::seconds(1));
            // MakeReject<std::runtime_error>(*rejecter, "titi");
            [[maybe_unused]] auto result = (*resolver)(5);
            assert(result);
            co_await promall;

            std::cout << "prom_catch_asyn " << *(co_await prom_catch_asyn) << std::endl;
            {
               auto [prom_race_delay, resolve2, _] = Promise<void>::Create();
               auto prom_race1 = MakePromise([&prom_race_delay]() -> Promise<double> {
                  co_await prom_race_delay;
                  throw std::runtime_error("test race1");
                  co_return 2;
               });
               auto prom_race2 = MakePromise([&prom_race_delay]() -> Promise<double> {
                  co_await prom_race_delay;
                  co_return 2;
               });
               auto prom_race3 = MakePromise([&prom_race_delay]() -> Promise<int> {
                  co_await prom_race_delay;
                  co_return 3;
               });

               auto prom_race4 =
                 promise::Race(prom_race1, prom_race2, prom_race3)
                   .Catch([](std::runtime_error const& exception) {
                      std::cout << "race1 exception: " << exception.what() << std::endl;
                   })
                   .Then([](std::optional<std::variant<double, int>> const& value) {
                      if (value.has_value()) {
                         if (std::holds_alternative<int>(*value)) {
                            std::cout << "race1 int " << std::get<int>(*value) << std::endl;
                         } else {
                            std::cout << "race1 double " << std::get<double>(*value) << std::endl;
                         }
                      }
                   });
               (*resolve2)();

               co_await promise::All(prom_race1, prom_race2, prom_race3)
                 .Catch([](std::runtime_error const& exception) {
                    std::cout << "race1 all exception: " << exception.what() << std::endl;
                 });
            }

            {
               auto prom_race1 = MakePromise([]() -> Promise<int> { co_return 1; });
               auto prom_race2 = MakePromise([]() -> Promise<double> { co_return 2; });
               auto prom_race3 = MakePromise([]() -> Promise<int> { co_return 3; });

               co_await promise::Race(prom_race1, prom_race2, prom_race3)
                 .Then([](std::variant<int, double> const& value) {
                    if (std::holds_alternative<int>(value)) {
                       std::cout << "race2 int " << std::get<int>(value) << std::endl;
                    } else {
                       std::cout << "race2 double " << std::get<double>(value) << std::endl;
                    }
                 });

               co_await promise::All(prom_race1, prom_race2, prom_race3);
            }
            {
               auto prom_race1 = MakePromise([]() -> Promise<int> { co_return 1; });
               auto prom_race2 = MakePromise([]() -> Promise<double> {
                  throw std::runtime_error("test race3");
                  co_return 2;
               });
               auto prom_race3 = MakePromise([]() -> Promise<int> { co_return 3; });

               co_await promise::Race(prom_race1, prom_race2, prom_race3)
                 .Catch([](std::runtime_error const& exception) {
                    std::cout << "race3 exception: " << exception.what() << std::endl;
                 })
                 .Then([](std::optional<std::variant<int, double>> const& value) {
                    if (value.has_value()) {
                       if (std::holds_alternative<int>(*value)) {
                          std::cout << "race3 int " << std::get<int>(*value) << std::endl;
                       } else {
                          std::cout << "race3 double " << std::get<double>(*value) << std::endl;
                       }
                    } else {
                       std::cout << "race3 no value" << std::endl;
                    }
                 });

               co_await promise::All(prom_race1, prom_race2, prom_race3)
                 .Catch([](std::runtime_error const& exception) {
                    std::cout << "race3 all exception: " << exception.what() << std::endl;
                 });
            }

            auto const prom_Create1 = MakePromise([] { return 42; });
            std::cout << "prom_Create1 " << co_await prom_Create1 << std::endl;

            auto const prom_Create2 = MakePromise([](Resolve<int> const& resolve) { resolve(42); });
            std::cout << "prom_Create2 " << co_await prom_Create2 << std::endl;

            auto const prom_Create3 = MakePromise([](Resolve<int> const&, Reject const& reject) {
                                         reject.Apply<std::runtime_error>("test");
                                      }).Catch([](std::runtime_error const& exception) {
               std::cout << "prom_Create3 exception: " << exception.what() << std::endl;
            });

            auto const prom_Create4 = MakePromise([](Resolve<int> const&) {
                                         throw std::runtime_error("test");
                                      }).Catch([](std::runtime_error const& exception) {
               std::cout << "prom_Create4 exception: " << exception.what() << std::endl;
            });

            auto const prom_Create5 = MakePromise([]() {
                                         throw std::runtime_error("test");
                                      }).Catch([](std::runtime_error const& exception) {
               std::cout << "prom_Create5 exception: " << exception.what() << std::endl;
            });

            try {
               co_await Test();
            } catch (std::exception const& e) {
               std::cout << "exc3? " << e.what() << std::endl;
            }
         } catch (std::exception const& e) {
            std::cout << "exc? " << e.what() << std::endl;
         }

         co_return;
      })};

      assert(main_prom.Done());
   }

   return 0;
}

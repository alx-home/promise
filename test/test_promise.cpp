#include "TestCommon.h"
#include <utils/Pool.h>

TEST_CASE("Promise resolves with value", "[Promise][Core]") {
   auto p = Promise<int>::Resolve(42);

   REQUIRE(p.Done());
   REQUIRE(p.Resolved());
   REQUIRE_FALSE(p.Rejected());
   REQUIRE(p.Value() == 42);
}

TEST_CASE("Promise resolves with void", "[Promise][Core]") {
   auto p = Promise<void>::Resolve();

   REQUIRE(p.Done());
   REQUIRE(p.Resolved());
   REQUIRE_FALSE(p.Rejected());
}

TEST_CASE("Promise rejects with exception", "[Promise][Core]") {
   auto p = Promise<int>::Reject<TestError>("boom");

   REQUIRE(p.Done());
   REQUIRE_FALSE(p.Resolved());
   REQUIRE(p.Rejected());
   RequireException<TestError>(p.Exception());
}

TEST_CASE("Then transforms a resolved value", "[Promise][Core]") {
   auto p = Promise<int>::Resolve(21).Then([](int value) { return value * 2; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 42);
}

TEST_CASE("Then can chain coroutine promises", "[Promise][Core]") {
   auto p = Promise<int>::Resolve(21).Then([](int value) -> Promise<int> { co_return value * 2; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 42);
}

TEST_CASE("Catch recovers from rejection", "[Promise][Core]") {
   auto p =
     Promise<int>::Reject<TestError>("boom").Catch([](std::exception_ptr const&) { return 42; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 42);
}

TEST_CASE("Catch preserves exception when rethrowing", "[Promise][Core]") {
   auto p = Promise<int>::Reject<TestError>("boom").Catch([](std::exception_ptr exception) -> int {
      std::rethrow_exception(exception);
   });

   REQUIRE(p.Rejected());
   RequireException<TestError>(p.Exception());
}

TEST_CASE("Then Catch Then passes through when no exception is raised", "[Promise][Core]") {
   auto p = Promise<int>::Resolve(4).Then(
                                      [](int value) { return value + 1; }
   ).Catch([](std::exception_ptr const&) {
       return 0;
    }).Then([](int value) { return value * 2; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 10);
}

TEST_CASE("Then Catch Then recovers when the first Then throws", "[Promise][Core]") {
   auto p = Promise<int>::Resolve(4)
              .Then([](int) -> int { throw TestError("then failed"); })
              .Catch([](TestError const& exception) {
                 REQUIRE(std::string{exception.what()} == "then failed");
                 return 7;
              })
              .Then([](int value) { return value * 3; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 21);
}

TEST_CASE("Then Catch Then stays rejected when Catch rethrows", "[Promise][Core]") {
   auto p = Promise<int>::Resolve(4).Then(
                                      [](int) -> int { throw TestError("then failed"); }
   ).Catch([](std::exception_ptr exception) -> int {
       std::rethrow_exception(exception);
    }).Then([](int value) { return value * 2; });

   REQUIRE(p.Rejected());
   RequireException<TestError>(p.Exception());
}

TEST_CASE("Then Catch Then stays rejected when Catch throws a new exception", "[Promise][Core]") {
   auto p = Promise<int>::Reject<TestError>("initial failure")
              .Then([](int value) { return value + 1; })
              .Catch([](TestError const& exception) -> int {
                 REQUIRE(std::string{exception.what()} == "initial failure");
                 throw CatchError("catch failed");
              })
              .Then([](int value) { return value * 2; });

   REQUIRE(p.Rejected());
   RequireException<CatchError>(p.Exception());
}

TEST_CASE("Then Catch Then stays rejected when the final Then throws", "[Promise]") {
   auto p = Promise<int>::Reject<TestError>("initial failure")
              .Then([](int value) { return value + 1; })
              .Catch([](TestError const& exception) {
                 REQUIRE(std::string{exception.what()} == "initial failure");
                 return 5;
              })
              .Then([](int) -> int { throw FinalThenError("final then failed"); });

   REQUIRE(p.Rejected());
   RequireException<FinalThenError>(p.Exception());
}

TEST_CASE("Async catch-through chain preserves old promise_test behavior", "[Promise]") {
   auto p = WPromise{[]() -> Promise<int> { co_return 0; }}
              .Then([](int value) -> Promise<int> { co_return value + 3; })
              .Catch([](std::exception_ptr) -> Promise<void> { co_return; })
              .Then([](std::optional<int> const&) -> Promise<int> { co_return 0; })
              .Catch([](std::exception_ptr) -> Promise<int> { co_return 0; })
              .Then([](int) -> Promise<void> { co_return; })
              .Catch([](std::exception_ptr) -> Promise<int> { co_return 0; })
              .Then([](std::optional<int> const&) -> Promise<void> { co_return; })
              .Catch([](std::exception_ptr) -> Promise<void> { co_return; })
              .Then([]() -> Promise<void> { co_return; })
              .Then([]() -> Promise<int> { co_return 800; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 800);
}

TEST_CASE("Sync catch-through chain preserves old promise_test behavior", "[Promise]") {
   auto p = WPromise{[]() -> Promise<int> { co_return 0; }}
              .Then([](int value) { return value + 3; })
              .Catch([](std::exception_ptr) {})
              .Then([](std::optional<int> const&) { return 0; })
              .Catch([](std::exception_ptr) { return 0; })
              .Then([](int) {})
              .Catch([](std::exception_ptr) { return 0; })
              .Then([](std::optional<int> const&) {})
              .Catch([](std::exception_ptr) {})
              .Then([]() {})
              .Then([]() { return 800; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 800);
}

TEST_CASE("Complex Then Catch Finally chain matches old promise_test flow", "[Promise]") {
   auto p = Promise<int>::Resolve(6)
              .Then([](int value) -> Promise<double> {
                 throw TestError("test");
                 co_return value + 3;
              })
              .Then([](double value) -> Promise<double> { co_return value; })
              .Catch([](std::exception_ptr) -> Promise<int> { co_return 300; })
              .Finally([]() {})
              .Then([](std::variant<int, double> const&) -> Promise<double> {
                 throw CatchError("test3");
                 co_return 0.0;
              })
              .Finally([]() {})
              .Catch([](std::exception_ptr) -> Promise<double> { co_return 300; })
              .Finally([]() {})
              .Finally([]() { throw FinallyError("test55"); })
              .Catch([](FinallyError const&) -> Promise<double> { co_return 300; })
              .Then([](Resolve<int> const& resolve, double value) -> Promise<int, true> {
                 REQUIRE(resolve(static_cast<int>(value) + 3));
                 co_return;
              });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 303);
}

TEST_CASE("Finally runs on resolved promise", "[Promise]") {
   bool cleanup_ran = false;

   auto p = Promise<int>::Resolve(7).Finally([&cleanup_ran]() { cleanup_ran = true; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 7);
   REQUIRE(cleanup_ran);
}

TEST_CASE("Finally runs on rejected promise", "[Promise]") {
   bool cleanup_ran = false;

   auto p =
     Promise<int>::Reject<TestError>("boom").Finally([&cleanup_ran]() { cleanup_ran = true; });

   REQUIRE(p.Rejected());
   REQUIRE(cleanup_ran);
   RequireException<TestError>(p.Exception());
}

TEST_CASE("promise::Create resolves externally", "[Promise]") {
   auto [p, resolve, reject] = promise::Create<int>();

   REQUIRE_FALSE(p.Done());
   REQUIRE_FALSE(*reject);

   REQUIRE((*resolve)(99));
   REQUIRE_FALSE((*resolve)(100));
   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 99);
}

TEST_CASE("promise::Create rejects externally", "[Promise]") {
   auto [p, resolve, reject] = promise::Create<int>();

   REQUIRE_FALSE(p.Done());
   REQUIRE((*reject)(std::make_exception_ptr(TestError{"boom"})));
   REQUIRE_FALSE((*resolve)(99));
   REQUIRE(p.Rejected());
   RequireException<TestError>(p.Exception());
}

TEST_CASE("MakePromise wraps synchronous and coroutine callables", "[Promise]") {
   auto sync      = MakePromise([] { return 12; });
   auto coroutine = MakePromise([]() -> Promise<int> { co_return 30; });

   REQUIRE(sync.Resolved());
   REQUIRE(sync.Value() == 12);
   REQUIRE(coroutine.Resolved());
   REQUIRE(coroutine.Value() == 30);
}

TEST_CASE("Direct coroutine promises resolve", "[Promise]") {
   auto p = MakePromise(CoroutineValue, 15);
   auto v = MakePromise(CoroutineVoid);

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 15);
   REQUIRE(v.Resolved());
}

TEST_CASE("MessageQueue exposes a running worker thread", "[Pool]") {
   promise::MessageQueue queue{"TestQueue"};
   REQUIRE(queue.ThreadId() != std::thread::id{});
}

TEST_CASE("Dispatch after Stop throws QueueStopped", "[Pool]") {
   promise::MessageQueue queue{"StoppedQueue"};
   queue.Stop();
   auto promise = queue.Dispatch([] {});
   REQUIRE(promise.Rejected());
   RequireException<QueueStopped>(promise.Exception());
}

TEST_CASE("Direct construction and throw of QueueStopped", "[Pool]") {
   REQUIRE_THROWS_AS(([] { throw QueueStopped{"DirectTest"}; }()), QueueStopped);
}

TEST_CASE("Explicit Pool::GetName instantiations", "[Pool]") {
   auto check_pool_name = []<bool ASYNC, std::size_t SIZE>(char const* name) {
      Pool<ASYNC, SIZE> pool{name};
      REQUIRE(std::string{pool.GetName()} == name);
      pool.Stop();
   };

   check_pool_name.template operator()<false, 63>("Pool63");
   check_pool_name.template operator()<true, 64>("Pool64");
}

TEST_CASE("Reject callback ignores duplicate invocation", "[Promise][Reject]") {
   auto [promise, resolve, reject] = promise::Create<int>();

   REQUIRE((*reject)(std::make_exception_ptr(TestError{"first reject"})));
   REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"second reject"})));
   REQUIRE_FALSE((*resolve)(5));

   REQUIRE(promise.Rejected());
   RequireException<TestError>(promise.Exception());
}

TEST_CASE("Resolve<void> bool reflects resolver completion", "[Promise][Resolve]") {
   auto [promise, resolve, reject] = promise::Create<void>();

   REQUIRE_FALSE(static_cast<bool>(*resolve));
   REQUIRE((*resolve)());
   REQUIRE(static_cast<bool>(*resolve));
   REQUIRE_FALSE((*resolve)());
   REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));

   REQUIRE(promise.Resolved());
}

TEST_CASE("WPromise static helpers and detach APIs", "[Promise][WPromise]") {
   auto resolved = WPromise<int>::Resolve(9);
   REQUIRE(resolved.Resolved());
   REQUIRE(resolved.Value() == 9);

   auto rejected = WPromise<int>::Reject<TestError>("wreject");
   REQUIRE(rejected.Rejected());
   RequireException<TestError>(rejected.Exception());

   auto [pending, resolve, reject] = WPromise<int>::Create();
   REQUIRE_FALSE(pending.Done());
   REQUIRE((*resolve)(12));
   REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));
   REQUIRE(pending.Resolved());
   REQUIRE(pending.Value() == 12);

   auto detached = WPromise{[]() -> Promise<void> { co_return; }};
   std::move(detached).Detach();

   auto vdetached = WPromise{[]() -> Promise<void> { co_return; }};
   std::move(vdetached).VDetach();
}

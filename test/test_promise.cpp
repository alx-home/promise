#if defined(__clang__)
#   pragma clang diagnostic ignored "-Wc2y-extensions"
#endif

#include <catch2/catch_test_macros.hpp>

#include <promise/CVPromise.h>
#include <promise/MessageQueue.h>
#include <promise/Pool.h>
#include <promise/StatePromise.h>
#include <promise/promise.h>

#include <atomic>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <variant>

namespace {
using namespace std::chrono_literals;

struct TestError : std::runtime_error {
   using std::runtime_error::runtime_error;
};

struct CatchError : std::runtime_error {
   using std::runtime_error::runtime_error;
};

struct FinalThenError : std::runtime_error {
   using std::runtime_error::runtime_error;
};

struct FinallyError : std::runtime_error {
   using std::runtime_error::runtime_error;
};

template <class EXCEPTION>
void
RequireException(std::exception_ptr const& exception) {
   REQUIRE(exception != nullptr);
   REQUIRE_THROWS_AS(std::rethrow_exception(exception), EXCEPTION);
}

Promise<int>
CoroutineValue(int value) {
   co_return value;
}

Promise<void>
CoroutineVoid() {
   co_return;
}

}  // namespace

TEST_CASE("Promise resolves with value", "[promise]") {
   auto p = Promise<int>::Resolve(42);

   REQUIRE(p.Done());
   REQUIRE(p.Resolved());
   REQUIRE_FALSE(p.Rejected());
   REQUIRE(p.Value() == 42);
}

TEST_CASE("Promise resolves with void", "[promise]") {
   auto p = Promise<void>::Resolve();

   REQUIRE(p.Done());
   REQUIRE(p.Resolved());
   REQUIRE_FALSE(p.Rejected());
}

TEST_CASE("Promise rejects with exception", "[promise]") {
   auto p = Promise<int>::Reject<TestError>("boom");

   REQUIRE(p.Done());
   REQUIRE_FALSE(p.Resolved());
   REQUIRE(p.Rejected());
   RequireException<TestError>(p.Exception());
}

TEST_CASE("Then transforms a resolved value", "[promise]") {
   auto p = Promise<int>::Resolve(21).Then([](int value) { return value * 2; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 42);
}

TEST_CASE("Then can chain coroutine promises", "[promise]") {
   auto p = Promise<int>::Resolve(21).Then([](int value) -> Promise<int> { co_return value * 2; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 42);
}

TEST_CASE("Catch recovers from rejection", "[promise]") {
   auto p =
     Promise<int>::Reject<TestError>("boom").Catch([](std::exception_ptr const&) { return 42; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 42);
}

TEST_CASE("Catch preserves exception when rethrowing", "[promise]") {
   auto p = Promise<int>::Reject<TestError>("boom").Catch([](std::exception_ptr exception) -> int {
      std::rethrow_exception(exception);
   });

   REQUIRE(p.Rejected());
   RequireException<TestError>(p.Exception());
}

TEST_CASE("Then Catch Then passes through when no exception is raised", "[promise]") {
   auto p = Promise<int>::Resolve(4).Then(
                                      [](int value) { return value + 1; }
   ).Catch([](std::exception_ptr const&) {
       return 0;
    }).Then([](int value) { return value * 2; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 10);
}

TEST_CASE("Then Catch Then recovers when the first Then throws", "[promise]") {
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

TEST_CASE("Then Catch Then stays rejected when Catch rethrows", "[promise]") {
   auto p = Promise<int>::Resolve(4).Then(
                                      [](int) -> int { throw TestError("then failed"); }
   ).Catch([](std::exception_ptr exception) -> int {
       std::rethrow_exception(exception);
    }).Then([](int value) { return value * 2; });

   REQUIRE(p.Rejected());
   RequireException<TestError>(p.Exception());
}

TEST_CASE("Then Catch Then stays rejected when Catch throws a new exception", "[promise]") {
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

TEST_CASE("Then Catch Then stays rejected when the final Then throws", "[promise]") {
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

TEST_CASE("Async catch-through chain preserves old promise_test behavior", "[promise]") {
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

TEST_CASE("Sync catch-through chain preserves old promise_test behavior", "[promise]") {
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

TEST_CASE("Complex Then Catch Finally chain matches old promise_test flow", "[promise]") {
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

TEST_CASE("Finally runs on resolved promise", "[promise]") {
   bool cleanup_ran = false;

   auto p = Promise<int>::Resolve(7).Finally([&cleanup_ran]() { cleanup_ran = true; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 7);
   REQUIRE(cleanup_ran);
}

TEST_CASE("Finally runs on rejected promise", "[promise]") {
   bool cleanup_ran = false;

   auto p =
     Promise<int>::Reject<TestError>("boom").Finally([&cleanup_ran]() { cleanup_ran = true; });

   REQUIRE(p.Rejected());
   REQUIRE(cleanup_ran);
   RequireException<TestError>(p.Exception());
}

TEST_CASE("promise::Create resolves externally", "[promise]") {
   auto [p, resolve, reject] = promise::Create<int>();

   REQUIRE_FALSE(p.Done());
   REQUIRE_FALSE(*reject);

   REQUIRE((*resolve)(99));
   REQUIRE_FALSE((*resolve)(100));
   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 99);
}

TEST_CASE("promise::Create rejects externally", "[promise]") {
   auto [p, resolve, reject] = promise::Create<int>();

   REQUIRE_FALSE(p.Done());
   REQUIRE((*reject)(std::make_exception_ptr(TestError{"boom"})));
   REQUIRE_FALSE((*resolve)(99));
   REQUIRE(p.Rejected());
   RequireException<TestError>(p.Exception());
}

TEST_CASE("MakePromise wraps synchronous and coroutine callables", "[promise]") {
   auto sync      = MakePromise([] { return 12; });
   auto coroutine = MakePromise([]() -> Promise<int> { co_return 30; });

   REQUIRE(sync.Resolved());
   REQUIRE(sync.Value() == 12);
   REQUIRE(coroutine.Resolved());
   REQUIRE(coroutine.Value() == 30);
}

TEST_CASE("Direct coroutine promises resolve", "[promise]") {
   auto p = MakePromise(CoroutineValue, 15);
   auto v = MakePromise(CoroutineVoid);

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 15);
   REQUIRE(v.Resolved());
}

TEST_CASE("co_await propagates promise exceptions like the old driver", "[promise]") {
   auto p = WPromise{[]() -> Promise<std::string> {
      try {
         co_await WPromise{[]() -> Promise<void> {
            throw TestError("TEST_EXCEPTION");
            co_return;
         }};
      } catch (TestError const& exception) {
         co_return std::string{exception.what()};
      }

      co_return std::string{"unexpected"};
   }};

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == "TEST_EXCEPTION");
}

TEST_CASE("Direct WPromise constructors cover resolve reject and throw paths", "[promise]") {
   WPromise resolved{[] { return 42; }};
   WPromise resolved_with_resolver{[](Resolve<int> const& resolve) { REQUIRE(resolve(42)); }};

   auto rejected =
     WPromise{[](Resolve<int> const&, Reject const& reject) {
        reject.Apply<TestError>("reject path");
     }}.Catch([](TestError const& exception) { return std::string{exception.what()}; });

   auto throwing_resolver =
     WPromise{[](Resolve<int> const&) {
        throw TestError("resolver throw");
     }}.Catch([](TestError const& exception) { return std::string{exception.what()}; });

   auto throwing_plain =
     WPromise{[]() -> int {
        throw TestError("plain throw");
     }}.Catch([](TestError const& exception) { return std::string{exception.what()}; });

   REQUIRE(resolved.Resolved());
   REQUIRE(resolved.Value() == 42);

   REQUIRE(resolved_with_resolver.Resolved());
   REQUIRE(resolved_with_resolver.Value() == 42);

   REQUIRE(rejected.Resolved());
   REQUIRE(std::holds_alternative<std::string>(rejected.Value()));
   REQUIRE(std::get<std::string>(rejected.Value()) == "reject path");

   REQUIRE(throwing_resolver.Resolved());
   REQUIRE(std::holds_alternative<std::string>(throwing_resolver.Value()));
   REQUIRE(std::get<std::string>(throwing_resolver.Value()) == "resolver throw");

   REQUIRE(throwing_plain.Resolved());
   REQUIRE(std::holds_alternative<std::string>(throwing_plain.Value()));
   REQUIRE(std::get<std::string>(throwing_plain.Value()) == "plain throw");
}

TEST_CASE(
  "Resolver-style WPromise can be resolved after dependent chains are created",
  "[promise]"
) {
   std::shared_ptr<Resolve<int> const> resolver;
   std::shared_ptr<Reject const>       rejecter;

   WPromise prom{
     [&resolver,
      &rejecter](Resolve<int> const& resolve, Reject const& reject) -> Promise<int, true> {
        resolver = resolve.shared_from_this();
        rejecter = reject.shared_from_this();
        co_return;
     }
   };

   auto prom2 = WPromise{[&]() -> Promise<int> {
      auto const result = (co_await prom) + 1;
      co_return result;
   }};

   REQUIRE(resolver);
   REQUIRE(rejecter);
   REQUIRE((*resolver)(5));
   REQUIRE(prom2.Resolved());
   REQUIRE(prom2.Value() == 6);
}

TEST_CASE("Then supports resolver-based continuations across value and void", "[promise]") {
   auto p =
     WPromise{[]() -> Promise<int> { co_return 999; }}
       .Then([](int value) -> Promise<void> {
          REQUIRE(value == 999);
          co_return;
       })
       .Then([]() -> Promise<void> { co_return; })
       .Then([](Resolve<int> const& resolve, Reject const&) -> Promise<int, true> {
          REQUIRE(resolve(111));
          co_return;
       })
       .Then([](Resolve<void> const& resolve, Reject const&, int value) -> Promise<void, true> {
          REQUIRE(value == 111);
          REQUIRE(resolve());
          co_return;
       })
       .Then([]() -> Promise<int> { co_return 888; });

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == 888);
}

TEST_CASE("Asynchronous Catch can await another promise before recovering", "[promise]") {
   auto [gate, resolve, reject] = promise::Create<void>();

   auto p = WPromise{[]() -> Promise<void> {
               throw TestError("test async");
               co_return;
            }}
              .Catch([&](TestError const& exception) -> Promise<std::string> {
                 co_await gate;
                 co_return std::string{exception.what()};
              })
              .Then([](std::optional<std::string> const& value) { return value.value(); });

   REQUIRE_FALSE(p.Done());
   REQUIRE((*resolve)());
   REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));

   REQUIRE(p.Done());

   REQUIRE(p.Resolved());
   REQUIRE(p.Value() == "test async");
}

TEST_CASE("All combines non-void results and skips void", "[promise]") {
   auto all = promise::All(
     Promise<int>::Resolve(4),
     Promise<void>::Resolve(),
     Promise<std::string>::Resolve(std::string{"ok"})
   );

   REQUIRE(all.Resolved());

   auto const [number, text] = all.Value();
   REQUIRE(number == 4);
   REQUIRE(text == "ok");
}

TEST_CASE("All rejects when any member rejects", "[promise]") {
   auto all = promise::All(Promise<int>::Resolve(4), Promise<int>::Reject<TestError>("boom"));

   REQUIRE(all.Rejected());
   RequireException<TestError>(all.Exception());
}

TEST_CASE("Race returns the first resolved alternative", "[promise]") {
   auto [pending, resolve, reject] = promise::Create<std::string>();
   auto raced                      = promise::Race(Promise<int>::Resolve(7), pending);

   REQUIRE(raced.Resolved());
   REQUIRE(std::holds_alternative<int>(raced.Value()));
   REQUIRE(std::get<int>(raced.Value()) == 7);

   REQUIRE((*resolve)("late"));
   REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));
}

TEST_CASE("Race rejection flows through Catch into optional variant", "[promise]") {
   auto [delay, resolve, reject] = promise::Create<void>();

   auto raced = promise::Race(
                  WPromise{[]() -> Promise<double> {
                     throw TestError("race failure");
                     co_return 0.0;
                  }},
                  WPromise{[&delay]() -> Promise<double> {
                     co_await delay;
                     co_return 2.0;
                  }},
                  WPromise{[&delay]() -> Promise<int> {
                     co_await delay;
                     co_return 3;
                  }}
   )
                  .Catch([](TestError const& exception) {
                     REQUIRE(std::string{exception.what()} == "race failure");
                  })
                  .Then([](std::optional<std::variant<double, int>> const& value) {
                     return !value.has_value();
                  });

   REQUIRE(raced.Resolved());
   REQUIRE(raced.Value());

   REQUIRE((*resolve)());
   REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));
}

TEST_CASE("CVPromise notifies, rejects, and destructor behavior", "[promise]") {
   CVPromise cv;

   auto initial = cv.Wait();
   REQUIRE_FALSE(initial.Done());

   cv.Notify();
   REQUIRE(initial.Done());
   REQUIRE(initial.Resolved());

   bool     caught = false;
   WPromise waiter{[&]() -> Promise<void> {
      try {
         co_await cv.Wait();
      } catch (const TestError&) {
         caught = true;
      }
      co_return;
   }};
   cv.Reject<TestError>("boom");
   REQUIRE(waiter.Done());
   REQUIRE(caught);

   // Test destructor rejects outstanding waiters
   bool     end_caught = false;
   auto     temp_cv    = std::make_unique<CVPromise>();
   WPromise waiter2{[&]() -> Promise<void> {
      try {
         co_await temp_cv->Wait();
      } catch (const CVPromise::End&) {
         end_caught = true;
      }
      co_return;
   }};
   REQUIRE(!waiter2.Done());
   REQUIRE(!end_caught);

   temp_cv = nullptr;  // Destroy the CVPromise, should cause waiter to catch End

   REQUIRE(waiter2.Done());
   REQUIRE(end_caught);
}

TEST_CASE("StatePromise tracks ready, done, and reject transitions", "[promise]") {
   StatePromise state;

   auto wait_any   = state.Wait();
   auto wait_ready = state.WaitReady();

   state.Ready();
   REQUIRE(wait_any.Done());
   REQUIRE(wait_ready.Done());

   REQUIRE(wait_any.Resolved());
   REQUIRE(wait_ready.Resolved());
   REQUIRE_FALSE(state.IsDone());

   auto wait_done = state.WaitDone();
   state.Done();
   REQUIRE(wait_done.Done());

   REQUIRE(wait_done.Resolved());
   REQUIRE(state.IsDone());

   // Test WaitDoneWithReject
   StatePromise state2;
   auto         wait_done_reject = state2.WaitDoneWithReject();
   state2.Done();
   REQUIRE(wait_done_reject.Done());
   REQUIRE(wait_done_reject.Rejected());
   RequireException<StatePromise::End>(wait_done_reject.Exception());

   // Test WaitWithReject: resolves if Ready, rejects if Done
   StatePromise state3;
   auto         wait_with_reject = state3.WaitWithReject();
   state3.Ready();
   REQUIRE(wait_with_reject.Done());
   REQUIRE(wait_with_reject.Resolved());

   StatePromise state4;
   auto         wait_with_reject2 = state4.WaitWithReject();
   state4.Done();
   REQUIRE(wait_with_reject2.Done());
   REQUIRE(wait_with_reject2.Rejected());
   RequireException<StatePromise::End>(wait_with_reject2.Exception());

   // Test destructor calls Done
   bool     end_caught_state = false;
   auto     temp_state       = std::make_unique<StatePromise>();
   WPromise waiter_state{[&]() -> Promise<void> {
      try {
         co_await temp_state->WaitDoneWithReject();
      } catch (const StatePromise::End&) {
         end_caught_state = true;
      }
      co_return;
   }};
   REQUIRE(!waiter_state.Done());
   REQUIRE(!end_caught_state);

   temp_state = nullptr;  // Destroy the StatePromise, should cause waiter to catch End

   REQUIRE(waiter_state.Done());
   REQUIRE(end_caught_state);
}

TEST_CASE("MessageQueue dispatch runs work and resolves on queue thread", "[promise]") {
   promise::MessageQueue queue{"promise-test"};
   std::atomic<bool>     ran_on_queue_thread{false};

   auto flow = WPromise{[&]() -> Promise<void> {
      auto dispatched = queue.Dispatch([&queue, &ran_on_queue_thread]() {
         ran_on_queue_thread.store(std::this_thread::get_id() == queue.ThreadId());
         return 42;
      });

      auto value = co_await dispatched;

      REQUIRE(dispatched.Done());
      REQUIRE(dispatched.Resolved());
      REQUIRE(value == 42);
      REQUIRE(dispatched.Value() == 42);
      REQUIRE(ran_on_queue_thread.load());

      co_return;
   }};

   flow.WaitDone();
   REQUIRE(flow.Resolved());
   queue.Stop();
}

TEST_CASE("ToPointer exposes type-erased awaiting", "[promise]") {
   auto pointer = WPromise{[]() -> Promise<void> { co_return; }}.ToPointer();

   auto waiter = WPromise{[pointer]() -> Promise<int> {
      co_await pointer->VAwait();
      co_return 1;
   }};

   REQUIRE(waiter.Done());

   REQUIRE(waiter.Resolved());
   REQUIRE(waiter.Value() == 1);
}

TEST_CASE("Pending Then registration updates awaiter counters", "[promise]") {
   auto [source, resolve, reject] = promise::Create<int>();

   REQUIRE(source.Awaiters() == 0);
   REQUIRE(source.UseCount() == 0);

   auto chained = source.Then([](int value) { return value + 1; });

   source.WaitAwaited(0);
   REQUIRE(source.Awaiters() == 1);
   REQUIRE(source.UseCount() >= 1);

   REQUIRE((*resolve)(10));
   REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));

   REQUIRE(chained.Resolved());
   REQUIRE(chained.Value() == 11);
   REQUIRE(source.Awaiters() == 0);
}

TEST_CASE("Pending Catch registration recovers once rejected", "[promise]") {
   auto [source, resolve, reject] = promise::Create<int>();

   auto recovered = source.Catch([](TestError const& exception) {
      REQUIRE(std::string{exception.what()} == "pending failure");
      return 42;
   });

   source.WaitAwaited(0);
   REQUIRE(source.Awaiters() == 1);

   REQUIRE((*reject)(std::make_exception_ptr(TestError{"pending failure"})));
   REQUIRE_FALSE((*resolve)(7));

   REQUIRE(recovered.Resolved());
   REQUIRE(recovered.Value() == 42);
}

TEST_CASE("MakePromise supports resolver-first synchronous callables", "[promise]") {
   auto resolved = MakePromise([](Resolve<int> const& resolve) { REQUIRE(resolve(55)); });

   auto rejected = MakePromise([](Resolve<int> const&, Reject const& reject) {
      reject.Apply<TestError>("reject through resolver callable");
   });

   REQUIRE(resolved.Resolved());
   REQUIRE(resolved.Value() == 55);

   REQUIRE(rejected.Rejected());
   RequireException<TestError>(rejected.Exception());
}

// TEST_CASE("MakeRPromise returns external resolve reject handles", "[promise]") {
//    auto [promise, resolve, reject] =
//      MakeRPromise([](Resolve<int> const&, Reject const&) -> Promise<int, true> { co_return; });

//    REQUIRE_FALSE(promise.Done());
//    REQUIRE((*resolve)(101));
//    REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));

//    REQUIRE(promise.Resolved());
//    REQUIRE(promise.Value() == 101);
// }

TEST_CASE("StatePromise reset restores pending transitions", "[promise]") {
   WPromise flow{[]() -> Promise<void> {
      StatePromise state;

      auto wait_ready = state.WaitReady();
      auto wait_done  = state.WaitDone();

      state.Ready();
      co_await wait_ready;
      REQUIRE(wait_ready.Resolved());
      REQUIRE_FALSE(wait_done.Done());

      state.Reset();
      auto wait_ready_after_reset = state.WaitReady();
      auto wait_done_after_reset  = state.WaitDone();

      REQUIRE_FALSE(wait_ready_after_reset.Done());
      REQUIRE_FALSE(wait_done_after_reset.Done());

      state.Done();
      co_await wait_done;
      co_await wait_done_after_reset;
      co_await wait_ready_after_reset;
      REQUIRE(state.IsDone());

      StatePromise state_ready_only;
      auto         ready_only_wait_ready = state_ready_only.WaitReady();
      auto         ready_only_wait_done  = state_ready_only.WaitDone();

      state_ready_only.Ready();
      co_await ready_only_wait_ready;
      REQUIRE_FALSE(ready_only_wait_done.Done());

      state_ready_only.Reset();
      auto ready_only_wait_ready_after_reset = state_ready_only.WaitReady();
      auto ready_only_wait_done_after_reset  = state_ready_only.WaitDone();

      REQUIRE_FALSE(ready_only_wait_ready_after_reset.Done());
      REQUIRE_FALSE(ready_only_wait_done_after_reset.Done());

      state_ready_only.Done();
      co_await ready_only_wait_done;
      co_await ready_only_wait_done_after_reset;
      co_await ready_only_wait_ready_after_reset;

      co_return;
   }};

   REQUIRE(flow.Resolved());
}

TEST_CASE("CVPromise conversion operators expose updated state", "[promise]") {
   CVPromise cv;

   REQUIRE_FALSE(cv.Resolved());
   REQUIRE_FALSE(cv.Rejected());

   auto wait_via_deref = *cv;
   cv.Notify();

   REQUIRE(wait_via_deref.Done());
   REQUIRE(wait_via_deref.Resolved());

   auto wait_via_conversion = static_cast<WPromise<void>>(cv);
   REQUIRE_FALSE(wait_via_conversion.Done());

   cv.Reject<TestError>("cv rejected");
   REQUIRE(wait_via_conversion.Rejected());
   RequireException<TestError>(wait_via_conversion.Exception());
}

TEST_CASE("Pool and MessageQueue reject dispatch after stop", "[promise]") {
   promise::Pool<2>      pool{"promise-pool-test"};
   promise::MessageQueue queue{"promise-queue-stopped"};

   WPromise flow{[&]() -> Promise<void> {
      auto pool_value = pool.Dispatch([] { return 9; });

      REQUIRE(co_await pool_value == 9);
      REQUIRE(pool_value.Resolved());
      REQUIRE(pool_value.Value() == 9);

      auto delayed = pool.Dispatch(1ms);
      co_await delayed;
      REQUIRE(delayed.Resolved());

      co_return;
   }};

   flow.WaitDone();
   REQUIRE(flow.Resolved());

   pool.Stop();
   auto stopped_pool_call = pool.Dispatch([] { return 3; });
   REQUIRE(stopped_pool_call.Rejected());

   queue.Stop();
   auto ensure_after_stop = queue.Ensure();
   REQUIRE(ensure_after_stop.Rejected());
}

TEST_CASE("Ensure co_await verifies thread ID consistency in MessageQueue", "[messagequeue]") {
   promise::MessageQueue queue{"promise-queue-ensure"};

   // Coroutine to test Ensure with co_await
   WPromise promise{[&]() -> Promise<void> {
      co_await queue.Ensure();
      REQUIRE(std::this_thread::get_id() == queue.ThreadId());
   }};

   // Run the coroutine
   promise.WaitDone();
   REQUIRE(promise.Resolved());
}

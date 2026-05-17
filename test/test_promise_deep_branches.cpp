
#include "TestCommon.h"

TEST_CASE("Make promise covers resolver signatures", "[Resolver][DeepBranches]") {
   RunWithTimeout(2s, [&] {
      auto with_resolve_and_reject =
        MakePromise([](Resolve<int> const& resolve, Reject const&) -> Promise<int, true> {
           REQUIRE(resolve(11));
           co_return;
        });
      REQUIRE(with_resolve_and_reject.Resolved());
      REQUIRE(with_resolve_and_reject.Value() == 11);

      auto with_resolve_and_arg = MakePromise(
        [](Resolve<int> const& resolve, int base) -> Promise<int, true> {
           REQUIRE(resolve(base + 1));
           co_return;
        },
        20
      );
      REQUIRE(with_resolve_and_arg.Resolved());
      REQUIRE(with_resolve_and_arg.Value() == 21);

      auto promise_arg = MakePromise([](int base) -> Promise<int> { co_return base * 2; }, 12);
      REQUIRE(promise_arg.Resolved());
      REQUIRE(promise_arg.Value() == 24);

      auto promise_no_arg = MakePromise([]() -> Promise<int> { co_return 7; });
      REQUIRE(promise_no_arg.Resolved());
      REQUIRE(promise_no_arg.Value() == 7);
   });
}

TEST_CASE("Race covers void and optional branches", "[Promise][DeepBranches]") {
   RunWithTimeout(2s, [&] {
      auto [pending_int, resolve_int, reject_int] = promise::Create<int>();
      auto raced_optional = promise::Race(Promise<void>::Resolve(), pending_int);
      REQUIRE(raced_optional.Resolved());
      REQUIRE_FALSE(raced_optional.Value().has_value());
      REQUIRE((*resolve_int)(15));
      REQUIRE_FALSE((*reject_int)(std::make_exception_ptr(TestError{"ignored"})));

      auto [pending_void, resolve_void, reject_void] = promise::Create<void>();
      auto raced_void = promise::Race(Promise<void>::Resolve(), pending_void);
      REQUIRE(raced_void.Resolved());
      REQUIRE((*resolve_void)());
      REQUIRE_FALSE((*reject_void)(std::make_exception_ptr(TestError{"ignored"})));
   });
}

TEST_CASE(
  "Catch with promise returning handlers preserves passthrough",
  "[Promise][DeepBranches]"
) {
   RunWithTimeout(2s, [&] {
      auto void_source =
        Promise<void>::Resolve().Catch([](TestError const&) -> Promise<void> { co_return; });
      REQUIRE(void_source.Resolved());

      auto value_source =
        Promise<int>::Resolve(66).Catch([](TestError const&) -> Promise<void> { co_return; });
      REQUIRE(value_source.Resolved());
      REQUIRE(value_source.Value().has_value());
      REQUIRE(value_source.Value().value() == 66);
   });
}

TEST_CASE(
  "Finally with promise returning handlers preserves values and exceptions",
  "[Promise][DeepBranches]"
) {
   RunWithTimeout(2s, [&] {
      auto resolved_value = Promise<int>::Resolve(5).Finally([]() -> Promise<void> { co_return; });
      REQUIRE(resolved_value.Resolved());
      REQUIRE(resolved_value.Value() == 5);

      auto resolved_void = Promise<void>::Resolve().Finally([]() -> Promise<void> { co_return; });
      REQUIRE(resolved_void.Resolved());
   });
}

TEST_CASE("Catch rejected paths produce expected optional shapes", "[Promise][DeepBranches]") {
   RunWithTimeout(2s, [&] {
      auto rejected_value_to_void =
        Promise<int>::Reject<TestError>("drop value").Catch([](TestError const&) {});
      REQUIRE(rejected_value_to_void.Resolved());
      REQUIRE_FALSE(rejected_value_to_void.Value().has_value());

      auto rejected_void_to_value =
        Promise<void>::Reject<TestError>("supply value").Catch([](TestError const&) {
           return 303;
        });
      REQUIRE(rejected_void_to_value.Resolved());
      REQUIRE(rejected_void_to_value.Value().has_value());
      REQUIRE(rejected_void_to_value.Value().value() == 303);
   });
}

TEST_CASE("WPromise create and v detach type erased paths", "[Promise][DeepBranches]") {
   RunWithTimeout(2s, [&] {
      auto [created, resolve, reject] = WPromise<int>::Create();
      REQUIRE_FALSE(created.Done());
      REQUIRE((*resolve)(404));
      REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));
      REQUIRE(created.Resolved());
      REQUIRE(created.Value() == 404);

      auto [pending, resolve_pending, reject_pending] = WPromise<int>::Create();
      auto erased = std::move(pending).ToPointer<promise::VPromise>();
      std::move(*erased).VDetach();
      REQUIRE((*resolve_pending)(505));
      REQUIRE_FALSE((*reject_pending)(std::make_exception_ptr(TestError{"ignored"})));
   });
}

TEST_CASE("Member race covers void target and rejection forwarding", "[Promise][DeepBranches]") {
   RunWithTimeout(2s, [&] {
      auto [void_race, resolve_void_race, reject_void_race] = promise::Create<void>();
      auto source_value                                     = Promise<int>::Resolve(1);
      auto race_void_result =
        source_value.Race(std::move(void_race), resolve_void_race, reject_void_race);
      REQUIRE(race_void_result.Resolved());
      REQUIRE_FALSE((*resolve_void_race)());
      REQUIRE_FALSE((*reject_void_race)(std::make_exception_ptr(TestError{"ignored"})));

      auto [pending_race, resolve_pending_race, reject_pending_race] = promise::Create<int>();
      auto source_rejected = Promise<int>::Reject<TestError>("race reject");
      auto race_rejected_result =
        source_rejected.Race(std::move(pending_race), resolve_pending_race, reject_pending_race);
      REQUIRE(race_rejected_result.Rejected());
      RequireException<TestError>(race_rejected_result.Exception());
      REQUIRE_FALSE((*resolve_pending_race)(66));
   });
}

TEST_CASE("Pending then promise continuation propagates exception", "[Promise][DeepBranches]") {
   RunWithTimeout(2s, [&] {
      auto [source, resolve, reject] = promise::Create<int>();
      auto chained                   = source.Then([](int) -> Promise<int> {
         throw CatchError("then async failure");
         co_return 0;
      });
      source.WaitAwaited(0);
      REQUIRE((*resolve)(7));
      REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));
      REQUIRE(chained.Rejected());
      RequireException<CatchError>(chained.Exception());
   });
}

TEST_CASE("Pending then sync continuation keeps rejection from source", "[Promise][DeepBranches]") {
   RunWithTimeout(2s, [&] {
      auto [source, resolve, reject] = promise::Create<int>();
      auto chained                   = source.Then([](int value) { return value + 1; });
      source.WaitAwaited(0);
      REQUIRE((*reject)(std::make_exception_ptr(TestError{"pending then reject"})));
      REQUIRE_FALSE((*resolve)(10));
      REQUIRE(chained.Rejected());
      RequireException<TestError>(chained.Exception());
   });
}

TEST_CASE("Pending catch handler throwing turns into rejection", "[Promise][DeepBranches]") {
   RunWithTimeout(2s, [&] {
      auto [source, resolve, reject] = promise::Create<int>();
      auto recovered =
        source.Catch([](TestError const&) -> int { throw CatchError("catch sync failure"); });
      source.WaitAwaited(0);
      REQUIRE((*reject)(std::make_exception_ptr(TestError{"trigger"})));
      REQUIRE_FALSE((*resolve)(12));
      REQUIRE(recovered.Rejected());
      RequireException<CatchError>(recovered.Exception());
   });
}

TEST_CASE("Pending catch async handler rejection propagates", "[Promise][DeepBranches]") {
   RunWithTimeout(2s, [&] {
      auto [source, resolve, reject] = promise::Create<int>();
      auto recovered                 = source.Catch([](TestError const&) -> Promise<int> {
         throw CatchError("catch async failure");
         co_return 0;
      });
      source.WaitAwaited(0);
      REQUIRE((*reject)(std::make_exception_ptr(TestError{"trigger async"})));
      REQUIRE_FALSE((*resolve)(42));
      REQUIRE(recovered.Rejected());
      RequireException<CatchError>(recovered.Exception());
   });
}

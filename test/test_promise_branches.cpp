#include "TestCommon.h"

TEST_CASE(
  "Catch pass-through preserves resolved values across return kinds",
  "[Promise][Branches]"
) {
   auto resolved_int_with_void_catch = Promise<int>::Resolve(33).Catch([](TestError const&) {});

   REQUIRE(resolved_int_with_void_catch.Resolved());
   REQUIRE(resolved_int_with_void_catch.Value().has_value());
   REQUIRE(resolved_int_with_void_catch.Value().value() == 33);

   bool catch_called = false;
   auto resolved_void_with_value_catch =
     Promise<void>::Resolve().Catch([&catch_called](TestError const&) {
        catch_called = true;
        return 77;
     });

   REQUIRE(resolved_void_with_value_catch.Resolved());
   REQUIRE_FALSE(catch_called);
   REQUIRE_FALSE(resolved_void_with_value_catch.Value().has_value());
}

TEST_CASE(
  "Catch async typed exception keeps exception alive across suspension",
  "[Promise][Branches]"
) {
   auto [gate, resolve, reject] = promise::Create<void>();

   auto recovered = Promise<int>::Reject<TestError>("typed async")
                      .Catch([&](TestError const& exception) -> Promise<std::string> {
                         co_await gate;
                         co_return std::string{exception.what()};
                      })
                      .Then([](std::variant<std::string, int> const& value) {
                         REQUIRE(std::holds_alternative<std::string>(value));
                         return std::get<std::string>(value);
                      });

   REQUIRE_FALSE(recovered.Done());
   REQUIRE((*resolve)());
   REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));

   REQUIRE(recovered.Resolved());
   REQUIRE(recovered.Value() == "typed async");
}

TEST_CASE(
  "Catch with exception_ptr async recovery resolves expected value",
  "[Promise][Branches]"
) {
   auto recovered = Promise<int>::Reject<TestError>("ptr async")
                      .Catch([](std::exception_ptr exception) -> Promise<int> {
                         RequireException<TestError>(exception);
                         co_return 909;
                      });

   REQUIRE(recovered.Resolved());
   REQUIRE(recovered.Value() == 909);
}

TEST_CASE("Finally async failure overrides previous rejection", "[Promise][Branches]") {
   auto p = Promise<int>::Reject<TestError>("initial").Finally([]() -> Promise<void> {
      throw FinallyError("finally async fail");
      co_return;
   });

   REQUIRE(p.Rejected());
   RequireException<FinallyError>(p.Exception());
}

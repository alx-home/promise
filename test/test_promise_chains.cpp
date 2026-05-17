#include "TestCommon.h"

TEST_CASE("co_await propagates promise exceptions like the old driver", "[Promise][Chains]") {
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

TEST_CASE(
  "Direct WPromise constructors cover resolve reject and throw paths",
  "[Promise][Chains]"
) {
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
  "[Promise][Chains]"
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

TEST_CASE(
  "Then supports resolver-based continuations across value and void",
  "[Resolver][Chains]"
) {
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

TEST_CASE("Asynchronous Catch can await another promise before recovering", "[Promise][Chains]") {
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

TEST_CASE("All combines non-void results and skips void", "[Promise][Chains]") {
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

TEST_CASE("All rejects when any member rejects", "[Promise][Chains]") {
   auto all = promise::All(Promise<int>::Resolve(4), Promise<int>::Reject<TestError>("boom"));

   REQUIRE(all.Rejected());
   RequireException<TestError>(all.Exception());
}

TEST_CASE("Race returns the first resolved alternative", "[Promise]") {
   auto [pending, resolve, reject] = promise::Create<std::string>();
   auto raced                      = promise::Race(Promise<int>::Resolve(7), pending);

   REQUIRE(raced.Resolved());
   REQUIRE(std::holds_alternative<int>(raced.Value()));
   REQUIRE(std::get<int>(raced.Value()) == 7);

   REQUIRE((*resolve)("late"));
   REQUIRE_FALSE((*reject)(std::make_exception_ptr(TestError{"ignored"})));
}

TEST_CASE("Race rejection flows through Catch into optional variant", "[Promise]") {
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

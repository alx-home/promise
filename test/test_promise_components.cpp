#include "TestCommon.h"

using namespace std::chrono_literals;

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

   temp_cv = nullptr;

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

   StatePromise state2;
   auto         wait_done_reject = state2.WaitDoneWithReject();
   state2.Done();
   REQUIRE(wait_done_reject.Done());
   REQUIRE(wait_done_reject.Rejected());
   RequireException<StatePromise::End>(wait_done_reject.Exception());

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

   temp_state = nullptr;

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

   WPromise promise{[&]() -> Promise<void> {
      co_await queue.Ensure();
      REQUIRE(std::this_thread::get_id() == queue.ThreadId());
   }};

   promise.WaitDone();
   REQUIRE(promise.Resolved());
}

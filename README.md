<p align="center">
    <a href="https://github.com/alx-home/promise/actions/workflows/build.yml">
        <img alt="Build" src="https://github.com/alx-home/promise/actions/workflows/build.yml/badge.svg">
    </a>
    <a href="https://sonarqube.alex-home.fr/dashboard?id=alx-home_promise_4d0d1d2f-bd15-4ba9-8b0e-1b25f4b783ce">
        <img alt="Quality Gate Status" src="https://sonarqube.alex-home.fr/api/project_badges/measure?project=alx-home_promise_4d0d1d2f-bd15-4ba9-8b0e-1b25f4b783ce&metric=alert_status&token=sqb_9fa9f23299ff58970210cbc2e5dc06ee9da6dc09">
    </a>
</p>

<p align="center">
	<img src=".github/logo.svg" alt="alx-home promise logo" width="560">
</p>

<h1 align="center">alx-home promise</h1>

<p align="center">
<strong>JavaScript‑style Promises for modern c++23 — coroutine‑native, fully thread‑safe, and designed for human‑friendly async code.</strong>
</p>

<p align="center">
Chain with <code>Then</code>, recover with <code>Catch</code>, clean up with <code>Finally</code>, and keep captures alive until completion — just like JS promises, but with C++ performance and correctness.
</p>

---

## Why this library exists

c++20 coroutines are powerful, but the ecosystem around them is still low‑level and hard to use.  
JavaScript, on the other hand, nailed the ergonomics of async programming years ago.

This library brings that experience to C++:

- **JS‑style API** (`Then`, `Catch`, `Finally`, chaining, composition)
- **Native coroutine integration** (`co_await`, `Promise<T>` return types)
- **Fully thread‑safe** resolution, rejection, and awaiting
- **Zero allocations** in the common path
- **Safe lambda capture lifetime** (captures live until the promise completes)
- **Combinators** like `All`, `Race`, resolver‑style promises, pools, message queues, and async coordination primitives

If you know JavaScript promises, you already know how to use this library — but with the performance and determinism of C++.


## At a glance

| Capability | What you get |
| --- | --- |
| JS‑style ergonomics | `Then`, `Catch`, `Finally`, chaining, composition |
| Native coroutine support | `Promise<T>` return types, seamless `co_await` |
| Full thread safety | Resolve/reject/await from any thread |
| Resolver model | `Resolve<T>`, `Reject`, `Promise<T, true>` |
| Combinators | `promise::All`, `promise::Race`, `promise::Create<T>()` |
| Async coordination | `CVPromise`, `StatePromise` |
| Threaded dispatch | `promise::Pool`, `promise::MessageQueue` |
| Introspection | `Done()`, `Resolved()`, `Rejected()`, `Value()`, `Exception()` |

<img width="1100" height="420" alt="image" src="https://github.com/user-attachments/assets/da147329-20f6-4b99-8bb3-074debda3276" />

## Contents

- [Thread safety](#thread-safety)
- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Basic usage](#basic-usage)
- [Quick card](#quick-card)
- [Promise handle types](#promise-handle-types)
- [`MakePromise` scope and coroutine lambda rule](#makepromise-scope-and-coroutine-lambda-rule)
- [Forwarding arguments into `MakePromise`](#forwarding-arguments-into-makepromise)
- [Lambda capture lifetime](#lambda-capture-lifetime)
- [Resolver-style promises](#resolver-style-promises)
- [`Promise::Resolve` and `Promise::Reject`](#promiseresolve-and-promisereject)
- [Using resolvers outside the coroutine scope](#using-resolvers-outside-the-coroutine-scope)
- [`promise::All` and `promise::Race`](#promiseall-and-promiserace)
- [`CVPromise` for async notification](#cvpromise-for-async-notification)
- [`StatePromise` for ready/done workflows](#statepromise-for-readydone-workflows)
- [`promise::Pool` and `promise::MessageQueue`](#promisepool-and-promisemessagequeue)
- [Finally usage](#finally-usage)
- [Using non-promise lambdas in Then/Catch/Finally](#using-non-promise-lambdas-in-thencatchfinally)
- [Type flow after Then/Catch](#type-flow-after-thencatch)
- [Done, Resolved, Rejected, Value, Exception, Awaiters, UseCount](#done-resolved-rejected-value-exception-awaiters-usecount)
- [`[[nodiscard]]` and detached chains](#nodiscard-and-detached-chains)
- [Detach](#detach)
- [Build](#build)
- [Memory checking](#memory-checking)


## Thread safety

Promise state transitions are internally synchronized.  
You can safely resolve, reject, chain, and `co_await` promises from **any thread** without data races.

> ⚠️ Note: C++ coroutines and `thread_local` do not mix well. A coroutine may resume on a different thread, so `thread_local` state may not be what you expect.


## Requirements

- `C++23` compiler.
- `CMake 3.20+`.
- `Conan 2.x` or `vcpkg` (optional, for dependency management).
- `Git` access if CMake needs to fetch **build_tools** or **cpp_utils** automatically.

## Install instructions

Choose one of the following installation methods.

#### Option 1: `Conan`

Use Conan 2 with `CMakeDeps` and `CMakeToolchain`:

```text
[requires]
alx-promise/1.6.0

[generators]
CMakeDeps
CMakeToolchain
```

Then configure your project with Conan's generated toolchain:

```powershell
conan install . -s build_type=Debug --build=missing
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake
cmake --build build
```

Example CMake usage:

```cmake
find_package(alx-promise CONFIG REQUIRED)

target_link_libraries(your_target PRIVATE alx-home::promise)
```

#### Option 2: `vcpkg`

You can also install this library with `vcpkg`.

Then configure your project with vcpkg's toolchain:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
# eg: C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

Example CMake usage:

```cmake
find_package(alx-promise CONFIG REQUIRED)

target_link_libraries(your_target PRIVATE alx-home::promise)
```

#### Option 3: CMake (`FetchContent`)

Add `promise` directly to your CMake project:

- If `win32_library` / `win32_executable` are not available, `promise` automatically fetches `build_tools`.
- If `alx-home::cpp_utils` is not available, `promise` automatically fetches that dependency as well.

```cmake
include(FetchContent)

FetchContent_Declare(
	alx_home_promise
	GIT_REPOSITORY https://github.com/alx-home/promise.git
	GIT_TAG release
)

FetchContent_MakeAvailable(alx_home_promise)

target_link_libraries(your_target PRIVATE alx-home::promise)
```

### Usage examples

Usage examples can be found at: https://github.com/alx-home/promise_exemples

> ⚠️ Warning: that example repository has not been formally reviewed and was generated with Copilot assistance.

### Useful cache variables

- `PROMISE_BUILD_TESTS=OFF` disables the local `promise_test` executable. This is `OFF` by default when `promise` is added as a subproject.
- `PROMISE_FETCH_BUILD_TOOLS=OFF` means the caller must provide `win32_library` and `win32_executable`.
- `PROMISE_BUILD_TOOLS_GIT_REPOSITORY` and `PROMISE_BUILD_TOOLS_GIT_TAG` allow pinning or mirroring the helper repository.
- `PROMISE_FETCH_CPP_UTILS=OFF` means the caller must provide `alx-home::cpp_utils`.
- `PROMISE_CPP_UTILS_GIT_REPOSITORY` and `PROMISE_CPP_UTILS_GIT_TAG` allow pinning or mirroring the dependency source.

## Quick start

Build from this folder:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target alx-home_promise promise_test
./build/promise_test.exe
```

Minimal async chain:

```cpp
#include <promise/promise.h>

WPromise demo{[]() -> Promise<int> { co_return 21; }};

int const answer = co_await demo.Then([](int v) { return v * 2; });
```

## Basic usage

```cpp
#include <promise/promise.h>
#include <stdexcept>

WPromise GetAnswer {[] -> Promise<int> { 
	co_return 42; 
}};

// Returning a promise handle (not a coroutine) uses WPromise.
WPromise FetchCachedOrAsync(bool use_cache) {
	if (use_cache) {
		return Promise<int>::Resolve(42);
	}
	return []() -> Promise<int> { co_return 42; };
}


WPromise Demo {[]() -> Promise<void> {
	auto result = co_await GetAnswer();

	auto chained = WPromise{[=]() -> Promise<int> {
		co_return result + 1;
	}}
		.Then([](int value) { return value * 2; })
		.Then([](int value) -> Promise<int> { co_return value / 2; })
		.Then([](int value) -> WPromise<int> {
			return FetchCachedOrAsync(value > 0);
		})
		.Catch([](std::exception_ptr) -> WPromise<void> {
			return Promise<void>::Resolve();
		})
		.Then([](std::optional<int> const& value) -> Promise<int> {
			// value is std::optional<int> because Catch returned void
			co_return value.value_or(-1);
		});

	[[maybe_unused]] auto value = co_await chained;
	(void)value;
	co_return;
}};
```

## Quick Card

```cpp
#include <promise/promise.h>
#include <promise/StatePromise.h>
#include <stdexcept>

// -----------------------------------------------------------------------------
// Create a resolver‑enabled promise using Create<T>()
// - 'created' is the WPromise<int>
// - 'resolve' is a pointer to the Resolve<int> object
// - 'reject'  is a pointer to the Reject object
// -----------------------------------------------------------------------------
auto [created, resolve, reject] = promise::Create<int>();

(*resolve)(7);  // Resolve the promise with value 7
reject->Apply<std::runtime_error>("Exception argument"); // Safe but ignored (already resolved)

// -----------------------------------------------------------------------------
// Same behavior using MakeRPromise()
// - The lambda defines the resolver‑style coroutine body
// - 'created2' is the resulting WPromise<int>
// - 'resolve2' is the Resolve<int> handle
// - 'reject2'  is the Reject handle
// -----------------------------------------------------------------------------
auto [created2, resolve2, reject2] = MakeRPromise(
    [](Resolve<int> const&, Reject const&) -> Promise<int, true> {
        co_return; // resolver‑style promises always return void
    }
);

reject2->Apply<std::runtime_error>("Exception argument"); // Reject the promise
(*resolve2)(123);  // Safe but ignored (already rejected)

// -----------------------------------------------------------------------------
// Awaiting 'all' produces a std::tuple<int, double, int>
// - The void-producing task is awaited but omitted from the tuple
// - Order is preserved: (created, double, void, int) → (int, double, int)
// -----------------------------------------------------------------------------
auto all = promise::All(
    created,                               // WPromise<int>
    [] -> double { return 1.0; },          // Synchronous value
    [] -> Promise<void> { co_return; },    // Coroutine returning void
    [] -> Promise<int> { co_return 2; }    // Coroutine returning int
);
auto [a, b, c] = co_await all;

// -----------------------------------------------------------------------------
// Awaiting 'raced' yields a std::variant<int, double>
// - The result corresponds to whichever task completes first
// - The variant index matches the winning branch:
//       0 → created (int)
//       1 → coroutine returning int
//       2 → synchronous double
// -----------------------------------------------------------------------------
auto raced = promise::Race(
    created,                               // WPromise<int>
    [] -> Promise<int> { co_return 1; },   // Coroutine returning int
    [] -> double { return 2.5; }           // Synchronous value
);
auto raced_result = co_await raced;


// -----------------------------------------------------------------------------
// 1. Promise without a resolver
//    - The lambda returns a Promise<int>
//    - No Resolve/Reject handles are provided
//    - The coroutine simply co_returns a value
// -----------------------------------------------------------------------------
WPromise prom{[]() -> Promise<int> {	
	co_return 1;
}};

// -----------------------------------------------------------------------------
// 2. Resolver-style promise with explicit Resolve + Reject
//    - The lambda receives both Resolve<int> and Reject
//    - You manually resolve/reject the promise using the Resolve/Reject handles
//    - Promise<int, true>, true means “resolver-enabled promise”
// -----------------------------------------------------------------------------
WPromise prom{[](Resolve<int> const&, Reject const& reject) -> Promise<int, true> {
	MakeReject<std::runtime_error>(reject, "failed");
	co_return; // Always return void
}};

// -----------------------------------------------------------------------------
// 3. Resolver-style promise with only Resolve<T>
//    - Only the Resolve<int> handle is provided
//    - Useful when failure is not expected or handled elsewhere
//    - You manually resolve the promise with a value
// -----------------------------------------------------------------------------
WPromise prom{[](Resolve<int> const& resolve) -> Promise<int, true> {
	resolve(123);
	co_return; // Always return void
}};

// -----------------------------------------------------------------------------
// 4. Resolver‑style promise with only Reject
//    - The lambda receives only the Reject handle
//    - Useful when the promise can only fail (no success path)
//    - You manually reject the promise with an error
// -----------------------------------------------------------------------------
WPromise prom{
    [](Reject const& reject) -> Promise<int, true> {
        MakeReject<std::runtime_error>(reject, "failed");
        co_return; // Always return void
    }
};

// Create an already‑resolved Promise<int> with value 5
auto ok = Promise<int>::Resolve(5);

// Create an already‑rejected Promise<int> using a specific exception type
auto err = Promise<int>::Reject<std::runtime_error>("fail");

// Create an already‑rejected Promise<int> using MakeReject helper
// (equivalent to Promise<int>::Reject<T>(...))
auto err2 = MakeReject<Promise<int>, std::runtime_error>("failed fast"); // Same

// -----------------------------------------------------------------------------
// CVPromise — coroutine‑friendly condition‑variable style primitive
// - co_await *ready     waits until the notifier signals
// - Notify()            resolves all current waiters (one‑shot signal)
// - Reset()             resolves current waiters AND arms the next wait cycle
// - Destroying or rejecting the notifier throws CVPromise::End in waiters
// -----------------------------------------------------------------------------

CVPromise ready;

// Notify all current waiters (one‑shot)
ready.Notify();

// Reset the CVPromise:
// - prepares the next wait cycle for future waiters
// - If the current promise is still pending, this function is a no-op.
ready.Reset();

// Wait for Notify()
co_await *ready;  

// -----------------------------------------------------------------------------
// StatePromise — coroutine‑friendly state-transition primitive
// - Ready()      signals the "ready" state (non‑terminal)
// - Done()       signals the "done" state (terminal)
// - Wait()       waits for either Ready or Done
// - WaitReady()  waits specifically for Ready
// - WaitDone()   waits specifically for Done
// - IsDone()     returns true only if the state has reached the Done state
// - Reset()      returns the state to its initial (not ready, not done) form
// -----------------------------------------------------------------------------
StatePromise state;

state.Ready();               // Signal the Ready state
state.Done();                // Transition to the Done state (terminal)

co_await state.Wait();       // Waits for Ready or Done
co_await state.WaitReady();  // Waits until Ready is signaled
co_await state.WaitDone();   // Waits until Done is signaled

[[maybe_unused]] bool done = state.IsDone();  // true only if Done() was signaled

state.Reset();        // Reset to initial state (neither Ready nor Done)

```


## Promise handle types

`Promise<T>` is the coroutine return type you write in function signatures. `WPromise<T>` is an
owning, awaitable handle you can store, pass around, or convert to type-erased pointers via
`ToPointer()` when you need to keep promises in containers or interfaces. `VPromise` uses `V` for
Virtual (type-erased base class), and `WPromise` uses `W` for Wrapped (owning handle).

Important: `WPromise<T>` is RAII-owning. If a live, non-detached promise reaches its destructor
before it is done, the destructor waits for completion. This helps prevent unfinished async work
from being dropped silently, but it also means true fire-and-forget flows should call `Detach()`
explicitly.

If a function returns a promise handle (not a coroutine), use `WPromise<T>`.
There is no way in C++ to distinguish a function returning a promise from a coroutine promise
type in the signature alone, so `Promise<T>` is reserved for coroutine return types.

## `MakePromise` scope and coroutine lambda rule

Use `MakePromise(...)` as the boundary that creates and owns async work from callables/lambdas.
In practice, `[]() -> Promise<T> { ... }` coroutine lambdas should not be used standalone outside
`MakePromise(...)` or a `WPromise<T>` wrapper.

Exception: inside a coroutine that already returns `Promise<U>`, you can `co_await` a function that
returns a promise. The await flow is handled by promise awaitables and the resulting state is
managed through `WPromise` internals.

```cpp
#include <promise/promise.h>

// Allowed form 1: wrap coroutine lambdas with MakePromise.
auto wrapped1 = MakePromise([]() -> Promise<int> { co_return 42; });

// Allowed form 2: wrap coroutine lambdas with WPromise.
WPromise wrapped2{[]() -> Promise<int> {
	co_return 42;
}};

// Invalid: raw standalone coroutine lambda returning Promise (will not work as expected).
// auto raw = []() -> Promise<int> { co_return 42; };
```


## Forwarding arguments into `MakePromise`

`MakePromise` forwards any extra arguments to the callable you pass in. This makes it easy to
use function pointers or function objects without capturing arguments manually.

```cpp
#include <promise/promise.h>

Promise<int> Add(int a, int b) {
	co_return a + b;
}

auto sum = co_await MakePromise(Add, 2, 3);
```

For resolver-style promises, resolver parameters come first, followed by your forwarded
arguments. You may accept just `Resolve<T>` or both `Resolve<T>` and `Reject`.

```cpp
#include <promise/promise.h>

Promise<int, true> AddAsync(Resolve<int> const& resolve, Reject const&, int a, int b) {
	resolve(a + b);
	co_return;
}

auto sum = co_await MakePromise(AddAsync, 2, 3);

// Resolve-only form (Reject is optional)
Promise<int, true> AddAsync2(Resolve<int> const& resolve, int a, int b) {
	resolve(a + b);
	co_return;
}

auto sum2 = co_await MakePromise(AddAsync2, 2, 3);
```

## Lambda capture lifetime

Promises store the lambdas you pass to `MakePromise`, `Then`, `Catch`, and `Finally`. That means
captures are kept alive until the promise resolves or rejects. This avoids the typical coroutine
lifetime issue where a lambda's captured values might be destroyed before the coroutine completes.

Example: safe value capture from a local scope

```cpp
#include <promise/promise.h>


WPromise MakeGreeting() {
	std::string name = "Alex";
	return [=]() -> Promise<std::string> {
		co_return "Hello, " + name;
	};
}

auto greeting = co_await MakeGreeting();
```

Example: move-only capture kept alive through the chain

```cpp
#include <memory>
#include <promise/promise.h>

WPromise UseResource() {
	auto resource = std::make_unique<int>(7);
	return [res = std::move(resource)]() mutable -> Promise<int> {
		co_return *res + 1;
	};
}

auto value = co_await UseResource();
```

Note: reference captures still require the referenced object to outlive the promise.

## Resolver-style promises

Use `Promise<T, true>` when the resolver should be passed into the coroutine.

```cpp
#include <promise/promise.h>

WPromise prom{[](Resolve<int> const& resolve, Reject const&) -> Promise<int, true> {
	resolve(123);
	co_return;
}};

auto value = co_await prom;
```

Reject convenience helpers:

```cpp
#include <promise/promise.h>
#include <stdexcept>

WPromise prom2 {[](Resolve<int> const&, Reject const& reject) -> Promise<int, true> {
	static constexpr bool REJECT_WITH_EXCEPTION = true;

	if constexpr (REJECT_WITH_EXCEPTION) {
		reject(std::runtime_error("failed"));
	} else {
		reject.Apply<std::runtime_error>("failed");
	}
	co_return;
}};
```

`MakeReject<EXCEPTION>(reject, ...)` is relaxed by default. If the promise is already rejected,
it returns `false` instead of throwing. Use `MakeReject<EXCEPTION, false>(reject, ...)` when you
want double rejects to raise an exception instead.

## `Promise::Resolve` and `Promise::Reject`

Use these static helpers to create an already-resolved or already-rejected promise without
starting a coroutine. This is useful for fast paths or for adapting existing sync results.

```cpp
#include <promise/promise.h>
#include <stdexcept>

auto ok = Promise<int>::Resolve(5);
auto value = co_await ok;

auto err = Promise<int>::Reject(std::make_exception_ptr(std::runtime_error("fail")));
auto err2 = MakeReject<Promise<int>, std::runtime_error>("failed fast");
try {
	(void)co_await err;
   (void)co_await err2;
} catch (std::exception const&) {
	// Handle error
}
```

Using `MakeRPromise` (promise + resolver tuple):

```cpp
#include <promise/promise.h>

auto [prom, resolve, reject] = MakeRPromise(
	[](Resolve<int> const&, Reject const&) -> Promise<int, true> { co_return; }
);

StartAsyncWork([resolve]() {
	(*resolve)(99);
});

auto value = co_await prom;
```

## Using resolvers outside the coroutine scope

Resolvers can be stored and used later (for example from another thread or callback). The
resolver objects are shared, so keep a `std::shared_ptr` to them. The general rule is that you
may call `resolve`/`reject` only while the underlying promise state is still alive or after it
has already completed. The promise handle itself can go out of scope as long as the underlying
state is kept alive (for example by calling `Detach()` or storing the promise elsewhere).

```cpp
#include <promise/promise.h>

auto [prom, resolve, reject] = promise::Create<int>();

StartAsyncWork([resolve]() {
	(*resolve)(42);
});

auto value = co_await prom;
```

```cpp
#include <promise/promise.h>

std::shared_ptr<Resolve<int>> resolver;
std::shared_ptr<Reject> rejecter;

WPromise prom {[
	&resolver,
	&rejecter
](Resolve<int> const& resolve, Reject const& reject) -> Promise<int, true> {
	resolver = resolve.shared_from_this();
	rejecter = reject.shared_from_this();
	co_return;
}};

// Later, from outside the coroutine body:
[[maybe_unused]] auto ok = (*resolver)(42);
// or: (*rejecter)(std::current_exception());

auto value = co_await prom;
```

If you do not need to await the promise but want it to stay alive until resolved, call
`Detach()` on the promise handle before it goes out of scope.

## `promise::All` and `promise::Race`

Use `promise::All(...)` to await every promise and collect all results in a tuple. Use
`promise::Race(...)` to complete as soon as the first promise resolves or rejects.

```cpp
#include <promise/promise.h>
#include <variant>

auto all = promise::All(
	[]() -> Promise<int> { co_return 10; },
	[]() -> Promise<void> { co_return; },
	[]() -> Promise<double> { co_return 2.5; }
);

auto [i, d] = co_await all; // std::tuple<int, double>

auto first = promise::Race(
	[]() -> Promise<int> { co_return 1; },
	[]() -> Promise<double> { co_return 2.5; }
);

auto value = co_await first; // std::variant<int, double>
```

Return-type rules for `All`:

- `co_await promise::All(...)` yields a tuple of each resolved value.
- Promises returning `void` are still awaited, but do not contribute a tuple slot.

Return-type rules for `Race`:

- If all raced promises return the same non-void type, `co_await` yields that type.
- If raced promises return different non-void types, `co_await` yields `std::variant<...>`.
- If any raced promise returns `void`, the result becomes `std::optional<...>` (or `void` if all
  are `void`).
- If the first completed promise rejects, the race rejects and can be handled with `Catch`.

## `CVPromise` for async notification

`CVPromise` is a small condition-variable-style helper for coroutines. Include it with
`#include <promise/CVPromise.h>` when you want one or more coroutines to wait for a signal.

```cpp
#include <promise/CVPromise.h>

CVPromise ready;

WPromise waiter{[&]() -> Promise<void> {
	try {
		co_await *ready;
	} catch (const CVPromise::End&) {
		// The notifier was destroyed or rejected.
	}
	co_return;
}};

ready.Notify(); // resolves the current waiters
ready.Reset();  // resolves current waiters and arms the next wait
```

Notes:

- `Wait()` returns a `WPromise<void>` and `CVPromise` can also be used directly in `co_await`.
- `operator*()` returns the current `WPromise<void>`, so `co_await *ready;` is equivalent to `co_await ready.Wait();`.
- `operator->()` gives access to the underlying promise handle for inspection such as `ready->Resolved()`.
- `Notify()` resolves the current wait state.
- `Reset()` resolves the current waiters and creates a fresh wait state for the next cycle.
- Destroying a `CVPromise` rejects outstanding waiters with `CVPromise::End`.

## `StatePromise` for ready/done workflows

`StatePromise` combines two `CVPromise` instances into a small helper for state-machine style
coordination between coroutines.

```cpp
#include <promise/StatePromise.h>

StatePromise state;

WPromise worker{[&]() -> Promise<void> {
	co_await state.WaitReady();
	// Start work after the state becomes ready.
	co_await state.WaitDone();
	co_return;
}};

state.Ready();
state.Done();

if (state.IsDone()) {
	// ready/done cycle is complete
}
```

Methods:

- `WaitReady()` waits until `Ready()` is called.
- `WaitDone()` waits until `Done()` is called.
- `Wait()` races ready/done and resolves when one of them happens first.
- `Reset()` arms the helper again for the next cycle.
- `IsDone()` returns `true` once the done state has been reached.

Destroying a `StatePromise` calls `Done()` so waiting coroutines are unblocked.

## `promise::Pool` and `promise::MessageQueue`

Use `promise::Pool<SIZE>` to dispatch work onto worker threads and get back promise handles.
Use `promise::MessageQueue` when you specifically want a single-thread queue and need to funnel
work back onto that queue thread.

```cpp
#include <promise/MessageQueue.h>

promise::Pool<4> pool{"worker"};
promise::MessageQueue queue{"ui"};

auto background = pool.Dispatch([]() {
	return 42;
});

auto ordered = queue.Ensure([]() {
	// Continue on the queue thread.
});

auto value = co_await background;
co_await ordered;
```

Notes:

- `Dispatch(...)` returns a `WPromise<T>` or `WPromise<void>` depending on the callable.
- Delayed execution is supported via `Dispatch(duration)` and `Dispatch(func, duration)`.
- `MessageQueue::Ensure(...)` is the helper to use when a continuation must run in the message-queue thread context. In practice, use `co_await queue.Ensure();` to switch execution back into that queue when the current code may be running on a different thread, then continue the coroutine from there.
- `Ensure(...)` is currently an alias for `Dispatch(...)` on a single worker thread.
- `MessageQueue::ThreadId()` returns the queue thread id.

## Finally usage

Use `Finally` to run cleanup regardless of resolve or reject. The handler does not receive a value
or exception.

```cpp
#include <promise/promise.h>

WPromise prom{[]() -> Promise<int> {
	co_return 42;
}};

auto done = prom
	.Then([](int value) -> Promise<int> { co_return value + 1; })
	.Catch([](std::exception_ptr) -> Promise<int> { co_return -1; })
	.Finally([]() -> Promise<void> {
		// Cleanup, logging, or release resources here.
		co_return;
	});

(void)co_await done;
```

## Using non-promise lambdas in Then/Catch/Finally

You can also chain `Then`, `Catch`, and `Finally` with non-promise lambdas.
The return values are wrapped into promises automatically.
This avoids creating a coroutine frame for those steps.

```cpp
#include <promise/promise.h>

WPromise done{[]() -> Promise<int> { co_return 5; }}
	.Then([](int value) { return value + 10; })
	.Catch([](std::exception_ptr) { return 0; })
	.Finally([]() {
		// Cleanup work.
	});

(void)co_await done;
```

## Type flow after Then/Catch

The type expected by each `Then` or the value you get from `co_await` depends on what the previous
steps can produce. The chain computes a single promise value type, and that is the type passed into
the next `Then` or returned by `co_await`.

Catch argument rules:

- A `Catch` handler must take exactly one argument: `std::exception_ptr` or `const Exception&`.
- Using a specific exception type by const reference behaves like `try { } catch (const T&) { }`.
- A `Catch(std::exception_ptr)` handler always runs for any thrown exception.
- A `Catch(const T&)` handler runs only when the exception is dynamically castable to `T`.
- Typed `Catch(const T&)` is implemented by rethrowing the stored `std::exception_ptr` and
  catching `const T&`, so it works on standard-conforming compilers and only matches when the
  stored exception is of type `T`.
- For typed `Catch(const T&)` that returns a promise, the exception is copied before the
  continuation runs to avoid dangling references when the continuation is async.

You can also use standard `try { } catch { }` inside a coroutine when awaiting another promise.
Exceptions raised by an awaited promise propagate through `co_await` and can be handled normally.

```cpp
#include <promise/promise.h>

Promise<int> MightFail();

WPromise Demo{[]() -> Promise<void> {
	try {
		auto value = co_await MightFail();
		(void)value;
	} catch (const std::exception& ex) {
		(void)ex;
	}
	co_return;
}};
```

Then argument rules:

- The value passed to `Then` shall be taken by const reference when it is a value type
  (for example `const T&`, `const std::optional<T>&`, or `const std::variant<...>&`).
- The same const-reference rule applies to `Catch` when its argument is a typed exception.

Value type rules for a `Catch` block:

- If both the previous value type `T` and the `Catch` return type are `void`, the result is
  `Promise<void>`.
- If `T` is non-void and `Catch` returns `void`, the result is `Promise<std::optional<T>>`.
- If `T` is `void` and `Catch` returns `T2`, the result is `Promise<std::optional<T2>>`.
- If `T` and `T2` are the same type, the result is `Promise<T>`.
- If `T` and `T2` differ, the result is `Promise<std::variant<T, T2>>`.

Example with `std::optional` after `Catch`:

```cpp
#include <promise/promise.h>

WPromise chain{[]() -> Promise<int> { co_return 1; }}
	.Catch([](std::exception_ptr) -> Promise<void> { co_return; })
	.Then([](std::optional<int> const& value) -> Promise<int> {
		co_return value.value_or(0);
	});

auto result = co_await chain; // result is int
```

Example with `std::variant` when types differ:

```cpp
#include <promise/promise.h>

WPromise chain{[]() -> Promise<int> { co_return 2; }}
	.Then([](int const& value) -> Promise<double> { co_return value * 1.5; })
	.Catch([](std::exception_ptr) -> Promise<int> { co_return 0; })
	.Then([](std::variant<int, double> const& value) -> Promise<void> {
		// Handle both types.
		if (std::holds_alternative<int>(value)) {
			(void)std::get<int>(value);
		} else {
			(void)std::get<double>(value);
		}
		co_return;
	});

(void)co_await chain; // co_await type follows the same rules
```

## Done, Resolved, Rejected, Value, Exception, Awaiters, UseCount

These methods let you inspect a promise state without awaiting it:

- `Done()` returns `true` when the promise is resolved or rejected.
- `Resolved()` returns `true` when the promise completed successfully.
- `Rejected()` returns `true` when the promise completed with an exception.
- `Value()` returns the resolved value (only valid when resolved).
- `Exception()` returns the stored exception (only meaningful when rejected).
- `Awaiters()` returns the number of coroutines currently waiting on the promise.
- `UseCount()` returns the total number of awaiter registrations observed by that promise.

```cpp
#include <promise/promise.h>

WPromise prom{[]() -> Promise<int> { co_return 10; }};

if (prom.Done()) {
	if (prom.Resolved()) {
		auto value = prom.Value();
		(void)value;
	} else if (prom.Rejected()) {
		auto exception = prom.Exception();
		(void)exception;
	}
}
```

Warnings:

- Calling `Value()` or `Exception()` before the promise is done is invalid and will assert or
  throw, depending on build/runtime checks.
- `Value()` is only valid for resolved promises; `Exception()` is only meaningful for rejected
  promises. Prefer `co_await` or `Then`/`Catch` for correct flow.

## `[[nodiscard]]` and detached chains

`MakePromise`, `Then`, `Catch`, `Finally`, `MakeRPromise`, and the queue/pool dispatch helpers are
annotated so the compiler warns if you create a promise and immediately discard it.

If you intentionally do not need the returned handle, call `Detach()` explicitly:

```cpp
WPromise{[]() -> Promise<void> {
	co_return;
}}.Finally([]() {
	// Cleanup only.
}).Detach();
```

## Detach

`Detach()` transfers ownership and lets a promise run without keeping a handle to it. Use it for
fire-and-forget chains when you do not need to await or inspect the result.

Without `Detach()`, destroying an unfinished `WPromise` waits for the promise chain to complete.

```cpp
#include <promise/promise.h>

WPromise{[]() -> Promise<void> {
	// Do work asynchronously.
	co_return;
}}.Then([]() -> Promise<void> {
	// Follow-up work.
	co_return;
}).Detach();
```

Warnings:

- Once detached, you cannot `co_await`, `Then`, or inspect the promise state anymore.
- Use `Detach()` only when you are sure errors can be ignored or handled internally.

## Build

From this folder:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target alx-home_promise promise_test
```

Run the test binary:

```powershell
./build/promise_test.exe
```

## Memory checking

Enable promise leak detection via CMake options:

- `PROMISE_MEMCHECK_DEBUG` (default ON)
- `PROMISE_MEMCHECK_RELEASE` (default OFF)
- `PROMISE_MEMCHECK_FULL` (default ON)

These options define `PROMISE_MEMCHECK` and `PROMISE_MEMCHECK_FULL` for the library and the test.

To enable runtime leak reporting, add this at the beginning of `main` or `WinMain`:

```cpp
#ifdef PROMISE_MEMCHECK
auto const _{promise::Memcheck()};
#endif
```

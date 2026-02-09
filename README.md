[![Build](https://github.com/alx-home/promise/actions/workflows/build.yml/badge.svg)](https://github.com/alx-home/promise/actions/workflows/build.yml)

# alx-home promise

Coroutine-native Promise implementation for C++20 with a JavaScript-like feel. Chain `Then`,
recover with `Catch`, and clean up with `Finally`, all with minimal boilerplate and clear flow.
Lambdas passed into promises are stored by the promise itself, so lambda captures live until
completion. This avoids a common C++ coroutine pitfall where values captured in `[]` can be
destroyed before the coroutine finishes. Use resolver-style promises when you need explicit
control, while keeping the ergonomics of JS Promises in modern C++.

## Features

- C++20 coroutine promise type with `co_await` support.
- Chaining: `Then`, `Catch`, `Finally`.
- Resolver promises: `Promise<T, true>` plus `Resolve<T>` and `Reject`.
- Helpers: `promise::All(...)` and `promise::Pure<T>()`.
- Optional leak detection via `PROMISE_MEMCHECK`.
- Lambdas are stored in the promise, so captures survive until resolution.

## Thread safety

Promise state transitions are synchronized internally, so you can resolve, reject, and await
promises from multiple threads without data races.

Warning about `thread_local` (C++ coroutine truth): using `thread_local` variables in coroutine
code is risky because a coroutine may resume on a different thread than the one that created the
`thread_local` state. That can lead to reading a different instance than expected, or to accessing
a `thread_local` object that has already been destroyed when a thread exits.

## Requirements

- C++20 compiler.
- CMake 3.20+.
- Dependency: `alx-home::cpp_utils` (linked by CMake).

## Basic usage

```cpp
#include <promise/promise.h>
#include <stdexcept>

Promise<int> GetAnswer() {
	co_return 42;
}

Promise<void> Demo() {
	auto result = co_await GetAnswer();

	   auto chained = MakePromise([=]() -> Promise<int> {
							   co_return result + 1;
						   })
						   .Then([](int value) -> Promise<int> { co_return value * 2; })
						   .Catch([](std::exception_ptr) -> Promise<int> { co_return -1; });

	(void)co_await chained;
	co_return;
}
```

## Quick Card

```cpp
#include <promise/promise.h>
#include <stdexcept>

auto [pure, resolve, reject] = promise::Pure<int>();
resolve->operator()(7);

auto all = promise::All(
	MakePromise([]() -> Promise<int> { co_return 1; }),
	MakePromise([]() -> Promise<int> { co_return 2; })
);

auto prom = MakePromise([](Resolve<int> const&, Reject const& reject) -> Promise<int, true> {
	MakeReject<std::runtime_error>(reject, "failed");
	co_return;
});

auto [prom2, resolve2, reject2] = MakeRPromise(
	[](Resolve<int> const&, Reject const&) -> Promise<int, true> { co_return; }
);
(*resolve2)(123);

auto ok = Promise<int>::Resolve(5);
auto err = Promise<int>::Reject(std::make_exception_ptr(std::runtime_error("fail")));
auto err2 = MakeReject<Promise<int>, std::runtime_error>("failed fast");
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

For resolver-style promises, the resolver parameters come first, followed by your forwarded
arguments.

```cpp
#include <promise/promise.h>

Promise<int, true> AddAsync(Resolve<int> const& resolve, Reject const&, int a, int b) {
	resolve(a + b);
	co_return;
}

auto sum = co_await MakePromise(AddAsync, 2, 3);
```

## Lambda capture lifetime

Promises store the lambdas you pass to `MakePromise`, `Then`, `Catch`, and `Finally`. That means
captures are kept alive until the promise resolves or rejects. This avoids the typical coroutine
lifetime issue where a lambda's captured values might be destroyed before the coroutine completes.

Example: safe value capture from a local scope

```cpp
#include <promise/promise.h>

Promise<std::string> MakeGreeting() {
	std::string name = "Alex";
	return MakePromise([=]() -> Promise<std::string> {
		co_return "Hello, " + name;
	});
}

auto greeting = co_await MakeGreeting();
```

Example: move-only capture kept alive through the chain

```cpp
#include <memory>
#include <promise/promise.h>

Promise<int> UseResource() {
	auto resource = std::make_unique<int>(7);
	return MakePromise([res = std::move(resource)]() mutable -> Promise<int> {
		co_return *res + 1;
	});
}

auto value = co_await UseResource();
```

Note: reference captures still require the referenced object to outlive the promise.

## Resolver-style promises

Use `Promise<T, true>` when the resolver should be passed into the coroutine.

```cpp
#include <promise/promise.h>

auto prom = MakePromise([](Resolve<int> const& resolve, Reject const&) -> Promise<int, true> {
	resolve(123);
	co_return;
});

auto value = co_await prom;
```

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

auto [prom, resolve, reject] = promise::Pure<int>();

StartAsyncWork([resolve]() {
	(*resolve)(42);
});

auto value = co_await prom;
```

```cpp
#include <promise/promise.h>

std::shared_ptr<Resolve<int>> resolver;
std::shared_ptr<Reject> rejecter;

auto prom = MakePromise([
	&resolver,
	&rejecter
](Resolve<int> const& resolve, Reject const& reject) -> Promise<int, true> {
	resolver = resolve.shared_from_this();
	rejecter = reject.shared_from_this();
	co_return;
});

// Later, from outside the coroutine body:
[[maybe_unused]] auto ok = (*resolver)(42);
// or: (*rejecter)(std::current_exception());

auto value = co_await prom;
```

If you do not need to await the promise but want it to stay alive until resolved, call
`Detach()` on the promise handle before it goes out of scope.

## Finally usage

Use `Finally` to run cleanup regardless of resolve or reject. The handler does not receive a value
or exception.

```cpp
#include <promise/promise.h>

auto prom = MakePromise([]() -> Promise<int> {
	co_return 42;
});

auto done = prom
	.Then([](int value) -> Promise<int> { co_return value + 1; })
	.Catch([](std::exception_ptr) -> Promise<int> { co_return -1; })
	.Finally([]() -> Promise<void> {
		// Cleanup, logging, or release resources here.
		co_return;
	});

(void)co_await done;
```

# Using non-promise lambdas in Then/Catch/Finally

You can also chain `Then`, `Catch`, and `Finally` with non-promise lambdas.
The return values are wrapped into promises automatically.
This avoids creating a coroutine frame for those steps.

```cpp
#include <promise/promise.h>

auto done = MakePromise([]() -> Promise<int> { co_return 5; })
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
- This typed `Catch(const T&)` behavior is a bit hacky: C++ has no standard way to cast an
  `std::exception_ptr`, and you cannot both `rethrow_exception` and `co_await` in the same
  catch block. It currently relies on MSVC internals and is only supported on specific MSVC
  versions.
- Supported MSVC versions for this behavior: 2019 (v1929) and 2022 (v1943).
- To support other compilers/versions, update the `ExceptionWrapper` implementation in
  `include/promise/impl/Promise.inl` (the block guarded by the `_MSC_VER` static assert). That is
  the only place using compiler-specific exception layout to extract typed exceptions from
  `std::exception_ptr`.

You can also use standard `try { } catch { }` inside a coroutine when awaiting another promise.
Exceptions raised by an awaited promise propagate through `co_await` and can be handled normally.

```cpp
#include <promise/promise.h>

Promise<int> MightFail();

Promise<void> Demo() {
	try {
		auto value = co_await MightFail();
		(void)value;
	} catch (const std::exception& ex) {
		(void)ex;
	}
	co_return;
}
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

auto chain = MakePromise([]() -> Promise<int> { co_return 1; })
	.Catch([](std::exception_ptr) -> Promise<void> { co_return; })
	.Then([](std::optional<int> const& value) -> Promise<int> {
		co_return value.value_or(0);
	});

auto result = co_await chain; // result is int
```

Example with `std::variant` when types differ:

```cpp
#include <promise/promise.h>

auto chain = MakePromise([]() -> Promise<int> { co_return 2; })
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

## Done, Value, Exception

These methods let you inspect a promise state without awaiting it:

- `Done()` returns `true` when the promise is resolved or rejected.
- `Value()` returns the resolved value (only valid when resolved).
- `Exception()` returns the stored exception (only meaningful when rejected).

```cpp
#include <promise/promise.h>

auto prom = MakePromise([]() -> Promise<int> { co_return 10; });

if (prom.Done()) {
	auto value = prom.Value();
	(void)value;
}
```

Warnings:

- Calling `Value()` or `Exception()` before the promise is done is invalid and will assert or
  throw, depending on build/runtime checks.
- `Value()` is only valid for resolved promises; `Exception()` is only meaningful for rejected
  promises. Prefer `co_await` or `Then`/`Catch` for correct flow.

## Detach

`Detach()` transfers ownership and lets a promise run without keeping a handle to it. Use it for
fire-and-forget chains when you do not need to await or inspect the result.

```cpp
#include <promise/promise.h>

MakePromise([]() -> Promise<void> {
	// Do work asynchronously.
	co_return;
}).Then([]() -> Promise<void> {
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

[![Build](https://github.com/alx-home/promise/actions/workflows/build.yml/badge.svg)](https://github.com/alx-home/promise/actions/workflows/build.yml)

# alx-home promise

Coroutine-based Promise implementation for C++20. It supports chaining with `Then`, error handling
with `Catch`, finalization with `Finally`, and resolver-style promises when you need to resolve or
reject from the outside.
The goal is to mirror JavaScript Promise behavior and ergonomics in C++.

## Features

- C++20 coroutine promise type with `co_await` support.
- Chaining: `Then`, `Catch`, `Finally`.
- Resolver promises: `Promise<T, true>` plus `Resolve<T>` and `Reject`.
- Helpers: `promise::All(...)` and `promise::Pure<T>()`.
- Optional leak detection via `PROMISE_MEMCHECK`.

## Requirements

- C++20 compiler.
- CMake 3.20+.
- Dependency: `alx-home::cpp_utils` (linked by CMake).

## Basic usage

```cpp
#include <promise/promise.h>

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

## Helper utilities

```cpp
#include <promise/promise.h>

auto [pure, resolve, reject] = promise::Pure<int>();
resolve->operator()(7);

auto all = promise::All(
	MakePromise([]() -> Promise<int> { co_return 1; }),
	MakePromise([]() -> Promise<int> { co_return 2; })
);
```

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

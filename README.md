# Async++ library

[![License Badge](https://img.shields.io/github/license/Thalhammer/asyncpp)](https://github.com/Thalhammer/asyncpp/blob/master/LICENSE)
[![Stars Badge](https://img.shields.io/github/stars/Thalhammer/asyncpp)](https://github.com/Thalhammer/asyncpp/stargazers)


Async++ is a c++ library providing polyfills and a large set of general purpose utilities
for making use of c++20 coroutines. It aims to provide modern and easy to use interfaces 
without sacrificing performance. While it is primarily tested and developed on modern linux,
patches and improvements for other platforms are welcome.

Tested and supported compilers:
| Ubuntu 20.04                                                          | Ubuntu 22.04                                                          |
|-----------------------------------------------------------------------|-----------------------------------------------------------------------|
| [![ubuntu-2004_clang-10][img_ubuntu-2004_clang-10]][Compiler-Support] | [![ubuntu-2204_clang-12][img_ubuntu-2204_clang-12]][Compiler-Support] |
| [![ubuntu-2004_clang-11][img_ubuntu-2004_clang-11]][Compiler-Support] | [![ubuntu-2204_clang-13][img_ubuntu-2204_clang-13]][Compiler-Support] |
| [![ubuntu-2004_clang-12][img_ubuntu-2004_clang-12]][Compiler-Support] | [![ubuntu-2204_clang-14][img_ubuntu-2204_clang-14]][Compiler-Support] |
| [![ubuntu-2004_gcc-10][img_ubuntu-2004_gcc-10]][Compiler-Support]     | [![ubuntu-2204_gcc-10][img_ubuntu-2204_gcc-10]][Compiler-Support]     |
|                                                                       | [![ubuntu-2204_gcc-11][img_ubuntu-2204_gcc-11]][Compiler-Support]     |


[img_ubuntu-2004_clang-10]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Thalhammer/asyncpp/badges/compiler/ubuntu-2004_clang-10/shields.json
[img_ubuntu-2004_clang-11]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Thalhammer/asyncpp/badges/compiler/ubuntu-2004_clang-11/shields.json
[img_ubuntu-2004_clang-12]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Thalhammer/asyncpp/badges/compiler/ubuntu-2004_clang-12/shields.json
[img_ubuntu-2004_gcc-10]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Thalhammer/asyncpp/badges/compiler/ubuntu-2004_gcc-10/shields.json
[img_ubuntu-2204_clang-12]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Thalhammer/asyncpp/badges/compiler/ubuntu-2204_clang-12/shields.json
[img_ubuntu-2204_clang-13]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Thalhammer/asyncpp/badges/compiler/ubuntu-2204_clang-13/shields.json
[img_ubuntu-2204_clang-14]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Thalhammer/asyncpp/badges/compiler/ubuntu-2204_clang-14/shields.json
[img_ubuntu-2204_gcc-10]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Thalhammer/asyncpp/badges/compiler/ubuntu-2204_gcc-10/shields.json
[img_ubuntu-2204_gcc-11]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Thalhammer/asyncpp/badges/compiler/ubuntu-2204_gcc-11/shields.json
[Compiler-Support]: https://github.com/Thalhammer/asyncpp/actions/workflows/compiler-support.yml

This library also supports Windows 10 / MSVC; the clang-cl support is, however, still a [WIP](https://github.com/llvm/llvm-project/issues/56300)

Also checkout the async wrappers for other libraries:
* [asyncpp-curl](https://github.com/Thalhammer/asyncpp-curl)
* [asyncpp-grpc](https://github.com/Thalhammer/asyncpp-grpc)
* [asyncpp-uring](https://github.com/Thalhammer/asyncpp-uring)

The provided tools include:
* Coroutine Types:
  * [`fire_and_forget_task`](#fire_and_forget_task)
  * [`eager_fire_and_forget_task`](#eager_fire_and_forget_task)
  * [`generator<T>`](#generatort)
  * [`task<T>`](#taskt)
* Awaitable Types:
  * [`defer`](#defer)
  * [`promise<T>`](#promiset)
* Functions:
  * [`launch()`](#launch)
  * [`as_promise()`](#as_promise)
* Concepts:
  * [`Dispatcher`](#dispatcher-concept)
  * [`ByteAllocator`](#byteallocator-concept)
* Utilities:
  * [`dispatcher`](#dispatcher)
  * [`async_launch_scope`](#async_launch_scope)
  * [Pointer tagging](#pointer-tagging)
  * [Reference counting](#reference-counting)
  * [`scope_guard`](#scope_guard)
  * [`threadsafe_queue<T>`](#threadsafe_queuet)

## `fire_and_forget_task`
A coroutine task with void return type that can not be awaited. It can be used as an
entrypoint for asynchronous operations started from synchronous code. Coroutines using
`fire_and_forget_task` as a return type are not started automatically and instead provide a
`start()` method for the initial start.

The coroutine function can use `co_await` to initiate asynchronous operations, but may not return any values other than void or use `co_yield`.

If an exception propagates outside the coroutine body the default behaviour is to call `std::terminate()` similar to the behaviour provided by `std::thread`. This can be changed by awaiting a value of type `exception_policy`. The library provides two predefined values, `exception_policy::terminate`, which invokes `std::terminate` and `exception_policy::ignore` which ignores the exception and terminates the coroutine as if `co_return` was invoked inside the function body. The third option is `exception_policy::handle(callback)` which allows the coroutine to register an arbitrary callback which gets invoke inside the catch block.

## `eager_fire_and_forget_task`
Similar to `fire_and_forget_task` but execution is immediately started and no `start()` method is available. 

## `generator<T>`
A generator represents a synchronous coroutine returning a sequence of values of a certain
type. The coroutine can use `co_yield` to generate a new value in the returned sequence or end
the sequence by returning from the function. However the generator does not support using
`co_await` to wait for an asynchronous operation. `generator<T>` coroutines serve as a
more readable and potentially more performant alternative to custom iterator types.
```cpp
generator<int> sample_generator(int max) {
	for (int i = 0; i < max; i++)
		co_yield i;
}
int main() {
    for (auto e : sample_generator(10))
		std::cout << e << "\n";
}
```
When a new instance of the coroutine is created the coroutine is initially suspended. Once
`begin()` is called on the returned coroutine it is executed until the first `co_yield` and 
resumed every time `operator++()` is invoked on the iterator type.

Exceptions thrown inside  the coroutine will propagate outside `begin()` or `operator++()`. Calling `begin()` multiple times or keeping an iterator beyond the lifetime of the originating coroutine causes undefined behaviour. Dereferencing the iterator returns a reference to the variable passed to `co_yield` inside the coroutine and might therefore be invalid after advancing the coroutine.

The method `end()` returns a constant sentinel value and is save to call any number of times.

## `task<T>`
The most fundamental building block for coroutine programs is `task<T>`. It provides a generic asynchronous coroutine task which serves as an awaitable and can await other awaitables within. It provides a single result of type T and forwards exceptions thrown within to the awaiting coroutine.

## `defer`
The defer class allows switching a coroutine to another dispatcher. This is commonly used for
operations that need to be executed on a certain thread or to switch to a thread pool in 
order to utilize multiple cores better. It accepts any class implementing the `Dispatcher` 
concept and schedules the current coroutine for execution on the dispatcher. You can either
pass a dispatcher by reference or pointer, where passing a nullpointer will result in a noop.
```cpp
#include <asyncpp/defer.h>

// Use inside a coroutine type
co_await defer{some_dispatcher};
```

## `promise<T>`
Async++ provides a generic promise type similar to `std::promise<T>` but with additional features. You can either `reject()` a promise with an exception provide a value using `fulfill()`. You can also synchronously wait for the promise using `get()`, which optionally accepts a timeout. Unlike `std::promise` however you can also register a callback using `on_result()` which gets executed immediately after a result is available. It also intergrates nicely with coroutines using `co_await`, which will suspend the current coroutine until a result is provided. Unlike `std::promise`, theres no distinction between future and promise, meaning anyone with access to the promise can resolve it.

## `launch()`
Start a coroutine which awaits the provided awaitable. This serves as an optimized version of a coroutine returning `eager_fire_and_forget_task` that immediately invokes `co_await` on the awaitable. The main use case is to start new coroutines that continue execution independent of the invoking function.

## `as_promise()`
`as_promise()` allows a user to wrap an arbitrary awaitable in a `std::promise` and therefore allows one to synchronously wait for it.

## `Dispatcher` Concept
A `Dispatcher` is a class used by async++ to schedule an action for later execution. The
interface consists of a method `push()` that accepts a value of type `std::function<void()>`,
which is checked by this concept. You can use for example `defer` to schedule a coroutine on
a different dispatcher and most I/O frameworks provide custom dispatchers. Examples would be
thread pools, libcurl's multi executor, grpc's event loop or a gui main thread.

## `ByteAllocator` Concept
A `ByteAllocator` is an allocator providing memory of type `std::byte` to the caller. It is 
used whenever async++ needs to get temporary dynamic memory and for allocating coroutine
frames. Most of the functions and classes for interacting with coroutines are allocator aware
and accept a custom allocator as the last argument, in order to control how and where memory
is allocated. The concept is satisfied if `std::allocator_traits<Allocator>::allocate()`
returns a type thats convertible to `std::byte*` and
`std::allocator_traits<Allocator>::deallocate()` accepts a `std::byte*` as the pointer.

## `dispatcher`
This is a base class for dispatcher types. It provides virtual methods implementing the
`Dispatcher` concept. In addition it provides a static `current()` method that gives you the
dispatcher currently executing the coroutine or a nullptr if theres none. This dispatcher can
be passed to `defer` to switch between dispatchers. All async++ provided executors/dispatchers
implement this support and 3rd party ones are encouraged to do as well.

## `async_launch_scope`
`async_launch_scope` provides a holder class that groups a number of coroutines together and allows a parent coroutine to wait until all of them have finished processing. A good example for this would be a tcp server that starts a new coroutine for each incoming connection. Using `async_launch_scope` the parent coroutine can use `scope.spawn(awaitable)` to start the client coroutines and keep track of them. Once the server receives a shutdown signal it can use the awaitable returned from `scope.join()` to wait until all of them have finished. This is similar to joining a `std::thread`. Note that when destructing the scope the number of running coroutines needs to be zero. This can be achieved by `co_await`ing the `join()` function or making sure all coroutines returned using some other way. 

## Pointer tagging
Async++ provides support for tagging pointer values by inserting a numeric ID into the unused bits of a pointer. In C++ each type has a certain alignment. As a result of this a pointer to a valid object of said type will always have its lowest bits cleared. Pointer tagging uses these bits and inserts a user specified value. Since the alignment is a compiletime constant value it is possible to later split the pointer back into the original pointer and ID. A common use case for this is passing a handler object to multiple C style operations.

## Reference counting
Async++ provides a highly customizable intrusive reference counting library with support for seamlessly integrating the reference count often built into C api's and wrapping them in C++ RAII types providing exception safety and ease of use.

## `scope_guard`
`scope_guard` is a RAII type that allows executing a custom callback when a scope exits.

## `threadsafe_queue<T>`
`threadsafe_queue<T>` is a generic threadsafe queue which provides atomic pop and push operations to allow easy implementation of multithreading.

# Async++ library
<img src="https://raw.githubusercontent.com/asyncpp/asyncpp/master/.github/logo.svg" alt="logo" width="100%" style="max-width:300px;margin-left:auto; margin-right:auto;">

[![License Badge](https://img.shields.io/github/license/asyncpp/asyncpp)](https://github.com/asyncpp/asyncpp/blob/master/LICENSE)
[![Stars Badge](https://img.shields.io/github/stars/asyncpp/asyncpp)](https://github.com/asyncpp/asyncpp/stargazers)


Async++ is a c++ library providing polyfills and a large set of general purpose utilities
for making use of c++20 coroutines. It aims to provide modern and easy to use interfaces 
without sacrificing performance. While it is primarily tested and developed on modern linux,
patches and improvements for other platforms are welcome.

Tested and supported compilers:
| Linux                                                                 | Windows                                                             | MacOS (best effort)                                                      |
|-----------------------------------------------------------------------|---------------------------------------------------------------------|--------------------------------------------------------------------------|
| [![ubuntu-2404_clang-16][img_ubuntu-2404_clang-16]][Compiler-Support] | [![windows-2025_msvc17][img_windows-2025_msvc17]][Compiler-Support] | [![macos-15-arm_clang-18][img_macos-15-arm_clang-18]][Compiler-Support]  |
| [![ubuntu-2404_clang-17][img_ubuntu-2404_clang-17]][Compiler-Support] | [![windows-2022_msvc17][img_windows-2022_msvc17]][Compiler-Support] | [![macos-14-arm_clang-15][img_macos-14-arm_clang-15]][Compiler-Support]  |
| [![ubuntu-2404_clang-18][img_ubuntu-2404_clang-18]][Compiler-Support] |                                                                     | [![macos-13_clang-15][img_macos-13_clang-15]][Compiler-Support]          |                                                                     |
| [![ubuntu-2404_gcc-12][img_ubuntu-2404_gcc-12]][Compiler-Support]     |                                                                     |                                                                          |
| [![ubuntu-2404_gcc-13][img_ubuntu-2404_gcc-13]][Compiler-Support]     |                                                                     |                                                                          |
| [![ubuntu-2404_gcc-14][img_ubuntu-2404_gcc-14]][Compiler-Support]     |                                                                     |                                                                          |
| [![ubuntu-2204_clang-13][img_ubuntu-2204_clang-13]][Compiler-Support] |                                                                     |                                                                          |
| [![ubuntu-2204_clang-14][img_ubuntu-2204_clang-14]][Compiler-Support] |                                                                     |                                                                          |
| [![ubuntu-2204_clang-15][img_ubuntu-2204_clang-15]][Compiler-Support] |                                                                     |                                                                          |
| [![ubuntu-2204_gcc-11][img_ubuntu-2204_gcc-11]][Compiler-Support]     |                                                                     |
| [![ubuntu-2204_gcc-10][img_ubuntu-2204_gcc-10]][Compiler-Support]     |                                                                     |


[img_ubuntu-2404_clang-16]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2404_clang-16/shields.json
[img_ubuntu-2404_clang-17]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2404_clang-17/shields.json
[img_ubuntu-2404_clang-18]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2404_clang-18/shields.json
[img_ubuntu-2404_gcc-12]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2404_gcc-12/shields.json
[img_ubuntu-2404_gcc-13]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2404_gcc-13/shields.json
[img_ubuntu-2404_gcc-14]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2404_gcc-14/shields.json
[img_ubuntu-2204_clang-13]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2204_clang-13/shields.json
[img_ubuntu-2204_clang-14]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2204_clang-14/shields.json
[img_ubuntu-2204_clang-15]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2204_clang-15/shields.json
[img_ubuntu-2204_gcc-11]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2204_gcc-11/shields.json
[img_ubuntu-2204_gcc-10]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/ubuntu-2204_gcc-10/shields.json
[img_windows-2025_msvc17]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/windows-2025_msvc17/shields.json
[img_windows-2022_msvc17]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/windows-2022_msvc17/shields.json
[img_macos-15-arm_clang-18]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/macos-15-arm_clang-18/shields.json
[img_macos-14-arm_clang-15]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/macos-14-arm_clang-15/shields.json
[img_macos-13_clang-15]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/asyncpp/asyncpp/badges/compiler/macos-13_clang-15/shields.json
[Compiler-Support]: https://github.com/asyncpp/asyncpp/actions/workflows/compiler-support.yml

This library also supports Windows 10 / MSVC; the clang-cl support is, however, still a [WIP](https://github.com/llvm/llvm-project/issues/56300).
If possible compatibility with MacOS is ensured, however AppleClang is _special_.

Also checkout the async wrappers for other libraries:
* [asyncpp-curl](https://github.com/asyncpp/asyncpp-curl)
* [asyncpp-grpc](https://github.com/asyncpp/asyncpp-grpc)
* [asyncpp-uring](https://github.com/asyncpp/asyncpp-uring)

The provided tools include:
* Types:
  * [`fire_and_forget_task`](#fire_and_forget_task)
  * [`eager_fire_and_forget_task`](#eager_fire_and_forget_task)
  * [`generator<T>`](#generatort)
  * [`task<T>`](#taskt)
  * [`defer`](#defer)
  * [`promise<T>`](#promiset)
  * [`single_consumer_event`](#single_consumer_event)
  * [`single_consumer_auto_reset_event`](#single_consumer_auto_reset_event)
  * [`multi_consumer_event`](#multi_consumer_event)
  * [`multi_consumer_auto_reset_event`](#multi_consumer_auto_reset_event)
  * [`mutex`](#mutex)
  * [`latch`](#latch)
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
  * [Stop tokens](#stop-tokens)
  * [`thread_pool`](#thread_pool)
  * [`timer`](#timer)

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
Async++ provides a generic promise type similar to `std::promise<T>` but with additional features. You can either `reject()` a promise with an exception or provide a value using `fulfill()`. You can also synchronously wait for the promise using `get()`, which optionally accepts a timeout. Unlike `std::promise` however you can also register a callback using `on_result()` which gets executed immediately after a result is available. It also intergrates nicely with coroutines using `co_await`, which will suspend the current coroutine until a result is provided. Unlike `std::promise`, theres no distinction between future and promise, meaning anyone with access to the promise can resolve it.
### Summary
```cpp
template<typename TResult>
class asyncpp::promise {
public:
  using result_type = TResult;
  promise();
  promise(const promise& other);
  promise& operator=(const promise& other);
  bool is_pending() const noexcept;
  bool is_fulfilled() const noexcept;
  bool is_rejected() const noexcept;
  void fulfill(TResult&& value);
  bool try_fulfill(TResult&& value);
  void reject(std::exception_ptr e);
  bool try_reject(std::exception_ptr e);
  template<typename TException, typename... Args>
  void reject(Args&&... args);
  template<typename TException, typename... Args>
  bool try_reject(Args&&... args);
  void on_result(std::function<void(TResult*, std::exception_ptr)> cb);
  TResult& get() const;
  TResult* get(std::chrono::duration timeout) const;
  std::pair<TResult*, std::exception_ptr> try_get(std::nothrow_t) const noexcept;
  TResult* try_get() const;
  auto operator co_await() const noexcept;

  static promise make_fulfilled(TResult&& value);
  static promise make_rejected(std::exception_ptr ex);
  template<typename TException, typename... Args>
  static promise make_rejected(Args&&... args);
  template<typename... T>
  static promise first(promise<T>... args);
  template<typename... T>
  static promise first_successful(promise<T>... args);
  static promise<std::vector<promise<TResult>>> all(std::vector<promise<TResult>> args);
  static promise<std::vector<TResult>> all_values(std::vector<promise<TResult>> args);
};
```

## `single_consumer_event`
This is similar in concept to a `std::condition_variable` and allows synchronization between coroutines, as well as normal code and coroutines. If the current coroutine co_await's the event it is suspended until some other coroutine or thread calls `set()`. If the event is already set when calling co_await the coroutine will directly continue execution in the current thread. If the event is not set, the coroutine gets resumed on the dispatcher that's passed into `wait()` or inside the call to `set()` if no dispatcher was provided. The operator co_await will behave as if `wait()` was called with the result of `dispatcher::current()`, meaning the coroutine is resumed on the same dispatcher it suspended (not necessarily the same thread, e.g. on a thread pool). If no dispatcher is associated with the current thread it is resumed inside `set()`.
### Summary
```cpp
class asyncpp::single_consumer_event {
public:
  single_consumer_event(bool set_initially = false) noexcept;
  bool is_set() const noexcept;
  bool is_awaited() const noexcept;
  bool set(dispatcher* resume_dispatcher = nullptr) noexcept;
  void reset() noexcept;
  auto operator co_await() noexcept;
  auto wait(dispatcher* resume_dispatcher = nullptr) noexcept;
};
```

## `single_consumer_auto_reset_event`
Similar to `single_consumer_event`, but the event is automatically reset if a coroutine is resumed.

## `multi_consumer_event`
Similar to `single_consumer_event`, but can be awaited by multiple coroutines concurrently. The coroutines are resumed in LIFO order.

## `multi_consumer_auto_reset_event`
Similar to `single_consumer_event`, but can be awaited by multiple coroutines concurrently. The coroutines are resumed in LIFO order. The event is automatically reset if a coroutine is resumed.

## `mutex`
`mutex` provides a simple mutex that can be used inside coroutines to restrict access to a resource. Locking suspends the current coroutine until the mutex is available again. The `mutex` does not depend on being unlocked in the same thread it was locked, allowing it to be locked across suspension points that might switch the coroutine to a different thread (like a network request). `mutex_lock` is a companion class that provides a RAII wrapper similar to `std::lock_guard`.

## `latch`
An async latch is a synchronization primitive that allows coroutines to asynchronously wait until a counter has been decremented to zero. The latch is a single-use object. Once the counter reaches zero the latch becomes 'ready' and will remain ready until the latch is destroyed.
### Summary
```cpp
class asyncpp::latch {
public:
  latch(std::size_t initial) noexcept;
  latch(const latch&) = delete;
  latch& operator=(const latch&) = delete;
  bool is_ready() const noexcept;
  void decrement(std::size_t n = 1) noexcept;
  auto operator co_await() noexcept;
  auto wait(dispatcher* resume_dispatcher = nullptr) noexcept;
};
```

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

## Stop tokens
Async++ provides an implementation of the `stop_token` header in order to support the functionality on libc++ based systems (like MacOS). If the header is natively supported by the used stl the provided types are an alias for the `std` implementation in order to increase compatibility.

## `thread_pool`
`thread_pool` is a dynamic pool of threads that can be resized at runtime and implements the `dispatcher` interface. By default each of the threads has its own queue to reduce locking overhead, but other threads can steal work if they run dry.

## `timer`
`timer` implements a simple timer thread that allows scheduling a callback at a specified time. It also enables a coroutine to wait in asynchronously and supports cancellation of callbacks/coroutine waits. It also implements the `dispatcher` interface.

## Compatibility with shared objects / dll
`asyncpp` uses static thread_local objects in some places. Currently those are
- `dispatcher` To provide the `dispatcher::current()` method
- `fiber` To allow access to the current fiber from within that fiber.

Because on most systems static inline variables can exist multiple times when
shared libraries are used special care must be taken when this is the case.
`asyncpp` provides a special preprocessor flag `ASYNCPP_SO_COMPAT` to help with this issue,
When this flag is defined it turns all static inline variables into regular static variables
that lack a definition, turning them into a unresolved symbol inside the shared object.
You can provide this definition by defining a second macro `ASYNCPP_SO_COMPAT_IMPL` and including
the relevant header files (currently only `dispatcher.h`). Make sure you only do this in a single file
inside the host application or you might get multiple definition errors.
In addition to defining the macros above manually theres is also a cmake option `ASYNCPP_SO_COMPAT`, which
when enabled, defines `ASYNCPP_SO_COMPAT` in all targets that link to asyncpp.
As always when using C++ constructs in multiple shared libraries special care must be take that all of them use
compatible (ideally identical) versions of asyncpp.
# Async++ library
This is a base c++ library providing polyfills and basic task/generator types for the c++20 coroutines.

Also checkout the async wrappers for other libraries:
* [asyncpp-curl](https://github.com/Thalhammer/asyncpp-curl)
* [asyncpp-grpc](https://github.com/Thalhammer/asyncpp-grpc)

Tested and supported compilers:
* Clang 12
* Clang 14

### Provided classes
* `defer` Allows defering the current coroutine to a different dispatcher
* `dispatcher` Base class for implementing a dispatcher
* `fire_and_forget` A coroutine that allows lauching a new async context/fiber/coroutine
* `generator` Sync generator supporting co_yield
* `policy` Allows setting an exception handling policy for `fire_and_forget`
* `ptr_tag` Allows for adding a tag to a pointer
* `sync_wait` Wrap a coroutine in a std::future. Allows for synchronously calling co_await methods.
* `task` Basic task that supports co_await and can be awaited. Can return a result.
* `threadsafe_queue` A basic thread safe queue.
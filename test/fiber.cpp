#include "asyncpp/defer.h"
#include "asyncpp/launch.h"
#include "asyncpp/task.h"
#include "asyncpp/timer.h"
#include <asyncpp/fiber.h>
#include <gtest/gtest.h>

using namespace asyncpp::detail;
using namespace asyncpp;

TEST(ASYNCPP, Fiber) {
	static fiber_context main_ctx{};
	static fiber_context fiber_ctx{};
	static bool fiber_did_enter = false;
	static bool fiber_arg_did_match = false;

	stack_context stack;
	ASSERT_TRUE(fiber_allocate_stack(stack, 10240));
	ASSERT_TRUE(fiber_makecontext(
		&fiber_ctx, stack,
		[](void* ptr) {
			fiber_did_enter = true;
			fiber_arg_did_match = ptr == (void*)0x12345678fedebeef;
			fiber_swapcontext(&fiber_ctx, &main_ctx);
			fiber_did_enter = true;
			fiber_arg_did_match = ptr == (void*)0x12345678fedebeef;
			fiber_swapcontext(&fiber_ctx, &main_ctx);
			fiber_did_enter = true;
			fiber_arg_did_match = ptr == (void*)0x12345678fedebeef;
			fiber_swapcontext(&fiber_ctx, &main_ctx);
		},
		(void*)0x12345678fedebeef));
	ASSERT_TRUE(fiber_swapcontext(&main_ctx, &fiber_ctx));
	ASSERT_TRUE(fiber_did_enter);
	ASSERT_TRUE(fiber_arg_did_match);
	fiber_did_enter = fiber_arg_did_match = false;
	ASSERT_TRUE(fiber_swapcontext(&main_ctx, &fiber_ctx));
	ASSERT_TRUE(fiber_did_enter);
	ASSERT_TRUE(fiber_arg_did_match);
	fiber_did_enter = fiber_arg_did_match = false;
	ASSERT_TRUE(fiber_swapcontext(&main_ctx, &fiber_ctx));
	ASSERT_TRUE(fiber_did_enter);
	ASSERT_TRUE(fiber_arg_did_match);
}

TEST(ASYNCPP, FiberHandleAwaitReadyCanSkipSuspend) {
	static int ready_called = 0;
	static int suspend_called = 0;
	static int resume_called = 0;
	auto hndl = make_fiber_handle(10240, []() {
		struct my_awaitable {
			bool await_ready() const noexcept {
				ready_called++;
				return true;
			}
			void await_suspend(coroutine_handle<>) const noexcept { suspend_called++; }
			void await_resume() const noexcept { resume_called++; }
		};
		fiber_await(my_awaitable{});
	});
	ASSERT_EQ(0, ready_called);
	ASSERT_EQ(0, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_FALSE(hndl.done());
	hndl.resume();
	ASSERT_EQ(1, ready_called);
	ASSERT_EQ(0, suspend_called);
	ASSERT_EQ(1, resume_called);
	ASSERT_TRUE(hndl.done());
	hndl.destroy();
}

TEST(ASYNCPP, FiberHandleSuspendIsCalled) {
	static int ready_called = 0;
	static int suspend_called = 0;
	static int resume_called = 0;
	auto hndl = make_fiber_handle(10240, []() {
		struct my_awaitable {
			bool await_ready() const noexcept {
				ready_called++;
				return false;
			}
			void await_suspend(coroutine_handle<>) const noexcept { suspend_called++; }
			void await_resume() const noexcept { resume_called++; }
		};
		fiber_await(my_awaitable{});
	});
	ASSERT_EQ(0, ready_called);
	ASSERT_EQ(0, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_FALSE(hndl.done());
	hndl.resume();
	ASSERT_EQ(1, ready_called);
	ASSERT_EQ(1, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_FALSE(hndl.done());
	hndl.resume();
	ASSERT_EQ(1, ready_called);
	ASSERT_EQ(1, suspend_called);
	ASSERT_EQ(1, resume_called);
	ASSERT_TRUE(hndl.done());
	hndl.destroy();
}

TEST(ASYNCPP, FiberHandleSuspendReturnFalse) {
	static int ready_called = 0;
	static int suspend_called = 0;
	static int resume_called = 0;
	auto hndl = make_fiber_handle(10240, []() {
		struct my_awaitable {
			bool await_ready() const noexcept {
				ready_called++;
				return false;
			}
			bool await_suspend(coroutine_handle<>) const noexcept {
				suspend_called++;
				return false;
			}
			void await_resume() const noexcept { resume_called++; }
		};
		fiber_await(my_awaitable{});
	});
	ASSERT_EQ(0, ready_called);
	ASSERT_EQ(0, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_FALSE(hndl.done());
	hndl.resume();
	ASSERT_EQ(1, ready_called);
	ASSERT_EQ(1, suspend_called);
	ASSERT_EQ(1, resume_called);
	ASSERT_TRUE(hndl.done());
	hndl.destroy();
}

TEST(ASYNCPP, FiberHandleSuspendReturnTrue) {
	static int ready_called = 0;
	static int suspend_called = 0;
	static int resume_called = 0;
	auto hndl = make_fiber_handle(10240, []() {
		struct my_awaitable {
			bool await_ready() const noexcept {
				ready_called++;
				return false;
			}
			bool await_suspend(coroutine_handle<>) const noexcept {
				suspend_called++;
				return true;
			}
			void await_resume() const noexcept { resume_called++; }
		};
		fiber_await(my_awaitable{});
	});
	ASSERT_EQ(0, ready_called);
	ASSERT_EQ(0, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_FALSE(hndl.done());
	hndl.resume();
	ASSERT_EQ(1, ready_called);
	ASSERT_EQ(1, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_FALSE(hndl.done());
	hndl.resume();
	ASSERT_EQ(1, ready_called);
	ASSERT_EQ(1, suspend_called);
	ASSERT_EQ(1, resume_called);
	ASSERT_TRUE(hndl.done());
	hndl.destroy();
}

TEST(ASYNCPP, FiberHandleSuspendReturnHandle) {
	static int ready_called = 0;
	static int suspend_called = 0;
	static int resume_called = 0;
	static int handle_resume_called = 0;
	static struct dummy_handle_t {
		void (*resume)(dummy_handle_t*) = [](dummy_handle_t*) { handle_resume_called++; };
		void (*destroy)(dummy_handle_t*) = [](dummy_handle_t*) {};
	} dummy_handle;
	auto hndl = make_fiber_handle(10240, []() {
		struct my_awaitable {
			bool await_ready() const noexcept {
				ready_called++;
				return false;
			}
			auto await_suspend(coroutine_handle<>) const noexcept {
				suspend_called++;
				return coroutine_handle<>::from_address(&dummy_handle);
			}
			void await_resume() const noexcept { resume_called++; }
		};
		fiber_await(my_awaitable{});
	});
	ASSERT_EQ(0, ready_called);
	ASSERT_EQ(0, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_EQ(0, handle_resume_called);
	ASSERT_FALSE(hndl.done());
	hndl.resume();
	ASSERT_EQ(1, ready_called);
	ASSERT_EQ(1, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_EQ(1, handle_resume_called);
	ASSERT_FALSE(hndl.done());
	hndl.resume();
	ASSERT_EQ(1, ready_called);
	ASSERT_EQ(1, suspend_called);
	ASSERT_EQ(1, resume_called);
	ASSERT_EQ(1, handle_resume_called);
	ASSERT_TRUE(hndl.done());
	hndl.destroy();
}

TEST(ASYNCPP, FiberHandleSuspendThrows) {
	static int ready_called = 0;
	static int suspend_called = 0;
	static int resume_called = 0;
	static int exception_caught = 0;
	auto hndl = make_fiber_handle(10240, []() {
		struct my_awaitable {
			bool await_ready() const noexcept {
				ready_called++;
				return false;
			}
			void await_suspend(coroutine_handle<>) const {
				suspend_called++;
				throw 42;
			}
			void await_resume() const noexcept { resume_called++; }
		};
		try {
			fiber_await(my_awaitable{});
		} catch (...) { exception_caught++; }
	});
	ASSERT_EQ(0, ready_called);
	ASSERT_EQ(0, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_EQ(0, exception_caught);
	ASSERT_FALSE(hndl.done());
	hndl.resume();
	ASSERT_EQ(1, ready_called);
	ASSERT_EQ(1, suspend_called);
	ASSERT_EQ(0, resume_called);
	ASSERT_EQ(1, exception_caught);
	ASSERT_TRUE(hndl.done());
	hndl.destroy();
}

TEST(ASYNCPP, FiberFull) {
	asyncpp::async_launch_scope scope;
	scope.invoke([]() -> asyncpp::task<> {
		fiber f([]() {
			std::cout << "This is a stackful coroutine, yet I can still await C++20 coroutine awaiters:" << std::endl;
			fib_await asyncpp::timer::get_default().wait(std::chrono::milliseconds(100));
			std::cout << "This is printed after 100 milliseconds" << std::endl;
			return 10;
		});
		std::cout << "You can also await stackful coroutines inside a c++20 coroutine:" << std::endl;
		co_await f;
	});
	scope.join_future().get();
}

TEST(ASYNCPP, FiberAwait) {
	struct debug_dispatcher {
		std::function<void()> next;
		void push(std::function<void()> fn) { next = std::move(fn); }
	};
	debug_dispatcher dp;
	asyncpp::async_launch_scope scope;
	scope.launch(fiber([&dp]() {
		fib_await asyncpp::defer{dp};
		return 10;
	}));
	ASSERT_FALSE(scope.all_done());
	ASSERT_TRUE(dp.next);
	auto fn = std::move(dp.next);
	fn();
	ASSERT_TRUE(scope.all_done());
	ASSERT_FALSE(dp.next);
}

TEST(ASYNCPP, FiberDestroyThrows) {
	struct debug_dispatcher {
		std::function<void()> next;
		void push(std::function<void()> fn) { next = std::move(fn); }
	};
	debug_dispatcher dp;
	bool did_throw = false;
	auto handle = make_fiber_handle(10240, [&dp, &did_throw]() {
		try {
			fib_await asyncpp::defer{dp};
		} catch (...) { did_throw = true; }
	});
	handle.resume();
	ASSERT_TRUE(dp.next);
	ASSERT_FALSE(did_throw);
	handle.destroy();
	ASSERT_TRUE(did_throw);
}
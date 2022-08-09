#include <asyncpp/defer.h>
#include <asyncpp/fire_and_forget.h>
#include <gtest/gtest.h>

using namespace asyncpp;

TEST(ASYNCPP, Defer) {
	struct test_dispatcher {
		bool push_called = false;
		void push(std::function<void()> fn) {
			push_called = true;
			fn();
		}
	};
	test_dispatcher d{};
	[](test_dispatcher& d) -> fire_and_forget_task<> { co_await defer{d}; }(d).start();
	ASSERT_TRUE(d.push_called);
}

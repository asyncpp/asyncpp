#include <asyncpp/defer.h>
#include <asyncpp/launch.h>
#include <asyncpp/task.h>
#include <gtest/gtest.h>
#include <stdexcept>

using namespace asyncpp;

TEST(ASYNCPP, AsyncLaunchScope) {
	struct test_dispatcher {
		std::vector<std::function<void()>> waiting;
		void push(std::function<void()> fn) { waiting.emplace_back(std::move(fn)); }
		bool invoke_all() {
			auto w = std::move(waiting);
			for (auto& e : w)
				e();
			return !w.empty();
		}
	};
	test_dispatcher d{};
	bool spawn_did_execute = false, spawn_did_return = false, join_did_return = false;
	async_launch_scope scope{};

	scope.launch([](test_dispatcher& d, async_launch_scope& scope, bool& spawn_did_execute, bool& spawn_did_return) -> task<void> {
		co_await defer{d};
		spawn_did_execute = true;

		scope.launch([](test_dispatcher& d, async_launch_scope& scope, bool& spawn_did_return) -> task<void> {
			co_await defer{d};
			spawn_did_return = true;
			co_return;
		}(d, scope, spawn_did_return));
	}(d, scope, spawn_did_execute, spawn_did_return));
	launch([](async_launch_scope& scope, bool& join_did_return) -> task<void> {
		co_await scope.join();

		join_did_return = true;
		co_return;
	}(scope, join_did_return));

	ASSERT_FALSE(spawn_did_execute);
	ASSERT_FALSE(spawn_did_return);
	ASSERT_FALSE(join_did_return);
	ASSERT_TRUE(d.invoke_all());

	ASSERT_TRUE(spawn_did_execute);
	ASSERT_FALSE(spawn_did_return);
	ASSERT_FALSE(join_did_return);
	ASSERT_TRUE(d.invoke_all());

	ASSERT_TRUE(spawn_did_execute);
	ASSERT_TRUE(spawn_did_return);
	ASSERT_TRUE(join_did_return);
	ASSERT_FALSE(d.invoke_all());
}

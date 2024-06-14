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

	scope.launch([](test_dispatcher& d, async_launch_scope& scope, bool& spawn_did_execute,
					bool& spawn_did_return) -> task<void> {
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

TEST(ASYNCPP, AsyncLaunchScopeInvoke) {
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
	struct destroy_tracker {
		bool* ptr;
		constexpr explicit destroy_tracker(bool* pval) noexcept : ptr(pval) {}
		destroy_tracker(const destroy_tracker&) = delete;
		destroy_tracker& operator=(const destroy_tracker&) = delete;
		destroy_tracker(destroy_tracker&& other) noexcept : ptr(other.ptr) {
			other.ptr = nullptr;
		}
		destroy_tracker& operator=(destroy_tracker&& other) noexcept  {
			ptr = other.ptr;
			other.ptr = nullptr;
			return *this;
		}
		~destroy_tracker() {
			if(ptr != nullptr) *ptr = true;
		}
	};
	test_dispatcher d{};
	bool did_destroy = false;
	async_launch_scope scope{};

	scope.invoke([t = destroy_tracker{&did_destroy}](test_dispatcher& d) -> task<void> {
		co_await defer{d};
	}, d);

	ASSERT_FALSE(did_destroy);
	ASSERT_TRUE(d.invoke_all());

	ASSERT_TRUE(did_destroy);
}

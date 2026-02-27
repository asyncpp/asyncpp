#include <asyncpp/fire_and_forget.h>
#include <asyncpp/queue.h>

#include <gtest/gtest.h>

using asyncpp::eager_fire_and_forget_task;
using asyncpp::queue;

namespace {
	struct test_dispatcher {
		bool push_called = false;
		void push(std::function<void()> fn) {
			push_called = true;
			fn();
		}
	};
} // namespace

TEST(ASYNCPP, QueuePopWaits) {
	static queue<int> q;
	static std::optional<int> pop_result;
	static bool pop_done = false;
	[]() -> eager_fire_and_forget_task<> {
		pop_result = co_await q.pop();
		pop_done = true;
	}();
	ASSERT_FALSE(pop_result.has_value());
	ASSERT_FALSE(pop_done);

	ASSERT_TRUE(q.try_push(42));
	ASSERT_TRUE(pop_result.has_value());
	ASSERT_TRUE(pop_done);
	ASSERT_EQ(pop_result.value(), 42);
}

TEST(ASYNCPP, QueuePushWaits) {
	static queue<int> q(1);
	static bool push1_done = false;
	static bool push2_done = false;

	ASSERT_FALSE(q.try_pop().has_value());

	[]() -> eager_fire_and_forget_task<> {
		co_await q.push(42);
		push1_done = true;
	}();
	[]() -> eager_fire_and_forget_task<> {
		co_await q.push(43);
		push2_done = true;
	}();
	ASSERT_TRUE(push1_done);
	ASSERT_FALSE(push2_done);
	ASSERT_FALSE(q.empty());
	ASSERT_EQ(q.size(), 2);

	auto res = q.try_pop();
	ASSERT_TRUE(res.has_value());
	ASSERT_EQ(res.value(), 42);
	res.reset();

	ASSERT_TRUE(push1_done);
	ASSERT_TRUE(push2_done);
	ASSERT_FALSE(q.empty());
	ASSERT_EQ(q.size(), 1);

	res = q.try_pop();
	ASSERT_TRUE(res.has_value());
	ASSERT_EQ(res.value(), 43);
	ASSERT_TRUE(q.empty());
	ASSERT_EQ(q.size(), 0);
}

TEST(ASYNCPP, QueueMoveOnly) {
	struct move_only_type {
		move_only_type() = default;
		move_only_type(const move_only_type&) = delete;
		move_only_type(move_only_type&& other) noexcept = default;
		move_only_type& operator=(const move_only_type&) = delete;
		move_only_type& operator=(move_only_type&& other) noexcept = delete;
	};
	static queue<move_only_type> q;

	[]() -> eager_fire_and_forget_task<> { co_await q.push({}); }();
	[]() -> eager_fire_and_forget_task<> { [[maybe_unused]] auto res = co_await q.pop(); }();
	[[maybe_unused]] auto ok = q.try_push({});
	q.try_pop().reset();
	ok = q.try_emplace();
}

TEST(ASYNCPP, QueueClear) {
	static queue<int> q(1);
	static bool did_push = false;

	ASSERT_TRUE(q.try_push(41));
	[]() -> eager_fire_and_forget_task<> {
		co_await q.push(42);
		did_push = true;
	}();
	ASSERT_EQ(q.size(), 2);
	ASSERT_FALSE(did_push);

	q.clear();
	ASSERT_EQ(q.size(), 0);
	ASSERT_TRUE(did_push);
}

TEST(ASYNCPP, QueueMoveConstruct) {
	queue<int> q(1);
	queue<int> q2(std::move(q));
}

TEST(ASYNCPP, QueueMoveAssign) {
	queue<int> q(1);
	bool did_async = false;
	bool async_res;

	ASSERT_TRUE(q.try_push(41));
	[](queue<int>& q, bool& did_async, bool& async_res) -> eager_fire_and_forget_task<> {
		async_res = co_await q.push(42);
		did_async = true;
	}(q, did_async, async_res);
	ASSERT_EQ(q.size(), 2);
	ASSERT_FALSE(did_async);

	queue<int> q2 = std::move(q);
	ASSERT_EQ(q2.size(), 2);
	ASSERT_FALSE(did_async);

	q2 = queue<int>();

	ASSERT_EQ(q.size(), 0);
	ASSERT_TRUE(did_async);
	ASSERT_FALSE(async_res);

	did_async = false;
	[](queue<int>& q2, bool& did_async, bool& async_res) -> eager_fire_and_forget_task<> {
		async_res = (co_await q2.pop()).has_value();
		did_async = true;
	}(q2, did_async, async_res);
	ASSERT_EQ(q.size(), 0);
	ASSERT_FALSE(did_async);

	q2 = queue<int>();
	ASSERT_TRUE(did_async);
	ASSERT_FALSE(async_res);
}

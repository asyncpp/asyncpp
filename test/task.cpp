#include "debug_allocator.h"
#include <asyncpp/sync_wait.h>
#include <asyncpp/task.h>
#include <gtest/gtest.h>

using namespace asyncpp;

namespace {
	task<int> fib_task(int idx) {
		if (idx <= 1) co_return idx;
		co_return co_await fib_task(idx - 1) + co_await fib_task(idx - 2);
	}
} // namespace

TEST(ASYNCPP, TaskRecursive) {
	auto x = as_promise(fib_task(10)).get();
	ASSERT_EQ(x, 55);
}

TEST(ASYNCPP, TaskAllocator) {
	debug_allocator alloc{};
	auto x = as_promise([](allocator_ref<debug_allocator>) -> task<int, allocator_ref<debug_allocator>> {
				 co_return 100;
			 }({alloc}))
				 .get();
	ASSERT_EQ(x, 100);
	ASSERT_EQ(alloc.allocated_sum, alloc.released_sum);
	ASSERT_EQ(alloc.allocated_count, alloc.released_count);
	ASSERT_NE(0, alloc.released_sum);
	ASSERT_NE(0, alloc.released_count);
}

TEST(ASYNCPP, TaskAllocatorDefault) {
	auto x = as_promise([]() -> task<int> { co_return 100; }()).get();
	ASSERT_EQ(x, 100);
	static_assert(std::is_same_v<task<int>::promise_type::allocator_type, default_allocator_type>,
				  "Default allocator is not std::allocator");
}

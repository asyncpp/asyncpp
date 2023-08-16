#include "debug_allocator.h"
#include <asyncpp/async_generator.h>
#include <asyncpp/sync_wait.h>
#include <asyncpp/task.h>
#include <asyncpp/timer.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace asyncpp;

namespace {
	async_generator<int> sample_async_generator(int max) {
		for (int i = 0; i < max; i++) {
			co_await timer::get_default().wait(std::chrono::milliseconds(1));
			co_yield i;
		}
	}
	async_generator<int, allocator_ref<debug_allocator>> sample_async_generator_alloc(int max, debug_allocator&) {
		for (int i = 0; i < max; i++) {
			co_await timer::get_default().wait(std::chrono::milliseconds(1));
			co_yield i;
		}
	}
} // namespace

TEST(ASYNCPP, AsyncGenerator) {
	size_t num = 0;
	auto res_thread = std::this_thread::get_id();
	as_promise([](size_t& num, std::thread::id& res_thread) -> task<> {
		auto t = sample_async_generator(10);
		for (auto it = co_await t.begin(); it != t.end(); co_await ++it)
			num += *it;
		res_thread = std::this_thread::get_id();
	}(num, res_thread))
		.get();
	ASSERT_EQ(num, 45);
	ASSERT_NE(res_thread, std::this_thread::get_id());
}

TEST(ASYNCPP, AsyncGeneratorAllocator) {
	debug_allocator alloc{};
	size_t num = 0;
	auto res_thread = std::this_thread::get_id();
	as_promise([](debug_allocator& alloc, size_t& num, std::thread::id& res_thread) -> task<> {
		auto t = sample_async_generator_alloc(10, alloc);
		for (auto it = co_await t.begin(); it != t.end(); co_await ++it)
			num += *it;
		res_thread = std::this_thread::get_id();
	}(alloc, num, res_thread))
		.get();
	ASSERT_EQ(num, 45);
	ASSERT_NE(res_thread, std::this_thread::get_id());
	ASSERT_EQ(alloc.allocated_sum, alloc.released_sum);
	ASSERT_EQ(alloc.allocated_count, alloc.released_count);
	ASSERT_NE(0, alloc.released_sum);
	ASSERT_NE(0, alloc.released_count);
}

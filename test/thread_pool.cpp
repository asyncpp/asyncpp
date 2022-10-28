#include <asyncpp/defer.h>
#include <asyncpp/sync_wait.h>
#include <asyncpp/task.h>
#include <asyncpp/thread_pool.h>
#include <gtest/gtest.h>

using namespace asyncpp;

TEST(ASYNCPP, ThreadPoolResize) {
	thread_pool pool;
	ASSERT_EQ(pool.size(), std::thread::hardware_concurrency());
	for (int i = 0; i < 20; i++) {
		auto wanted_size = rand() % std::thread::hardware_concurrency() + 1;
		pool.resize(wanted_size);
		ASSERT_EQ(wanted_size, pool.size());
	}
}

TEST(ASYNCPP, ThreadPool) {
	thread_pool pool;
	ASSERT_EQ(pool.size(), std::thread::hardware_concurrency());

	auto f = as_promise([](thread_pool& p) -> task<std::thread::id> {
		co_await defer{p};
		co_return std::this_thread::get_id();
	}(pool));
	auto status = f.wait_for(std::chrono::seconds(1));
	ASSERT_EQ(status, std::future_status::ready);
	ASSERT_NE(f.get(), std::this_thread::get_id());
}

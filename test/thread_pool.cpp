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

	auto id = as_promise([](thread_pool& p) -> task<std::thread::id> {
				  co_await defer{p};
				  co_return std::this_thread::get_id();
			  }(pool))
				  .get();
	ASSERT_NE(id, std::this_thread::get_id());
}

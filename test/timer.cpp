#include <asyncpp/defer.h>
#include <asyncpp/sync_wait.h>
#include <asyncpp/task.h>
#include <asyncpp/timer.h>
#include <chrono>
#include <exception>
#include <gtest/gtest.h>

using namespace asyncpp;

TEST(ASYNCPP, Timer) { timer t; }

TEST(ASYNCPP, TimerPush) {
	timer t;
	auto f = as_promise([](timer& p) -> task<std::thread::id> {
		co_await defer{p};
		co_return std::this_thread::get_id();
	}(t));
	auto status = f.wait_for(std::chrono::seconds(1));
	ASSERT_EQ(status, std::future_status::ready);
	ASSERT_NE(f.get(), std::this_thread::get_id());
}

TEST(ASYNCPP, TimerWait) {
	timer t;
	auto start = std::chrono::steady_clock::now();
	auto f = as_promise([](timer& p) -> task<std::thread::id> {
		bool res = co_await p.wait(std::chrono::milliseconds(50));
		co_return std::this_thread::get_id();
	}(t));
	auto status = f.wait_for(std::chrono::seconds(1));
	ASSERT_EQ(status, std::future_status::ready);
	ASSERT_NE(f.get(), std::this_thread::get_id());
	ASSERT_LE(std::chrono::milliseconds{50}, (std::chrono::steady_clock::now() - start));
}

TEST(ASYNCPP, TimerWaitChrono) {
	auto start = std::chrono::steady_clock::now();
	auto f = as_promise([]() -> task<std::thread::id> {
		bool res = co_await std::chrono::milliseconds(50);
		co_return std::this_thread::get_id();
	}());
	auto status = f.wait_for(std::chrono::seconds(1));
	ASSERT_EQ(status, std::future_status::ready);
	ASSERT_NE(f.get(), std::this_thread::get_id());
	ASSERT_LE(std::chrono::milliseconds{50}, (std::chrono::steady_clock::now() - start));
}

TEST(ASYNCPP, TimerDestroyCancel) {
	auto t = std::make_unique<timer>();
	auto start = std::chrono::steady_clock::now();
	auto f = as_promise([](timer& p) -> task<bool> { co_return co_await p.wait(std::chrono::seconds(1)); }(*t));
	ASSERT_EQ(f.wait_for(std::chrono::milliseconds(5)), std::future_status::timeout);
	t.reset();
	ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	ASSERT_EQ(f.get(), false);
	ASSERT_GE(std::chrono::milliseconds{50}, (std::chrono::steady_clock::now() - start));
}

TEST(ASYNCPP, TimerCancelDestroy) {
	timer t;
	std::stop_source source;
	auto start = std::chrono::steady_clock::now();
	std::promise<bool> res;
	auto f = res.get_future();
	t.schedule([&res](bool ok) mutable { res.set_value(ok); }, std::chrono::seconds(1), source.get_token());
	ASSERT_EQ(f.wait_for(std::chrono::milliseconds(5)), std::future_status::timeout);
	source.request_stop();
	ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	ASSERT_EQ(f.get(), false);
	ASSERT_GE(std::chrono::milliseconds{50}, (std::chrono::steady_clock::now() - start));
}

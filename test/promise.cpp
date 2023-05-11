#include "debug_allocator.h"
#include <asyncpp/promise.h>
#include <asyncpp/sync_wait.h>
#include <chrono>
#include <exception>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

using namespace asyncpp;

TEST(ASYNCPP, PromiseMakeFulfilled) {
	auto p = promise<int>::make_fulfilled(42);
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	auto val = p.get();
	ASSERT_EQ(val, 42);
}

TEST(ASYNCPP, PromiseMakeRejected) {
	auto p = promise<int>::make_rejected<std::runtime_error>("");
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
	ASSERT_THROW(p.get(), std::runtime_error);
}

TEST(ASYNCPP, PromiseAwaitFulfilled) {
	auto p = promise<int>::make_fulfilled(42);
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	auto x = as_promise(p).get();
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	ASSERT_EQ(x, 42);
}

TEST(ASYNCPP, PromiseAwaitRejected) {
	auto p = promise<int>::make_rejected<std::runtime_error>("");
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
	auto x = as_promise(p);
	ASSERT_THROW(x.get(), std::runtime_error);
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
}

TEST(ASYNCPP, PromiseAsyncFulfill) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	std::thread th{[&]() { p.fulfill(42); }};
	th.detach();
	auto x = as_promise(p).get();
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	ASSERT_EQ(x, 42);
}

TEST(ASYNCPP, PromiseAsyncReject) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	std::thread th{[&]() { p.reject<std::runtime_error>(""); }};
	th.detach();
	auto x = as_promise(p);
	ASSERT_THROW(x.get(), std::runtime_error);
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
}

TEST(ASYNCPP, PromiseDoubleFulfill) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	p.fulfill(42);
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	ASSERT_THROW(p.fulfill(32), std::logic_error);
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	ASSERT_EQ(p.get(), 42);
}

TEST(ASYNCPP, PromiseDoubleReject) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	p.reject<std::runtime_error>("");
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
	ASSERT_THROW(p.reject<std::logic_error>(""), std::logic_error);
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
	ASSERT_THROW(p.get(), std::runtime_error);
}

TEST(ASYNCPP, PromiseDoubleFulfillReject) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	p.fulfill(42);
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	ASSERT_THROW(p.reject<std::runtime_error>(""), std::logic_error);
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	ASSERT_EQ(p.get(), 42);
}

TEST(ASYNCPP, PromiseDoubleRejectFulfill) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	p.reject<std::runtime_error>("");
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
	ASSERT_THROW(p.fulfill(42), std::logic_error);
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
	ASSERT_THROW(p.get(), std::runtime_error);
}

TEST(ASYNCPP, PromiseFulfillTimeout) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	std::mutex wait_mtx;
	wait_mtx.lock();
	std::thread([&]() {
		wait_mtx.lock();
		wait_mtx.unlock();
		p.fulfill(42);
	}).detach();
	auto res = p.get(std::chrono::milliseconds(1));
	wait_mtx.unlock();
	ASSERT_EQ(res, nullptr);
	res = p.get(std::chrono::milliseconds(500));
	ASSERT_NE(res, nullptr);
}

TEST(ASYNCPP, PromiseRejectTimeout) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	std::mutex wait_mtx;
	wait_mtx.lock();
	std::thread([&]() {
		wait_mtx.lock();
		wait_mtx.unlock();
		p.reject<std::runtime_error>("");
	}).detach();
	auto res = p.get(std::chrono::milliseconds(1));
	wait_mtx.unlock();
	ASSERT_EQ(res, nullptr);
	ASSERT_THROW(p.get(std::chrono::milliseconds(500)), std::runtime_error);
}

TEST(ASYNCPP, PromiseCallbackFulfilled) {
	auto p = promise<int>::make_fulfilled(42);
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	bool was_called = false;
	p.on_result([&was_called](int* res, std::exception_ptr ex) {
		ASSERT_NE(res, nullptr);
		ASSERT_EQ(*res, 42);
		ASSERT_EQ(ex, nullptr);
		was_called = true;
	});
	ASSERT_TRUE(was_called);
}

TEST(ASYNCPP, PromiseCallbackRejected) {
	auto p = promise<int>::make_rejected<std::runtime_error>("");
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
	bool was_called = false;
	p.on_result([&was_called](int* res, std::exception_ptr ex) {
		ASSERT_EQ(res, nullptr);
		ASSERT_NE(ex, nullptr);
		try {
			std::rethrow_exception(ex);
		} catch (const std::runtime_error& e) {

		} catch (...) { ASSERT_FALSE(true) << "Invalid exception received"; }
		was_called = true;
	});
	ASSERT_TRUE(was_called);
}

TEST(ASYNCPP, PromiseCallbackFulfill) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	bool was_called = false;
	p.on_result([&was_called](int* res, std::exception_ptr ex) {
		ASSERT_NE(res, nullptr);
		ASSERT_EQ(*res, 42);
		ASSERT_EQ(ex, nullptr);
		was_called = true;
	});
	ASSERT_FALSE(was_called);
	p.fulfill(42);
	ASSERT_FALSE(p.is_pending());
	ASSERT_TRUE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
}

TEST(ASYNCPP, PromiseCallbackReject) {
	promise<int> p;
	ASSERT_TRUE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_FALSE(p.is_rejected());
	bool was_called = false;
	p.on_result([&was_called](int* res, std::exception_ptr ex) {
		ASSERT_EQ(res, nullptr);
		ASSERT_NE(ex, nullptr);
		try {
			std::rethrow_exception(ex);
		} catch (const std::runtime_error& e) {

		} catch (...) { ASSERT_FALSE(true) << "Invalid exception received"; }
		was_called = true;
	});
	ASSERT_FALSE(was_called);
	p.reject<std::runtime_error>("");
	ASSERT_FALSE(p.is_pending());
	ASSERT_FALSE(p.is_fulfilled());
	ASSERT_TRUE(p.is_rejected());
}

TEST(ASYNCPP, PromiseFirst) {
	{
		promise<int> p1;
		promise<int> p2;
		auto pall = promise<int>::first(p1, p2);
		ASSERT_TRUE(pall.is_pending());
		p1.fulfill(42);
		ASSERT_TRUE(pall.is_fulfilled());
		ASSERT_EQ(pall.get(), 42);
	}
	{
		promise<int> p1;
		promise<int> p2;
		auto pall = promise<int>::first(p1, p2);
		ASSERT_TRUE(pall.is_pending());
		p2.fulfill(42);
		ASSERT_TRUE(pall.is_fulfilled());
		ASSERT_EQ(pall.get(), 42);
	}
	{
		promise<int> p1;
		promise<int> p2;
		auto pall = promise<int>::first(p1, p2);
		ASSERT_TRUE(pall.is_pending());
		p1.reject<int>(42);
		ASSERT_TRUE(pall.is_rejected());
		bool hit = false;
		try {
			pall.get();
		} catch (int i) {
			hit = true;
			ASSERT_EQ(i, 42);
		}
		ASSERT_TRUE(hit);
	}
	{
		promise<int> p1;
		promise<int> p2;
		auto pall = promise<int>::first(p1, p2);
		ASSERT_TRUE(pall.is_pending());
		p2.reject<int>(42);
		ASSERT_TRUE(pall.is_rejected());
		bool hit = false;
		try {
			pall.get();
		} catch (int i) {
			hit = true;
			ASSERT_EQ(i, 42);
		}
		ASSERT_TRUE(hit);
	}
}

TEST(ASYNCPP, PromiseFirstSuccessful) {
	{
		promise<int> p1;
		promise<int> p2;
		auto pall = promise<int>::first_successful(p1, p2);
		ASSERT_TRUE(pall.is_pending());
		p1.fulfill(42);
		ASSERT_TRUE(pall.is_fulfilled());
		p2.reject<int>(43);
		ASSERT_TRUE(pall.is_fulfilled());
		ASSERT_EQ(pall.get(), 42);
	}
	{
		promise<int> p1;
		promise<int> p2;
		auto pall = promise<int>::first_successful(p1, p2);
		ASSERT_TRUE(pall.is_pending());
		p1.reject<int>(43);
		ASSERT_TRUE(pall.is_pending());
		p2.fulfill(42);
		ASSERT_TRUE(pall.is_fulfilled());
		ASSERT_EQ(pall.get(), 42);
	}
	{
		promise<int> p1;
		promise<int> p2;
		auto pall = promise<int>::first_successful(p1, p2);
		ASSERT_TRUE(pall.is_pending());
		p2.reject<int>(43);
		ASSERT_TRUE(pall.is_pending());
		p1.reject<int>(42);
		ASSERT_TRUE(pall.is_rejected());
		bool hit = false;
		try {
			pall.get();
		} catch (int i) {
			hit = true;
			ASSERT_EQ(i, 42);
		}
		ASSERT_TRUE(hit);
	}
}

TEST(ASYNCPP, PromiseAll) {
	promise<int> p1, p2;
	auto all = promise<int>::all({p1, p2});
	ASSERT_TRUE(p1.is_pending());
	ASSERT_TRUE(p2.is_pending());
	ASSERT_TRUE(all.is_pending());
	p1.fulfill(1);
	ASSERT_FALSE(p1.is_pending());
	ASSERT_TRUE(p2.is_pending());
	ASSERT_TRUE(all.is_pending());
	p2.fulfill(2);
	ASSERT_FALSE(p1.is_pending());
	ASSERT_FALSE(p2.is_pending());
	ASSERT_FALSE(all.is_pending());
}

TEST(ASYNCPP, PromiseTryGet) {
	promise<int> p;
	auto val = p.try_get();
	ASSERT_EQ(val, nullptr);
	p.fulfill(42);
	val = p.try_get();
	ASSERT_EQ(*val, 42);

	p = promise<int>::make_rejected<int>(42);
	ASSERT_THROW(p.try_get(), int);
	auto res = p.try_get(std::nothrow);
	ASSERT_EQ(res.first, nullptr);
	ASSERT_NE(res.second, nullptr);
}

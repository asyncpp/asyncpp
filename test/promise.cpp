#include "debug_allocator.h"
#include <asyncpp/promise.h>
#include <asyncpp/sync_wait.h>
#include <chrono>
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

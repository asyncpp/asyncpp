#include <asyncpp/fire_and_forget.h>
#include <asyncpp/mutex.h>
#include <gtest/gtest.h>
#include <mutex>
#include <stdexcept>

using namespace asyncpp;

TEST(ASYNCPP, Mutex) {
	mutex mtx;
	// Initial state is unlocked
	ASSERT_FALSE(mtx.is_locked());

	// try_lock with no concurrency in unlocked state should always succeed
	ASSERT_TRUE(mtx.try_lock());
	ASSERT_TRUE(mtx.is_locked());

	// try_lock with locked mutex should fail
	ASSERT_FALSE(mtx.try_lock());

	// One waiting coroutine is resumed on unlock
	{
		bool done = false;
		bool done2 = false;
		[](mutex& mtx, bool& done) -> eager_fire_and_forget_task<> {
			co_await mtx.lock();
			done = true;
		}(mtx, done);
		[](mutex& mtx, bool& done2) -> eager_fire_and_forget_task<> {
			co_await mtx.lock();
			done2 = true;
		}(mtx, done2);
		ASSERT_FALSE(done);
		ASSERT_FALSE(done2);
		mtx.unlock();
		ASSERT_TRUE(done);
		ASSERT_FALSE(done2);
		// Now locked by coro1
		ASSERT_TRUE(mtx.is_locked());
		mtx.unlock();
		ASSERT_TRUE(done);
		ASSERT_TRUE(done2);
		// Now locked by coro2
		ASSERT_TRUE(mtx.is_locked());
	}
}

TEST(ASYNCPP, MutexLock) {
	mutex mtx;
	ASSERT_TRUE(mtx.try_lock());
	bool was_locked = false;
	bool was_locked_mtx = false;
	[](mutex& mtx, bool& was_locked, bool& was_locked_mtx) -> eager_fire_and_forget_task<> {
		auto lck = co_await mtx.lock_scoped();
		was_locked = lck.is_locked();
		was_locked_mtx = lck.mutex().is_locked();
	}(mtx, was_locked, was_locked_mtx);
	ASSERT_FALSE(was_locked);
	ASSERT_FALSE(was_locked_mtx);
	mtx.unlock();
	ASSERT_TRUE(was_locked);
	ASSERT_TRUE(was_locked_mtx);
	ASSERT_FALSE(mtx.is_locked());
	ASSERT_TRUE(mtx.try_lock());
	{
		mutex_lock lck(mtx, std::adopt_lock);
		ASSERT_FALSE(mtx.try_lock());
		ASSERT_TRUE(lck.is_locked());
		lck.unlock();
		ASSERT_FALSE(lck.is_locked());
		ASSERT_FALSE(mtx.is_locked());
		mtx.try_lock();
		ASSERT_FALSE(lck.is_locked());
		ASSERT_TRUE(mtx.is_locked());
		ASSERT_FALSE(lck.try_lock());
		mtx.unlock();
		ASSERT_TRUE(lck.try_lock());
		ASSERT_TRUE(lck.is_locked());
		ASSERT_TRUE(mtx.is_locked());
	}
	ASSERT_FALSE(mtx.is_locked());
}

#include <asyncpp/fire_and_forget.h>
#include <gtest/gtest.h>

using namespace asyncpp;

TEST(ASYNCPP, FireAndForget) {
	bool executed = false;
	[&]() -> fire_and_forget_task<> {
		executed = true;
		co_return;
	}().start();
	ASSERT_TRUE(executed);
	executed = false;
	[&]() -> eager_fire_and_forget_task<> {
		executed = true;
		co_return;
	}();
	ASSERT_TRUE(executed);
	executed = false;
	[&]() -> fire_and_forget_task<> {
		executed = true;
		co_return;
	}();
	ASSERT_FALSE(executed);
}

#include <asyncpp/fire_and_forget.h>
#include <gtest/gtest.h>

using namespace asyncpp;

TEST(ASYNCPP, FireAndForget) {
	bool executed = false;
	[](bool& executed) -> fire_and_forget_task<> {
		executed = true;
		co_return;
	}(executed)
				 .start();
	ASSERT_TRUE(executed);
	executed = false;
	[](bool& executed) -> eager_fire_and_forget_task<> {
		executed = true;
		co_return;
	}(executed);
	ASSERT_TRUE(executed);
	executed = false;
	[](bool& executed) -> fire_and_forget_task<> {
		executed = true;
		co_return;
	}(executed);
	ASSERT_FALSE(executed);
}

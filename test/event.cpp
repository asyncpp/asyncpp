#include <asyncpp/event.h>
#include <asyncpp/launch.h>
#include <asyncpp/task.h>
#include <gtest/gtest.h>

using namespace asyncpp;

namespace {
	struct test_dispatcher : dispatcher {
		bool push_called = false;
		void push(std::function<void()> fn) override {
			push_called = true;
			fn();
		}
	};
} // namespace

TEST(ASYNCPP, SingleConsumerEvent) {
	single_consumer_event evt;
	ASSERT_FALSE(evt.is_set());
	evt.set();
	ASSERT_TRUE(evt.is_set());
	evt.reset();
	ASSERT_FALSE(evt.is_set());

	bool did_call = false;
	launch([](bool& did_call, single_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_FALSE(did_call);
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(evt.is_set());

	did_call = false;
	launch([](bool& did_call, single_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_TRUE(did_call);
}

TEST(ASYNCPP, SingleConsumerEventDispatcher) {
	single_consumer_event evt;
	test_dispatcher disp;

	bool did_call = false;
	launch([](bool& did_call, single_consumer_event& evt, dispatcher& disp) -> task<void> {
		co_await evt.wait(&disp);
		did_call = true;
	}(did_call, evt, disp));
	ASSERT_FALSE(did_call);
	ASSERT_FALSE(disp.push_called);
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(disp.push_called);
	ASSERT_TRUE(evt.is_set());

	did_call = false;
	disp.push_called = false;
	launch([](bool& did_call, single_consumer_event& evt, dispatcher& disp) -> task<void> {
		co_await evt.wait(&disp);
		did_call = true;
	}(did_call, evt, disp));
	ASSERT_TRUE(did_call);
	// No dispatcher invoke because the event was already signaled
	ASSERT_FALSE(disp.push_called);
}

TEST(ASYNCPP, SingleConsumerEventInitiallySet) {
	single_consumer_event evt(true);
	ASSERT_TRUE(evt.is_set());
}

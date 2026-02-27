#include <asyncpp/event.h>
#include <asyncpp/launch.h>
#include <asyncpp/task.h>

#include <gtest/gtest.h>

using asyncpp::dispatcher;
using asyncpp::multi_consumer_auto_reset_event;
using asyncpp::multi_consumer_event;
using asyncpp::single_consumer_auto_reset_event;
using asyncpp::single_consumer_event;
using asyncpp::task;

namespace {
	struct test_dispatcher : dispatcher {
		bool push_called = false;
		void push(std::function<void()> callback) override {
			push_called = true;
			callback();
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
	ASSERT_FALSE(evt.is_awaited());

	bool did_call = false;
	launch([](bool& did_call, single_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_FALSE(did_call);
	ASSERT_TRUE(evt.is_awaited());
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	did_call = false;
	launch([](bool& did_call, single_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_TRUE(did_call);
	ASSERT_FALSE(evt.is_awaited());
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

TEST(ASYNCPP, SingleConsumerAutoResetEvent) {
	single_consumer_auto_reset_event evt;
	ASSERT_FALSE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());
	evt.set();
	ASSERT_TRUE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());
	evt.reset();
	ASSERT_FALSE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	bool did_call = false;
	launch([](bool& did_call, single_consumer_auto_reset_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_FALSE(did_call);
	ASSERT_TRUE(evt.is_awaited());
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_FALSE(evt.is_awaited());
	ASSERT_FALSE(evt.is_set());

	did_call = false;
	evt.set();
	ASSERT_TRUE(evt.is_set());
	launch([](bool& did_call, single_consumer_auto_reset_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_TRUE(did_call);
	ASSERT_FALSE(evt.is_awaited());
	ASSERT_FALSE(evt.is_set());
}

TEST(ASYNCPP, SingleConsumerAutoResetEventDispatcher) {
	single_consumer_auto_reset_event evt;
	test_dispatcher disp;

	bool did_call = false;
	launch([](bool& did_call, single_consumer_auto_reset_event& evt, dispatcher& disp) -> task<void> {
		co_await evt.wait(&disp);
		did_call = true;
	}(did_call, evt, disp));
	ASSERT_FALSE(did_call);
	ASSERT_FALSE(disp.push_called);
	ASSERT_FALSE(evt.is_set());
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(disp.push_called);
	ASSERT_FALSE(evt.is_set());

	did_call = false;
	disp.push_called = false;
	evt.set();
	launch([](bool& did_call, single_consumer_auto_reset_event& evt, dispatcher& disp) -> task<void> {
		co_await evt.wait(&disp);
		did_call = true;
	}(did_call, evt, disp));
	ASSERT_TRUE(did_call);
	// No dispatcher invoke because the event was already signaled
	ASSERT_FALSE(disp.push_called);
}

TEST(ASYNCPP, SingleConsumerAutoResetEventInitiallySet) {
	single_consumer_auto_reset_event evt(true);
	ASSERT_TRUE(evt.is_set());
}

TEST(ASYNCPP, MultiConsumerEvent) {
	multi_consumer_event evt;
	ASSERT_FALSE(evt.is_set());
	evt.set();
	ASSERT_TRUE(evt.is_set());
	evt.reset();
	ASSERT_FALSE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	// Single awaiter
	bool did_call = false;
	launch([](bool& did_call, multi_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_FALSE(did_call);
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	did_call = false;
	launch([](bool& did_call, multi_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_TRUE(did_call);
	ASSERT_FALSE(evt.is_awaited());

	evt.reset();

	// Multiple awaiters
	bool did_call2 = false;
	did_call = false;
	launch([](bool& did_call, multi_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	launch([](bool& did_call, multi_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call2, evt));
	ASSERT_FALSE(did_call);
	ASSERT_FALSE(did_call2);
	ASSERT_TRUE(evt.is_awaited());
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(did_call2);
	ASSERT_TRUE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	did_call = false;
	did_call2 = false;
	launch([](bool& did_call, multi_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	launch([](bool& did_call, multi_consumer_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call2, evt));
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(did_call2);
	ASSERT_FALSE(evt.is_awaited());
}

TEST(ASYNCPP, MultiConsumerEventInitiallySet) {
	multi_consumer_event evt(true);
	ASSERT_TRUE(evt.is_set());
}

TEST(ASYNCPP, MultiConsumerEventDispatcher) {
	multi_consumer_event evt;
	test_dispatcher disp;

	bool did_call = false;
	launch([](bool& did_call, multi_consumer_event& evt, dispatcher& disp) -> task<void> {
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
	launch([](bool& did_call, multi_consumer_event& evt, dispatcher& disp) -> task<void> {
		co_await evt.wait(&disp);
		did_call = true;
	}(did_call, evt, disp));
	ASSERT_TRUE(did_call);
	// No dispatcher invoke because the event was already signaled
	ASSERT_FALSE(disp.push_called);
}

TEST(ASYNCPP, MultiConsumerAutoResetEvent) {
	multi_consumer_auto_reset_event evt;
	ASSERT_FALSE(evt.is_set());
	evt.set();
	ASSERT_TRUE(evt.is_set());
	evt.reset();
	ASSERT_FALSE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	// Single awaiter
	bool did_call = false;
	launch([](bool& did_call, multi_consumer_auto_reset_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_FALSE(did_call);
	ASSERT_TRUE(evt.is_awaited());
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_FALSE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	// Check that setting non awaited even does nothing
	did_call = false;
	evt.set();
	ASSERT_FALSE(did_call);
	ASSERT_TRUE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	// Check that reseting the even works
	evt.reset();
	ASSERT_FALSE(did_call);
	ASSERT_FALSE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	// Check that already set events continue immediately and reset the event
	evt.set();
	did_call = false;
	launch([](bool& did_call, multi_consumer_auto_reset_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_TRUE(did_call);
	ASSERT_FALSE(evt.is_set());
	ASSERT_FALSE(evt.is_awaited());

	// Multiple awaiters
	did_call = false;
	bool did_call2 = false;
	launch([](bool& did_call, multi_consumer_auto_reset_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	launch([](bool& did_call2, multi_consumer_auto_reset_event& evt) -> task<void> {
		co_await evt;
		did_call2 = true;
	}(did_call2, evt));
	ASSERT_FALSE(did_call);
	ASSERT_FALSE(did_call2);
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(did_call2);
	ASSERT_FALSE(evt.is_set());

	evt.set();

	// When awaiting an already set event only the first one should continue right away,
	// because the event gets reset automatically after the first await. The second await will suspend.
	did_call = false;
	did_call2 = false;
	launch([](bool& did_call, multi_consumer_auto_reset_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call, evt));
	ASSERT_TRUE(did_call);
	ASSERT_FALSE(did_call2);
	ASSERT_FALSE(evt.is_set());
	launch([](bool& did_call, multi_consumer_auto_reset_event& evt) -> task<void> {
		co_await evt;
		did_call = true;
	}(did_call2, evt));
	ASSERT_TRUE(did_call);
	ASSERT_FALSE(did_call2);
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(did_call2);
	ASSERT_FALSE(evt.is_set());
}

TEST(ASYNCPP, MultiConsumerAutoResetEventInitiallySet) {
	multi_consumer_auto_reset_event evt(true);
	ASSERT_TRUE(evt.is_set());
}

TEST(ASYNCPP, MultiConsumerAutoResetEventDispatcher) {
	multi_consumer_auto_reset_event evt;
	test_dispatcher disp;

	bool did_call = false;
	launch([](bool& did_call, multi_consumer_auto_reset_event& evt, dispatcher& disp) -> task<void> {
		co_await evt.wait(&disp);
		did_call = true;
	}(did_call, evt, disp));
	ASSERT_FALSE(did_call);
	ASSERT_FALSE(disp.push_called);
	ASSERT_FALSE(evt.is_set());
	evt.set();
	ASSERT_TRUE(did_call);
	ASSERT_TRUE(disp.push_called);
	ASSERT_FALSE(evt.is_set());

	did_call = false;
	disp.push_called = false;
	evt.set();
	launch([](bool& did_call, multi_consumer_auto_reset_event& evt, dispatcher& disp) -> task<void> {
		co_await evt.wait(&disp);
		did_call = true;
	}(did_call, evt, disp));
	ASSERT_TRUE(did_call);
	// No dispatcher invoke because the event was already signaled
	ASSERT_FALSE(disp.push_called);
}

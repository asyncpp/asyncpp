#include <asyncpp/scope_guard.h>
#include <gtest/gtest.h>

using namespace asyncpp;

namespace {
	void sample_noexcept_fn() noexcept;
}

TEST(ASYNCPP, ScopeGuard) {
	static bool did_call;
	did_call = false;
	// Call on exit
	{
		scope_guard guard{[]() noexcept { did_call = true; }};
		ASSERT_TRUE(guard.is_engaged());
		ASSERT_FALSE(did_call);
	}
	ASSERT_TRUE(did_call);
	did_call = false;
	// disengage
	{
		scope_guard guard{[]() noexcept { did_call = true; }};
		ASSERT_TRUE(guard.is_engaged());
		ASSERT_FALSE(did_call);
		guard.disengage();
		ASSERT_FALSE(guard.is_engaged());
		ASSERT_FALSE(did_call);
	}
	ASSERT_FALSE(did_call);
	did_call = false;
	// disengage & engage
	{
		scope_guard guard{[]() noexcept { did_call = true; }};
		ASSERT_TRUE(guard.is_engaged());
		ASSERT_FALSE(did_call);
		guard.disengage();
		ASSERT_FALSE(guard.is_engaged());
		ASSERT_FALSE(did_call);
		guard.engage();
		ASSERT_TRUE(guard.is_engaged());
		ASSERT_FALSE(did_call);
	}
	ASSERT_TRUE(did_call);
	did_call = false;
	// No default engage
	{
		scope_guard guard{[]() noexcept { did_call = true; }, false};
		ASSERT_FALSE(guard.is_engaged());
		ASSERT_FALSE(did_call);
	}
	ASSERT_FALSE(did_call);
	did_call = false;
	// No default engage & engage
	{
		scope_guard guard{[]() noexcept { did_call = true; }, false};
		ASSERT_FALSE(guard.is_engaged());
		ASSERT_FALSE(did_call);
		guard.engage();
		ASSERT_TRUE(guard.is_engaged());
		ASSERT_FALSE(did_call);
	}
	ASSERT_TRUE(did_call);
	did_call = false;
	// No default engage & engage & disengage
	{
		scope_guard guard{[]() noexcept { did_call = true; }, false};
		ASSERT_FALSE(guard.is_engaged());
		ASSERT_FALSE(did_call);
		guard.engage();
		ASSERT_TRUE(guard.is_engaged());
		ASSERT_FALSE(did_call);
		guard.disengage();
		ASSERT_FALSE(guard.is_engaged());
		ASSERT_FALSE(did_call);
	}
	ASSERT_FALSE(did_call);
	did_call = false;
	// Get function
	{
		void (*fn)() noexcept = []() noexcept { did_call = true; };
		scope_guard guard{fn, false};
		ASSERT_EQ(fn, guard.function());
	}
	// Get function const
	{
		void (*fn)() noexcept = []() noexcept { did_call = true; };
		const scope_guard guard{fn, false};
		ASSERT_EQ(fn, guard.function());
	}
}

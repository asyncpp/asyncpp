#include <asyncpp/ptr_tag.h>
#include <gtest/gtest.h>

using namespace asyncpp;

namespace {
	struct test {
		size_t x;
	};

	enum class tag {
		test0,
		test1
	};
}

TEST(ASYNCPP, PtrTag) {
	test t;
	auto tagged = asyncpp::ptr_tag<1>(&t);
	static_assert(std::is_same_v<decltype(tagged), void*>, "ptr_tag needs to return a void pointer");
	ASSERT_NE(&t, tagged);
	ASSERT_EQ(asyncpp::ptr_get_tag<test>(tagged), 1);
	auto [untagged, id] = asyncpp::ptr_untag<test>(tagged);
	ASSERT_EQ(untagged, &t);
	ASSERT_EQ(id, 1);
}

TEST(ASYNCPP, PtrTagEnum) {
	test t;
	auto tagged = asyncpp::ptr_tag<tag::test1>(&t);
	static_assert(std::is_same_v<decltype(tagged), void*>, "ptr_tag needs to return a void pointer");
	ASSERT_NE(&t, tagged);
	ASSERT_EQ(asyncpp::ptr_get_tag<test>(tagged), static_cast<size_t>(tag::test1));
	auto [untagged, id] = asyncpp::ptr_untag<test, tag>(tagged);
	ASSERT_EQ(untagged, &t);
	ASSERT_EQ(id, tag::test1);
}

#include <asyncpp/ref.h>
#include <gtest/gtest.h>

using namespace asyncpp;

namespace {
	static bool did_destroy;
	struct test : intrusive_refcount<test> {
		~test() noexcept { did_destroy = true; }
		using intrusive_refcount::use_count;
	};
} // namespace

TEST(ASYNCPP, RefCounted) {
	did_destroy = false;
	ref hdl{new test()};
	ASSERT_TRUE(hdl);
	ASSERT_EQ(hdl->use_count(), 1);
	ref hdl2{new test()};
	ASSERT_TRUE(hdl2);
	ASSERT_EQ(hdl2->use_count(), 1);
	ASSERT_NE(hdl.get(), hdl2.get());
	hdl = hdl2;
	ASSERT_TRUE(hdl);
	ASSERT_TRUE(hdl2);
	ASSERT_TRUE(did_destroy);
	did_destroy = false;
	ASSERT_EQ(hdl->use_count(), 2);
	ASSERT_EQ(hdl.get(), hdl2.get());
	hdl.reset();
	ASSERT_FALSE(hdl);
	ASSERT_TRUE(hdl2);
	ASSERT_FALSE(did_destroy);

	ASSERT_EQ(hdl2.get(), hdl2.operator->());

	hdl2.reset();
	ASSERT_FALSE(hdl2);
	ASSERT_TRUE(did_destroy);
	did_destroy = false;

	hdl = {new test()};
	ASSERT_TRUE(hdl);
	ASSERT_EQ(hdl->use_count(), 1);
	auto ptr = hdl.release();
	ASSERT_FALSE(did_destroy);
	ASSERT_FALSE(hdl);
	ASSERT_NE(ptr, nullptr);
	ASSERT_EQ(ptr->use_count(), 1);
	delete ptr;
	ASSERT_TRUE(did_destroy);
}

TEST(ASYNCPP, RefCountedNoexcept) {
	static_assert(ref<test>::add_ref_noexcept);
	static_assert(ref<test>::remove_ref_noexcept);
}

TEST(ASYNCPP, AtomicRefCounted) {
	did_destroy = false;
	atomic_ref hdl{ref(new test())};
	ASSERT_TRUE(hdl.load());
	ASSERT_EQ(hdl->use_count(), 2);
	atomic_ref hdl2{ref(new test())};
	ASSERT_TRUE(hdl2.load());
	ASSERT_EQ(hdl2->use_count(), 2);
	ASSERT_NE(hdl.load(), hdl2.load());
	hdl.store(hdl2);
	ASSERT_TRUE(hdl.load());
	ASSERT_TRUE(hdl2);
	ASSERT_TRUE(did_destroy);
	did_destroy = false;
	ASSERT_EQ(hdl->use_count(), 3);
	ASSERT_EQ(hdl, hdl2);
	hdl.reset();
	ASSERT_FALSE(hdl.load());
	ASSERT_TRUE(hdl2);
	ASSERT_FALSE(did_destroy);

	ASSERT_EQ(hdl2.load(), hdl2.operator->());

	hdl2.reset();
	ASSERT_FALSE(hdl2.load());
	ASSERT_TRUE(did_destroy);
	did_destroy = false;

	hdl = ref{new test()};
	ASSERT_TRUE(hdl);
	ASSERT_EQ(hdl->use_count(), 2);
	auto ptr = hdl.release();
	ASSERT_FALSE(did_destroy);
	ASSERT_FALSE(hdl.load());
	ASSERT_NE(ptr, nullptr);
	ASSERT_EQ(ptr->use_count(), 1);
	delete ptr;
	ASSERT_TRUE(did_destroy);
}

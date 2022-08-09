#include <asyncpp/generator.h>
#include <gtest/gtest.h>
#include "debug_allocator.h"

using namespace asyncpp;

namespace {
	generator<int> sample_generator(int max) {
		for (int i = 0; i < max; i++)
			co_yield i;
	}
	generator<int, allocator_ref<debug_allocator>> sample_generator_alloc(int max, debug_allocator&) {
		for (int i = 0; i < max; i++)
			co_yield i;
	}
} // namespace

TEST(ASYNCPP, Generator) {
	for (auto e : sample_generator(100))
		;
}

TEST(ASYNCPP, GeneratorAllocator) {
	debug_allocator alloc{};
	for (auto e : sample_generator_alloc(100, alloc))
		;
	ASSERT_EQ(alloc.allocated_sum, alloc.released_sum);
	ASSERT_NE(0, alloc.released_sum);
}
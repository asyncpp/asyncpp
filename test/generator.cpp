#include <asyncpp/generator.h>
#include <gtest/gtest.h>

using namespace asyncpp;

namespace {
	generator<int> sample_generator(int max) {
		for (int i = 0; i < max; i++)
			co_yield i;
	}
} // namespace

TEST(ASYNCPP, Generator) {
	for (auto e : sample_generator(100))
		;
}

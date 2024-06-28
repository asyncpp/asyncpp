#include <asyncpp/channel.h>
#include <asyncpp/fire_and_forget.h>
#include <gtest/gtest.h>

using namespace asyncpp;

namespace {
	struct test_dispatcher {
		bool push_called = false;
		void push(std::function<void()> fn) {
			push_called = true;
			fn();
		}
	};
} // namespace

TEST(ASYNCPP, ChannelBasic) {
	static channel<int> chan;
	static std::optional<int> read_value{};
	static bool read_did_finish = false;
	static bool write_did_finish = false;

	[]() -> fire_and_forget_task<> {
		read_value = co_await chan.read();
		read_did_finish = true;
	}()
				.start();
	ASSERT_FALSE(read_value.has_value());
	ASSERT_FALSE(read_did_finish);
	[]() -> fire_and_forget_task<> {
		co_await chan.write(42);
		write_did_finish = true;
	}()
				.start();
	ASSERT_TRUE(read_value.has_value());
	ASSERT_TRUE(read_did_finish);
	ASSERT_TRUE(write_did_finish);
}

TEST(ASYNCPP, ChannelMultipleRead) {
	static channel<int> chan;
	static std::optional<int> read_value{};
	static bool read_did_finish = false;
	static bool read2_did_finish = false;
	static bool write_did_finish = false;

	[]() -> fire_and_forget_task<> {
		read_value = co_await chan.read();
		read_did_finish = true;
	}()
				.start();
	ASSERT_FALSE(read_value.has_value());
	ASSERT_FALSE(read_did_finish);
	[]() -> fire_and_forget_task<> {
		read_value = co_await chan.read();
		read2_did_finish = true;
	}()
				.start();
	ASSERT_FALSE(read_value.has_value());
	ASSERT_FALSE(read_did_finish);
	ASSERT_FALSE(read2_did_finish);

	[]() -> fire_and_forget_task<> {
		co_await chan.write(42);
		write_did_finish = true;
	}()
				.start();
	ASSERT_TRUE(read_value.has_value());
	ASSERT_EQ(read_value.value(), 42);
	ASSERT_TRUE(read_did_finish);
	ASSERT_FALSE(read2_did_finish);
	ASSERT_TRUE(write_did_finish);

	read_value.reset();
	write_did_finish = false;
	[]() -> fire_and_forget_task<> {
		co_await chan.write(84);
		write_did_finish = true;
	}()
				.start();
	ASSERT_TRUE(read_value.has_value());
	ASSERT_EQ(read_value.value(), 84);
	ASSERT_TRUE(read_did_finish);
	ASSERT_TRUE(read2_did_finish);
	ASSERT_TRUE(write_did_finish);
}

TEST(ASYNCPP, ChannelMultipleWrite) {
	static channel<int> chan;
	static std::optional<int> read_value{};
	static bool read_did_finish = false;
	static bool write_did_finish = false;

	for (int i = 0; i < 10; i++) {
		[i]() -> fire_and_forget_task<> {
			co_await chan.write(i);
			write_did_finish = true;
		}()
					 .start();
	}
	ASSERT_FALSE(write_did_finish);

	for (size_t i = 0; i < 10; i++) {
		read_value.reset();
		read_did_finish = false;
		write_did_finish = false;
		[]() -> fire_and_forget_task<> {
			read_value = co_await chan.read();
			read_did_finish = true;
		}()
					.start();
		ASSERT_TRUE(read_value.has_value());
		ASSERT_TRUE(read_did_finish);
		ASSERT_TRUE(write_did_finish);
		ASSERT_EQ(read_value.value(), i);
	}
}

TEST(ASYNCPP, ChannelCloseRead) {
	static channel<int> chan;
	static std::optional<int> read_value{};
	static bool read_did_finish = false;
	static bool write_value = false;
	static bool write_did_finish = false;

	[]() -> eager_fire_and_forget_task<> {
		write_value = co_await chan.write(42);
		write_did_finish = true;
	}();
	ASSERT_FALSE(write_did_finish);

	[]() -> eager_fire_and_forget_task<> {
		read_value = co_await chan.read();
		read_did_finish = true;
		read_value = co_await chan.read();
		read_did_finish = true;
	}();
	ASSERT_TRUE(read_did_finish);
	ASSERT_TRUE(read_value.has_value());
	ASSERT_EQ(read_value.value(), 42);
	ASSERT_TRUE(write_did_finish);
	ASSERT_TRUE(write_value);

	read_did_finish = false;
	chan.close();
	ASSERT_TRUE(!read_value.has_value());
	ASSERT_TRUE(read_did_finish);
	
	write_did_finish = false;
	[]() -> eager_fire_and_forget_task<> {
		co_await chan.write(42);
		write_did_finish = true;
	}();
	ASSERT_TRUE(write_did_finish);
	ASSERT_TRUE(write_value);
}

TEST(ASYNCPP, ChannelTryRead) {
	static channel<int> chan;
	static bool write_value = false;
	static bool write_did_finish = false;

	[]() -> eager_fire_and_forget_task<> {
		write_value = co_await chan.write(42);
		write_did_finish = true;
	}();
	ASSERT_FALSE(write_did_finish);

	auto read_value = chan.try_read();
	ASSERT_TRUE(read_value.has_value());
	ASSERT_EQ(read_value.value(), 42);
	ASSERT_TRUE(write_did_finish);
	ASSERT_TRUE(write_value);

	chan.close();
	read_value = chan.try_read();
	ASSERT_TRUE(!read_value.has_value());
}

TEST(ASYNCPP, ChannelTryWrite) {
	static channel<int> chan;
	static std::optional<int> read_value{};
	static bool read_did_finish = false;

	[]() -> eager_fire_and_forget_task<> {
		read_value = co_await chan.read();
		read_did_finish = true;
	}();
	ASSERT_FALSE(read_did_finish);
	ASSERT_FALSE(read_value.has_value());

	ASSERT_TRUE(chan.try_write(42));
	ASSERT_TRUE(read_did_finish);
	ASSERT_TRUE(read_value.has_value());
	ASSERT_EQ(read_value.value(), 42);

	// Write with no active reader should fail
	ASSERT_FALSE(chan.try_write(43));

	[]() -> eager_fire_and_forget_task<> {
		read_value = co_await chan.read();
		read_did_finish = true;
	}();
	chan.close();

	// Write after close should fail
	ASSERT_FALSE(chan.try_write(43));
}

TEST(ASYNCPP, ChannelIsClosed) {
	channel<int> chan;
	ASSERT_FALSE(chan.is_closed());
	chan.close();
	ASSERT_TRUE(chan.is_closed());
}

TEST(ASYNCPP, ChannelResumeOn) {
	static channel<int> chan;
	[]() -> eager_fire_and_forget_task<> {
		co_await chan.read().resume_on(nullptr);
	}();
	ASSERT_TRUE(chan.try_write(42));
}

#include <asyncpp/signal.h>
#include <gtest/gtest.h>
#include <thread>

TEST(ASYNCPP, Signal) {
	int param = 0;

	asyncpp::signal<void(int)> sig;
	auto con = sig += [&](int val) { param = val; };

	ASSERT_EQ(sig(42), 1);
	ASSERT_EQ(param, 42);
}

TEST(ASYNCPP, SignalConnectionDisconnect) {
	int param = 0;

	asyncpp::signal<void(int)> sig;
	auto con = sig += [&](int val) { param = val; };
	sig.remove(con);

	ASSERT_EQ(sig(42), 0);
	ASSERT_EQ(param, 0);
}

TEST(ASYNCPP, SignalDisconnect) {
	int param = 0;

	asyncpp::signal<void(int)> sig;
	auto con = sig += [&](int val) { param = val; };

	ASSERT_EQ(sig(42), 1);
	ASSERT_EQ(param, 42);

	sig.remove(con);

	ASSERT_EQ(sig(41), 0);
	ASSERT_EQ(param, 42);
}

TEST(ASYNCPP, SignalRecursiveCall) {
	int param = 0;

	asyncpp::signal<void(int)> sig;
	sig += [&](int val) {
		param = val;
		if (val == 42) sig(41);
	};

	ASSERT_EQ(sig(42), 1);
	ASSERT_EQ(param, 41);
}

TEST(ASYNCPP, SignalRecursiveCallDisconnectSelf) {
	int param = 0;

	asyncpp::signal<void(int)> sig;
	asyncpp::signal_handle con;
	con = sig += [&](int val) {
		param = val;
		sig.remove(con);
		if (val == 42) sig(41);
	};

	ASSERT_EQ(sig(42), 1);
	ASSERT_EQ(param, 42);
}

TEST(ASYNCPP, SignalConnectVoid) {
	int param = 0;

	asyncpp::signal<void(int)> sig;
	sig += [&](int val) { param = val; };

	ASSERT_EQ(sig(42), 1);
	ASSERT_EQ(param, 42);
}

TEST(ASYNCPP, SignalThreadedRemove) {
	int param = 0;

	asyncpp::signal<void(int)> sig;
	asyncpp::signal_handle con;
	con = sig += [&](int val) {
		param = val;
		std::thread th([&]() { sig.remove(con); });
		th.join();
		if (val == 42) sig(41);
	};

	ASSERT_EQ(sig(42), 1);
	ASSERT_EQ(param, 42);
}

TEST(ASYNCPP, SignalManager) {
	asyncpp::signal_manager<int, void(int)> mgr;

	int param = 0;

	auto hdl = mgr.append(10, [&param](int x) { param = x; });
	ASSERT_TRUE(hdl);
	ASSERT_TRUE(mgr.owns_handle(10, hdl));
	ASSERT_FALSE(mgr.owns_handle(11, hdl));
	ASSERT_EQ(mgr(11, 42), 0);
	ASSERT_EQ(param, 0);

	ASSERT_EQ(mgr(10, 42), 1);
	ASSERT_EQ(param, 42);

	mgr.remove(10, hdl);

	ASSERT_EQ(mgr(10, 43), 0);
	ASSERT_EQ(param, 42);

	hdl = mgr.append(10, [&param](int x) { param = x; });

	ASSERT_EQ(mgr(10, 41), 1);
	ASSERT_EQ(param, 41);

	hdl.disconnect();

	ASSERT_EQ(mgr(10, 43), 0);
	ASSERT_EQ(param, 41);
}
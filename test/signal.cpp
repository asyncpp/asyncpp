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

TEST(ASYNCPP, SignalOwnsHandle) {
	asyncpp::signal<void()> sig;
	ASSERT_FALSE(sig.owns_handle(asyncpp::signal_handle()));
	auto con = sig += []() {};
	ASSERT_TRUE(sig.owns_handle(con));
	ASSERT_FALSE(sig.empty());
	sig.remove(con);
	ASSERT_FALSE(sig.owns_handle(con));
	ASSERT_TRUE(sig.empty());
	con = sig += []() {};
	ASSERT_TRUE(sig.owns_handle(con));
	ASSERT_FALSE(sig.empty());
	con.disconnect();
	ASSERT_FALSE(sig.owns_handle(con));
	ASSERT_TRUE(sig.empty());
}

TEST(ASYNCPP, SignalHandleEquals) {
	asyncpp::signal<void()> sig;
	auto con = sig += []() {};
	auto con2 = sig += []() {};
	auto con3 = con;
	ASSERT_NE(con, con2);
	ASSERT_EQ(con, con3);
	ASSERT_NE(con2, con3);
	con3 = con2;
	ASSERT_NE(con, con2);
	ASSERT_NE(con, con3);
	ASSERT_EQ(con2, con3);
	con2.disconnect();
	ASSERT_NE(con, con2);
	ASSERT_NE(con, con3);
	ASSERT_NE(con2, con3);
}

TEST(ASYNCPP, SignalScopedSignalHandle) {
	asyncpp::signal<void()> sig;
	{
		asyncpp::scoped_signal_handle con = sig += []() {};
		ASSERT_TRUE(sig.owns_handle(con));
		ASSERT_FALSE(sig.empty());
	}
	ASSERT_TRUE(sig.empty());
	{
		asyncpp::scoped_signal_handle con = sig += []() {};
		ASSERT_TRUE(sig.owns_handle(con));
		ASSERT_FALSE(sig.empty());
		con.release();
	}
	ASSERT_FALSE(sig.empty());
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

TEST(ASYNCPP, SignalExplicitThreading) {
	asyncpp::signal_mt<void()> sigmt;
	asyncpp::signal_st<void()> sigst;
}

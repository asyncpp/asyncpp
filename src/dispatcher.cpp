#include <asyncpp/dispatcher.h>

namespace asyncpp {
	/**
     * This is the current dispatcher.
     * If you implement your own dispatcher and want to support dispatcher::current,
     * extern this variable and set it to the dispatcher instance while you call your callbacks.
     * Make sure to persist any previous value and restore it afterwards, as your dispatcher
     * might be called from within another dispatcher.
     */
	thread_local dispatcher* g_this_dispatcher = nullptr;

	dispatcher::~dispatcher() {}

	dispatcher* dispatcher::current() noexcept { return g_this_dispatcher; }
} // namespace asyncpp
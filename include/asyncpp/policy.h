#pragma once
#include <exception>
#include <functional>
#include <utility>

namespace asyncpp {
	/**
	 * \brief Exception policy for e.g. fire and forget tasks.
	 * 
	 * Allows changing the default terminate call if an exception leaves the async function and calls the specified handler instead.
	 * It can be used by awaiting the object in supported task types.
	 * \code{.cpp}
	 * co_await exception_policy::ignore;
	 * \endcode
	 */
	struct exception_policy {
		/// \brief Handler method to invoke if an exception is thrown.
		std::function<void()> handler;

		/// \brief The default terminate handler. Calls std::terminate() and usually ends the program.
		static const exception_policy terminate;
		/// \brief Noop that ignores the thrown exception. The coroutine ends at the throw point and the stack frame is destroyed.
		static const exception_policy ignore;
		/// \brief Call the specified method on exception. The callback is called within the catch block.
		static exception_policy handle(std::function<void()> cbfn) { return exception_policy{std::move(cbfn)}; }
	};
	inline const exception_policy exception_policy::terminate = {[]() { std::terminate(); }};
	inline const exception_policy exception_policy::ignore = {};
} // namespace asyncpp

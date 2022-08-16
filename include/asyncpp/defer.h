#pragma once
#include <asyncpp/detail/concepts.h>
#include <asyncpp/detail/std_import.h>
#include <utility>

namespace asyncpp {
	/**
	 * \brief Defer the current coroutine to a different dispatcher context.
	 * \tparam TDispatcher Type of the target dispatcher. Needs to provide a public "push" method.
	 * 
	 * This stops executions on the current dispatcher and reschedules the function on the provided dispatcher.
	 * It can be used if a function needs to run within a certain thread (e.g. libuv I/O) or to give a different task
	 * the chance to get work done. Does nothing if the dispatcher is nullptr.
	 */
	template<Dispatcher TDispatcher>
	struct defer {
		/// \brief The new dispatcher to resume execution on.
		TDispatcher* target_dispatcher;

		/**
		 * \brief Construct with a pointer to a dispatcher.
		 * \param target The dispatcher to resume on.
		 * \note If target is nullptr the defer is a noop and does not suspend.
		 */
		constexpr defer(TDispatcher* target) noexcept : target_dispatcher(target) {}

		/**
		 * \brief Construct with a reference to a dispatcher.
		 * \param target The dispatcher to resume on.
		 */
		constexpr defer(TDispatcher& target) noexcept : target_dispatcher(&target) {}

		/**
		 * \brief Check if await should suspend
		 * \return true if a dispatcher was set.
		 */
		constexpr bool await_ready() const noexcept { return target_dispatcher == nullptr; }
		/**
		 * \brief Suspend the current coroutine and schedule resumption on the specified dispatcher.
		 * \param h The current coroutine
		 */
		void await_suspend(coroutine_handle<> h) const noexcept(noexcept(target_dispatcher->push(std::declval<std::function<void()>>()))) {
			target_dispatcher->push([h]() mutable { h.resume(); });
		}
		/// \brief Called on resumption
		constexpr void await_resume() const noexcept {}
	};
} // namespace asyncpp

#pragma once
#include <functional>
#include <stdexcept>

namespace asyncpp {
	/**
     * \brief Basic dispatcher interface class
     */
	class dispatcher {
		static thread_local inline dispatcher* g_current_dispatcher = nullptr;

	protected:
		~dispatcher() = default;

		/**
		 * \brief Set the current dispatcher for this thread and reurns the current one.
		 * Implementers of dispatchers can use this to give convenient access to the current dispatcher, for example for yielding.
		 * A dispatcher usually calls this function once at the start of an event loop, before calling any callbacks and persists
		 * the return value. It then calls it again after calling all callbacks and restores the previously persisted value. This
		 * allows invoking a dispatcher loop from within another dispatcher. The end call is optional if the implementation can
		 * guarantee that the thread will terminate afterwards.
		 * \param d The dispatcher instance of this thread.
		 * \return The previous dispatcher
		 */
		static dispatcher* current(dispatcher* d) {
			const auto old = g_current_dispatcher;
			g_current_dispatcher = d;
			return old;
		}

	public:
		/**
         * Push a function to be executed on the dispatcher.
         * 
         * The function gets added to the queue and is executed once all existing tasks have run.
         * \param fn Callback
         */
		virtual void push(std::function<void()> fn) = 0;
		/**
         * Get the dispatcher associated with the current thread.
         * This can be used to shedule more tasks on the current dispatcher.
         * Returns the current dispatcher, or nullptr if the current thread is
         * not associated with a dispatcher.
         */
		static dispatcher* current() noexcept { return g_current_dispatcher; }
	};
} // namespace asyncpp

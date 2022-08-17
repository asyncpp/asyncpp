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
		 * \brief Set the current dispatcher for this thread.
		 * Implementers of dispatchers can use this to give convenient access to the current dispatcher, for example for yielding.
		 * A dispatcher usually calls this function twice, once at the start of an event loop, before calling any callbacks and once at the very end (with a nullptr)
		 * When this thread of the dispatcher is about to exit. The end call is optional if the implementation can guarantee that the thread will terminate afterwards.
		 * \param d The dispatcher instance of this thread.
		 * \throws std::logic_error if this thread is already assigned to a different dispatcher and the supplied pointer is not null.
		 */
		static void current(dispatcher* d) {
			if(g_current_dispatcher && d && g_current_dispatcher != d)
				throw std::logic_error("thread dispatcher already set");
			g_current_dispatcher = d;
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
		static dispatcher* current() noexcept {
			return g_current_dispatcher;
		}
	};
} // namespace asyncpp
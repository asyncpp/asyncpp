#pragma once
#include <functional>

namespace asyncpp {
    /**
     * \brief Basic dispatcher interface class
     */
	class dispatcher {
	public:
		virtual ~dispatcher();
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
		static dispatcher* current() noexcept;
	};
} // namespace asyncpp
#pragma once
#include <asyncpp/event.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace asyncpp {
	/**
     * \brief The latch class is a downward counter of type std::size_t which can be used to synchronize coroutines.
     *
     * The value of the counter is initialized on creation. Threads may block on the latch until the counter is
     * decremented to zero. There is no possibility to increase or reset the counter, which makes the latch a
     * single-use barrier. Concurrent invocations of the member functions of `latch`, except for the destructor,
     * do not introduce data races.
     */
	class latch {
	public:
		/// \brief The maximum value of counter supported
		static constexpr std::size_t max = std::numeric_limits<std::size_t>::max();

		/**
         * \brief Constructs a latch and initializes its internal counter.
         * \param initial The initial counter value
         */
		latch(std::size_t initial) noexcept : m_count{initial}, m_event{initial <= 0} {}

		/// \brief latch is not copyable
		latch(const latch&) = delete;

		/// \brief latch is not copyable
		latch& operator=(const latch&) = delete;

		/// \brief Check if the latch reached zero
		bool is_ready() const noexcept { return m_event.is_set(); }

		/**
         * \brief Decrement the latch counter
         * \param n Amount to decrement from the counter
         * \note It is undefined if the sum of all decrement calls exceeds the counter value.
         */
		void decrement(std::size_t n = 1) noexcept {
			if (m_count.fetch_sub(n, std::memory_order::acq_rel) == 0) { m_event.set(); }
		}

		/**
         * \brief Wait for the counter to reach zero
         */
		auto operator co_await() noexcept { return m_event.wait(dispatcher::current()); }

		/**
         * \brief Wait for the counter to reach zero
         */
		auto wait(dispatcher* resume_dispatcher = nullptr) noexcept { return m_event.wait(resume_dispatcher); }

	private:
		std::atomic<std::size_t> m_count;
		multi_consumer_event m_event;
	};
} // namespace asyncpp
#pragma once
#include <asyncpp/detail/concepts.h>
#include <asyncpp/detail/std_import.h>
#include <asyncpp/dispatcher.h>

#include <atomic>
#include <cassert>
#include <mutex>
#include <optional>

namespace asyncpp {

	/**
	 * \brief Channel for communication between coroutines
	 * \note Channels are not a queue. Writing to a channel that has no active reader will suspend until one
	 * 		calls read (or the channel is closed).
	 * This class allows passing values between multiple coroutines in a thread safe and convenient way.
	 * They work similar to a pipe/loopback socket.
	 * \tparam T Type of the payload
	 */
	template<typename T>
	class channel {
		struct read_awaiter;
		struct write_awaiter;

		std::atomic<bool> m_closed{false};
		std::mutex m_mtx;
		read_awaiter* m_reader_list{};
		write_awaiter* m_writer_list{};

	public:
#ifndef NDEBUG
		~channel() {
			assert(m_reader_list == nullptr && m_writer_list == nullptr && "channel destroyed with waiting coroutines");
		}
#endif

		/**
		 * \brief Read from the channel.
		 * 
		 * Suspends until write/try_write is called on a different coroutine or the channel is closed.
		 * \return Awaiter for reading (resumes with std::optional<T>).
		 */
		[[nodiscard]] read_awaiter read();
		/**
		 * \brief Attempt to read a value without suspending.
		 * If the channel is closed or no writer is suspended this returns std::nullopt.
		 */
		[[nodiscard]] std::optional<T> try_read();

		/**
		 * \brief Write to the channel.
		 * \note This will suspend until a reader is available to receive the value or the channel is closed.
		 * \return Awaiter for reading (resumes with true if the value was received and false if the channel was closed).
		 */
		[[nodiscard]] write_awaiter write(T value);
		/**
		 * \brief Attempt to write a value without suspending.
		 * If the channel is closed or no reader is suspended this returns false.
		 */
		[[nodiscard]] bool try_write(T value);

		/**
		 * \brief Close the channel.
		 * 
		 * This resumes all currently blocked readers and writers and marks the channel as closed.
		 */
		void close();

		/**
		 * \brief Check if the channel is closed.
		 * \note You can not assume any operation to succeed if this returns false, 
		 * 		because the channel might get closed between this and the next call.
		 * 		However since a closed channel can never transition back to open, a
		 * 		returned true guarantees that any further calls with fail.
		 * \return true if the channel is closed, false if not
		 */
		[[nodiscard]] bool is_closed() const noexcept { return m_closed.load(std::memory_order::relaxed); }
	};

	template<typename T>
	struct channel<T>::read_awaiter {
		channel* m_parent;

		read_awaiter* m_next{};
		coroutine_handle<> m_handle;
		dispatcher* m_dispatcher = dispatcher::current();
		std::optional<T> m_result{std::nullopt};

		/**
		 * \brief Specify a dispatcher to resume after on after reading.
		 * 
		 * By default the coroutine is resumed on the current dispatcher
		 * or inline of the write call if no dispatcher was active at the time
		 * of suspension.
		 * \param dsp The dispatcher to resume on or nullptr to resume inside the write call.
		 */
		read_awaiter& resume_on(dispatcher* dsp) noexcept {
			m_dispatcher = dsp;
			return *this;
		}

		[[nodiscard]] constexpr bool await_ready() const noexcept;
		bool await_suspend(coroutine_handle<> hndl);
		std::optional<T> await_resume();
	};

	template<typename T>
	struct channel<T>::write_awaiter {
		channel* m_parent;
		T m_value;

		write_awaiter* m_next{};
		coroutine_handle<> m_handle;
		dispatcher* m_dispatcher = dispatcher::current();
		bool m_result{false};

		/**
		 * \brief Specify a dispatcher to resume after on after writing.
		 * 
		 * By default the coroutine is resumed on the current dispatcher
		 * or inline of the read call if no dispatcher was active at the time
		 * of suspension.
		 * \param dsp The dispatcher to resume on or nullptr to resume inside the read call.
		 */
		write_awaiter& resume_on(dispatcher* dsp) noexcept {
			m_dispatcher = dsp;
			return *this;
		}

		[[nodiscard]] constexpr bool await_ready() const noexcept;
		bool await_suspend(coroutine_handle<> hndl);
		bool await_resume();
	};

	template<typename T>
	inline typename channel<T>::read_awaiter channel<T>::read() {
		return read_awaiter{this};
	}

	template<typename T>
	inline std::optional<T> channel<T>::try_read() {
		if (m_closed.load(std::memory_order::relaxed)) return std::nullopt;
		std::unique_lock lck{m_mtx};
		// Check if there is a writer waiting
		if (auto wrt = m_writer_list; wrt != nullptr && !m_closed.load(std::memory_order::relaxed)) {
			// Unhook writer
			m_writer_list = wrt->m_next;
			lck.unlock();
			// Take the value out
			std::optional<T> res = std::move(wrt->m_value);
			wrt->m_result = true;
			// Resume writer
			if (wrt->m_dispatcher != nullptr)
				wrt->m_dispatcher->push([wrt]() { wrt->m_handle.resume(); });
			else
				wrt->m_handle.resume();
			return res;
		}
		return std::nullopt;
	}

	template<typename T>
	inline typename channel<T>::write_awaiter channel<T>::write(T value) {
		return write_awaiter{this, std::move(value)};
	}

	template<typename T>
	inline bool channel<T>::try_write(T value) {
		if (m_closed.load(std::memory_order::relaxed)) return false;
		std::unique_lock lck{m_mtx};
		// Check if there is a reader waiting
		if (auto rdr = m_reader_list; rdr != nullptr && !m_closed.load(std::memory_order::relaxed)) {
			// Unhook reader
			m_reader_list = rdr->m_next;
			lck.unlock();
			// Take the value out
			rdr->m_result = std::move(value);
			// Resume writer
			if (rdr->m_dispatcher != nullptr)
				rdr->m_dispatcher->push([rdr]() { rdr->m_handle.resume(); });
			else
				rdr->m_handle.resume();
			return true;
		}
		return false;
	}

	template<typename T>
	inline void channel<T>::close() {
		if (m_closed.load(std::memory_order::relaxed)) return;
		std::unique_lock lck{m_mtx};
		if (!m_closed.exchange(true)) {
			// This is the first close, so cancel all waiting awaiters
			while (m_reader_list != nullptr) {
				auto rdr = m_reader_list;
				m_reader_list = rdr->m_next;
				// We do not need to unlock, because all further attempts to access this
				// cannel will get refused.
				rdr->m_result.reset();
				// Resume reader
				if (rdr->m_dispatcher != nullptr)
					rdr->m_dispatcher->push([rdr]() { rdr->m_handle.resume(); });
				else
					rdr->m_handle.resume();
			}
			while (m_writer_list != nullptr) {
				auto wrt = m_writer_list;
				m_writer_list = wrt->m_next;
				// We do not need to unlock, because all further attempts to access this
				// cannel will get refused.
				wrt->m_result = false;
				// Resume reader
				if (wrt->m_dispatcher != nullptr)
					wrt->m_dispatcher->push([wrt]() { wrt->m_handle.resume(); });
				else
					wrt->m_handle.resume();
			}
		}
	}

	template<typename T>
	inline constexpr bool channel<T>::read_awaiter::await_ready() const noexcept {
		return m_parent->m_closed.load(std::memory_order::relaxed);
	}

	template<typename T>
	inline bool channel<T>::read_awaiter::await_suspend(coroutine_handle<> hndl) {
		m_handle = hndl;
		m_next = nullptr;
		std::unique_lock lck{m_parent->m_mtx};
		// Check if there is a writer waiting
		if (auto wrt = m_parent->m_writer_list; wrt != nullptr) {
			// Unhook writer
			m_parent->m_writer_list = wrt->m_next;
			lck.unlock();
			// Take the value out
			m_result = std::move(wrt->m_value);
			wrt->m_result = true;
			// Resume writer
			if (wrt->m_dispatcher != nullptr)
				wrt->m_dispatcher->push([wrt]() { wrt->m_handle.resume(); });
			else
				wrt->m_handle.resume();
			// Do not suspend ourself
			return false;
		}

		// No writer available, we have to wait...
		auto last = m_parent->m_reader_list;
		while (last && last->m_next)
			last = last->m_next;
		if (last == nullptr)
			m_parent->m_reader_list = this;
		else
			last->m_next = this;
		return true;
	}

	template<typename T>
	inline std::optional<T> channel<T>::read_awaiter::await_resume() {
		return std::move(m_result);
	}

	template<typename T>
	inline constexpr bool channel<T>::write_awaiter::await_ready() const noexcept {
		return m_parent->m_closed.load(std::memory_order::relaxed);
	}

	template<typename T>
	inline bool channel<T>::write_awaiter::await_suspend(coroutine_handle<> hndl) {
		m_handle = hndl;
		m_next = nullptr;
		if (m_parent->m_closed.load(std::memory_order::relaxed)) return false;
		std::unique_lock lck{m_parent->m_mtx};
		if (m_parent->m_closed.load(std::memory_order::relaxed)) return false;
		// Check if there is a reader waiting
		if (auto rdr = m_parent->m_reader_list; rdr != nullptr) {
			// Unhook reader
			m_parent->m_reader_list = rdr->m_next;
			lck.unlock();
			// Copy the value over
			rdr->m_result = std::move(m_value);
			// Resume reader
			if (rdr->m_dispatcher != nullptr)
				rdr->m_dispatcher->push([rdr]() { rdr->m_handle.resume(); });
			else
				rdr->m_handle.resume();
			// Do not suspend ourself
			return false;
		}

		// No reader available, we have to wait...
		auto last = m_parent->m_writer_list;
		while (last && last->m_next)
			last = last->m_next;
		if (last == nullptr)
			m_parent->m_writer_list = this;
		else
			last->m_next = this;
		return true;
	}

	template<typename T>
	inline bool channel<T>::write_awaiter::await_resume() {
		return m_result;
	}
} // namespace asyncpp

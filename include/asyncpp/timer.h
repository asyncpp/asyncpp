#pragma once
#include <asyncpp/detail/std_import.h>
#include <asyncpp/dispatcher.h>
#include <asyncpp/stop_token.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <thread>

namespace asyncpp {
	/**
	 * \brief A dispatcher that provides a way to schedule coroutines based on time.
	 */
	class timer final : public dispatcher {
		struct scheduled_entry {
			std::chrono::steady_clock::time_point timepoint;
			std::function<void(bool)> invokable;

			scheduled_entry(std::chrono::steady_clock::time_point time, std::function<void(bool)> cbfn) noexcept
				: timepoint(time), invokable(std::move(cbfn)) {}

			/// \brief Comparator struct that compares the timepoints
			struct time_less {
				/// \brief Compare two entries
				constexpr bool operator()(const scheduled_entry& lhs, const scheduled_entry& rhs) const noexcept {
					return lhs.timepoint < rhs.timepoint;
				}
			};
		};
		struct cancellable_scheduled_entry : public scheduled_entry {
			/// \brief Callback type used with stop_tokens
			struct cancel_callback {
				/// \brief Pointer to the timer containing this node
				timer* parent;
				/// \brief Iterator to this node in the entry set
				std::multiset<cancellable_scheduled_entry, scheduled_entry::time_less>::const_iterator it;
				/// \brief Invocation operator called on cancellation
				void operator()() const {
					// We have to distinguish between cancellation while in the process of construction and
					// cancellation afterwards. If the stop_token is signalled at the time of construction it directly
					// invokes the callback, however if we would lock at that point we would cause a deadlock with the
					// outside lock in schedule(). We also can't directly delete the node in this case because
					// std::optional<>::emplace tries to set a flag after construction which would cause use after free.
					// Thus we set a flag in schedule() and postpone both the invoke and deletion until after emplace is done.
					// If we are not in construction we need to lock and can directly destroy the node once we are done.
					if (it->in_construction) {
						auto entry =
							new std::multiset<cancellable_scheduled_entry, scheduled_entry::time_less>::node_type(
								parent->m_scheduled_cancellable_set.extract(it));
						if (entry->value().invokable) {
							parent->m_pushed.emplace([entry]() {
								entry->value().invokable(false);
								delete entry;
							});
						}
					} else {
						std::unique_lock lck{parent->m_mtx};
						auto entry = parent->m_scheduled_cancellable_set.extract(it);
						lck.unlock();
						if (entry.value().invokable) {
							parent->m_pushed.emplace([cbfn = std::move(entry.value().invokable)]() { cbfn(false); });
							parent->m_cv.notify_all();
						}
					}
				}
			};
			mutable std::optional<asyncpp::stop_callback<cancel_callback>> cancel_token;
			mutable bool in_construction{true};

			cancellable_scheduled_entry(std::chrono::steady_clock::time_point time,
										std::function<void(bool)> cbfn) noexcept
				: scheduled_entry{time, std::move(cbfn)} {}
		};

	public:
		/**
		 * \brief Construct a new timer
		 */
		timer() : m_thread{[this]() noexcept { this->run(); }} {}
		~timer() {
			{
				std::unique_lock lck{m_mtx};
				m_exit = true;
				m_cv.notify_all();
			}
			if (m_thread.joinable()) m_thread.join();
			assert(m_pushed.empty());
			assert(m_scheduled_set.empty());
			assert(m_scheduled_cancellable_set.empty());
		}
		timer(const timer&) = delete;
		timer& operator=(const timer&) = delete;

		/**
		 * \brief Push a callback to be executed in the timer thread
		 * \param cbfn Callback to execute
		 */
		void push(std::function<void()> cbfn) override {
			if (m_exit) throw std::logic_error("shutting down");
			std::unique_lock lck(m_mtx);
			m_pushed.emplace(std::move(cbfn));
			m_cv.notify_all();
		}

		/**
		 * \brief Schedule a callback to be executed at a specific point in time
		 * \param cbfn The callback to execute
		 * \param timeout The time_point at which the callback should get executed
		 */
		void schedule(std::function<void(bool)> cbfn, std::chrono::steady_clock::time_point timeout) {
			if (m_exit) throw std::logic_error("shutting down");
			std::unique_lock lck(m_mtx);
			m_scheduled_set.emplace(timeout, std::move(cbfn));
			m_cv.notify_all();
		}
		/**
		 * \brief Schedule a callback to be executed after a certain duration expires.
		 * \param cbfn The callbackto execute
		 * \param timeout The duration to wait before executing the callback
		 */
		void schedule(std::function<void(bool)> cbfn, std::chrono::nanoseconds timeout) {
			schedule(std::move(cbfn), std::chrono::steady_clock::now() + timeout);
		}
		/**
		 * \brief Schedule a callback to be executed after a certain duration expires.
		 * \param cbfn The callbackto execute
		 * \param timeout The duration to wait before executing the callback
		 */
		template<typename Clock, typename Duration>
		void schedule(std::function<void(bool)> cbfn, std::chrono::time_point<Clock, Duration> timeout) {
			schedule(std::move(cbfn), timeout - Clock::now());
		}

		/**
		 * \brief Schedule a callback to be executed at a specific point in time with a stop_token
		 * \param cbfn The callback to execute
		 * \param timeout The time_point at which the callback should get executed
		 * \param stoken stop_token allowing to cancel the callback
		 */
		void schedule(std::function<void(bool)> cbfn, std::chrono::steady_clock::time_point timeout,
					  asyncpp::stop_token stoken) {
			if (m_exit) throw std::logic_error("shutting down");
			std::unique_lock lck(m_mtx);
			auto iter = m_scheduled_cancellable_set.emplace(timeout, std::move(cbfn));
			iter->cancel_token.emplace(std::move(stoken), cancellable_scheduled_entry::cancel_callback{this, iter});
			iter->in_construction = false;
			m_cv.notify_all();
		}
		/**
		 * \brief Schedule a callback to be executed after a certain duration expires with a stop_token
		 * \param cbfn The callbackto execute
		 * \param timeout The duration to wait before executing the callback
		 * \param stoken stop_token allowing to cancel the callback
		 */
		void schedule(std::function<void(bool)> cbfn, std::chrono::nanoseconds timeout, asyncpp::stop_token stoken) {
			schedule(std::move(cbfn), std::chrono::steady_clock::now() + timeout, std::move(stoken));
		}
		/**
		 * \brief Schedule a callback to be executed after a certain duration expires with a stop_token
		 * \param cbfn The callbackto execute
		 * \param timeout The duration to wait before executing the callback
		 * \param stoken stop_token allowing to cancel the callback
		 */
		template<typename Clock, typename Duration>
		void schedule(std::function<void(bool)> cbfn, std::chrono::time_point<Clock, Duration> timeout,
					  asyncpp::stop_token stoken) {
			schedule(std::move(cbfn), timeout - Clock::now(), std::move(stoken));
		}

		/**
		 * \brief Get an awaitable that pauses the current coroutine until the specified time_point is reached.
		 * \return An awaitable
		 */
		auto wait(std::chrono::steady_clock::time_point timeout) noexcept {
			struct awaiter {
				timer* const m_parent;
				const std::chrono::steady_clock::time_point m_timeout;
				bool m_result{true};
				constexpr awaiter(timer* parent, std::chrono::steady_clock::time_point timeout) noexcept
					: m_parent(parent), m_timeout(timeout) {}

				[[nodiscard]] bool await_ready() const noexcept {
					return std::chrono::steady_clock::now() >= m_timeout;
				}
				void await_suspend(coroutine_handle<> hndl) {
					m_parent->schedule(
						[this, hndl](bool res) mutable {
							m_result = res;
							hndl.resume();
						},
						m_timeout);
				}
				//NOLINTNEXTLINE(modernize-use-nodiscard)
				constexpr bool await_resume() const noexcept { return m_result; }
			};
			return awaiter{this, timeout};
		}
		/**
		 * \brief Get an awaitable that pauses the current coroutine until the specified duration is elapsed.
		 * \return An awaitable
		 */
		template<typename Rep, typename Period>
		auto wait(std::chrono::duration<Rep, Period> timeout) noexcept {
			return wait(std::chrono::steady_clock::now() + timeout);
		}
		/**
		 * \brief Get an awaitable that pauses the current coroutine until the specified duration is elapsed.
		 * \return An awaitable
		 */
		template<typename Clock, typename Duration>
		auto wait(std::chrono::time_point<Clock, Duration> timeout) {
			return wait(timeout - Clock::now());
		}

		/**
		 * \brief Get an awaitable that pauses the current coroutine until the specified time_point is reached, allows cancellation.
		 * \param timeout The time_point to wait for
		 * \param stoken A stop_token that allows cancellation of the wait
		 * \return An awaitable
		 */
		auto wait(std::chrono::steady_clock::time_point timeout, asyncpp::stop_token stoken) noexcept {
			struct awaiter {
				timer* const m_parent;
				const std::chrono::steady_clock::time_point m_timeout;
				asyncpp::stop_token m_stoptoken;
				bool m_result{true};
				awaiter(timer* parent, std::chrono::steady_clock::time_point timeout,
						asyncpp::stop_token stoken) noexcept
					: m_parent(parent), m_timeout(timeout), m_stoptoken(std::move(stoken)) {}

				[[nodiscard]] bool await_ready() const noexcept {
					return std::chrono::steady_clock::now() >= m_timeout;
				}
				void await_suspend(coroutine_handle<> hndl) {
					m_parent->schedule(
						[this, hndl](bool res) mutable {
							m_result = res;
							hndl.resume();
						},
						m_timeout, std::move(m_stoptoken));
				}
				//NOLINTNEXTLINE(modernize-use-nodiscard)
				constexpr bool await_resume() const noexcept { return m_result; }
			};
			return awaiter{this, timeout, std::move(stoken)};
		}

		/**
		 * \brief Get an awaitable that pauses the current coroutine until the specified duration is elapsed, allows cancellation.
		 * \param timeout The duration to wait for
		 * \param stoken A stop_token that allows cancellation of the wait
		 * \return An awaitable
		 */
		template<typename Rep, typename Period>
		auto wait(std::chrono::duration<Rep, Period> timeout, asyncpp::stop_token stoken) noexcept {
			return wait(std::chrono::steady_clock::now() + timeout, std::move(stoken));
		}
		/**
		 * \brief Get an awaitable that pauses the current coroutine until the specified duration is elapsed, allows cancellation.
		 * \param timeout The duration to wait for
		 * \param stoken A stop_token that allows cancellation of the wait
		 * \return An awaitable
		 */
		template<typename Clock, typename Duration>
		auto wait(std::chrono::time_point<Clock, Duration> timeout, asyncpp::stop_token stoken) {
			return wait(timeout - Clock::now(), std::move(stoken));
		}

		/**
		 * \brief Get a global timer instance. 
		 */
		static timer& get_default() {
			static timer instance;
			return instance;
		}

	private:
		std::mutex m_mtx{};
		std::condition_variable m_cv{};
		std::queue<std::function<void()>> m_pushed{};
		std::multiset<scheduled_entry, scheduled_entry::time_less> m_scheduled_set{};
		std::multiset<cancellable_scheduled_entry, scheduled_entry::time_less> m_scheduled_cancellable_set{};
		std::atomic<bool> m_exit{};
		std::thread m_thread{};

		void run() noexcept {
#ifdef __linux__
			pthread_setname_np(pthread_self(), "asyncpp_timer");
#endif
			while (true) {
				std::unique_lock lck(m_mtx);
				while (!m_pushed.empty()) {
					auto entry = std::move(m_pushed.front());
					m_pushed.pop();
					if (entry) {
						lck.unlock();
						try {
							entry();
						} catch (...) { std::terminate(); }
						lck.lock();
					}
				}
				auto now = std::chrono::steady_clock::now();
				while (!m_scheduled_set.empty()) {
					auto elem = m_scheduled_set.begin();
					if (elem->timepoint > now) break;
					auto handle = m_scheduled_set.extract(elem);
					if (handle.value().invokable) {
						lck.unlock();
						try {
							handle.value().invokable(true);
						} catch (...) { std::terminate(); }
						lck.lock();
					}
				}
				while (!m_scheduled_cancellable_set.empty()) {
					auto elem = m_scheduled_cancellable_set.begin();
					if (elem->timepoint > now) break;
					auto handle = m_scheduled_cancellable_set.extract(elem);
					if (handle.value().invokable) {
						handle.value().cancel_token.reset();
						lck.unlock();
						try {
							handle.value().invokable(true);
						} catch (...) { std::terminate(); }
						lck.lock();
					}
				}
				now = std::chrono::steady_clock::now();
				std::chrono::nanoseconds timeout{500 * 1000 * 1000};
				if (!m_scheduled_set.empty()) timeout = std::min(m_scheduled_set.begin()->timepoint - now, timeout);
				if (!m_scheduled_cancellable_set.empty())
					timeout = std::min(m_scheduled_cancellable_set.begin()->timepoint - now, timeout);
				if (m_pushed.empty() && timeout.count() > 0) {
					if (m_exit) break;
					m_cv.wait_for(lck, timeout);
				}
			}
			std::unique_lock lck(m_mtx);
			auto set = std::move(m_scheduled_set);
			auto cset = std::move(m_scheduled_cancellable_set);
			lck.unlock();
			for (const auto& entry : set) {
				if (entry.invokable) {
					try {
						entry.invokable(false);
					} catch (...) { std::terminate(); }
				}
			}
			for (const auto& entry : cset) {
				if (entry.invokable) {
					try {
						entry.invokable(false);
					} catch (...) { std::terminate(); }
				}
			}
			lck.lock();
			m_scheduled_set.clear();
			m_scheduled_cancellable_set.clear();
		}
	};

	template<typename Rep, typename Period>
	inline auto operator co_await(std::chrono::duration<Rep, Period> duration) {
		return timer::get_default().wait(duration);
	}
	template<typename Clock, typename Duration>
	inline auto operator co_await(std::chrono::time_point<Clock, Duration> timeout) {
		return timer::get_default().wait(timeout);
	}

} // namespace asyncpp

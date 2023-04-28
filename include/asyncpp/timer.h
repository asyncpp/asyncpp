#pragma once
#include <asyncpp/detail/std_import.h>
#include <asyncpp/dispatcher.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <stop_token>
#include <thread>

namespace asyncpp {
	/**
	 * \brief A basic thread pool implementation for usage as a dispatcher
	 */
	class timer : public dispatcher {
		struct scheduled_entry {
			std::chrono::steady_clock::time_point timepoint;
			std::function<void(bool)> invokable;

			scheduled_entry(std::chrono::steady_clock::time_point tp, std::function<void(bool)> cb) noexcept
				: timepoint(tp), invokable(std::move(cb)) {}

			struct time_less {
				constexpr bool operator()(const scheduled_entry& lhs, const scheduled_entry& rhs) const noexcept {
					return lhs.timepoint < rhs.timepoint;
				}
			};
		};
		struct cancellable_scheduled_entry : public scheduled_entry {
			struct cancel_callback {
				timer* parent;
				std::multiset<cancellable_scheduled_entry, scheduled_entry::time_less>::const_iterator it;
				void operator()() const {
					std::unique_lock lck{parent->m_mtx, std::defer_lock};
					if (parent->m_thread.get_id() != std::this_thread::get_id()) lck.lock();
					auto e = parent->m_scheduled_cancellable_set.extract(it);
					lck.unlock();
					if (e.value().invokable) {
						parent->push([cb = std::move(e.value().invokable)](){
							cb(false);
						});
					}
				}
			};
			mutable std::optional<std::stop_callback<cancel_callback>> cancel_token;

			cancellable_scheduled_entry(std::chrono::steady_clock::time_point tp, std::function<void(bool)> cb) noexcept
				: scheduled_entry{tp, std::move(cb)} {}
		};

	public:
		/**
		 * \brief Construct a new timer
		 */
		timer()
			: m_mtx{}, m_cv{}, m_pushed{}, m_scheduled_set{}, m_scheduled_cancellable_set{}, m_exit{false}, //
			  m_thread{[this]() noexcept { this->run(); }}													//
		{}
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

		void push(std::function<void()> fn) override {
			if (m_exit) throw std::logic_error("shutting down");
			if (m_thread.get_id() == std::this_thread::get_id()) {
				m_pushed.emplace(std::move(fn));
			} else {
				std::unique_lock<std::mutex> lck(m_mtx);
				m_pushed.emplace(std::move(fn));
				m_cv.notify_all();
			}
		}

		void schedule(std::function<void(bool)> fn, std::chrono::steady_clock::time_point timeout) {
			if (m_exit) throw std::logic_error("shutting down");
			if (m_thread.get_id() == std::this_thread::get_id()) {
				m_scheduled_set.emplace(timeout, std::move(fn));
			} else {
				std::unique_lock<std::mutex> lck(m_mtx);
				m_scheduled_set.emplace(timeout, std::move(fn));
				m_cv.notify_all();
				lck.unlock();
			}
		}
		void schedule(std::function<void(bool)> fn, std::chrono::nanoseconds timeout) {
			schedule(std::move(fn), std::chrono::steady_clock::now() + timeout);
		}
		template<typename Clock, typename Duration>
		void schedule(std::function<void(bool)> fn, std::chrono::time_point<Clock, Duration> timeout) {
			schedule(std::move(fn), timeout - Clock::now());
		}

		void schedule(std::function<void(bool)> fn, std::chrono::steady_clock::time_point timeout, std::stop_token st) {
			if (m_exit) throw std::logic_error("shutting down");
			if (m_thread.get_id() == std::this_thread::get_id()) {
				auto it = m_scheduled_cancellable_set.emplace(timeout, std::move(fn));
				it->cancel_token.emplace(std::move(st), cancellable_scheduled_entry::cancel_callback{this, it});
			} else {
				std::unique_lock<std::mutex> lck(m_mtx);
				auto it = m_scheduled_cancellable_set.emplace(timeout, std::move(fn));
				it->cancel_token.emplace(std::move(st), cancellable_scheduled_entry::cancel_callback{this, it});
				m_cv.notify_all();
				lck.unlock();
			}
		}
		void schedule(std::function<void(bool)> fn, std::chrono::nanoseconds timeout, std::stop_token st) {
			schedule(std::move(fn), std::chrono::steady_clock::now() + timeout, std::move(st));
		}
		template<typename Clock, typename Duration>
		void schedule(std::function<void(bool)> fn, std::chrono::time_point<Clock, Duration> timeout,
					  std::stop_token st) {
			schedule(std::move(fn), timeout - Clock::now(), std::move(st));
		}

		auto wait(std::chrono::steady_clock::time_point timeout) noexcept {
			struct awaiter {
				timer* const m_parent;
				const std::chrono::steady_clock::time_point m_timeout;
				bool m_result;
				constexpr awaiter(timer* t, std::chrono::steady_clock::time_point timeout) noexcept
					: m_parent(t), m_timeout(timeout) {}

				bool await_ready() const noexcept { return std::chrono::steady_clock::now() >= m_timeout; }
				void await_suspend(coroutine_handle<> h) {
					m_parent->schedule(
						[this, h](bool res) mutable {
							m_result = res;
							h.resume();
						},
						m_timeout);
				}
				constexpr bool await_resume() const noexcept { return m_result; }
			};
			return awaiter{this, timeout};
		}
		auto wait(std::chrono::nanoseconds timeout) noexcept {
			return wait(std::chrono::steady_clock::now() + timeout);
		}
		template<typename Clock, typename Duration>
		auto wait(std::chrono::time_point<Clock, Duration> timeout) {
			return wait(timeout - Clock::now());
		}

		auto wait(std::chrono::steady_clock::time_point timeout, std::stop_token st) noexcept {
			struct awaiter {
				timer* const m_parent;
				const std::chrono::steady_clock::time_point m_timeout;
				std::stop_token m_stoptoken;
				bool m_result;
				awaiter(timer* t, std::chrono::steady_clock::time_point timeout, std::stop_token st) noexcept
					: m_parent(t), m_timeout(timeout), m_stoptoken(std::move(st)) {}

				bool await_ready() const noexcept { return std::chrono::steady_clock::now() >= m_timeout; }
				void await_suspend(coroutine_handle<> h) {
					m_parent->schedule(
						[this, h](bool res) mutable {
							m_result = res;
							h.resume();
						},
						m_timeout, std::move(m_stoptoken));
				}
				constexpr bool await_resume() const noexcept { return m_result; }
			};
			return awaiter{this, timeout, std::move(st)};
		}
		auto wait(std::chrono::nanoseconds timeout, std::stop_token st) noexcept {
			return wait(std::chrono::steady_clock::now() + timeout, std::move(st));
		}
		template<typename Clock, typename Duration>
		auto wait(std::chrono::time_point<Clock, Duration> timeout, std::stop_token st) {
			return wait(timeout - Clock::now(), std::move(st));
		}

		static timer& get_default() {
			static timer instance;
			return instance;
		}

	private:
		std::mutex m_mtx;
		std::condition_variable m_cv;
		std::queue<std::function<void()>> m_pushed;
		std::multiset<scheduled_entry, scheduled_entry::time_less> m_scheduled_set;
		std::multiset<cancellable_scheduled_entry, scheduled_entry::time_less> m_scheduled_cancellable_set;
		std::atomic<bool> m_exit;
		std::thread m_thread;

		void run() noexcept {
			while (true) {
				std::unique_lock lck(m_mtx);
				while (!m_pushed.empty()) {
					auto cb = std::move(m_pushed.front());
					m_pushed.pop();
					if (cb) {
						lck.unlock();
						try {
							cb();
						} catch (...) {}
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
						} catch (...) {}
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
						} catch (...) {}
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
			for (auto it = set.begin(); it != set.end(); it++) {
				if (it->invokable) {
					try {
						it->invokable(false);
					} catch (...) {}
				}
			}
			for (auto it = cset.begin(); it != cset.end(); it++) {
				if (it->invokable) {
					try {
						it->invokable(false);
					} catch (...) {}
				}
			}
			m_scheduled_set.clear();
			m_scheduled_cancellable_set.clear();
		}
	};

	template<typename Rep, typename Period>
	inline auto operator co_await(std::chrono::duration<Rep, Period> ts) {
		return timer::get_default().wait(ts);
	}
	template<typename Clock, typename Duration>
	inline auto operator co_await(std::chrono::time_point<Clock, Duration> timeout) {
		return timer::get_default().wait(timeout);
	}

} // namespace asyncpp

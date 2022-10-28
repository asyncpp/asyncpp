#pragma once
#include <asyncpp/detail/std_import.h>
#include <asyncpp/dispatcher.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <queue>
#include <thread>

namespace asyncpp {
	/**
	 * \brief A basic thread pool implementation for usage as a dispatcher
	 */
	class timer : public dispatcher {
	public:
		/**
		 * \brief Construct a new thread pool
		 * \param initial_size The initial number of threads to spawn
		 */
		timer()
			: m_mtx{}, m_cv{}, m_pushed{}, m_scheduled{}, m_exit{false}, //
			  m_thread{[this]() noexcept { this->run(); }}				 //
		{}
		~timer() {
			{
				std::unique_lock lck{m_mtx};
				m_exit = true;
				m_cv.notify_all();
			}
			if (m_thread.joinable()) m_thread.join();
			assert(m_pushed.empty());
			assert(m_scheduled.empty());
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
				m_scheduled.emplace(timeout, std::move(fn));
			} else {
				std::unique_lock<std::mutex> lck(m_mtx);
				m_scheduled.emplace(timeout, std::move(fn));
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
						[this, h](bool res) {
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

		static timer& get_default() {
			static timer instance;
			return instance;
		}

	private:
		std::mutex m_mtx;
		std::condition_variable m_cv;
		std::queue<std::function<void()>> m_pushed;
		std::multimap<std::chrono::steady_clock::time_point, std::function<void(bool)>> m_scheduled;
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
				std::chrono::nanoseconds timeout{500 * 1000 * 1000};
				auto now = std::chrono::steady_clock::now();
				while (!m_scheduled.empty()) {
					auto elem = m_scheduled.begin();
					auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(elem->first - now);
					if (diff.count() > 0) {
						if (timeout > diff) timeout = diff;
						break;
					}
					auto cb = std::move(elem->second);
					m_scheduled.erase(elem);
					if (cb) {
						lck.unlock();
						try {
							cb(true);
						} catch (...) {}
						lck.lock();
					}
				}
				if (m_pushed.empty() && (m_scheduled.empty() || m_scheduled.begin()->first > now)) {
					if (m_exit) break;
					m_cv.wait_for(lck, timeout);
				}
			}
			std::unique_lock lck(m_mtx);
			for (auto it = m_scheduled.begin(); it != m_scheduled.end(); it++) {
				if (it->second) {
					lck.unlock();
					try {
						it->second(false);
					} catch (...) {}
					lck.lock();
				}
			}
			m_scheduled.clear();
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

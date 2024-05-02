#pragma once
#include <asyncpp/dispatcher.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace asyncpp {

	/**
     * \brief A very basic dispatcher that runs in a single thread until manually stopped.
     */
	class simple_dispatcher : public dispatcher {
		std::mutex m_mtx;
		std::condition_variable m_cv;
		std::deque<std::function<void()>> m_queue;
		std::atomic<bool> m_done = false;

	public:
		/**
		 * \brief Push a function to be executed on the dispatcher.
		 * \param cbfn The callback
		 */
		void push(std::function<void()> cbfn) override {
			if (!cbfn) return;
			std::unique_lock lck{m_mtx};
			m_queue.emplace_back(std::move(cbfn));
			m_cv.notify_all();
		}

		/**
         * \brief Stop the dispatcher. It will return the on the next iteration, regardless if there is any work left.
         */
		void stop() noexcept {
			std::unique_lock lck{m_mtx};
			m_done = true;
			m_cv.notify_all();
		}

		/**
         * \brief Block and process tasks pushed to it until stop is called.
         */
		void run() {
			dispatcher* const old_dispatcher = dispatcher::current(this);
			while (!m_done) {
				std::unique_lock lck{m_mtx};
				if (m_queue.empty()) {
					m_cv.wait(lck);
					continue;
				}
				auto cbfn = std::move(m_queue.front());
				m_queue.pop_front();
				lck.unlock();
				if (cbfn) cbfn();
			}
			dispatcher::current(old_dispatcher);
		}
	};
} // namespace asyncpp

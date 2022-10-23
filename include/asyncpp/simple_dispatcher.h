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
		std::mutex mtx;
		std::condition_variable cv;
		std::deque<std::function<void()>> queue;
		std::atomic<bool> done = false;

	public:
		void push(std::function<void()> fn) override {
			if (!fn) return;
			std::unique_lock lck{mtx};
			queue.emplace_back(std::move(fn));
			cv.notify_all();
		}

		/**
         * \brief Stop the dispatcher. It will return the on the next iteration, regardless if there is any work left.
         */
		void stop() noexcept {
			std::unique_lock lck{mtx};
			done = true;
			cv.notify_all();
		}

		/**
         * \brief Block and process tasks pushed to it until stop is called.
         */
		void run() {
			dispatcher* const old_dispatcher = dispatcher::current(this);
			while (!done) {
				std::unique_lock lck{mtx};
				if (queue.empty()) {
					cv.wait_for(lck, std::chrono::milliseconds(500));
					continue;
				}
				auto cb = std::move(queue.front());
				queue.pop_front();
				lck.unlock();
				if (cb) cb();
			}
			dispatcher::current(old_dispatcher);
		}
	};
} // namespace asyncpp
#pragma once
#include <asyncpp/dispatcher.h>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#endif

namespace asyncpp {
	/**
	 * \brief A basic thread pool implementation for usage as a dispatcher
	 */
	class thread_pool : public dispatcher {
	public:
		/**
		 * \brief Construct a new thread pool
		 * \param initial_size The initial number of threads to spawn
		 */
		thread_pool(size_t initial_size = std::thread::hardware_concurrency()) { this->resize(initial_size); }
		~thread_pool() { this->resize(0); }
		thread_pool(const thread_pool&) = delete;
		thread_pool& operator=(const thread_pool&) = delete;

		/**
		 * \brief Push a callback into the pool
		 * \param fn The callback to execute on the pool
		 */
		void push(std::function<void()> fn) override {
			if (!fn) return;
			if (g_current_thread) {
				std::unique_lock lck{g_current_thread->mutex};
				g_current_thread->queue.emplace(std::move(fn));
			} else {
				std::shared_lock lck{m_threads_mtx};
				auto size = m_valid_size.load();
				if (size == 0) throw std::runtime_error("pool is shutting down");
				auto thread = m_threads[g_queue_rand() % size].get();
				std::unique_lock lck2{thread->mutex};
				thread->queue.emplace(std::move(fn));
				thread->cv.notify_one();
			}
		}

		/**
		 * \brief Update the number of threads currently running
		 * \param target_size The new number of threads
		 */
		void resize(size_t target_size) {
			std::unique_lock lck{m_resize_mtx};
			auto old = m_target_size.load();
			if (old > target_size) {
				// Prevent new tasks from being scheduled on those threads
				m_valid_size = target_size;
				m_target_size = target_size;
				// Notify all and join threads, if the threads index is greater or equal to m_target_size,
				// it will invoke its remaining tasks and exit.
				for (size_t i = target_size; i < m_threads.size(); i++) {
					m_threads[i]->cv.notify_all();
					if (m_threads[i]->thread.joinable()) m_threads[i]->thread.join();
					assert(m_threads[i]->queue.empty());
				}
				std::unique_lock lck{m_threads_mtx};
				m_threads.resize(target_size);
			} else if (old < target_size) {
				m_target_size = target_size;
				std::unique_lock threads_lck{m_threads_mtx};
				m_threads.resize(target_size);
				threads_lck.unlock();
				// We need some new threads, spawn them with the relevant indexes
				for (size_t i = old; i < target_size; i++) {
					m_threads[i] = std::make_unique<thread_state>(this, i);
				}
				// Allow pushing work to our new threads
				m_valid_size = target_size;
			}
			assert(target_size == m_threads.size());
			assert(target_size == m_valid_size);
			assert(target_size == m_target_size);
		}

		/**
		 * \brief Get the current number of threads
		 * \return size_t Number of active threads
		 */
		size_t size() const noexcept { return m_valid_size.load(); }

	private:
		struct thread_state {
			thread_pool* const pool;
			size_t const thread_index;
			std::mutex mutex{};
			std::condition_variable cv{};
			std::queue<std::function<void()>> queue{};
			std::thread thread;

			thread_state(thread_pool* parent, size_t index)
				: pool{parent}, thread_index{index}, thread{[this]() { this->run(); }} {}

			std::function<void()> try_steal_task() {
				// Make sure we dont wait if its locked uniquely cause that might deadlock with resize()
				if (!pool->m_threads_mtx.try_lock_shared()) return {};
				std::shared_lock lck{pool->m_threads_mtx, std::adopt_lock};
				for (size_t i = 0; i < pool->m_valid_size; i++) {
					auto& e = pool->m_threads[i];
					if (e.get() == this || e == nullptr) continue;
					// if the other thread is currently locked skip it, we dont wanna wait too long
					if (!e->mutex.try_lock()) continue;
					std::unique_lock th_lck{e->mutex, std::adopt_lock};
					if (e->queue.empty()) continue;
					auto cb = std::move(e->queue.front());
					e->queue.pop();
					return cb;
				}
				return {};
			}

			void run() {
#ifdef __linux__
				{
					std::string name = "pool_" + std::to_string(thread_index);
					pthread_setname_np(pthread_self(), name.c_str());
				}
#endif
				dispatcher::current(pool);
				g_current_thread = this;
				while (true) {
					{
						std::unique_lock lck{mutex};
						while (!queue.empty()) {
							auto cb = std::move(queue.front());
							queue.pop();
							lck.unlock();
							cb();
							lck.lock();
						}
					}
					if (thread_index >= pool->m_target_size) break;
					if (auto cb = try_steal_task(); cb) {
						cb();
						continue;
					}
					{
						std::unique_lock lck{mutex};
						cv.wait_for(lck, std::chrono::milliseconds{100});
					}
				}
				std::unique_lock lck{mutex};
				g_current_thread = nullptr;
				while (!queue.empty()) {
					auto& cb = queue.front();
					lck.unlock();
					cb();
					lck.lock();
					queue.pop();
				}
				dispatcher::current(nullptr);
			}
		};

		inline static thread_local thread_state* g_current_thread{nullptr};
		inline static thread_local std::minstd_rand g_queue_rand{
			std::hash<std::thread::id>{}(std::this_thread::get_id())};
		std::atomic<size_t> m_target_size{0};
		std::atomic<size_t> m_valid_size{0};
		std::mutex m_resize_mtx{};
		std::shared_mutex m_threads_mtx{};
		// We use pointers, so nodes don't move with insert/erase
		std::vector<std::unique_ptr<thread_state>> m_threads{};
	};
} // namespace asyncpp

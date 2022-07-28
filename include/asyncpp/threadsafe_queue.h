#pragma once
#include <mutex>
#include <optional>
#include <queue>

namespace asyncpp {
	/**
	 * \brief A threadsafe (mutex protected) queue
	 * 
	 * \tparam T Type of the contained elements
	 * \tparam Container Type of the container to use, defaults to std::deque<T>
	 */
	template<typename T, typename Container = std::deque<T>>
	class threadsafe_queue {
		std::mutex m_mutex;
		std::queue<T, Container> m_queue;

	public:
		/**
		* \brief Construct a new queue
		* \param args Arguments to forward to the container constructor
		*/
		template<typename... Args>
		threadsafe_queue(Args&&... args) : m_mutex{}, m_queue(std::forward<Args>(args)...) {}

		threadsafe_queue(const threadsafe_queue& other) : m_mutex{}, m_queue{} {
			std::scoped_lock lck{other.m_mutex};
			m_queue = other.m_queue;
		}
		threadsafe_queue(threadsafe_queue&& other) : m_mutex{}, m_queue{} {
			std::scoped_lock lck{other.m_mutex};
			m_queue = std::move(other.m_queue);
		}
		threadsafe_queue& operator=(const threadsafe_queue& other) {
			std::scoped_lock lck{other.m_mutex, m_mutex};
			m_queue = other.m_queue;
		}
		threadsafe_queue& operator=(threadsafe_queue&& other) {
			std::scoped_lock lck{other.m_mutex, m_mutex};
			m_queue = std::move(other.m_queue);
		}

		/**
		 * \brief Pop the first element from the queue.
		 * \return The first element of the queue or std::nullopt if the queue is empty.
		 */
		std::optional<T> pop() {
			std::scoped_lock lck{m_mutex};
			if (m_queue.empty()) return std::nullopt;
			auto e = std::move(m_queue.front());
			m_queue.pop();
			return e;
		}

		/**
		 * \brief Push an element to the queue
		 * \param val The element to push
		 */
		void push(T val) {
			std::scoped_lock lck{m_mutex};
			m_queue.push(std::move(val));
		}

		/**
		 * \brief Emplace a new element to the queue
		 * \param args Arguments to forward to the element constructor
		 */
		template<typename... Args>
		void emplace(Args&&... args) {
			std::scoped_lock lck{m_mutex};
			m_queue.emplace(std::forward<Args>(args)...);
		}
	};
} // namespace asyncpp

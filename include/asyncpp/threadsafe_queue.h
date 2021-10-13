#pragma once
#include <mutex>
#include <optional>
#include <queue>

namespace asyncpp {
	template<typename T, typename Container = std::deque<T>>
	class threadsafe_queue {
		std::mutex m_mutex;
		std::queue<T, Container> m_queue;

	public:
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

		std::optional<T> pop() {
			std::scoped_lock lck{m_mutex};
			if (m_queue.empty()) return std::nullopt;
			auto e = std::move(m_queue.front());
			m_queue.pop();
			return e;
		}

		void push(const T& val) {
			std::scoped_lock lck{m_mutex};
			m_queue.push(val);
		}

		void push(T&& val) {
			std::scoped_lock lck{m_mutex};
			m_queue.push(std::move(val));
		}

		template<typename... Args>
		decltype(auto) emplace(Args&&... args) {
			std::scoped_lock lck{m_mutex};
			return m_queue.emplace(std::forward<Args>(args)...);
		}
	};
} // namespace asyncpp

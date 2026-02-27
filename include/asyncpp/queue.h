#pragma once
#include <asyncpp/detail/concepts.h>
#include <asyncpp/detail/std_import.h>
#include <asyncpp/dispatcher.h>

#include <cassert>
#include <cstdio>
#include <mutex>
#include <optional>
#include <queue>

namespace asyncpp {

	/**
	 * \brief Queue for sharing items between multiple producers/consumers
	 */
	template<typename T, typename TContainer = std::deque<T>>
	class queue {
		class pop_awaiter;
		class push_awaiter;

		mutable std::mutex m_mtx;
		std::queue<T, TContainer> m_queue;
		size_t m_max_size;
		dispatcher* m_dispatcher;
		pop_awaiter* m_pop_list{};
		push_awaiter* m_push_list{};

	public:
		/**
		 * \brief Construct a new queue
		 * \param max_size The maximum size of items being stored inside the queue before suspending producers.
		 * \param disp A default dispatcher to use for resuming pop/push operations
		 * \param args Arguments passed to the underlying std::queue constructor
		 */
		template<typename... Args>
		explicit queue(size_t max_size = std::numeric_limits<size_t>::max(), dispatcher* disp = nullptr, Args&&... args)
			: m_queue(std::forward<Args>(args)...), m_max_size(max_size), m_dispatcher{disp} {}

		queue(const queue&) = delete;
		queue(queue&&) noexcept;
		queue& operator=(const queue&) = delete;
		queue& operator=(queue&&) noexcept;

		/**
		 * \brief Get the number of items available without suspending
		 * \return Number of items until `pop()` blocks
		 * \note The number returned by size() can exceed the maximum set in the 
		 *       constructor, because it includes tasks suspended in push().
		 */
		[[nodiscard]] size_t size() const noexcept;
		/**
		 * \brief True if the queue is empty and calling `pop()` would suspend
		 */
		[[nodiscard]] bool empty() const noexcept;

		/**
		 * \brief Try pushing a new item to the queue.
		 * \param value The item to push
		 * \return true if the item was pushed to the queue, false if the queue is full
		 */
		[[nodiscard]] bool try_push(T&& value);

		/**
		 * \brief Try pushing a new item to the queue.
		 * \param value The item to push
		 * \return true if the item was pushed to the queue, false if the queue is full
		 */
		template<typename... Args>
		[[nodiscard]] bool try_emplace(Args&&... args);

		/**
		 * \brief Try poping a item from the queue.
		 * \return The removed item or std::nullopt if the queue is empty
		 */
		[[nodiscard]] std::optional<T> try_pop();

		/**
		 * \brief Pop a value from the queue and suspend if the queue is empty.
		 */
		[[nodiscard]] pop_awaiter pop();

		/**
		 * \brief Push a value to the queue and suspend if the queue is full.
		 * \param value The item to push
		 */
		[[nodiscard]] push_awaiter push(T&& value);

		/**
		 * \brief Clear the queue
		 */
		void clear();
	};

	template<typename T, typename TContainer>
	class queue<T, TContainer>::pop_awaiter {
		queue* m_parent;

		pop_awaiter* m_next{};
		coroutine_handle<> m_handle;
		dispatcher* m_dispatcher = dispatcher::current();
		std::optional<T> m_result{std::nullopt};

		friend class queue<T, TContainer>::push_awaiter;
		friend class queue<T, TContainer>;

		void resume(T&& value);

	public:
		explicit pop_awaiter(queue* parent) noexcept : m_parent(parent) {}

		/**
		 * \brief Set a different dispatcher to resume this operation on.
		 * \param dsp The new dispatcher to resume on
		 */
		pop_awaiter& resume_on(dispatcher* dsp) noexcept {
			m_dispatcher = dsp;
			return *this;
		}

		[[nodiscard]] bool await_ready();
		[[nodiscard]] bool await_suspend(coroutine_handle<> hndl);
		[[nodiscard]] std::optional<T> await_resume();
	};

	template<typename T, typename TContainer>
	class queue<T, TContainer>::push_awaiter {
		queue* m_parent;
		T m_value;

		push_awaiter* m_next{};
		coroutine_handle<> m_handle;
		dispatcher* m_dispatcher = dispatcher::current();
		bool m_result{true};

		friend class queue<T, TContainer>::pop_awaiter;
		friend class queue<T, TContainer>;

		void resume();

	public:
		explicit push_awaiter(queue* parent, T&& value) noexcept : m_parent(parent), m_value(std::forward<T>(value)) {}

		/**
		 * \brief Set a different dispatcher to resume this operation on.
		 * \param dsp The new dispatcher to resume on
		 */
		push_awaiter& resume_on(dispatcher* dsp) noexcept {
			m_dispatcher = dsp;
			return *this;
		}

		[[nodiscard]] bool await_ready();
		void await_suspend(coroutine_handle<> hndl);
		bool await_resume();
	};

	template<typename T, typename TContainer>
	inline queue<T, TContainer>::queue(queue&& other) noexcept {
		std::scoped_lock lck(other.m_mtx);
		m_queue = std::move(other.m_queue);
		m_max_size = other.m_max_size;
		m_dispatcher = other.m_dispatcher;
		// NOTE: We do not bother to update the m_parent pointer inside the awaitables,
		//       because its not used after await_suspend returned.
		m_pop_list = other.m_pop_list;
		other.m_pop_list = nullptr;
		m_push_list = other.m_push_list;
		other.m_push_list = nullptr;
	}

	template<typename T, typename TContainer>
	inline queue<T, TContainer>& queue<T, TContainer>::operator=(queue&& other) noexcept {
		pop_awaiter* old_pop = nullptr;
		push_awaiter* old_push = nullptr;
		{
			std::scoped_lock lck(m_mtx, other.m_mtx);
			m_queue = std::move(other.m_queue);
			m_max_size = other.m_max_size;
			m_dispatcher = other.m_dispatcher;
			// NOTE: We do not bother to update the m_parent pointer inside the awaitables,
			//       because its not used after await_suspend returned.
			old_pop = m_pop_list;
			m_pop_list = other.m_pop_list;
			other.m_pop_list = nullptr;
			old_push = m_push_list;
			m_push_list = other.m_push_list;
			other.m_push_list = nullptr;
		}
		// Fail all pop operations on the moved to queue
		while (old_pop != nullptr) {
			auto await = old_pop;
			old_pop = old_pop->m_next;
			await->m_result.reset();
			if (await->m_dispatcher != nullptr)
				await->m_dispatcher->push([await]() { await->m_handle.resume(); });
			else
				await->m_handle.resume();
		}
		// Fail all push operations on the moved to queue
		while (old_push != nullptr) {
			auto await = old_push;
			old_push = old_push->m_next;
			await->m_result = false;
			await->resume();
		}
		return *this;
	}

	template<typename T, typename TContainer>
	inline size_t queue<T, TContainer>::size() const noexcept {
		std::unique_lock lck{m_mtx};
		size_t cnt = m_queue.size();
		for (auto pa = m_push_list; pa != nullptr; pa = pa->m_next)
			cnt++;
		return cnt;
	}

	template<typename T, typename TContainer>
	inline bool queue<T, TContainer>::empty() const noexcept {
		std::unique_lock lck{m_mtx};
		return m_queue.empty() && m_push_list == nullptr;
	}

	template<typename T, typename TContainer>
	inline bool queue<T, TContainer>::try_push(T&& value) {
		std::unique_lock lck{m_mtx};
		// Early exit if we are oversized
		if (m_queue.size() >= m_max_size) return false;
		// Check if there is an task waiting in pop
		if (auto awaiter = m_pop_list; awaiter != nullptr) {
			m_pop_list = awaiter->m_next;
			// There should never be a task waiting in pop with a non empty queue
			assert(m_queue.empty());
			lck.unlock();
			awaiter->resume(std::forward<T>(value));
		} else {
			// There is noone waiting for the data (yet), so we just queue it
			m_queue.push(std::forward<T>(value));
		}
		return true;
	}

	template<typename T, typename TContainer>
	template<typename... Args>
	inline bool queue<T, TContainer>::try_emplace(Args&&... args) {
		std::unique_lock lck{m_mtx};
		// Early exit if we are oversized
		if (m_queue.size() >= m_max_size) return false;
		// Check if there is an task waiting in pop
		if (auto awaiter = m_pop_list; awaiter != nullptr) {
			m_pop_list = awaiter->m_next;
			// There should never be a task waiting in pop with a non empty queue
			assert(m_queue.empty());
			lck.unlock();
			awaiter->resume(T{std::forward<Args>(args)...});
		} else {
			// There is noone waiting for the data (yet), so we just queue it
			m_queue.emplace(std::forward<Args>(args)...);
		}
		return true;
	}

	template<typename T, typename TContainer>
	inline std::optional<T> queue<T, TContainer>::try_pop() {
		std::unique_lock lck{m_mtx};
		// Early out if the queue is empty
		if (m_queue.empty()) return std::nullopt;
		// Pop the first value
		std::optional<T> res{std::move(m_queue.front())};
		m_queue.pop();
		// Check if there is someone waiting in push
		if (auto awaiter = m_push_list; awaiter != nullptr) {
			m_push_list = awaiter->m_next;
			// This can only happen if the queue used to be full
			assert(m_queue.size() == m_max_size - 1);
			m_queue.push(std::move(awaiter->m_value));
			lck.unlock();
			// Resume the first task waiting to push a value
			awaiter->resume();
		}
		return res;
	}

	template<typename T, typename TContainer>
	inline typename queue<T, TContainer>::pop_awaiter queue<T, TContainer>::pop() {
		return pop_awaiter{this};
	}

	template<typename T, typename TContainer>
	inline typename queue<T, TContainer>::push_awaiter queue<T, TContainer>::push(T&& value) {
		return push_awaiter{this, std::forward<T>(value)};
	}

	template<typename T, typename TContainer>
	inline void queue<T, TContainer>::clear() {
		std::unique_lock lck{m_mtx};
		while (!m_queue.empty())
			m_queue.pop();
		// Check if there is someone waiting in push
		auto push_list = m_push_list;
		m_push_list = nullptr;
		lck.unlock();

		while (push_list != nullptr) {
			const auto await = push_list;
			push_list = push_list->m_next;
			await->m_result = false;
			await->resume();
		}
	}

	template<typename T, typename TContainer>
	inline void queue<T, TContainer>::pop_awaiter::resume(T&& value) {
		// NOTE: Because we do not update the m_parent variable in move constructors we can not use it here
		// Move the value right to this awaiter
		m_result.emplace(std::forward<T>(value));
		// And resume
		if (m_dispatcher != nullptr)
			m_dispatcher->push([this]() { m_handle.resume(); });
		else
			m_handle.resume();
	}

	template<typename T, typename TContainer>
	inline bool queue<T, TContainer>::pop_awaiter::await_ready() {
		std::unique_lock lck{m_parent->m_mtx};
		if (m_parent->m_queue.empty()) {
			lck.release(); // We unlock inside suspend
			return false;
		}
		m_result.emplace(std::move(m_parent->m_queue.front()));
		m_parent->m_queue.pop();
		// Check if there is someone waiting in push
		auto awaiter = m_parent->m_push_list;
		if (awaiter != nullptr) {
			m_parent->m_push_list = awaiter->m_next;
			// This can only happen if the queue used to be full
			assert(m_parent->m_queue.size() == m_parent->m_max_size - 1);
			m_parent->m_queue.push(std::move(awaiter->m_value));
			lck.unlock();
			// Resume the first task waiting to push a value
			awaiter->resume();
		}
		return true;
	}

	template<typename T, typename TContainer>
	inline bool queue<T, TContainer>::pop_awaiter::await_suspend(coroutine_handle<> hndl) {
		m_handle = hndl;
		auto val = m_parent->m_pop_list;
		while (val != nullptr && val->m_next != nullptr)
			val = val->m_next;
		if (val == nullptr)
			m_parent->m_pop_list = this;
		else
			val->m_next = this;
		// Copy dispatcher from parent if needed to allow for easier move constructor
		if (m_dispatcher == nullptr) m_dispatcher = m_parent->m_dispatcher;
		// Unlock the mutex locked in await_ready
		m_parent->m_mtx.unlock();
		return true;
	}

	template<typename T, typename TContainer>
	inline std::optional<T> queue<T, TContainer>::pop_awaiter::await_resume() {
		// NOTE: Because we do not update the m_parent variable in move constructors we can not use it here
		return std::move(m_result);
	}

	template<typename T, typename TContainer>
	inline void queue<T, TContainer>::push_awaiter::resume() {
		// NOTE: Because we do not update the m_parent variable in move constructors we can not use it here
		if (m_dispatcher != nullptr)
			m_dispatcher->push([this]() { m_handle.resume(); });
		else
			m_handle.resume();
	}

	template<typename T, typename TContainer>
	inline bool queue<T, TContainer>::push_awaiter::await_ready() {
		std::unique_lock lck{m_parent->m_mtx};
		// If there is a task waiting to pop
		if (m_parent->m_pop_list != nullptr) {
			assert(m_parent->m_queue.empty());
			auto awaiter = m_parent->m_pop_list;
			m_parent->m_pop_list = awaiter->m_next;
			lck.unlock();
			awaiter->resume(std::move(m_value));
			return true;
		}
		// If there is space in the queue push to it
		if (m_parent->m_queue.size() < m_parent->m_max_size) {
			m_parent->m_queue.push(std::move(m_value));
			return true;
		}
		// Otherwise we suspend
		lck.release();
		return false;
	}

	template<typename T, typename TContainer>
	inline void queue<T, TContainer>::push_awaiter::await_suspend(coroutine_handle<> hndl) {
		m_handle = hndl;
		auto val = m_parent->m_push_list;
		while (val != nullptr && val->m_next != nullptr)
			val = val->m_next;
		if (val == nullptr)
			m_parent->m_push_list = this;
		else
			val->m_next = this;
		// Copy dispatcher from parent if needed to allow for easier move constructor
		if (m_dispatcher == nullptr) m_dispatcher = m_parent->m_dispatcher;
		// Unlock the mutex locked in await_ready
		m_parent->m_mtx.unlock();
	}

	template<typename T, typename TContainer>
	inline bool queue<T, TContainer>::push_awaiter::await_resume() {
		// NOTE: Because we do not update the m_parent variable in move constructors we can not use it here
		return m_result;
	}
} // namespace asyncpp

#pragma once
#include <asyncpp/detail/std_import.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>

namespace asyncpp {
	class mutex_lock;
	/**
     * \brief A mutex with an asynchronous lock() operation.
     *
     * Provides a asynchronous lock() method that can be awaited to allow
     * usage in coroutines, without blocking others. Unlike std::mutex,
     * this mutex is not tied to a particular thread. This is usefull if
     * the coroutine gets suspended while holding the lock and is resumed
     * on a different thread. The implementation is lock-free and does not throw.
     */
	class mutex {
	public:
		struct lock_awaiter;
		struct scoped_lock_awaiter;
		struct construct_locked_t {};
		/// \brief Tag value used to construct locked mutex
		constexpr static construct_locked_t construct_locked{};
		friend class mutex_lock;
		/// \brief Construct mutex in its unlocked state
		constexpr mutex() noexcept : m_state{state_unlocked}, m_awaiters{nullptr} {}
		/// \brief Construct mutex in its locked state
		constexpr mutex(construct_locked_t) noexcept : m_state{state_locked_no_waiters}, m_awaiters{nullptr} {}
		/**
         * \brief Destruct mutex
         *
         * \note If the mutex is destroyed while there are coroutines trying to lock it, the behaviour is undefined.
         */
		~mutex() {
			[[maybe_unused]] auto state = m_state.load(std::memory_order_relaxed);
			assert(state == state_unlocked || state == state_locked_no_waiters);
			assert(m_awaiters == nullptr);
		}
		mutex(const mutex&) noexcept = delete;
		mutex(mutex&&) noexcept = delete;
		mutex& operator=(const mutex&) noexcept = delete;
		mutex& operator=(mutex&&) noexcept = delete;

		/**
         * \brief Attempt to acquire the mutex whitout blocking or yielding.
         * 
         * \return true The lock was aquired
         * \return false The lock could not be aquired (its already locked)
         */
		bool try_lock() noexcept {
			auto old = state_unlocked;
			return m_state.compare_exchange_strong(old, state_locked_no_waiters, std::memory_order::acquire,
												   std::memory_order::relaxed);
		}
		/**
         * \brief Acquire the the mutex using co_await.
         * 
         * \return An awaitable type that will try to lock the mutex once awaited
         *         and resume once it holds the mutex.
         */
		constexpr lock_awaiter lock() noexcept;
		/**
         * \brief Acquire the the mutex using co_await and wrap it in a mutex_lock.
         * 
         * \return An awaitable type that will try to lock the mutex once awaited
         *         and resume once it holds the mutex.
         */
		constexpr scoped_lock_awaiter lock_scoped() noexcept;
		/**
         * \brief Unlock the mutex
         *
         * \note Behaviour is undefined if the mutex is not currently locked. Resumes one
         *       of the waiting coroutines (if any) on the current thread before it returns.
         */
		void unlock() noexcept;
		/**
         * \brief Query if the lock is currently locked
         * \warning This is unreliable if the mutex is used in multiple preemtive threads.
         */
		bool is_locked() const noexcept { return m_state.load(std::memory_order::relaxed) != state_unlocked; }

	private:
		static constexpr std::uintptr_t state_locked_no_waiters = 0;
		static constexpr std::uintptr_t state_unlocked = 1;
		std::atomic<uintptr_t> m_state;
		lock_awaiter* m_awaiters;
	};

	struct [[nodiscard]] mutex::lock_awaiter {
		class mutex* mutex;
		lock_awaiter* next{nullptr};
		coroutine_handle<> handle{};
		constexpr bool await_ready() const noexcept { return false; }
		bool await_suspend(coroutine_handle<> h) noexcept {
			handle = h;
			auto old = mutex->m_state.load(std::memory_order::acquire);
			while (true) {
				if (old == state_unlocked) {
					if (mutex->m_state.compare_exchange_weak(old, state_locked_no_waiters, std::memory_order::acquire,
															 std::memory_order::relaxed))
						return false;
				} else {
					next = reinterpret_cast<lock_awaiter*>(old);
					if (mutex->m_state.compare_exchange_weak(old, reinterpret_cast<std::uintptr_t>(this),
															 std::memory_order::release, std::memory_order::relaxed))
						return true;
				}
			}
		}
		constexpr void await_resume() const noexcept {}
	};

	/**
     * \brief RAII type to automatically unlock a mutex once it leaves scope
     */
	class mutex_lock {
	public:
		/**
         * \brief Construct a mutex_lock for the given mutex, adopting the lock
         * \note Behaviour is undefined if the supplied mutex has not been locked (e.g. using try_lock()).
         */
		mutex_lock(class mutex& mtx, std::adopt_lock_t) noexcept : m_mtx(&mtx), m_locked{true} {
			assert(mtx.is_locked());
		}
		/**
         * \brief Construct an unlocked mutex_lock for the given mutex
         * \note Unlike std::lock_guard/unique_lock, the mutex is not locked by the constructor.
         */
		constexpr mutex_lock(class mutex& mtx) noexcept : m_mtx(&mtx), m_locked{false} {}
		constexpr mutex_lock(const mutex_lock&) noexcept = delete;
		constexpr mutex_lock& operator=(const mutex_lock&) noexcept = delete;
		constexpr mutex_lock(mutex_lock&& other) noexcept : m_mtx(other.m_mtx), m_locked{other.m_locked} {
			other.m_mtx = nullptr;
			other.m_locked = false;
		}
		mutex_lock& operator=(mutex_lock&& other) noexcept {
			if (m_mtx && m_locked) m_mtx->unlock();
			m_mtx = other.m_mtx;
			other.m_mtx = nullptr;
			m_locked = other.m_locked;
			other.m_locked = false;
			return *this;
		}
		~mutex_lock() {
			if (m_mtx && m_locked) m_mtx->unlock();
		}

		/**
         * \brief Unlock the lock
         * See mutex::unlock() for details.
         */
		void unlock() noexcept {
			assert(m_locked);
			m_mtx->unlock();
			m_locked = false;
		}

		/**
         * \brief Try locking the contained mutex
         *
         * See mutex::try_lock() for details.
         */
		bool try_lock() noexcept {
			assert(!m_locked);
			auto res = m_mtx->try_lock();
			m_locked = res;
			return res;
		}

		/**
         * \brief Asynchronously lock the contained mutex
         *
         * See mutex::lock() for details.
         */
		auto lock() noexcept {
			struct awaiter {
				mutex_lock* that;
				mutex::lock_awaiter mutex_awaiter;
				bool await_ready() const noexcept { return that->m_locked || mutex_awaiter.await_ready(); }
				auto await_suspend(coroutine_handle<> h) noexcept { return mutex_awaiter.await_suspend(h); }
				void await_resume() const noexcept {
					mutex_awaiter.await_resume();
					that->m_locked = true;
				}
			};
			return awaiter{this, this->m_mtx};
		}

		/// \brief Check if the mutex is held by this lock
		bool is_locked() const noexcept { return m_locked; }
		/// \brief Get the wrapped mutex
		class mutex& mutex() const noexcept { return *m_mtx; }

	private:
		class mutex* m_mtx;
		bool m_locked;
	};

	struct [[nodiscard]] mutex::scoped_lock_awaiter {
		lock_awaiter awaiter;
		constexpr bool await_ready() const noexcept { return awaiter.await_ready(); }
		bool await_suspend(coroutine_handle<> h) noexcept { return awaiter.await_suspend(h); }
		[[nodiscard]] mutex_lock await_resume() const noexcept {
			awaiter.await_resume();
			return mutex_lock{*awaiter.mutex, std::adopt_lock};
		}
	};

	constexpr inline mutex::lock_awaiter mutex::lock() noexcept { return lock_awaiter{this}; }

	constexpr inline mutex::scoped_lock_awaiter mutex::lock_scoped() noexcept { return scoped_lock_awaiter{this}; }

	inline void mutex::unlock() noexcept {
		assert(m_state.load(std::memory_order::relaxed) != state_unlocked);

		auto head = m_awaiters;
		if (head == nullptr) {
			auto old = state_locked_no_waiters;
			const auto didrelease = m_state.compare_exchange_strong(old, state_unlocked, std::memory_order::release,
																	std::memory_order::relaxed);
			if (didrelease) return;
			old = m_state.exchange(state_locked_no_waiters, std::memory_order::acquire);
			assert(old != state_locked_no_waiters && old != state_unlocked);
			auto next = reinterpret_cast<lock_awaiter*>(old);
			do {
				auto temp = next->next;
				next->next = head;
				head = next;
				next = temp;
			} while (next != nullptr);
		}
		assert(head != nullptr);
		m_awaiters = head->next;
		head->handle.resume();
	}

} // namespace asyncpp

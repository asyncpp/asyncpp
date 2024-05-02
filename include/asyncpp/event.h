#pragma once
#include <asyncpp/detail/std_import.h>
#include <asyncpp/dispatcher.h>
#include <atomic>
#include <cassert>

namespace asyncpp {
	/**
     * \brief Simple manual reset event supporting a single consumer.
     * 
     * This is similar in concept to a std::condition_variable and allows
     * synchronization between coroutines, as well as normal code and coroutines.
     * If the current coroutine co_await's the event it is suspended until
     * some other coroutine or thread calls set(). If the event is already
     * set when calling co_await the coroutine will directly continue execution
     * in the current thread. If the event is not set, the coroutine gets resumed
     * on the dispatcher that's passed into wait() or inside the call to set() if
     * no dispatcher was provided. The operator co_await will behave as if
     * wait() was called with the result of dispatcher::current(), meaning the
     * coroutine is resumed on the same dispatcher it suspended (not
     * necessarily the same thread, e.g. on a thread pool). If no dispatcher is
     * associated with the current thread it is resumed inside set().
     * 
     * \note single_consumer_event is threadsafe, however at most one coroutine can
     *      wait on it at any given time. Calling set(), reset() or is_set() from different
     *      threads at arbitrary times is fine, though it might be hard to predict the results.
     * \note Destroying a event thats currently being awaited can cause resource leaks as the
     *      waiting coroutine can never resume. (This asserts in debug mode)
     */
	class single_consumer_event {
	public:
		/**
         * \brief Construct a new event
         * \param set_initially The initial state of the event (true => set, false => unset)
         */
		explicit constexpr single_consumer_event(bool set_initially = false) noexcept
			: m_state(set_initially ? this : nullptr) {}
#ifndef NDEBUG
		~single_consumer_event() noexcept { assert(!is_awaited()); }
#endif
		/**
         * \brief Query if the event is currently set
         * \note Do not base decisions on this value, as it might change at any time by a call to reset()
         */
		[[nodiscard]] bool is_set() const noexcept { return m_state.load(std::memory_order::acquire) == this; }

		/**
         * \brief Query if the event is currently being awaited
         * \note Do not base decisions on this value, as it might change at any time by a call to set()
         */
		[[nodiscard]] bool is_awaited() const noexcept {
			auto val = m_state.load(std::memory_order::acquire);
			return val != nullptr && val != this;
		}

		/**
         * \brief Set the event
         * \note Depending on the way the event was awaited, the suspended coroutine might resume in this call.
         * \param resume_dispatcher Fallback dispatcher to use for resuming the coroutine if none was passed to wait()
         * \return true if a coroutine has been waiting and was resumed
         */
		bool set(dispatcher* resume_dispatcher = nullptr) noexcept {
			auto state = m_state.exchange(this, std::memory_order::acq_rel);
			if (state != nullptr && state != this) {
				auto await = static_cast<awaiter*>(state);
				assert(await->m_parent == this);
				assert(await->m_handle);
				if (await->m_dispatcher != nullptr) {
					await->m_dispatcher->push([hdl = await->m_handle]() mutable { hdl.resume(); });
				} else if (resume_dispatcher != nullptr) {
					resume_dispatcher->push([hdl = await->m_handle]() mutable { hdl.resume(); });
				} else {
					await->m_handle.resume();
				}
				return true;
			}
			return false;
		}

		/**
         * \brief Reset the event back to unset
         */
		void reset() noexcept {
			void* old_state = this;
			m_state.compare_exchange_strong(old_state, nullptr, std::memory_order::relaxed);
		}

		/**
         * \brief Suspend the current coroutine until the event is set
         * 
         * If the event is already set, it resumes immediately on the current thread, otherwise the coroutine
         * is suspended and waits until a call to set() is made. The coroutine will resume on the current
         * dispatcher if the thread belongs to a dispatcher or inside set() if not.
         * \return Awaitable
         */
		[[nodiscard]] auto operator co_await() noexcept { return awaiter{this, dispatcher::current()}; }

		/**
         * \brief Suspend the current coroutine until the event is set
         * 
         * If the event is already set, it resumes immediately on the current thread, otherwise the coroutine
         * is suspended and waits until a call to set() is made. The coroutine will resume on the provided
         * dispatcher if not nullptr or inside set() if not.
         * 
         * \param resume_dispatcher The dispatcher to resume on or nullptr to resume inside set()
         * \return Awaitable
         */
		[[nodiscard]] constexpr auto wait(dispatcher* resume_dispatcher = nullptr) noexcept {
			return awaiter{this, resume_dispatcher};
		}

	private:
		/* nullptr => unset
		 * this => set
		 * x => awaiter*
		 */
		std::atomic<void*> m_state;

		struct [[nodiscard]] awaiter {
			explicit constexpr awaiter(single_consumer_event* parent, dispatcher* dispatcher) noexcept
				: m_parent(parent), m_dispatcher(dispatcher) {}
			[[nodiscard]] bool await_ready() const noexcept { return m_parent->is_set(); }
			[[nodiscard]] bool await_suspend(coroutine_handle<> hdl) noexcept {
				m_handle = hdl;
				void* old_state = nullptr;
				// If the current state is unset set it to this
				bool was_equal = m_parent->m_state.compare_exchange_strong(old_state, this, std::memory_order::release,
																		   std::memory_order::acquire);
				// If the state was not unset it has to be set,
				// otherwise we have a concurrent await, which is not supported
				assert(was_equal || old_state == m_parent);
				return was_equal;
			}
			constexpr void await_resume() const noexcept {}

			single_consumer_event* m_parent;
			dispatcher* m_dispatcher;
			coroutine_handle<> m_handle{};
		};
	};

	/**
     * \brief Simple auto reset event supporting a single consumer.
     * 
     * This is similar in concept to a std::condition_variable and allows
     * synchronization between coroutines, as well as normal code and coroutines.
     * If the current coroutine co_await's the event it is suspended until
     * some other coroutine or thread calls set(). If the event is already
     * set when calling co_await the coroutine will directly continue execution
     * in the current thread. If the event is not set, the coroutine gets resumed
     * on the dispatcher that's passed into wait() or inside the call to set() if
     * no dispatcher was provided. The operator co_await will behave as if
     * wait() was called with the result of dispatcher::current(), meaning the
     * coroutine is resumed on the same dispatcher it suspended (not
     * necessarily the same thread, e.g. on a thread pool). If no dispatcher is
     * associated with the current thread it is resumed inside set(). Unlike
	 * `single_consumer_event` this does not need to get reset manually, it
	 * is automatically reset once a suspended coroutine is resumed.
     * 
     * \note single_consumer_auto_reset_event is threadsafe, however at most one coroutine can
     *      wait on it at any given time. Calling set() or is_set() from different
     *      threads at arbitrary times is fine, though it might be hard to predict the results.
     * \note Destroying a event thats currently being awaited can cause resource leaks as the
     *      waiting coroutine can never resume. (This asserts in debug mode)
     */
	class single_consumer_auto_reset_event {
	public:
		/**
         * \brief Construct a new event
         * \param set_initially The initial state of the event (true => set, false => unset)
         */
		explicit constexpr single_consumer_auto_reset_event(bool set_initially = false) noexcept
			: m_state(set_initially ? this : nullptr) {}
#ifndef NDEBUG
		~single_consumer_auto_reset_event() noexcept { assert(!is_awaited()); }
#endif
		/**
         * \brief Query if the event is currently set
         * \note Do not base decisions on this value, as it might change at any time by a call to reset()
         */
		[[nodiscard]] bool is_set() const noexcept { return m_state.load(std::memory_order::acquire) == this; }

		/**
         * \brief Query if the event is currently being awaited
         * \note Do not base decisions on this value, as it might change at any time by a call to set()
         */
		[[nodiscard]] bool is_awaited() const noexcept {
			auto ptr = m_state.load(std::memory_order::acquire);
			return ptr != nullptr && ptr != this;
		}

		/**
         * \brief Set the event
         * \note Depending on the way the event was awaited, the suspended coroutine might resume in this call.
         * \param resume_dispatcher Fallback dispatcher to use for resuming the coroutine if none was passed to wait()
         * \return true if a coroutine has been waiting and was resumed
         */
		bool set(dispatcher* resume_dispatcher = nullptr) noexcept {
			auto state = m_state.exchange(this, std::memory_order::release);
			if (state != nullptr && state != this) {
				auto await = static_cast<awaiter*>(state);

				// Only modify the state if it has not been changed in between
				state = this;
				m_state.compare_exchange_strong(state, nullptr, std::memory_order::acq_rel);

				assert(await->m_parent == this);
				assert(await->m_handle);
				if (await->m_dispatcher != nullptr) {
					await->m_dispatcher->push([hdl = await->m_handle]() mutable { hdl.resume(); });
				} else if (resume_dispatcher != nullptr) {
					resume_dispatcher->push([hdl = await->m_handle]() mutable { hdl.resume(); });
				} else {
					await->m_handle.resume();
				}
				return true;
			}
			return false;
		}

		/**
         * \brief Reset the event back to unset
         */
		void reset() noexcept {
			void* old_state = this;
			m_state.compare_exchange_strong(old_state, nullptr, std::memory_order::relaxed);
		}

		/**
         * \brief Suspend the current coroutine until the event is set
         * 
         * If the event is already set, it resumes immediately on the current thread, otherwise the coroutine
         * is suspended and waits until a call to set() is made. The coroutine will resume on the current
         * dispatcher if the thread belongs to a dispatcher or inside set() if not.
         * \return Awaitable
         */
		[[nodiscard]] auto operator co_await() noexcept { return awaiter{this, dispatcher::current()}; }

		/**
         * \brief Suspend the current coroutine until the event is set
         * 
         * If the event is already set, it resumes immediately on the current thread, otherwise the coroutine
         * is suspended and waits until a call to set() is made. The coroutine will resume on the provided
         * dispatcher if not nullptr or inside set() if not.
         * 
         * \param resume_dispatcher The dispatcher to resume on or nullptr to resume inside set()
         * \return Awaitable
         */
		[[nodiscard]] constexpr auto wait(dispatcher* resume_dispatcher = nullptr) noexcept {
			return awaiter{this, resume_dispatcher};
		}

	private:
		/* nullptr => unset
		 * this => set
		 * x => awaiter*
		 */
		std::atomic<void*> m_state;

		struct [[nodiscard]] awaiter {
			explicit constexpr awaiter(single_consumer_auto_reset_event* parent, dispatcher* dispatcher) noexcept
				: m_parent(parent), m_dispatcher(dispatcher) {}
			[[nodiscard]] constexpr bool await_ready() const noexcept { return false; }
			[[nodiscard]] bool await_suspend(coroutine_handle<> hdl) noexcept {
				m_handle = hdl;
				void* old_state = nullptr;
				if (!m_parent->m_state.compare_exchange_strong(old_state, this, std::memory_order::release,
															   std::memory_order::relaxed)) {
					// No duplicate awaiters allowed, so the only valid values are m_parent and nullptr
					assert(m_parent == old_state);
					m_parent->m_state.exchange(nullptr, std::memory_order::acquire);
					return false;
				}
				return true;
			}
			constexpr void await_resume() const noexcept {}

			single_consumer_auto_reset_event* m_parent;
			dispatcher* m_dispatcher;
			coroutine_handle<> m_handle{};
		};
	};

	/**
     * \brief Simple manual reset event supporting multiple consumers.
     * 
     * This is similar in concept to a std::condition_variable and allows
     * synchronization between coroutines, as well as normal code and coroutines.
     * If the current coroutine co_await's the event it is suspended until
     * some other coroutine or thread calls set(). If the event is already
     * set when calling co_await the coroutine will directly continue execution
     * in the current thread. If the event is not set, the coroutine gets resumed
     * on the dispatcher that's passed into wait() or inside the call to set() if
     * no dispatcher was provided. The operator co_await will behave as if
     * wait() was called with the result of dispatcher::current(), meaning the
     * coroutine is resumed on the same dispatcher it suspended (not
     * necessarily the same thread, e.g. on a thread pool). If no dispatcher is
     * associated with the current thread it is resumed inside set().
     * 
     * \note multi_consumer_event is threadsafe and multiple consumers can await it.
	 *		Calling set() or is_set() from different threads at arbitrary times is safe, though
	 *		it might be hard to predict the results.
     * \note Destroying a event thats currently being awaited can cause resource leaks as the
     *      waiting coroutine can never resume. (This asserts in debug mode)
     */
	class multi_consumer_event {
	public:
		/**
         * \brief Construct a new event
         * \param set_initially The initial state of the event (true => set, false => unset)
         */
		explicit constexpr multi_consumer_event(bool set_initially = false) noexcept
			: m_state(set_initially ? this : nullptr) {}
#ifndef NDEBUG
		~multi_consumer_event() noexcept { assert(!is_awaited()); }
#endif

		/**
         * \brief Query if the event is currently set
         * \note Do not base decisions on this value, as it might change at any time by a call to reset()
         */
		[[nodiscard]] bool is_set() const noexcept { return m_state.load(std::memory_order::acquire) == this; }

		/**
         * \brief Query if the event is currently being awaited
         * \note Do not base decisions on this value, as it might change at any time by a call to set()
         */
		[[nodiscard]] bool is_awaited() const noexcept {
			auto ptr = m_state.load(std::memory_order::acquire);
			return ptr != nullptr && ptr != this;
		}

		/**
         * \brief Set the event
         * \note Depending on the way the event was awaited, the suspended coroutine might resume in this call.
         * \param resume_dispatcher Fallback dispatcher to use for resuming the coroutine if none was passed to wait()
         * \return true if a coroutine has been waiting and was resumed
         */
		bool set(dispatcher* resume_dispatcher = nullptr) noexcept {
			auto state = m_state.exchange(this, std::memory_order::acq_rel);
			if (state == this) return false;
			auto await = static_cast<awaiter*>(state);
			while (await != nullptr) {
				auto next = await->m_next;
				assert(await->m_parent == this);
				assert(await->m_handle);
				if (await->m_dispatcher != nullptr) {
					await->m_dispatcher->push([hdl = await->m_handle]() mutable { hdl.resume(); });
				} else if (resume_dispatcher != nullptr) {
					resume_dispatcher->push([hdl = await->m_handle]() mutable { hdl.resume(); });
				} else {
					await->m_handle.resume();
				}
				await = next;
			}
			return true;
		}

		/**
         * \brief Reset the event back to unset
         */
		void reset() noexcept {
			void* old_state = this;
			m_state.compare_exchange_strong(old_state, nullptr, std::memory_order::relaxed);
		}

		/**
         * \brief Suspend the current coroutine until the event is set
         * 
         * If the event is already set, it resumes immediately on the current thread, otherwise the coroutine
         * is suspended and waits until a call to set() is made. The coroutine will resume on the current
         * dispatcher if the thread belongs to a dispatcher or inside set() if not.
         * \return Awaitable
         */
		[[nodiscard]] auto operator co_await() noexcept { return awaiter{this, dispatcher::current()}; }

		/**
         * \brief Suspend the current coroutine until the event is set
         * 
         * If the event is already set, it resumes immediately on the current thread, otherwise the coroutine
         * is suspended and waits until a call to set() is made. The coroutine will resume on the provided
         * dispatcher if not nullptr or inside set() if not.
         * 
         * \param resume_dispatcher The dispatcher to resume on or nullptr to resume inside set()
         * \return Awaitable
         */
		[[nodiscard]] constexpr auto wait(dispatcher* resume_dispatcher = nullptr) noexcept {
			return awaiter{this, resume_dispatcher};
		}

	private:
		/* nullptr => unset
		 * this => set
		 * x => head of awaiter* list
		 */
		std::atomic<void*> m_state;

		struct [[nodiscard]] awaiter {
			explicit constexpr awaiter(multi_consumer_event* parent, dispatcher* dispatcher) noexcept
				: m_parent(parent), m_dispatcher(dispatcher) {}
			[[nodiscard]] bool await_ready() const noexcept { return m_parent->is_set(); }
			[[nodiscard]] bool await_suspend(coroutine_handle<> hdl) noexcept {
				m_handle = hdl;
				void* old_state = m_parent->m_state.load(std::memory_order::acquire);
				do {
					// event became set
					if (old_state == m_parent) return false;
					m_next = static_cast<awaiter*>(old_state);
				} while (!m_parent->m_state.compare_exchange_weak( //
					old_state, this, std::memory_order::release, std::memory_order::acquire));
				return true;
			}
			constexpr void await_resume() const noexcept {}

			multi_consumer_event* m_parent;
			dispatcher* m_dispatcher;
			awaiter* m_next{nullptr};
			coroutine_handle<> m_handle{};
		};
	};

	/**
     * \brief Simple auto reset event supporting multiple consumers.
     * 
     * This is similar in concept to a std::condition_variable and allows
     * synchronization between coroutines, as well as normal code and coroutines.
     * If the current coroutine co_await's the event it is suspended until
     * some other coroutine or thread calls set(). If the event is already
     * set when calling co_await the coroutine will directly continue execution
     * in the current thread. If the event is not set, the coroutine gets resumed
     * on the dispatcher that's passed into wait() or inside the call to set() if
     * no dispatcher was provided. The operator co_await will behave as if
     * wait() was called with the result of dispatcher::current(), meaning the
     * coroutine is resumed on the same dispatcher it suspended (not
     * necessarily the same thread, e.g. on a thread pool). If no dispatcher is
     * associated with the current thread it is resumed inside set(). Unlike
	 * `multi_consumer_event` this does not need to get reset manually, it
	 * is automatically reset once a suspended coroutine is resumed.
     * 
     * \note multi_consumer_auto_reset_event is threadsafe and multiple consumers can await it.
	 *		Calling set() or is_set() from different threads at arbitrary times is safe, though
	 *		it might be hard to predict the results.
     * \note Destroying a event thats currently being awaited can cause resource leaks as the
     *      waiting coroutine can never resume. (This asserts in debug mode)
     */
	class multi_consumer_auto_reset_event {
		/**
         * \brief Construct a new event
         * \param set_initially The initial state of the event (true => set, false => unset)
         */
		explicit constexpr multi_consumer_auto_reset_event(bool set_initially = false) noexcept
			: m_state(set_initially ? this : nullptr) {}
#ifndef NDEBUG
		~multi_consumer_auto_reset_event() noexcept { assert(!is_awaited()); }
#endif

		/**
         * \brief Query if the event is currently set
         * \note Do not base decisions on this value, as it might change at any time by a call to reset()
         */
		[[nodiscard]] bool is_set() const noexcept { return m_state.load(std::memory_order::acquire) == this; }

		/**
         * \brief Query if the event is currently being awaited
         * \note Do not base decisions on this value, as it might change at any time by a call to set()
         */
		[[nodiscard]] bool is_awaited() const noexcept {
			auto ptr = m_state.load(std::memory_order::acquire);
			return ptr != nullptr && ptr != this;
		}

		/**
         * \brief Set the event
         * \note Depending on the way the event was awaited, the suspended coroutine might resume in this call.
         * \param resume_dispatcher Fallback dispatcher to use for resuming the coroutine if none was passed to wait()
         * \return true if a coroutine has been waiting and was resumed
         */
		bool set(dispatcher* resume_dispatcher = nullptr) noexcept {
			auto state = m_state.exchange(this, std::memory_order::acq_rel);
			if (state == this || state == nullptr) return false;
			auto await = static_cast<awaiter*>(state);

			// Only modify the state if it has not been changed in between
			state = this;
			m_state.compare_exchange_strong(state, nullptr, std::memory_order::acq_rel);

			while (await != nullptr) {
				auto next = await->m_next;
				assert(await->m_parent == this);
				assert(await->m_handle);
				if (await->m_dispatcher != nullptr) {
					await->m_dispatcher->push([hdl = await->m_handle]() mutable { hdl.resume(); });
				} else if (resume_dispatcher != nullptr) {
					resume_dispatcher->push([hdl = await->m_handle]() mutable { hdl.resume(); });
				} else {
					await->m_handle.resume();
				}
				await = next;
			}
			return true;
		}

		/**
         * \brief Reset the event back to unset
         */
		void reset() noexcept {
			void* old_state = this;
			m_state.compare_exchange_strong(old_state, nullptr, std::memory_order::relaxed);
		}

		/**
         * \brief Suspend the current coroutine until the event is set
         * 
         * If the event is already set, it resumes immediately on the current thread, otherwise the coroutine
         * is suspended and waits until a call to set() is made. The coroutine will resume on the current
         * dispatcher if the thread belongs to a dispatcher or inside set() if not.
         * \return Awaitable
         */
		[[nodiscard]] auto operator co_await() noexcept { return awaiter{this, dispatcher::current()}; }

		/**
         * \brief Suspend the current coroutine until the event is set
         * 
         * If the event is already set, it resumes immediately on the current thread, otherwise the coroutine
         * is suspended and waits until a call to set() is made. The coroutine will resume on the provided
         * dispatcher if not nullptr or inside set() if not.
         * 
         * \param resume_dispatcher The dispatcher to resume on or nullptr to resume inside set()
         * \return Awaitable
         */
		[[nodiscard]] constexpr auto wait(dispatcher* resume_dispatcher = nullptr) noexcept {
			return awaiter{this, resume_dispatcher};
		}

	private:
		/* nullptr => unset
		 * this => set
		 * x => head of awaiter* list
		 */
		std::atomic<void*> m_state;

		struct [[nodiscard]] awaiter {
			explicit constexpr awaiter(multi_consumer_auto_reset_event* parent, dispatcher* dispatcher) noexcept
				: m_parent(parent), m_dispatcher(dispatcher) {}
			[[nodiscard]] bool await_ready() const noexcept { return m_parent->is_set(); }
			[[nodiscard]] bool await_suspend(coroutine_handle<> hdl) noexcept {
				m_handle = hdl;
				void* old_state = m_parent->m_state.load(std::memory_order::acquire);
				do {
					// event became set
					if (old_state == m_parent) return false;
					m_next = static_cast<awaiter*>(old_state);
				} while (!m_parent->m_state.compare_exchange_weak( //
					old_state, this, std::memory_order::release, std::memory_order::acquire));
				return true;
			}
			constexpr void await_resume() const noexcept {}

			multi_consumer_auto_reset_event* m_parent;
			dispatcher* m_dispatcher{};
			awaiter* m_next{nullptr};
			coroutine_handle<> m_handle{};
		};
	};
} // namespace asyncpp

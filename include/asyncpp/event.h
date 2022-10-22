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
        constexpr single_consumer_event(bool set_initially = false) noexcept
            : m_state(set_initially ? state_set : state_unset)
        {}
#ifndef NDEBUG
        ~single_consumer_event() noexcept {
            assert(m_state.load(std::memory_order::acquire) <= state_set);
        }
#endif
        /**
         * \brief Query if the event is currently set
         * \note Do not base decisions on this value, as it might change at any time by a call to reset()
         */
		[[nodiscard]] constexpr bool is_set() const noexcept {
			return m_state.load(std::memory_order::acquire) == state_set;
		}

        /**
         * \brief Query if the event is currently being awaited
         * \note Do not base decisions on this value, as it might change at any time by a call to set()
         */
		[[nodiscard]] constexpr bool is_awaited() const noexcept {
			return m_state.load(std::memory_order::acquire) > state_set;
		}

        /**
         * \brief Set the event
         * \note Depending on the way the event was awaited, the suspended coroutine might resume in this call.
         * \param resume_dispatcher Fallback dispatcher to use for resuming the coroutine if none was passed to wait()
         * \return true if a coroutine has been waiting and was resumed
         */
		constexpr bool set(dispatcher* resume_dispatcher = nullptr) noexcept {
			auto state = m_state.exchange(state_set, std::memory_order::acq_rel);
			if (state != state_unset && state != state_set) {
				auto await = reinterpret_cast<awaiter*>(state);
				assert(await->m_parent == this);
				assert(await->m_handle);
				if (await->m_dispatcher != nullptr) {
					await->m_dispatcher->push([hdl = await->m_handle]() { hdl.resume(); });
				} else if(resume_dispatcher != nullptr) {
                    resume_dispatcher->push([hdl = await->m_handle]() { hdl.resume(); });
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
        constexpr void reset() noexcept {
			uintptr_t old_state = state_set;
			m_state.compare_exchange_strong(old_state, state_unset, std::memory_order::relaxed);
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
		enum {
			state_unset = 0,
			state_set = 1,
			// x => unset & waiting, contains to awaiter*
		};
		std::atomic<uintptr_t> m_state;

		struct [[nodiscard]] awaiter {
			single_consumer_event* m_parent;
			dispatcher* m_dispatcher{};
			coroutine_handle<> m_handle{};
			bool await_ready() const noexcept {
				// Dont wait if the event is already set
				return m_parent->m_state.load(std::memory_order::acquire) == state_set;
			}
			bool await_suspend(coroutine_handle<> hdl) noexcept {
				m_handle = hdl;
				uintptr_t state = reinterpret_cast<uintptr_t>(this);
				uintptr_t old_state = state_unset;
				// If the current state is unset set it to this
				bool was_equal = m_parent->m_state.compare_exchange_strong(old_state, state, std::memory_order::release,
																		   std::memory_order::acquire);
				// If the state was not unset it has to be set,
				// otherwise we have a concurrent await, which is not supported
				assert(was_equal || old_state == state_set);
				return was_equal;
			}
			constexpr void await_resume() const noexcept {}
		};
	};
} // namespace asyncpp

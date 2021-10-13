#pragma once
#include <asyncpp/detail/concepts.h>
#include <asyncpp/detail/std_import.h>
#include <asyncpp/policy.h>
#include <atomic>
#include <utility>

namespace asyncpp {
	/**
	 * \brief Fire and Forget coroutine task.
	 * 
	 * Allows to "spin of" an async task from a sync method or to start another thread of execution from within a async method.
	 * This task does not return any value and can not be awaited. It might finish right away (Eager && no await) or outlive the calling method.
	 * An example use case is medius_handler which starts a new task for every message received.
	 * \note Since the coroutine might outlive its caller, care must be taken if pointers or references are passed.
	 * \note If it is used as the return value of a lambda this applies to captures by value as well.
	 * \tparam Eager Flag to indicate if execution should start immediately or only after calling start.
	 */
	template<bool Eager = false>
	struct fire_and_forget_task_impl {
		// Promise type of this task
		class promise_type {
			// Coroutine and task each have a reference
			// TODO: This could be a boolean flag
			std::atomic<size_t> m_ref_count{1};
			std::function<void()> m_exception_handler{};

		public:
			promise_type() noexcept = default;
			promise_type(const promise_type&) = delete;
			promise_type(promise_type&&) = delete;

			coroutine_handle<promise_type> get_return_object() noexcept { return coroutine_handle<promise_type>::from_promise(*this); }
			auto initial_suspend() noexcept {
				if constexpr (Eager) {
					ref();
					return suspend_never{};
				} else
					return suspend_always{};
			}
			auto final_suspend() noexcept {
				struct awaiter {
					promise_type* self;
					constexpr bool await_ready() const noexcept { return self->m_ref_count.fetch_sub(1) == 1; }
					void await_suspend(coroutine_handle<>) const noexcept {}
					constexpr void await_resume() const noexcept {}
				};
				return awaiter{this};
			}
			void return_void() noexcept {}
			void unhandled_exception() {
				if (m_exception_handler)
					m_exception_handler();
				else
					std::terminate();
			}

			auto await_transform(exception_policy policy) {
				m_exception_handler = std::move(policy.handler);
				return suspend_never{};
			}
			template<typename U>
			U&& await_transform(U&& awaitable) noexcept {
				return static_cast<U&&>(awaitable);
			}

			void unref() noexcept {
				auto res = m_ref_count.fetch_sub(1);
				if (res == 1) { coroutine_handle<promise_type>::from_promise(*this).destroy(); }
			}
			void ref() noexcept { m_ref_count.fetch_add(1); }
		};

		using handle_t = coroutine_handle<promise_type>;

		/// \brief Construct from a handle
		fire_and_forget_task_impl(handle_t h) noexcept : m_coro(h) {}

		/// \brief Move constructor
		fire_and_forget_task_impl(fire_and_forget_task_impl&& t) noexcept : m_coro(std::exchange(t.m_coro, {})) {}

		/// \brief Move assignment
		fire_and_forget_task_impl& operator=(fire_and_forget_task_impl&& t) noexcept {
			m_coro = std::exchange(t.m_coro, m_coro);
			return *this;
		}

		fire_and_forget_task_impl(const fire_and_forget_task_impl&) = delete;
		fire_and_forget_task_impl& operator=(const fire_and_forget_task_impl&) = delete;

		/// \brief Destructor
		~fire_and_forget_task_impl() {
			// Since we still have the coroutine, it has never been fire_and_forgeted, so we need to destroy it
			if (m_coro) m_coro.promise().unref();
		}

		/// \brief Start the coroutine execution
		void start() noexcept requires(!Eager) {
			if (m_coro && !m_coro.done()) {
				m_coro.promise().ref();
				m_coro.resume();
			}
		}

	private:
		handle_t m_coro;
	};

	/// \brief Eager task, that immediately starts execution once called.
	using eager_fire_and_forget_task = fire_and_forget_task_impl<true>;
	/// \brief Lazy task, that only starts after calling start().
	using fire_and_forget_task = fire_and_forget_task_impl<false>;
} // namespace asyncpp

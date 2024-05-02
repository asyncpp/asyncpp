#pragma once
#include <asyncpp/detail/concepts.h>
#include <asyncpp/detail/promise_allocator_base.h>
#include <asyncpp/detail/std_import.h>
#include <asyncpp/policy.h>
#include <atomic>
#include <utility>

namespace asyncpp {
	namespace detail {
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
		template<bool Eager = false, ByteAllocator Allocator = default_allocator_type>
		struct fire_and_forget_task_impl {
			// Promise type of this task
			class promise_type : public promise_allocator_base<Allocator> {
				std::atomic<size_t> m_ref_count{1};
				std::function<void()> m_exception_handler{};

			public:
				constexpr promise_type() noexcept = default;
				promise_type(const promise_type&) = delete;
				promise_type(promise_type&&) = delete;

				auto get_return_object() noexcept { return coroutine_handle<promise_type>::from_promise(*this); }
				constexpr auto initial_suspend() noexcept {
					struct awaiter {
						promise_type* self;

						[[nodiscard]] constexpr bool await_ready() const noexcept { return Eager; }
						constexpr void await_suspend(coroutine_handle<>) const noexcept {}
						constexpr void await_resume() const noexcept { self->ref(); }
					};
					return awaiter{this};
				}
				auto final_suspend() noexcept {
					struct awaiter {
						promise_type* self;
						[[nodiscard]] constexpr bool await_ready() const noexcept {
							return self->m_ref_count.fetch_sub(1) == 1;
						}
						void await_suspend(coroutine_handle<>) const noexcept {}
						constexpr void await_resume() const noexcept {}
					};
					return awaiter{this};
				}
				constexpr void return_void() noexcept {}
				void unhandled_exception() noexcept {
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
				constexpr U&& await_transform(U&& awaitable) noexcept {
					return static_cast<U&&>(awaitable);
				}

				void unref() noexcept {
					if (m_ref_count.fetch_sub(1) == 1) coroutine_handle<promise_type>::from_promise(*this).destroy();
				}
				void ref() noexcept { m_ref_count.fetch_add(1); }
			};

			/// \brief Construct from a handle
			//NOLINTNEXTLINE(google-explicit-constructor)
			fire_and_forget_task_impl(coroutine_handle<promise_type> hndl) noexcept : m_coro(hndl) {}

			/// \brief Move constructor
			fire_and_forget_task_impl(fire_and_forget_task_impl&& other) noexcept
				: m_coro(std::exchange(other.m_coro, {})) {}

			/// \brief Move assignment
			fire_and_forget_task_impl& operator=(fire_and_forget_task_impl&& other) noexcept {
				m_coro = std::exchange(other.m_coro, m_coro);
				return *this;
			}

			fire_and_forget_task_impl(const fire_and_forget_task_impl& other) : m_coro{other.m_coro} {
				if (m_coro) m_coro.promise().ref();
			}

			fire_and_forget_task_impl& operator=(const fire_and_forget_task_impl& other) {
				if (&other != this) {
					if (m_coro) m_coro.promise().unref();
					m_coro = other.m_coro;
					if (m_coro) m_coro.promise().ref();
				}
				return *this;
			}

			~fire_and_forget_task_impl() {
				if (m_coro) m_coro.promise().unref();
			}

			/// \brief Start the coroutine execution
			void start() noexcept
				requires(!Eager)
			{
				if (m_coro && !m_coro.done()) {
					m_coro.resume();
					// Make sure it is only started once
					m_coro.promise().unref();
					m_coro = nullptr;
				}
			}

		private:
			coroutine_handle<promise_type> m_coro;
		};
	} // namespace detail

	/// \brief Eager task, that immediately starts execution once called.
	template<class Allocator = default_allocator_type>
	using eager_fire_and_forget_task = detail::fire_and_forget_task_impl<true, Allocator>;
	/// \brief Lazy task, that only starts after calling start().
	template<class Allocator = default_allocator_type>
	using fire_and_forget_task = detail::fire_and_forget_task_impl<false, Allocator>;
} // namespace asyncpp

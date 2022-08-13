#pragma once
#include <asyncpp/detail/promise_allocator_base.h>
#include <asyncpp/scope_guard.h>
#include <atomic>
#include <cassert>
#include <stdexcept>
#include <utility>

namespace asyncpp {
	namespace detail {
		/**
		* \brief Launch coroutine task.
		* 
		* This is a simplified version of fire_and_forget_task, that skips all the unused stuff.
        * If an exception leaves the task it calls std::terminate().
		*/
		template<ByteAllocator Allocator = std::allocator<std::byte>>
		struct launch_task {
			// Promise type of this task
			struct promise_type : promise_allocator_base<Allocator> {
				constexpr launch_task get_return_object() noexcept { return {}; }
				constexpr suspend_never initial_suspend() noexcept { return {}; }
				constexpr suspend_never final_suspend() noexcept { return {}; }
				constexpr void return_void() noexcept {}
				void unhandled_exception() noexcept { std::terminate(); }
			};
		};
	} // namespace detail

	/**
	 * \brief Launch a new asynchronous coroutine. This coroutine will run in the current thread until it executes its first co_await.
	 * 
	 * \tparam Awaitable Type of the awaitable
	 * \tparam Allocator Type of the allocator used for allocating the wrapper task
	 * \param awaitable Awaitable to await
	 * \param allocator Allocator used for allocating the wrapper task
	 */
	template<typename Awaitable, detail::ByteAllocator Allocator = std::allocator<std::byte>>
	void launch(Awaitable&& awaitable, const Allocator& allocator = {}) {
		[](std::decay_t<Awaitable> awaitable, const Allocator&) -> detail::launch_task<Allocator> { co_await std::move(awaitable); }(std::move(awaitable),
																																	 allocator);
	}

	/**
	 * \brief Holder class for spawning child tasks. Allows waiting for all of them to finish.
	 */
	class async_launch_scope {
		std::atomic<size_t> m_count{1u};
		coroutine_handle<> m_continuation{};

	public:
		constexpr async_launch_scope() noexcept = default;
		async_launch_scope(const async_launch_scope&) = delete;
		async_launch_scope& operator=(const async_launch_scope&) = delete;
		~async_launch_scope() { assert(m_continuation); }

		/**
		 * \brief Spawn a new task for the given awaitable.
		 * \param awaitable Awaitable to run
		 * \param allocator Allocator used for allocating the wrapper task
		 */
		template<typename Awaitable, detail::ByteAllocator Allocator = std::allocator<std::byte>>
		void launch(Awaitable&& awaitable, const Allocator& allocator = {}) {
			if (m_count.load(std::memory_order::relaxed) == 0) throw std::logic_error("async_launch_scope can not be reused");
			[](async_launch_scope* scope, std::decay_t<Awaitable> awaitable, const Allocator&) -> detail::launch_task<Allocator> {
				assert(scope->m_count.load(std::memory_order::relaxed) != 0);
				scope->m_count.fetch_add(1, std::memory_order::relaxed);
				scope_guard guard{[scope]() noexcept {
					if (scope->m_count.fetch_sub(1u, std::memory_order::acq_rel) == 1) scope->m_continuation.resume();
				}};
				co_await std::move(awaitable);
			}(this, std::move(awaitable), allocator);
		}
		/**
		 * \brief Wait for all active tasks to finish
		 * \return auto Awaiter that pauses the current coroutine until all spawned task have finished.
		 */
		[[nodiscard]] auto join() {
			if (m_continuation) throw std::logic_error("async_launch_scope was already awaited");
			struct awaiter {
				async_launch_scope* m_scope;
				bool await_ready() noexcept { return m_scope->m_count.load(std::memory_order::acquire) == 0; }
				bool await_suspend(coroutine_handle<> hdl) noexcept {
					m_scope->m_continuation = hdl;
					return m_scope->m_count.fetch_sub(1, std::memory_order::acq_rel);
				}
				void await_resume() noexcept {}
			};
			return awaiter{this};
		}
	};
} // namespace asyncpp

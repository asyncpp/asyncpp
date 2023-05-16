#pragma once
#include <asyncpp/detail/promise_allocator_base.h>
#include <asyncpp/scope_guard.h>
#include <atomic>
#include <cassert>
#include <exception>
#include <future>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace asyncpp {
	namespace detail {
		/**
		* \brief Launch coroutine task.
		* 
		* This is a simplified version of fire_and_forget_task, that skips all the unused stuff.
        * If an exception leaves the task it calls std::terminate().
		*/
		template<ByteAllocator Allocator = default_allocator_type>
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
	template<typename Awaitable, ByteAllocator Allocator = default_allocator_type>
	void launch(Awaitable&& awaitable, const Allocator& allocator = {}) {
		[](std::decay_t<Awaitable> awaitable, const Allocator&) -> detail::launch_task<Allocator> {
			co_await std::move(awaitable);
		}(std::move(awaitable), allocator);
	}

	/**
	 * \brief Holder class for spawning child tasks. Allows waiting for all of them to finish.
	 */
	class async_launch_scope {
		std::atomic<size_t> m_count{0u};
		std::atomic<void*> m_continuation{};

	public:
		constexpr async_launch_scope() noexcept = default;
		async_launch_scope(const async_launch_scope&) = delete;
		async_launch_scope& operator=(const async_launch_scope&) = delete;
		~async_launch_scope() { assert(m_count.load() == 0); }

		/**
		 * \brief Spawn a new task for the given awaitable.
		 * \param awaitable Awaitable to run
		 * \param allocator Allocator used for allocating the wrapper task
		 */
		template<typename Awaitable, ByteAllocator Allocator = default_allocator_type>
		void launch(Awaitable&& awaitable, const Allocator& allocator = {}) {
			[](async_launch_scope* scope, std::decay_t<Awaitable> awaitable,
			   const Allocator&) -> detail::launch_task<Allocator> {
				scope->m_count.fetch_add(1);
				scope_guard guard{[scope]() noexcept {
					// If this is the last task
					if (scope->m_count.fetch_sub(1) == 1) {
						// And we are being awaited
						auto hdl = scope->m_continuation.exchange(nullptr);
						// Resume the awaiter
						if (hdl != nullptr) coroutine_handle<>::from_address(hdl).resume();
					}
				}};
				co_await std::move(awaitable);
			}(this, std::move(awaitable), allocator);
		}

		/**
		 * \brief Invoke the provided callable in a new task. The callable is
		 *		  copied into the task, making it save for usage with e.g. lambda captures.
		 * \param callable The callable to invoke
		 * \param args The arguments to pass into callable on invocation
		 * \param allocator Allocator used for allocating the wrapper task
		 */
		template<typename Callable, typename... Args, ByteAllocator Allocator = default_allocator_type>
			requires(std::is_invocable_v<Callable, Args...>)
		void invoke_tuple(Callable&& callable, std::tuple<Args...>&& args, const Allocator& allocator = {}) {
			[](async_launch_scope* scope, Callable callable, std::tuple<Args...>&& args,
			   const Allocator& allocator) -> detail::launch_task<Allocator> {
				scope->m_count.fetch_add(1);
				scope_guard guard{[scope]() noexcept {
					// If this is the last task
					if (scope->m_count.fetch_sub(1) == 1) {
						// And we are being awaited
						auto hdl = scope->m_continuation.exchange(nullptr);
						// Resume the awaiter
						if (hdl != nullptr) coroutine_handle<>::from_address(hdl).resume();
					}
				}};
				co_await std::apply(std::move(callable), std::move(args));
			}(this, std::forward<Callable>(callable), std::move(args), allocator);
		}

		/**
		 * \brief Invoke the provided callable in a new task. The callable is
		 *		  copied into the task, making it save for usage with e.g. lambda captures.
		 * \note If you need to use a custom allocator use the tuple overload
		 * \param callable The callable to invoke
		 * \param args The arguments to pass into callable on invocation
		 */
		template<typename Callable, typename... Args>
			requires(std::is_invocable_v<Callable, Args...>)
		void invoke(Callable&& callable, Args&&... args) {
			invoke_tuple(std::forward<Callable>(callable), std::forward_as_tuple<Args&&...>(args...));
		}

		/**
		 * \brief Wait for all active tasks to finish
		 * \return auto Awaiter that pauses the current coroutine until all spawned task have finished.
		 */
		[[nodiscard]] auto join() noexcept {
			struct awaiter {
				async_launch_scope* m_scope;
				bool await_ready() noexcept {
					// Dont wait if theres nothing to await
					return m_scope->m_count.load() == 0;
				}
				bool await_suspend(coroutine_handle<> hdl) const {
					// Set our coroutine if there is noone waiting
					void* expected = nullptr;
					if (!m_scope->m_continuation.compare_exchange_strong(expected, hdl.address()))
						throw std::logic_error("duplicate join");
					// We might have nothing left to wait on if the last coroutine finished in the meantime
					return m_scope->m_count.load() != 0;
				}
				constexpr void await_resume() const noexcept {}
			};
			return awaiter{this};
		}

		/**
		 * \brief Create a future that finishes once all tasks are done.
		 *			This is equivalent to awaiting join() in a promise.
		 * \return std::future waiting for all tasks
		 */
		template<ByteAllocator Allocator = default_allocator_type>
		std::future<void> join_future(const Allocator& allocator = {}) noexcept {
			std::promise<void> done;
			auto res = done.get_future();
			[](async_launch_scope* that, std::promise<void> done, const Allocator&) -> detail::launch_task<Allocator> {
				try {
					co_await that->join();
					done.set_value();
				} catch (...) { done.set_exception(std::current_exception()); }
			}(this, std::move(done), allocator);
			return res;
		}

		/**
		 * \brief Returns the number of active task on this scope
		 * \warning Only use this value for informational purposes, it might change at any time.
		 */
		size_t inflight_coroutines() const noexcept { return m_count.load(std::memory_order::relaxed); }

		/**
		 * \brief Returns true if there is no active task currently running on this scope
		 */
		bool all_done() const noexcept { return inflight_coroutines() == 0; }
	};
} // namespace asyncpp

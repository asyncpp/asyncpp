#pragma once
#include <asyncpp/detail/concepts.h>
#include <asyncpp/detail/std_import.h>
#include <asyncpp/policy.h>
#include <atomic>
#include <utility>

namespace asyncpp {
	namespace detail {
		/**
		* \brief Launch coroutine task.
		* 
		* This is a simplified version of fire_and_forget_task, that skips all the unused stuff.
        * If an exception leaves the task it calls std::terminate().
		*/
		struct launch_task_impl {
			// Promise type of this task
			class promise_type {
			public:
				constexpr promise_type() noexcept = default;
				promise_type(const promise_type&) = delete;
				promise_type(promise_type&&) = delete;

				auto get_return_object() noexcept { return coroutine_handle<promise_type>::from_promise(*this); }
				constexpr std::suspend_never initial_suspend() noexcept { return {}; }
				constexpr std::suspend_never final_suspend() noexcept { return {}; }
				constexpr void return_void() noexcept {}
				void unhandled_exception() noexcept { std::terminate(); }
			};

			/// \brief Construct from a handle
			launch_task_impl(coroutine_handle<promise_type>) noexcept {}
			launch_task_impl(launch_task_impl&&) = delete;
			launch_task_impl& operator=(launch_task_impl&&) = delete;
			launch_task_impl(const launch_task_impl&) = delete;
			launch_task_impl& operator=(const launch_task_impl&) = delete;
		};
	} // namespace detail

    /**
     * \brief Launch a new asynchronous coroutine. This coroutine will run in the current thread until it executes its first co_await.
     * 
     * \tparam FN The function type
     * \tparam Args The argument types
     * \param fn Function type to co_await. `fn(args...)` needs to return an awaitable.
     * \param args Arguments to pass to the invokation of fn.
     */
	template<typename FN, typename... Args>
	void launch(FN&& fn, Args&&... args) {
		[&args...](FN&& fn) -> detail::launch_task_impl {
			co_await fn(std::forward<Args>(args)...);
		}(std::move(fn));
	}
} // namespace asyncpp

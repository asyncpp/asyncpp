#pragma once
#include <asyncpp/fire_and_forget.h>
#include <future>
#include <type_traits>

namespace asyncpp {
	/**
	 * \brief Execute the given awaitable and return a std::promise representing the call.
	 * \note The returned promise will block until the async function finishes or throws.
	 * 
	 * This can be used to synchronously wait for the result of a coroutine.
	 */
	template<typename T, typename Awaitable>
	std::future<T> as_promise(Awaitable&& await)
		requires(!std::is_void_v<T>)
	{
		std::promise<T> promise;
		auto res = promise.get_future();
		[](std::decay_t<Awaitable> await, std::promise<T> promise) -> eager_fire_and_forget_task<> {
			try {
				promise.set_value(co_await std::move(await));
			} catch (...) { promise.set_exception(std::current_exception()); }
		}(std::forward<Awaitable>(await), std::move(promise));
		return res;
	}

	/**
	 * \brief Execute the given awaitable and return a std::promise representing the call.
	 * \note The returned promise will block until the async function finishes or throws.
	 * 
	 * This can be used to synchronously wait for the result of a coroutine.
	 */
	template<typename T, typename Awaitable>
	std::future<void> as_promise(Awaitable&& await)
		requires(std::is_void_v<T>)
	{
		std::promise<void> promise;
		auto res = promise.get_future();
		[](std::decay_t<Awaitable> await, std::promise<void> promise) -> eager_fire_and_forget_task<> {
			try {
				co_await std::move(await);
				promise.set_value();
			} catch (...) { promise.set_exception(std::current_exception()); }
		}(std::forward<Awaitable>(await), std::move(promise));
		return res;
	}

	/**
	 * \brief Execute the given awaitable and return a std::promise representing the call.
	 * \note The returned promise will block until the async function finishes or throws.
	 * \note This function tries to autodetect the return type of the co_await expression.
	 * However the C++20 coroutines have lots of extension points which makes this hard (impossible?)
	 * to do in all cases. If the detection fails you can use the above functions and explicitly specify the return type.
	 * 
	 * This can be used to synchronously wait for the result of a coroutine.
	 */
	auto as_promise(auto&& await) {
		return as_promise<typename detail::await_return_type<std::remove_cvref_t<decltype(await)>>::type>(std::forward<std::decay_t<decltype(await)>>(await));
	}
} // namespace asyncpp

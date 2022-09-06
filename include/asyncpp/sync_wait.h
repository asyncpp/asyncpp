#pragma once
#include <asyncpp/fire_and_forget.h>
#include <future>

namespace asyncpp {
	/**
	 * \brief Execute the given awaitable and return a std::promise representing the call.
	 * \note The returned promise will block until the async function finishes or throws.
	 * 
	 * This can be used to synchronously wait for the result of a coroutine.
	 */
	template<typename T, typename Awaitable>
	std::future<T> as_promise(Awaitable&& t) requires(!std::is_void_v<T>) {
		std::promise<T> p;
		auto res = p.get_future();
		[](std::decay_t<Awaitable> t, std::promise<T> p) -> eager_fire_and_forget_task<> {
			try {
				p.set_value(co_await std::move(t));
			} catch (...) { p.set_exception(std::current_exception()); }
		}(std::move(t), std::move(p));
		return res;
	}

	/**
	 * \brief Execute the given awaitable and return a std::promise representing the call.
	 * \note The returned promise will block until the async function finishes or throws.
	 * 
	 * This can be used to synchronously wait for the result of a coroutine.
	 */
	template<typename T, typename Awaitable>
	std::future<void> as_promise(Awaitable&& t) requires(std::is_void_v<T>) {
		std::promise<void> p;
		auto res = p.get_future();
		[](std::decay_t<Awaitable> t, std::promise<void> p) -> eager_fire_and_forget_task<> {
			try {
				co_await std::move(t);
				p.set_value();
			} catch (...) { p.set_exception(std::current_exception()); }
		}(std::move(t), std::move(p));
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
	auto as_promise(auto&& t) {
		return as_promise<typename detail::await_return_type<std::remove_cvref_t<decltype(t)>>::type>(std::move(t));
	}
} // namespace asyncpp

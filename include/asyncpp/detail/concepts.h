#pragma once
#include <asyncpp/detail/std_import.h>
#include <type_traits>
#include <functional>

namespace asyncpp::detail {
	template<typename T>
	struct is_coroutine_handle : std::false_type {};

	template<typename TPromise>
	struct is_coroutine_handle<coroutine_handle<TPromise>> : std::true_type {};

	template<typename T>
	inline constexpr bool is_coroutine_handle_v = is_coroutine_handle<T>::value;

	template<typename T>
	concept is_valid_await_suspend_return_value = std::is_void_v<T> || std::is_same_v<T, bool> || is_coroutine_handle_v<T>;

	template<typename T>
	concept is_awaiter = requires(T&& a) {
		{ a.await_ready() }
		->std::convertible_to<bool>;
		{a.await_suspend(std::declval<coroutine_handle<>>())};
		{ a.await_resume() }
		->is_valid_await_suspend_return_value<>;
	};

	template<typename T>
	concept is_dispatcher = requires(T&& a) {
		{a.push(std::declval<std::function<void()>>)};
	};

	template<typename T>
	struct await_return_type;
	template<bool b, typename T>
	struct await_return_type_impl;

	template<typename T>
	struct await_return_type_impl<true, T> {
		using type = decltype(std::declval<T>().await_resume());
	};

	template<typename T>
	struct await_return_type_impl<false, T> {
		using type = typename await_return_type<decltype(std::declval<T>().operator co_await())>::type;
	};

	template<typename T>
	struct await_return_type {
		using type = typename await_return_type_impl<is_awaiter<T>, T>::type;
	};

} // namespace asyncpp::detail

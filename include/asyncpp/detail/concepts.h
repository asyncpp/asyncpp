#pragma once
#include <asyncpp/detail/std_import.h>
#include <functional>
#include <type_traits>

namespace asyncpp::detail {
	/** \brief Check if T is a coroutine_handle */
	template<typename T>
	struct is_coroutine_handle : std::false_type {};

	/** \brief Check if T is a coroutine_handle */
	template<typename TPromise>
	struct is_coroutine_handle<coroutine_handle<TPromise>> : std::true_type {};

	/** \brief Check if T is a coroutine_handle */
	template<typename T>
	inline constexpr bool is_coroutine_handle_v = is_coroutine_handle<T>::value;

	/** \brief Check if T is a valid return type for await_suspend */
	template<typename T>
	concept is_valid_await_suspend_return_value =
		std::is_void_v<T> || std::is_same_v<T, bool> || is_coroutine_handle_v<T>;

	/** \brief Check if T implements the awaitable interface */
	template<typename T>
	concept is_awaiter = requires(T&& a) {
		{ a.await_ready() }
		->std::convertible_to<bool>;
		{ a.await_suspend(std::declval<coroutine_handle<>>()) }
		->is_valid_await_suspend_return_value<>;
		{a.await_resume()};
	};

	/** 
	 * \brief Get the return type of an awaitable type, resolving operator co_await overloads.
	 * \note This is a best effort attempt. It will not be correct in all cases, for example it does not handle std::coroutine_traits.
	 */
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

namespace asyncpp {
	/** \brief Check if T implements the dispatcher interface */
	template<typename T>
	concept Dispatcher = requires(T&& a) {
		{a.push(std::declval<std::function<void()>>)};
	};

	/** \brief Check if a type is a valid allocator providing std::byte allocations. */
	template<class Allocator>
	concept ByteAllocator = requires(Allocator&& a) {
		{ std::allocator_traits<Allocator>::allocate(a, 0) }
		->std::convertible_to<std::byte*>;
		{std::allocator_traits<Allocator>::deallocate(a, std::declval<std::byte*>(), 0)};
	};
} // namespace asyncpp

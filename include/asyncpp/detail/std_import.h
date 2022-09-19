#pragma once
/**
 * \file std_import.h
 * \brief Provides a consistent import interface for coroutine, experimental/coroutine or a best effort fallback definition.
 */

#if __has_include(<coroutine>) && __cpp_impl_coroutine
// This is perfect
#include <coroutine>
namespace asyncpp {
	using std::coroutine_handle;
	using std::noop_coroutine;
	using std::suspend_always;
	using std::suspend_never;
} // namespace asyncpp
#elif __has_include(<experimental/coroutine>)
// We might hit some bugs
#include <experimental/coroutine>
namespace asyncpp {
	using std::experimental::coroutine_handle;
	using std::experimental::noop_coroutine;
	using std::experimental::suspend_always;
	using std::experimental::suspend_never;
} // namespace asyncpp
#else
// We are all going to die
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory> // for hash<T*>
#include <new>
#include <type_traits>

namespace std::experimental {
	inline namespace coroutines_v1 {

		template<class>
		struct __void_t {
			typedef void type;
		};

		template<class _Tp, class = void>
		struct __coroutine_traits_sfinae {};

		template<class _Tp>
		struct __coroutine_traits_sfinae<_Tp, typename __void_t<typename _Tp::promise_type>::type> {
			using promise_type = typename _Tp::promise_type;
		};

		template<typename _Ret, typename... _Args>
		struct coroutine_traits : public __coroutine_traits_sfinae<_Ret> {};

		template<typename _Promise = void>
		class __attribute__((__visibility__("default"))) coroutine_handle;

		template<>
		class __attribute__((__visibility__("default"))) coroutine_handle<void> {
		public:
			__attribute__((__visibility__("hidden")))
			__attribute__((__always_inline__)) constexpr coroutine_handle() noexcept
				: __handle_(nullptr) {}

			__attribute__((__visibility__("hidden")))
			__attribute__((__always_inline__)) constexpr coroutine_handle(nullptr_t) noexcept
				: __handle_(nullptr) {}

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) coroutine_handle&
			operator=(nullptr_t) noexcept {
				__handle_ = nullptr;
				return *this;
			}

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) constexpr void*
			address() const noexcept {
				return __handle_;
			}

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) constexpr explicit
			operator bool() const noexcept {
				return __handle_;
			}

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) void operator()() const {
				resume();
			}

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) void resume() const {
				assert(__is_suspended());
				assert(!done());
				__builtin_coro_resume(__handle_);
			}

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) void destroy() const {
				assert(__is_suspended());
				__builtin_coro_destroy(__handle_);
			}

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) bool done() const noexcept {
				assert(__is_suspended());
				return __builtin_coro_done(__handle_);
			}

		public:
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) static coroutine_handle
			from_address(void* __addr) noexcept {
				coroutine_handle __tmp;
				__tmp.__handle_ = __addr;
				return __tmp;
			}

			// FIXME: Should from_address(nullptr) be allowed?
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) static coroutine_handle
			from_address(nullptr_t) noexcept {
				return coroutine_handle(nullptr);
			}

			template<class _Tp, bool _CallIsValid = false>
			static coroutine_handle from_address(_Tp*) {
				static_assert(_CallIsValid, "coroutine_handle<void>::from_address cannot be called with "
											"non-void pointers");
			}

		private:
			bool __is_suspended() const noexcept {
				// FIXME actually implement a check for if the coro is suspended.
				return __handle_;
			}

			template<class _PromiseT>
			friend class coroutine_handle;
			void* __handle_;
		};

		// 18.11.2.7 comparison operators:
		inline __attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) bool
		operator==(coroutine_handle<> __x, coroutine_handle<> __y) noexcept {
			return __x.address() == __y.address();
		}
		inline __attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) bool
		operator!=(coroutine_handle<> __x, coroutine_handle<> __y) noexcept {
			return !(__x == __y);
		}
		inline __attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) bool
		operator<(coroutine_handle<> __x, coroutine_handle<> __y) noexcept {
			return less<void*>()(__x.address(), __y.address());
		}
		inline __attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) bool
		operator>(coroutine_handle<> __x, coroutine_handle<> __y) noexcept {
			return __y < __x;
		}
		inline __attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) bool
		operator<=(coroutine_handle<> __x, coroutine_handle<> __y) noexcept {
			return !(__x > __y);
		}
		inline __attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) bool
		operator>=(coroutine_handle<> __x, coroutine_handle<> __y) noexcept {
			return !(__x < __y);
		}

		template<typename _Promise>
		class __attribute__((__visibility__("default"))) coroutine_handle : public coroutine_handle<> {
			using _Base = coroutine_handle<>;

		public:
			// 18.11.2.1 construct/reset
			using coroutine_handle<>::coroutine_handle;
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) coroutine_handle&
			operator=(nullptr_t) noexcept {
				_Base::operator=(nullptr);
				return *this;
			}

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) _Promise& promise() const {
				return *static_cast<_Promise*>(__builtin_coro_promise(this->__handle_, alignof(_Promise), false));
			}

		public:
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) static coroutine_handle
			from_address(void* __addr) noexcept {
				coroutine_handle __tmp;
				__tmp.__handle_ = __addr;
				return __tmp;
			}

			// NOTE: this overload isn't required by the standard but is needed so
			// the deleted _Promise* overload doesn't make from_address(nullptr)
			// ambiguous.
			// FIXME: should from_address work with nullptr?
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) static coroutine_handle
			from_address(nullptr_t) noexcept {
				return coroutine_handle(nullptr);
			}

			template<class _Tp, bool _CallIsValid = false>
			static coroutine_handle from_address(_Tp*) {
				static_assert(_CallIsValid, "coroutine_handle<promise_type>::from_address cannot be called with "
											"non-void pointers");
			}

			template<bool _CallIsValid = false>
			static coroutine_handle from_address(_Promise*) {
				static_assert(_CallIsValid, "coroutine_handle<promise_type>::from_address cannot be used with "
											"pointers to the coroutine's promise type; use 'from_promise' instead");
			}

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) static coroutine_handle
			from_promise(_Promise& __promise) noexcept {
				typedef typename remove_cv<_Promise>::type _RawPromise;
				coroutine_handle __tmp;
				__tmp.__handle_ = __builtin_coro_promise(std::addressof(const_cast<_RawPromise&>(__promise)),
														 alignof(_Promise), true);
				return __tmp;
			}
		};

#if __has_builtin(__builtin_coro_noop)
		struct noop_coroutine_promise {};

		template<>
		class __attribute__((__visibility__("default")))
		coroutine_handle<noop_coroutine_promise> : public coroutine_handle<> {
			using _Base = coroutine_handle<>;
			using _Promise = noop_coroutine_promise;

		public:
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) _Promise& promise() const {
				return *static_cast<_Promise*>(__builtin_coro_promise(this->__handle_, alignof(_Promise), false));
			}

			constexpr explicit operator bool() const noexcept { return true; }
			constexpr bool done() const noexcept { return false; }

			constexpr void operator()() const noexcept {}
			constexpr void resume() const noexcept {}
			constexpr void destroy() const noexcept {}

		private:
			__attribute__((__visibility__("hidden")))
			__attribute__((__always_inline__)) friend coroutine_handle<noop_coroutine_promise>
			noop_coroutine() noexcept;

			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) coroutine_handle() noexcept {
				this->__handle_ = __builtin_coro_noop();
			}
		};

		using noop_coroutine_handle = coroutine_handle<noop_coroutine_promise>;

		inline __attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) noop_coroutine_handle
		noop_coroutine() noexcept {
			return noop_coroutine_handle();
		}
#endif // __has_builtin(__builtin_coro_noop)

		struct suspend_never {
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) bool
			await_ready() const noexcept {
				return true;
			}
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) void
			await_suspend(coroutine_handle<>) const noexcept {}
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) void
			await_resume() const noexcept {}
		};

		struct suspend_always {
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) bool
			await_ready() const noexcept {
				return false;
			}
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) void
			await_suspend(coroutine_handle<>) const noexcept {}
			__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) void
			await_resume() const noexcept {}
		};

	} // namespace coroutines_v1
} // namespace std::experimental

namespace std {
	template<class _Tp>
	struct hash<std::experimental::coroutine_handle<_Tp>> {
		using __arg_type = std::experimental::coroutine_handle<_Tp>;
		__attribute__((__visibility__("hidden"))) __attribute__((__always_inline__)) size_t
		operator()(__arg_type const& __v) const noexcept {
			return hash<void*>()(__v.address());
		}
	};
} // namespace std

namespace asyncpp {
	using std::experimental::coroutine_handle;
	using std::experimental::noop_coroutine;
	using std::experimental::suspend_always;
	using std::experimental::suspend_never;
} // namespace asyncpp

#endif

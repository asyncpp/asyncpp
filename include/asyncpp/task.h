#pragma once
#include <asyncpp/detail/promise_allocator_base.h>
#include <asyncpp/detail/std_import.h>
#include <cassert>
#include <exception>
#include <variant>

namespace asyncpp {
	template<class T, ByteAllocator Allocator>
	class task;

	namespace detail {
		template<class TVal, ByteAllocator Allocator, class TPromise>
		class task_promise_base : public promise_allocator_base<Allocator> {
		public:
			task_promise_base() noexcept = default;
			~task_promise_base() = default;
			task_promise_base(const task_promise_base&) = delete;
			task_promise_base(task_promise_base&&) = delete;
			task_promise_base& operator=(const task_promise_base&) = delete;
			task_promise_base& operator=(task_promise_base&&) = delete;

			coroutine_handle<TPromise> get_return_object() noexcept {
				return coroutine_handle<TPromise>::from_promise(*static_cast<TPromise*>(this));
			}

			suspend_always initial_suspend() { return {}; }
			auto final_suspend() noexcept {
				struct awaiter {
					constexpr bool await_ready() noexcept { return false; }
					auto await_suspend(coroutine_handle<TPromise> hndl) noexcept {
						assert(hndl);
						assert(hndl.promise().m_continuation);
						return hndl.promise().m_continuation;
					}
					constexpr void await_resume() noexcept {}
				};
				return awaiter{};
			}

			void unhandled_exception() noexcept {
				m_value.template emplace<std::exception_ptr>(std::current_exception());
			}

			TVal rethrow_if_exception() {
				if (std::holds_alternative<std::exception_ptr>(m_value))
					std::rethrow_exception(std::get<std::exception_ptr>(m_value));
				return std::get<TVal>(std::move(this->m_value));
			}

			coroutine_handle<> m_continuation{};
			std::variant<std::monostate, TVal, std::exception_ptr> m_value{};
		};

		template<class T, ByteAllocator Allocator>
		class task_promise : public task_promise_base<T, Allocator, task_promise<T, Allocator>> {
		public:
			template<class U>
			void return_value(U&& value)
				requires(std::is_convertible_v<U, T>)
			{
				this->m_value.template emplace<T>(std::forward<U>(value));
			}
			template<class U>
			void return_value(U const& value)
				requires(!std::is_reference_v<U>)
			{
				this->m_value.template emplace<T>(value);
			}
			T get() { return this->rethrow_if_exception(); }
		};

		struct returned {};
		template<ByteAllocator Allocator>
		class task_promise<void, Allocator>
			: public task_promise_base<returned, Allocator, task_promise<void, Allocator>> {
		public:
			void return_void() { this->m_value.template emplace<returned>(); }
			void get() { this->rethrow_if_exception(); }
		};
	} // namespace detail

	/**
	 * \brief Generic task type
	 * \tparam T Return type of the task
	 */
	template<class T = void, ByteAllocator Allocator = default_allocator_type>
	class [[nodiscard]] task {
	public:
		/// \brief Promise type
		using promise_type = detail::task_promise<T, Allocator>;
		/// \brief Handle type
		using handle_t = coroutine_handle<promise_type>;

		/// \brief Construct from handle
		//NOLINTNEXTLINE(google-explicit-constructor)
		task(handle_t hndl) noexcept : m_coro(hndl) {
			assert(this->m_coro);
			assert(!this->m_coro.done());
		}

		/// \brief Construct from nullptr. The resulting task is invalid.
		explicit task(std::nullptr_t) noexcept : m_coro{} {}

		/// \brief Move constructor
		task(task&& other) noexcept : m_coro{std::exchange(other.m_coro, {})} {}
		/// \brief Move assignment
		task& operator=(task&& other) noexcept {
			m_coro = std::exchange(other.m_coro, m_coro);
			return *this;
		}
		task(const task&) = delete;
		task& operator=(const task&) = delete;

		/// \brief Destructor
		~task() {
			if (m_coro) m_coro.destroy();
			m_coro = nullptr;
		}

		/// \brief Check if the task holds a valid coroutine.
		explicit operator bool() const noexcept { return m_coro != nullptr; }
		/// \brief Check if the task does not hold a valid coroutine.
		bool operator!() const noexcept { return m_coro == nullptr; }

		/// \brief Operator co_await
		auto operator co_await() noexcept {
			struct awaiter {
				constexpr explicit awaiter(handle_t coro) : m_coro(coro) {}
				constexpr bool await_ready() noexcept { return false; }
				auto await_suspend(coroutine_handle<void> hndl) noexcept {
					assert(this->m_coro);
					assert(hndl);
					m_coro.promise().m_continuation = hndl;
					return m_coro;
				}
				T await_resume() {
					assert(this->m_coro);
					return this->m_coro.promise().get();
				}

			private:
				handle_t m_coro;
			};
			assert(this->m_coro);
			return awaiter{m_coro};
		}

	private:
		handle_t m_coro;
	};
} // namespace asyncpp

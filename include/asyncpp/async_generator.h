#pragma once
#include <asyncpp/detail/concepts.h>
#include <asyncpp/detail/promise_allocator_base.h>
#include <asyncpp/detail/std_import.h>

#include <exception>
#include <iterator>
#include <memory>

namespace asyncpp {
	template<typename T, ByteAllocator Allocator = default_allocator_type>
	class async_generator;

	namespace detail {
		template<typename T>
		class async_generator_iterator;

		class async_generator_yield_operation final {
		public:
			explicit async_generator_yield_operation(coroutine_handle<> consumer) noexcept : m_consumer(consumer) {}
			[[nodiscard]] bool await_ready() const noexcept { return false; }
			[[nodiscard]] coroutine_handle<> await_suspend([[maybe_unused]] coroutine_handle<> producer) noexcept {
				return m_consumer;
			}
			void await_resume() noexcept {}

		private:
			coroutine_handle<> m_consumer;
		};

		template<typename T>
		class async_generator_promise_base {
		public:
			async_generator_promise_base() noexcept = default;
			async_generator_promise_base(const async_generator_promise_base& other) = delete;
			async_generator_promise_base& operator=(const async_generator_promise_base& other) = delete;
			[[nodiscard]] suspend_always initial_suspend() const noexcept { return {}; }
			[[nodiscard]] async_generator_yield_operation final_suspend() noexcept {
				m_value = nullptr;
				return internal_yield_value();
			}
			void unhandled_exception() noexcept { m_exception = std::current_exception(); }
			void return_void() noexcept {}
			[[nodiscard]] bool finished() const noexcept { return m_value == nullptr; }
			void rethrow_if_unhandled_exception() {
				if (m_exception) { std::rethrow_exception(std::move(m_exception)); }
			}
			[[nodiscard]] T* value() const noexcept { return m_value; }

		protected:
			[[nodiscard]] async_generator_yield_operation internal_yield_value() noexcept {
				return async_generator_yield_operation{m_consumer};
			}

		private:
			friend class async_generator_yield_operation;
			template<typename U, ByteAllocator Allocator>
			friend class asyncpp::async_generator;
			friend class async_generator_iterator<T>;

			std::exception_ptr m_exception{nullptr};
			coroutine_handle<> m_consumer;

		protected:
			T* m_value;
		};

		template<typename T, ByteAllocator Allocator>
		class async_generator_promise final : public async_generator_promise_base<T>,
											  public promise_allocator_base<Allocator> {
			using value_type = std::remove_reference_t<T>;

		public:
			async_generator_promise() noexcept = default;
			[[nodiscard]] coroutine_handle<async_generator_promise> get_return_object() noexcept {
				return coroutine_handle<async_generator_promise>::from_promise(*this);
			}
			[[nodiscard]] async_generator_yield_operation yield_value(value_type& value) noexcept {
				this->m_value = std::addressof(value);
				return this->internal_yield_value();
			}
			[[nodiscard]] async_generator_yield_operation yield_value(value_type&& value) noexcept {
				return yield_value(value);
			}
		};

		template<typename T, ByteAllocator Allocator>
		class async_generator_promise<T&&, Allocator> final : public async_generator_promise_base<T>,
															  public promise_allocator_base<Allocator> {
		public:
			async_generator_promise() noexcept = default;
			[[nodiscard]] coroutine_handle<async_generator_promise> get_return_object() noexcept {
				return coroutine_handle<async_generator_promise>::from_promise(*this);
			}
			[[nodiscard]] async_generator_yield_operation yield_value(T&& value) noexcept {
				this->m_value = std::addressof(value);
				return this->internal_yield_value();
			}
		};

		template<typename T>
		class async_generator_iterator final {
		public:
			using iterator_category = std::input_iterator_tag;
			// Not sure what type should be used for difference_type as we don't
			// allow calculating difference between two iterators.
			using difference_type = std::ptrdiff_t;
			using value_type = std::remove_reference_t<T>;
			using reference = std::add_lvalue_reference_t<T>;
			using pointer = std::add_pointer_t<value_type>;

			explicit async_generator_iterator(std::nullptr_t) noexcept {}
			[[nodiscard]] auto operator++() noexcept {
				class increment_op final {
				public:
					explicit increment_op(async_generator_iterator<T>& iterator) noexcept : m_iterator(iterator) {}
					[[nodiscard]] bool await_ready() const noexcept { return false; }
					[[nodiscard]] coroutine_handle<> await_suspend(coroutine_handle<> consumer) noexcept {
						m_iterator.m_promise->m_consumer = consumer;
						return m_iterator.m_coro;
					}
					async_generator_iterator<T>& await_resume() {
						if (m_iterator.m_promise->finished()) {
							// Update iterator to end()
							auto promise = m_iterator.m_promise;
							m_iterator = async_generator_iterator<T>{nullptr};
							promise->rethrow_if_unhandled_exception();
						}

						return m_iterator;
					}

				private:
					async_generator_iterator<T>& m_iterator;
				};
				return increment_op{*this};
			}
			[[nodiscard]] reference operator*() const noexcept { return *static_cast<T*>(m_promise->value()); }
			[[nodiscard]] bool operator==(const async_generator_iterator& other) const noexcept {
				return m_promise == other.m_promise;
			}
			[[nodiscard]] bool operator!=(const async_generator_iterator& other) const noexcept {
				return !(*this == other);
			}

		private:
			template<typename U, ByteAllocator Allocator>
			friend class asyncpp::async_generator;

			explicit async_generator_iterator(async_generator_promise_base<T>& promise,
											  coroutine_handle<> coro) noexcept
				: m_promise(std::addressof(promise)), m_coro{coro} {}

			async_generator_promise_base<T>* m_promise{};
			coroutine_handle<> m_coro{};
		};

	} // namespace detail

	/**
	 * \brief Generator coroutine class supporting co_await
	 */
	template<typename T, ByteAllocator Allocator>
	class [[nodiscard]] async_generator {
	public:
		using promise_type = detail::async_generator_promise<T, Allocator>;
		using iterator = detail::async_generator_iterator<T>;

		async_generator() noexcept : m_coroutine(nullptr) {}
		//NOLINTNEXTLINE(google-explicit-constructor)
		async_generator(coroutine_handle<promise_type> coro) noexcept : m_coroutine{coro} {}
		async_generator(async_generator&& other) noexcept : m_coroutine(other.m_coroutine) {
			other.m_coroutine = nullptr;
		}
		~async_generator() {
			if (m_coroutine) { m_coroutine.destroy(); }
		}
		async_generator& operator=(async_generator&& other) noexcept {
			async_generator temp(std::move(other));
			swap(temp);
			return *this;
		}
		async_generator(const async_generator&) = delete;
		async_generator& operator=(const async_generator&) = delete;

		[[nodiscard]] auto begin() noexcept {
			class begin_operation final {
				detail::async_generator_promise_base<T>* m_promise{};
				coroutine_handle<> m_producer{};

			public:
				explicit begin_operation(std::nullptr_t) noexcept {}
				explicit begin_operation(
					coroutine_handle<detail::async_generator_promise<T, Allocator>> producer) noexcept
					: m_promise(std::addressof(producer.promise())), m_producer(producer) {}
				[[nodiscard]] bool await_ready() const noexcept { return this->m_promise == nullptr; }
				[[nodiscard]] coroutine_handle<> await_suspend(coroutine_handle<> consumer) noexcept {
					m_promise->m_consumer = consumer;
					return m_producer;
				}
				[[nodiscard]] detail::async_generator_iterator<T> await_resume() {
					if (this->m_promise == nullptr) return detail::async_generator_iterator<T>{nullptr};
					if (this->m_promise->finished()) {
						// Completed without yielding any values.
						this->m_promise->rethrow_if_unhandled_exception();
						return detail::async_generator_iterator<T>{nullptr};
					}

					return detail::async_generator_iterator<T>{*this->m_promise, this->m_producer};
				}
			};
			if (!m_coroutine) return begin_operation{nullptr};
			return begin_operation{m_coroutine};
		}
		[[nodiscard]] iterator end() noexcept { return iterator{nullptr}; }
		void swap(async_generator& other) noexcept {
			using std::swap;
			swap(m_coroutine, other.m_coroutine);
		}

	private:
		coroutine_handle<promise_type> m_coroutine;
	};

	template<typename T, ByteAllocator Allocator>
	void swap(async_generator<T, Allocator>& lhs, async_generator<T, Allocator>& rhs) noexcept {
		lhs.swap(rhs);
	}
} // namespace asyncpp

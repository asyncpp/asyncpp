#pragma once
#include <asyncpp/detail/std_import.h>
#include <variant>

namespace asyncpp {
	template<class T>
	class generator;

	namespace detail {
		template<class T>
		class generator_promise {
		public:
			using value_type = std::remove_reference_t<T>;
			using reference_type = std::conditional_t<std::is_reference_v<T>, T, T&>;
			using pointer_type = std::add_pointer_t<value_type>;

			generator_promise() noexcept {}
			~generator_promise() {}
			generator_promise(const generator_promise&) = delete;
			generator_promise(generator_promise&&) = delete;

			generator<T> get_return_object() noexcept;
			suspend_always initial_suspend() noexcept { return {}; }
			suspend_always final_suspend() noexcept { return {}; }

			void return_void() noexcept {}

			suspend_always yield_value(std::remove_reference_t<T>&& value) noexcept {
				m_value = std::addressof(value);
				return {};
			}

			template<class U = T, typename = std::enable_if_t<!std::is_rvalue_reference_v<U>>>
			suspend_always yield_value(std::remove_reference_t<T>& value) noexcept {
				m_value = std::addressof(value);
				return {};
			}

			void unhandled_exception() noexcept { m_exception = std::current_exception(); }

			pointer_type value() const noexcept { return m_value; }

			std::exception_ptr exception() const noexcept { return m_exception; }

			// co_await is not supported in a synchronous generator
			template<typename U>
			std::experimental::suspend_never await_transform(U&& value) = delete;

		private:
			friend class generator<T>;
			T* m_value{nullptr};
			std::exception_ptr m_exception{nullptr};
		};

		struct generator_end {};

		template<class T>
		class generator_iterator {
		public:
			using promise_type = generator_promise<T>;
			using handle_t = coroutine_handle<promise_type>;
			using iterator_category = std::input_iterator_tag;
			using difference_type = std::ptrdiff_t;
			using value_type = typename promise_type::value_type;
			using reference = typename promise_type::reference_type;
			using pointer = typename promise_type::pointer_type;

			constexpr generator_iterator() noexcept : m_coro{nullptr} {}
			explicit constexpr generator_iterator(handle_t hdl) noexcept : m_coro{hdl} {}

			bool operator==(generator_end) const noexcept { return !m_coro || m_coro.done(); }

			generator_iterator& operator++() {
				m_coro.resume();
				if (m_coro.done()) {
					auto ex = m_coro.promise().exception();
					if (ex) std::rethrow_exception(ex);
				}
				return *this;
			}

			// Not really supported, but many people prefer postincrement even if the result is unused
			void operator++(int) { ++(*this); }

			reference operator*() const noexcept { return static_cast<reference>(*m_coro.promise().value()); }

			pointer operator->() const noexcept { return m_coro.promise().value(); }

		private:
			handle_t m_coro;
		};

		template<typename T>
		inline bool operator!=(const generator_iterator<T>& it, generator_end s) noexcept {
			return !(it == s);
		}

		template<typename T>
		inline bool operator==(generator_end s, const generator_iterator<T>& it) noexcept {
			return it == s;
		}

		template<typename T>
		inline bool operator!=(generator_end s, const generator_iterator<T>& it) noexcept {
			return it != s;
		}
	} // namespace detail

	/**
	 * \brief Generator coroutine class
	 */
	template<typename T>
	class [[nodiscard]] generator {
	public:
		/// \brief The promise type
		using promise_type = detail::generator_promise<T>;
		/// \brief The iterator type
		using iterator = detail::generator_iterator<T>;

		generator() noexcept : m_coro{nullptr} {}
		generator(generator && other) noexcept : m_coro{std::exchange(other.m_coro, {})} {}
		generator(const generator&) = delete;
		generator& operator=(generator&& other) noexcept {
			m_coro = std::exchange(other.m_coro, m_coro);
			return *this;
		}
		generator& operator=(const generator&) = delete;
		~generator() {
			if (m_coro) m_coro.destroy();
		}
		iterator begin() {
			if (m_coro) {
				m_coro.resume();
				if (m_coro.done()) {
					auto ex = m_coro.promise().exception();
					if (ex) std::rethrow_exception(ex);
				}
			}
			return iterator{m_coro};
		}
		detail::generator_end end() const noexcept { return {}; }

	private:
		coroutine_handle<promise_type> m_coro;

		friend class detail::generator_promise<T>;
		explicit generator(coroutine_handle<promise_type> coro) noexcept : m_coro{coro} {}
	};

	template<class T>
	inline generator<T> detail::generator_promise<T>::get_return_object() noexcept {
		return generator<T>{coroutine_handle<generator_promise<T>>::from_promise(*this)};
	}

	/**
	 * \brief Map the given source sequence using a function and return the result.
	 * \param func The function to invoke on the element
	 * \param sourc The source generator
	 * \return Generator of mapped elements.
	 */
	template<typename FUNC, typename T>
	generator<std::invoke_result_t<FUNC&, typename generator<T>::iterator::reference>> fmap(FUNC func, generator<T> source) {
		for (auto&& value : source) {
			co_yield std::invoke(func, static_cast<decltype(value)>(value));
		}
	}
} // namespace asyncpp

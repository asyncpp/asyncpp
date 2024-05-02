#pragma once
#include <type_traits>
#include <utility>

namespace asyncpp {
	/**
	 * \brief Execute a callback function when this scope gets destroyed.
	 * \note Because the callback is executed in the destructor it is required to be marked noexcept.
	 * \tparam TFunction Type of the callback function
	 */
	template<typename TFunction>
	class scope_guard {
		TFunction m_function;
		bool m_engaged;

	public:
		/**
		 * \brief Construct a new scope guard
		 * @param func The function to invoke
		 * @param engaged true if the guard is engaged right away.
		 */
		explicit scope_guard(TFunction func,
							 bool engaged = true) noexcept(std::is_nothrow_move_constructible_v<TFunction>)
			: m_function{std::move(func)}, m_engaged{engaged} {
			static_assert(noexcept(func()), "scope_guard function must not throw exceptions");
		}
		scope_guard(const scope_guard&) = delete;
		scope_guard& operator=(const scope_guard&) = delete;
		/// \brief Destructor
		~scope_guard() {
			if (m_engaged) m_function();
		}
		/// \brief Disengage the guard, making sure the callback is not executed on destruction
		void disengage() noexcept { m_engaged = false; }
		/// \brief Engage the guard, making sure the callback is executed on destruction
		void engage() noexcept { m_engaged = true; }
		/**
		 * \brief Check if the guard is engaged.
		 * @return true if the callback will execute on destruction, false if not.
		 */
		[[nodiscard]] bool is_engaged() const noexcept { return m_engaged; }
		/// \brief Get a reference to the contained function.
		TFunction& function() noexcept { return m_function; }
		/// \brief Get a const reference to the contained function.
		const TFunction& function() const noexcept { return m_function; }
	};
} // namespace asyncpp

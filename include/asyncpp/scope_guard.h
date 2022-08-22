#pragma once
#include <asyncpp/detail/std_import.h>
#include <asyncpp/ref.h>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <variant>
#include <vector>

namespace asyncpp {
	template<typename TFunction>
	class scope_guard {
		TFunction m_function;
		bool m_engaged;

	public:
		scope_guard(TFunction fn, bool engaged = true) : m_function{std::move(fn)}, m_engaged{engaged} {
			static_assert(noexcept(fn()), "scope_guard function must not throw exceptions");
		}
		scope_guard(const scope_guard&) = delete;
		scope_guard& operator=(const scope_guard&) = delete;
		~scope_guard() {
			if (m_engaged) m_function();
		}
		void disengage() noexcept { m_engaged = false; }
		void engage() noexcept { m_engaged = true; }
		bool is_engaged() const noexcept { return m_engaged; }
		TFunction& function() noexcept { return m_function; }
		const TFunction& function() const noexcept { return m_function; }
	};
} // namespace asyncpp

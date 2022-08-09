#pragma once
#include <cstddef>
#include <utility>

namespace asyncpp::detail {
	class parameter_pack {
		template<size_t Index, typename Arg0, typename... Args>
		static constexpr decltype(auto) get_at_index_impl(Arg0&& first_arg, Args&&... args) noexcept {
			if constexpr (Index == 0) {
				return std::forward<Arg0>(first_arg);
			} else {
				return get_at_index_impl<Index - 1>(std::forward<Args>(args)...);
			}
		}

		parameter_pack() = delete;
	public:
		template<size_t Index, typename... Args>
		static constexpr decltype(auto) get_at_index(Args&&... args) noexcept {
			static_assert(Index < sizeof...(Args), "index out of range");
			return get_at_index_impl<Index>(std::forward<Args>(args)...);
		}

		template<typename... Args>
		static constexpr decltype(auto) get_last(Args&&... args) noexcept {
			static_assert(0 < sizeof...(Args), "parameter pack is empty");
			return get_at_index_impl<sizeof...(Args) - 1>(std::forward<Args>(args)...);
		}
	};

} // namespace asyncpp

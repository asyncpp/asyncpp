#pragma once
#include <asyncpp/detail/concepts.h>
#include <asyncpp/detail/parameter_pack.h>
#include <asyncpp/detail/std_import.h>
#include <type_traits>

namespace asyncpp {
#ifndef ASYNCPP_DEFAULT_ALLOCATOR
	using default_allocator_type = std::allocator<std::byte>;
#else
	using default_allocator_type = ASYNCPP_DEFAULT_ALLOCATOR;
#endif
} // namespace asyncpp

namespace asyncpp::detail {
	template<ByteAllocator Allocator>
	class promise_allocator_base {
	public:
		using allocator_type = Allocator;

		template<typename... Args>
		void* operator new(size_t size, Args&&... args) {
			if constexpr (std::allocator_traits<allocator_type>::is_always_equal::value) {
				allocator_type alloc{};
				return std::allocator_traits<allocator_type>::allocate(alloc, size);
			} else {
				static_assert(sizeof...(Args) > 0, "using a statefull allocator but no allocator passed");
				allocator_type alloc = parameter_pack::get_last(args...);
				static_assert(std::is_convertible_v<std::remove_cvref_t<decltype(alloc)>&, allocator_type> ||
								  std::is_constructible_v<allocator_type, decltype(alloc)>,
							  "last argument is not of allocator type");
				auto ptr = std::allocator_traits<allocator_type>::allocate(alloc, size + sizeof(allocator_type));
				auto x = new (ptr) allocator_type{std::move(alloc)};
				return x + 1;
			}
		}
		void operator delete(void* ptr, size_t size) {
			if (ptr == nullptr) return;
			if constexpr (std::allocator_traits<allocator_type>::is_always_equal::value) {
				allocator_type alloc{};
				std::allocator_traits<allocator_type>::deallocate(alloc, static_cast<std::byte*>(ptr), size);
			} else {
				allocator_type* info = static_cast<allocator_type*>(ptr) - 1;
				auto alloc = std::move(*info);
				std::allocator_traits<allocator_type>::deallocate(alloc, static_cast<std::byte*>(static_cast<void*>(info)), size + sizeof(Allocator));
			}
		}
	};
} // namespace asyncpp::detail

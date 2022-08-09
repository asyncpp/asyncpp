#pragma once
#include <asyncpp/detail/concepts.h>
#include <asyncpp/detail/parameter_pack.h>
#include <asyncpp/detail/std_import.h>
#include <type_traits>

namespace asyncpp::detail {
	template<ByteAllocator Allocator>
	class promise_allocator_base {
		struct alloc_info {
			size_t size;
			[[no_unique_address]] Allocator allocator;
		};

	public:
		using allocator_type = Allocator;

		template<typename... Args>
		void* operator new(size_t size, Args&&... args) {
			auto full_size = size + sizeof(alloc_info);
			if constexpr (std::allocator_traits<Allocator>::is_always_equal::value) {
				Allocator alloc{};
				auto ptr = std::allocator_traits<Allocator>::allocate(alloc, full_size);
				alloc_info* x = new (ptr) alloc_info{full_size, std::move(alloc)};
				return x + 1;
			} else {
				static_assert(sizeof...(Args) > 0, "using a statefull allocator but no allocator passed");
				auto& alloc = parameter_pack::get_last(args...);
				static_assert(std::is_convertible_v<std::remove_cvref_t<decltype(alloc)>&, Allocator>
								|| std::is_constructible_v<Allocator, decltype(alloc)>, "last argument is not of allocator type");
				alloc_info info{full_size, {alloc}};
				auto ptr = std::allocator_traits<Allocator>::allocate(info.allocator, full_size);
				alloc_info* x = new (ptr) alloc_info{std::move(info)};
				return x + 1;
			}
		}
		void operator delete(void* ptr) {
			if (ptr == nullptr) return;
			alloc_info* info = static_cast<alloc_info*>(ptr) - 1;
			auto alloc = std::move(info->allocator);
			std::allocator_traits<Allocator>::deallocate(alloc, static_cast<std::byte*>(static_cast<void*>(info)), info->size);
		}
	};
} // namespace asyncpp::detail

#pragma once
#include <cstddef>
#include <memory>

struct debug_allocator {
	using value_type = std::byte;
	size_t allocated_sum{0};
	size_t allocated_count{0};
	size_t released_sum{0};
	size_t released_count{0};
	std::byte* allocate(size_t n) {
		allocated_sum += n;
		allocated_count++;
		return ::new std::byte[n];
	}
	void deallocate(std::byte* ptr, size_t n) {
		released_sum += n;
		released_count++;
		::delete[] ptr;
	}
};

template<typename T>
struct allocator_ref {
	using value_type = typename std::allocator_traits<T>::value_type;

	constexpr allocator_ref(T& p) : parent{p} {}

	T& parent;
	std::byte* allocate(size_t n) { return parent.allocate(n); }
	void deallocate(std::byte* ptr, size_t n) { return parent.deallocate(ptr, n); }
};

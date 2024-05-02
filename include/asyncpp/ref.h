#pragma once
#include <atomic>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace asyncpp {
	/**
	 * \brief Concept describing a refcount policy for use with intrusive_refcount.
	 */
	template<typename T>
	concept RefCount = requires() {
		{ T{std::declval<size_t>()} };
		{ std::declval<T&>().fetch_increment() } -> std::convertible_to<size_t>;
		{ std::declval<T&>().fetch_decrement() } -> std::convertible_to<size_t>;
		{ std::declval<const T&>().count() } -> std::convertible_to<size_t>;
	};

	/**
	 * \brief Threadsafe refcount policy
	 */
	struct thread_safe_refcount {
		std::atomic<size_t> m_count;
		explicit constexpr thread_safe_refcount(size_t init_val = 0) noexcept : m_count{init_val} {}
		thread_safe_refcount(const thread_safe_refcount& other) = delete;
		thread_safe_refcount& operator=(const thread_safe_refcount& other) = delete;

		size_t fetch_increment() noexcept { return m_count.fetch_add(1, std::memory_order::acquire); }
		[[nodiscard]] size_t fetch_decrement() noexcept { return m_count.fetch_sub(1, std::memory_order::release); }
		[[nodiscard]] size_t count() const noexcept { return m_count.load(std::memory_order::relaxed); }
	};
	/**
	 * \brief Thread unsafe refcount policy
	 */
	struct thread_unsafe_refcount {
		size_t m_count;
		explicit constexpr thread_unsafe_refcount(size_t init_val = 0) noexcept : m_count{init_val} {}
		thread_unsafe_refcount(const thread_unsafe_refcount& other) = delete;
		thread_unsafe_refcount& operator=(const thread_unsafe_refcount& other) = delete;

		size_t fetch_increment() noexcept { return m_count++; }
		[[nodiscard]] size_t fetch_decrement() noexcept { return m_count--; }
		[[nodiscard]] size_t count() const noexcept { return m_count; }
	};

	static_assert(RefCount<thread_safe_refcount>, "[INTERNAL] thread_safe_refcount does not satisfy RefCount");
	static_assert(RefCount<thread_unsafe_refcount>, "[INTERNAL] thread_unsafe_refcount does not satisfy RefCount");

	template<typename T, RefCount TCounter = thread_safe_refcount>
	class intrusive_refcount;

	/**
	 * \brief Intrusive refcounting base class
	 * \tparam T Derived type
	 * \tparam TCounter Counter policy to use, e.g. thread_safe_refcount or thread_unsafe_refcount
	 */
	template<typename T, RefCount TCounter>
	class intrusive_refcount {
	public:
		intrusive_refcount() = default;

	protected:
		~intrusive_refcount() noexcept = default;
		/**
		 * \brief Get the current use_count of this object
		 * \return size_t The reference count
		 */
		size_t use_count() const noexcept { return m_refcount.count(); }

		/**
		 * \brief Increment the reference count
		 */
		void add_ref() const noexcept {
			static_assert(std::is_base_of_v<intrusive_refcount<T, TCounter>, T>,
						  "T needs to inherit intrusive_refcount<T>");
			m_refcount.fetch_increment();
		}

		/**
		 * \brief Decrement the reference count and delete the object if the last reference is removed.
		 * \note This might invoke the equivalent of `delete (T*)this`. Make sure you do not access any
		 *       member data after calling unless you hold an extra reference to it.
		 */
		void remove_ref() const noexcept {
			static_assert(std::is_nothrow_destructible_v<T>, "Destructor needs to be noexcept!");
			static_assert(std::is_base_of_v<intrusive_refcount<T, TCounter>, T>,
						  "T needs to inherit intrusive_refcount<T>");
			auto cnt = m_refcount.fetch_decrement();
			if (cnt == 1) delete static_cast<const T*>(this);
		}

	private:
		mutable TCounter m_refcount{0};

		friend inline void refcounted_add_ref(const intrusive_refcount<T, TCounter>* ptr) noexcept {
			if (ptr) ptr->add_ref();
		}
		friend inline void refcounted_remove_ref(const intrusive_refcount<T, TCounter>* ptr) noexcept {
			if (ptr) ptr->remove_ref();
		}
	};

	inline constexpr struct {
	} adopt_ref{};

	/**
	 * \brief Concept checking if a type is viable for usage with ref<>
	 *        (i.e. it provides overloads for refcounted_add_ref and refcounted_remove_ref)
	 */
	template<typename T>
	concept RefCountable = requires(T* obj) {
		{ refcounted_add_ref(obj) };
		{ refcounted_remove_ref(obj) };
	};

	/**
	 * \brief Reference count handle
	 * \tparam T Type of the reference counted class
	 */
	template<typename T>
	class ref {
		T* m_ptr;

	public:
		/**
		 * \brief Construct an empty ref
		 */
		constexpr ref() noexcept : m_ptr(nullptr) { static_assert(RefCountable<T>, "T needs to be refcountable"); }
		/**
		 * \brief Construct a new ref object
		 * \param ptr The pointer to store
		 * \param adopt_ref the reference count is already incremented, keep it as is
		 */
		ref(T* ptr, decltype(adopt_ref)) noexcept : m_ptr{ptr} {
			static_assert(RefCountable<T>, "T needs to be refcountable");
		}
		/**
		 * \brief Construct a new ref object incrementing the reference count of the passed pointer
		 * \param ptr The pointer to store
		 */
		// NOLINTNEXTLINE(google-explicit-constructor)
		ref(T* ptr) noexcept(noexcept(refcounted_add_ref(std::declval<T*>()))) : m_ptr{ptr} {
			static_assert(RefCountable<T>, "T needs to be refcountable");
			if (m_ptr) refcounted_add_ref(m_ptr);
		}
		/**
		 * \brief Construct a new ref object
		 * 
		 * \param ptr The pointer to store
		 * \param adopt_ref the reference count is already incremented, keep it as is
		 */
		/// \brief Copy constructor
		ref(const ref& other) noexcept(noexcept(refcounted_add_ref(std::declval<T*>()))) : m_ptr{other.m_ptr} {
			if (m_ptr) refcounted_add_ref(m_ptr);
		}
		/// \brief Move constructor
		constexpr ref(ref&& other) noexcept : m_ptr{std::exchange(other.m_ptr, nullptr)} {}
		/// \brief Assignment operator
		ref& operator=(const ref& other) noexcept(
			noexcept(refcounted_add_ref(std::declval<T*>())) && noexcept(refcounted_remove_ref(std::declval<T*>()))) {
			if (&other != this) reset(other.m_ptr);
			return *this;
		}
		/// \brief Move assignment operator
		//NOLINTNEXTLINE(hicpp-noexcept-move,performance-noexcept-move-constructor)
		ref& operator=(ref&& other) noexcept(noexcept(refcounted_remove_ref(std::declval<T*>()))) {
			if (m_ptr) refcounted_remove_ref(m_ptr);
			m_ptr = std::exchange(other.m_ptr, nullptr);
			return *this;
		}
		/**
		 * \brief Reset the handle with a new value
		 * \param ptr The new pointer
		 * \param adopt_ref the reference count is already incremented, keep it as is
		 */
		void reset(T* ptr) noexcept(
			noexcept(refcounted_add_ref(std::declval<T*>())) && noexcept(refcounted_remove_ref(std::declval<T*>()))) {
			if (m_ptr) refcounted_remove_ref(m_ptr);
			m_ptr = ptr;
			if (m_ptr) refcounted_add_ref(m_ptr);
		}
		/**
		 * \brief Reset the handle with a new value
		 * \param ptr The new pointer
		 * \param adopt_ref the reference count is already incremented, keep it as is
		 */
		void reset(T* ptr, decltype(adopt_ref)) noexcept(noexcept(refcounted_remove_ref(std::declval<T*>()))) {
			if (m_ptr) refcounted_remove_ref(m_ptr);
			m_ptr = ptr;
		}
		/**
		 * \brief Reset the handle to nullptr
		 */
		void reset() noexcept(noexcept(refcounted_remove_ref(std::declval<T*>()))) { reset(nullptr, adopt_ref); }
		/// \brief Destructor
		//NOLINTNEXTLINE(performance-noexcept-destructor)
		~ref() noexcept(noexcept(refcounted_remove_ref(std::declval<T*>()))) { reset(); }
		/// \brief Dereference this handle
		constexpr T* operator->() const noexcept { return m_ptr; }
		/// \brief Dereference this handle
		constexpr T& operator*() const noexcept { return *m_ptr; }
		/// \brief Get the contained value
		constexpr T* get() const noexcept { return m_ptr; }
		/// \brief Release the contained pointer
		constexpr T* release() noexcept {
			auto ptr = m_ptr;
			m_ptr = nullptr;
			return ptr;
		}
		/// \brief Check if the handle contains a pointer
		explicit constexpr operator bool() const noexcept { return m_ptr != nullptr; }
		/// \brief Check if the handle contains no pointer
		constexpr bool operator!() const noexcept { return m_ptr == nullptr; }
	};

	template<RefCountable T, typename... Args>
	ref<T> make_ref(Args&&... args) {
		return ref<T>(new T(std::forward<Args>(args)...));
	}

	/**
	 * \brief Thread safe reference handle.
	 *
	 * atomic_ref is similar to ref but allows for thread safe assignment and read.
	 * It is similar to `std::atomic<std::shared_ptr<>>`. Note that atomic_ref is not
	 * lock free, it uses the upmost bit of the pointer as a locking flag. This allows
	 * for a small size (identical to raw pointer) and does not need direct access to
	 * the reference count. The downside is that it effectively serializes all access
	 * regardless of reading/writing.
	 * \tparam T Type of the reference counted class
	 */
	template<RefCountable T>
	class atomic_ref {
		friend struct std::hash<asyncpp::atomic_ref<T>>;
		template<RefCountable T2>
		friend constexpr auto operator<=>(const atomic_ref<T2>& lhs, const atomic_ref<T2>& rhs) noexcept;
		template<RefCountable T2>
		friend constexpr auto operator<=>(const atomic_ref<T2>& lhs, const T2* rhs) noexcept;
		template<RefCountable T2>
		friend constexpr auto operator<=>(const T2* lhs, const atomic_ref<T2>& rhs) noexcept;
		mutable std::atomic<uintptr_t> m_ptr;

		static constexpr uintptr_t lock_mask = uintptr_t{1} << (sizeof(uintptr_t) * 8 - 1);

		/**
		 * \brief Pause the current cpu. This is effectively an optimized nop
		 * 	      that reduces congestion in hyperthreading and improves power usage.
		 */
		inline static void cpu_pause() noexcept {
#if defined(__i386) || defined(_M_IX86) || defined(_X86_) || defined(__amd64) || defined(_M_AMD64)
#ifdef _MSC_VER
			_mm_pause();
#else
			__builtin_ia32_pause();
#endif
#elif defined(__arm__) || defined(_ARM) || defined(_M_ARM) || defined(__arm)
#ifdef _MSC_VER
			__yield();
#else
			asm volatile("yield");
#endif
#elif defined(__riscv)
			asm volatile("pause");
#endif
		}

		/**
		 * \brief Lock the pointer. This is effectively a spinlock on the most significant bit.
		 */
		uintptr_t lock() const noexcept {
			// Lock the current pointer value
			auto val = m_ptr.fetch_or(lock_mask, std::memory_order_acquire);
			while ((val & lock_mask) == lock_mask) {
				cpu_pause();
				val = m_ptr.fetch_or(lock_mask, std::memory_order_acquire);
			}
			return val;
		}

		/**
		 * \brief Unlocks the pointer by replacing the value with val.
		 */
		void unlock_with(uintptr_t val) const noexcept {
			assert((val & lock_mask) == 0);
			[[maybe_unused]] auto res = m_ptr.exchange(val, std::memory_order_release);
			assert((res & lock_mask) == lock_mask);
		}

	public:
		/**
		 * \brief Construct an empty atomic_ref
		 */
		constexpr atomic_ref() noexcept : m_ptr(0) {}
		/**
		 * \brief Construct a new atomic_ref object
		 *
		 * \param ptr The pointer to store
		 * \throw std::logic_error if the pointer value collides with the lock_mask
		 */
		// NOLINTNEXTLINE(google-explicit-constructor)
		atomic_ref(ref<T> ptr) {
			if ((reinterpret_cast<uintptr_t>(ptr.get()) & lock_mask) != 0) throw std::logic_error("invalid pointer");
			m_ptr = reinterpret_cast<uintptr_t>(ptr.release());
		}
		/** \brief Assignment operator */
		atomic_ref& operator=(const ref<T>& other) {
			exchange(other);
			return *this;
		}
		/** \brief Move assignment operator */
		atomic_ref& operator=(ref<T>&& other) {
			exchange(ref<T>(other.release(), adopt_ref));
			return *this;
		}
		/** \brief Assignment operator */
		atomic_ref& operator=(const atomic_ref<T>& other) noexcept(
			noexcept(refcounted_add_ref(std::declval<T*>())) && noexcept(refcounted_remove_ref(std::declval<T*>()))) {
			if (&other != this) exchange(other.load());
			return *this;
		}
		/** \brief Move assignment operator */
		//NOLINTNEXTLINE(hicpp-noexcept-move,performance-noexcept-move-constructor)
		atomic_ref& operator=(atomic_ref<T>&& other) noexcept(noexcept(refcounted_remove_ref(std::declval<T*>()))) {
			exchange(ref<T>(other.release(), adopt_ref));
			return *this;
		}
		/** \brief Destructor */
		//NOLINTNEXTLINE(performance-noexcept-destructor)
		~atomic_ref() noexcept(noexcept(refcounted_remove_ref(std::declval<T*>()))) { exchange(ref<T>()); }
		/** \brief Get the contained value */
		ref<T> load() const noexcept(noexcept(refcounted_add_ref(std::declval<T*>()))) {
			// Early out if nullptr
			if ((m_ptr.load(std::memory_order_relaxed) & ~lock_mask) == 0) return 0;

			// Lock the pointer to prevent concurrent modification
			auto val = lock();
			// Get original pointer
			auto ptr = reinterpret_cast<T*>(val);
			// Add reference
			if (ptr) refcounted_add_ref(ptr);
			// Unlock again
			unlock_with(val);
			return ref<T>(ptr, adopt_ref);
		}
		/**
		 * \brief Store a new value and destroy the old one
		 * \param hdl The new pointer
		 * \throw std::logic_error if the pointer value collides with the lock_mask
		 */
		void store(ref<T> hdl) { exchange(std::move(hdl)); }
		/**
		 * \brief Store a new value and destroy the old one
		 * \param hdl The new pointer
		 */
		void store(const atomic_ref<T>& hdl) { exchange(hdl.load()); }
		/**
		 * \brief Reset the handle with a new value
		 * \param hdl The new pointer
		 * \return The old value of this handle
		 * \throw std::logic_error if the pointer value collides with the lock_mask
		 */
		ref<T> exchange(ref<T> hdl) {
			auto ptr = hdl.release();
			if ((reinterpret_cast<uintptr_t>(ptr) & lock_mask) != 0) throw std::logic_error("invalid pointer");

			// Lock the current pointer value
			auto val = lock();
			// Unlock again with new value
			unlock_with(reinterpret_cast<uintptr_t>(ptr));
			// Return the old pointer without incrementing the reference count
			return ref<T>(reinterpret_cast<T*>(val), adopt_ref);
		}
		/**
		 * \brief Reset the handle with a new value
		 * \param hdl The new pointer
		 */
		ref<T> exchange(const atomic_ref<T>& hdl) { return exchange(hdl.load()); }
		/** \brief Release the contained pointer */
		T* release() noexcept { return exchange(ref<T>()).release(); }
		/** \brief Reset the pointer to nullptr */
		void reset() noexcept(noexcept(refcounted_remove_ref(std::declval<T*>()))) { exchange(ref<T>()); }
		/** \brief Check if the handle contains a pointer */
		//NOLINTNEXTLINE(google-explicit-constructor)
		operator bool() const noexcept { return (m_ptr.load(std::memory_order_relaxed) & ~lock_mask) != 0; }
		/** \brief Check if the handle contains no pointer */
		bool operator!() const noexcept { return (m_ptr.load(std::memory_order_relaxed) & ~lock_mask) == 0; }
		/** \brief Dereference this handle */
		ref<T> operator->() const noexcept(noexcept(refcounted_add_ref(std::declval<T*>()))) { return load(); }
	};

	template<typename T>
	inline constexpr auto operator<=>(const ref<T>& lhs, const ref<T>& rhs) noexcept {
		return lhs.get() <=> rhs.get();
	}
	template<typename T>
	inline constexpr auto operator<=>(const ref<T>& lhs, const T* rhs) noexcept {
		return lhs.get() <=> rhs;
	}
	template<typename T>
	inline constexpr auto operator<=>(const T* lhs, const ref<T>& rhs) noexcept {
		return lhs <=> rhs.get();
	}

	template<typename T>
	inline constexpr auto operator==(const ref<T>& lhs, const ref<T>& rhs) noexcept {
		return (lhs <=> rhs) == std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator==(const ref<T>& lhs, const T* rhs) noexcept {
		return (lhs <=> rhs) == std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator==(const T* lhs, const ref<T>& rhs) noexcept {
		return (lhs <=> rhs) == std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator!=(const ref<T>& lhs, const ref<T>& rhs) noexcept {
		return (lhs <=> rhs) != std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator!=(const ref<T>& lhs, const T* rhs) noexcept {
		return (lhs <=> rhs) != std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator!=(const T* lhs, const ref<T>& rhs) noexcept {
		return (lhs <=> rhs) != std::strong_ordering::equal;
	}

	template<RefCountable T>
	inline constexpr auto operator<=>(const atomic_ref<T>& lhs, const atomic_ref<T>& rhs) noexcept {
		return (lhs.m_ptr.load(std::memory_order::relaxed) & ~atomic_ref<T>::lock_mask) <=>
			   (rhs.m_ptr.load(std::memory_order::relaxed) & ~atomic_ref<T>::lock_mask);
	}
	template<RefCountable T>
	inline constexpr auto operator<=>(const atomic_ref<T>& lhs, const T* rhs) noexcept {
		return (lhs.m_ptr.load(std::memory_order::relaxed) & ~atomic_ref<T>::lock_mask) <=>
			   reinterpret_cast<uintptr_t>(rhs);
	}
	template<RefCountable T>
	inline constexpr auto operator<=>(const T* lhs, const atomic_ref<T>& rhs) noexcept {
		return reinterpret_cast<uintptr_t>(lhs) <=>
			   (rhs.m_ptr.load(std::memory_order::relaxed) & ~atomic_ref<T>::lock_mask);
	}

	template<typename T>
	inline constexpr auto operator==(const atomic_ref<T>& lhs, const atomic_ref<T>& rhs) noexcept {
		return (lhs <=> rhs) == std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator==(const atomic_ref<T>& lhs, const T* rhs) noexcept {
		return (lhs <=> rhs) == std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator==(const T* lhs, const atomic_ref<T>& rhs) noexcept {
		return (lhs <=> rhs) == std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator!=(const atomic_ref<T>& lhs, const atomic_ref<T>& rhs) noexcept {
		return (lhs <=> rhs) != std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator!=(const atomic_ref<T>& lhs, const T* rhs) noexcept {
		return (lhs <=> rhs) != std::strong_ordering::equal;
	}
	template<typename T>
	inline constexpr auto operator!=(const T* lhs, const atomic_ref<T>& rhs) noexcept {
		return (lhs <=> rhs) != std::strong_ordering::equal;
	}

	template<typename T, typename U>
	ref<T> static_ref_cast(const ref<U>& rhs) noexcept {
		return ref<T>(static_cast<T*>(rhs.get()));
	}
	template<typename T, typename U>
	ref<T> static_ref_cast(ref<U>&& rhs) noexcept {
		return ref<T>(static_cast<T*>(rhs.release()), adopt_ref);
	}
	template<typename T, typename U>
	ref<T> const_ref_cast(const ref<U>& rhs) noexcept {
		return ref<T>(const_cast<T*>(rhs.get()));
	}
	template<typename T, typename U>
	ref<T> const_ref_cast(ref<U>&& rhs) noexcept {
		return ref<T>(const_cast<T*>(rhs.release()), adopt_ref);
	}
	template<typename T, typename U>
	ref<T> dynamic_ref_cast(const ref<U>& rhs) noexcept {
		return ref<T>(dynamic_cast<T*>(rhs.get()));
	}
	template<typename T, typename U>
	ref<T> dynamic_ref_cast(ref<U>&& rhs) noexcept {
		auto ptr = dynamic_cast<T*>(rhs.get());
		if (!ptr) return ref<T>();
		// We already hold the correct pointer using get(),
		// so only clear it without removing the ref
		rhs.release();
		return ref<T>(ptr, adopt_ref);
	}

} // namespace asyncpp

template<typename T>
//NOLINTNEXTLINE(cert-dcl58-cpp)
struct std::hash<asyncpp::ref<T>> {
	constexpr size_t operator()(const asyncpp::ref<T>& rhs) const noexcept { return std::hash<void*>{}(rhs.get()); }
};

template<typename T>
//NOLINTNEXTLINE(cert-dcl58-cpp)
struct std::hash<asyncpp::atomic_ref<T>> {
	constexpr size_t operator()(const asyncpp::atomic_ref<T>& rhs) const noexcept {
		auto ptr = rhs.m_ptr.load(std::memory_order::relaxed) & ~asyncpp::atomic_ref<T>::lock_mask;
		return std::hash<uintptr_t>{}(ptr);
	}
};

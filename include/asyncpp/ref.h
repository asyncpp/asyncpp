#pragma once
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace asyncpp {
	/**
	 * \brief Concept describing a refcount policy for use with intrusive_refcount.
	 */
	template<typename T>
	concept RefCount = requires() {
		{T{std::declval<size_t>()}};
		{ std::declval<T&>().fetch_increment() }
		->std::convertible_to<size_t>;
		{ std::declval<T&>().fetch_decrement() }
		->std::convertible_to<size_t>;
		{ std::declval<const T&>().count() }
		->std::convertible_to<size_t>;
	};

	/**
	 * \brief Threadsafe refcount policy
	 */
	struct thread_safe_refcount {
		std::atomic<size_t> m_count;
		constexpr thread_safe_refcount(size_t init_val = 0) noexcept : m_count{init_val} {}
		thread_safe_refcount(const thread_safe_refcount& other) = delete;
		thread_safe_refcount& operator=(const thread_safe_refcount& other) = delete;

		size_t fetch_increment() noexcept { return m_count.fetch_add(1, std::memory_order::acquire); }
		size_t fetch_decrement() noexcept { return m_count.fetch_sub(1, std::memory_order::release); }
		size_t count() const noexcept { return m_count.load(std::memory_order::relaxed); }
	};
	/**
	 * \brief Thread unsafe refcount policy
	 */
	struct thread_unsafe_refcount {
		size_t m_count;
		constexpr thread_unsafe_refcount(size_t init_val = 0) noexcept : m_count{init_val} {}
		thread_unsafe_refcount(const thread_unsafe_refcount& other) = delete;
		thread_unsafe_refcount& operator=(const thread_unsafe_refcount& other) = delete;

		size_t fetch_increment() noexcept { return m_count++; }
		size_t fetch_decrement() noexcept { return m_count--; }
		size_t count() const noexcept { return m_count; }
	};

	static_assert(RefCount<thread_safe_refcount>, "[INTERNAL] thread_safe_refcount does not satisfy RefCount");
	static_assert(RefCount<thread_unsafe_refcount>, "[INTERNAL] thread_unsafe_refcount does not satisfy RefCount");

	template<RefCount TCounter = thread_safe_refcount>
	class intrusive_refcount;

	namespace detail {
		template<typename T>
		inline void is_intrusive_refcount(const intrusive_refcount<T>&);
	}

	/**
	 * \brief Concept to check if a class inherits from intrusive_refcount
	 */
	template<typename T>
	concept IntrusiveRefCount = requires(T& a) {
		{detail::is_intrusive_refcount(a)};
	};

	/**
	 * \brief Intrusive refcounting base class
	 * \tparam TCounter Counter policy to use, e.g. thread_safe_refcount or thread_unsafe_refcount
	 */
	template<RefCount TCounter>
	class intrusive_refcount {
		mutable TCounter m_refcount{0};
		template<IntrusiveRefCount T>
		friend void refcounted_add_ref(const T*);
		template<IntrusiveRefCount T>
		friend void refcounted_remove_ref(const T*);

	protected:
		~intrusive_refcount() noexcept = default;
		/**
		 * \brief Get the current use_count of this object
		 * \return size_t The reference count
		 */
		size_t use_count() const noexcept { return m_refcount.count(); }

	public:
		intrusive_refcount() = default;
	};

	/**
	 * \brief refcounted_add_ref specialization for intrusive_refcount
	 * \tparam T derived type
	 * \param ptr The pointer to add a reference to
	 */
	template<IntrusiveRefCount T>
	inline void refcounted_add_ref(const T* ptr) {
		assert(ptr);
		ptr->m_refcount.fetch_increment();
	}

	/**
	 * \brief refcounted_remove_ref specialization for intrusive_refcount
	 * \tparam T derived type
	 * \param ptr The pointer to remove a reference from
	 */
	template<IntrusiveRefCount T>
	inline void refcounted_remove_ref(const T* ptr) {
		assert(ptr);
		auto cnt = ptr->m_refcount.fetch_decrement();
		if (cnt == 1) delete ptr;
	}

	/**
	 * \brief Concept checking if a type is viable for usage with ref<> (i.e. it provides overloads for refcounted_add_ref and refcounted_remove_ref)
	 */
	template<typename T>
	concept RefCountable = requires(T* a) {
		{refcounted_add_ref(a)};
		{refcounted_remove_ref(a)};
	};

	/**
	 * \brief Reference count handle
	 * \tparam T Type of the reference counted class
	 */
	template<RefCountable T>
	class ref {
		T* m_ptr;

	public:
		static constexpr bool remove_ref_noexcept = noexcept(refcounted_remove_ref(std::declval<T*>()));
		static constexpr bool add_ref_noexcept = noexcept(refcounted_add_ref(std::declval<T*>()));

		/**
		 * \brief Construct a new ref object
		 * 
		 * \param ptr The pointer to store
		 * \param adopt_ref the reference count is already incremented, keep it as is
		 */
		ref(T* ptr, bool adopt_ref = false) noexcept(add_ref_noexcept) : m_ptr{ptr} {
			if (m_ptr && !adopt_ref) refcounted_add_ref(m_ptr);
		}
		/// \brief Copy constructor
		ref(const ref& other) noexcept(add_ref_noexcept) : m_ptr{other.m_ptr} {
			if (m_ptr) refcounted_add_ref(m_ptr);
		}
		/// \brief Assignment operator
		ref& operator=(const ref& other) noexcept(add_ref_noexcept && remove_ref_noexcept) {
			reset(other.m_ptr, false);
			return *this;
		}
		/**
		 * \brief Reset the handle with a new value
		 * \param ptr The new pointer
		 * \param adopt_ref the reference count is already incremented, keep it as is
		 */
		void reset(T* ptr = nullptr,
				   bool adopt_ref = false) noexcept(add_ref_noexcept && remove_ref_noexcept) {
			if (m_ptr) refcounted_remove_ref(m_ptr);
			m_ptr = ptr;
			if (m_ptr && !adopt_ref) refcounted_add_ref(m_ptr);
		}
		/// \brief Destructor
		~ref() noexcept(remove_ref_noexcept) { reset(); }
		/// \brief Dereference this handle
		T* operator->() const noexcept { return m_ptr; }
		/// \brief Dereference this handle
		T& operator*() const noexcept { return *m_ptr; }
		/// \brief Get the contained value
		T* get() const noexcept { return m_ptr; }
		/// \brief Release the contained pointer
		T* release() noexcept {
			auto ptr = m_ptr;
			m_ptr = nullptr;
			return ptr;
		}
		/// \brief Check if the handle contains a pointer
		operator bool() const noexcept { return m_ptr != nullptr; }
		/// \brief Check if the handle contains no pointer
		bool operator!() const noexcept { return m_ptr == nullptr; }
	};

	template<RefCountable T, typename... Args>
	ref<T> make_ref(Args&&... args) {
		return ref<T>(new T(std::forward<Args>(args)...));
	}
} // namespace asyncpp

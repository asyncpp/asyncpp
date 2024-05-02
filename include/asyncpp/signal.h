#pragma once
#include <asyncpp/ref.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace asyncpp {
	struct signal_traits_mt {
		using mutex_type = std::mutex;
		using refcount_type = thread_safe_refcount;
	};
	struct signal_traits_st {
		struct noop_mutex {
			constexpr void lock() noexcept {}
			constexpr void unlock() noexcept {}
			constexpr bool try_lock() noexcept { return true; }
		};
		using mutex_type = noop_mutex;
		using refcount_type = thread_unsafe_refcount;
	};

	template<typename, typename = signal_traits_mt>
	class signal;

	namespace detail {
		struct signal_node_base : intrusive_refcount<signal_node_base> {
			virtual ~signal_node_base() noexcept = default;
			std::atomic<size_t> counter;
		};
		static constexpr size_t signal_removed_counter = 0;
	} // namespace detail

	class signal_handle {
		ref<detail::signal_node_base> m_node;
		template<typename, typename>
		friend class signal;

	public:
		explicit signal_handle(ref<detail::signal_node_base> hndl = {}) : m_node(std::move(hndl)) {}
		explicit operator bool() const noexcept { return valid(); }
		[[nodiscard]] bool operator!() const noexcept { return !valid(); }
		[[nodiscard]] bool valid() const noexcept {
			return m_node && m_node->counter != detail::signal_removed_counter;
		}
		void disconnect() noexcept {
			if (m_node) m_node->counter = detail::signal_removed_counter;
			m_node.reset();
		}

		friend inline constexpr auto operator<=>(const signal_handle& lhs, const signal_handle& rhs) noexcept {
			return lhs.m_node.get() <=> rhs.m_node.get();
		}
		friend inline constexpr auto operator==(const signal_handle& lhs, const signal_handle& rhs) noexcept {
			return lhs.m_node.get() == rhs.m_node.get();
		}
		friend inline constexpr auto operator!=(const signal_handle& lhs, const signal_handle& rhs) noexcept {
			return lhs.m_node.get() != rhs.m_node.get();
		}
	};

	class scoped_signal_handle {
		signal_handle m_handle;

	public:
		//NOLINTNEXTLINE(google-explicit-constructor)
		scoped_signal_handle(ref<detail::signal_node_base> hdl = {}) : m_handle(std::move(hdl)) {}
		//NOLINTNEXTLINE(google-explicit-constructor)
		scoped_signal_handle(signal_handle hdl) : m_handle(std::move(hdl)) {}
		~scoped_signal_handle() noexcept { m_handle.disconnect(); }
		explicit operator bool() const noexcept { return valid(); }
		[[nodiscard]] bool operator!() const noexcept { return !valid(); }
		[[nodiscard]] bool valid() const noexcept { return m_handle.valid(); }
		void disconnect() noexcept { m_handle.disconnect(); }
		void release() noexcept { m_handle = signal_handle{}; }
		// NOLINTNEXTLINE(google-explicit-constructor)
		[[nodiscard]] constexpr operator signal_handle&() noexcept { return m_handle; }
		// NOLINTNEXTLINE(google-explicit-constructor)
		[[nodiscard]] constexpr operator const signal_handle&() const noexcept { return m_handle; }

		friend inline constexpr auto operator<=>(const scoped_signal_handle& lhs,
												 const scoped_signal_handle& rhs) noexcept {
			return lhs.m_handle <=> rhs.m_handle;
		}
		friend inline constexpr auto operator==(const scoped_signal_handle& lhs,
												const scoped_signal_handle& rhs) noexcept {
			return lhs.m_handle == rhs.m_handle;
		}
		friend inline constexpr auto operator!=(const scoped_signal_handle& lhs,
												const scoped_signal_handle& rhs) noexcept {
			return lhs.m_handle != rhs.m_handle;
		}
	};

	template<typename... TParams, typename TTraits>
	class signal<void(TParams...), TTraits> {
		struct node : detail::signal_node_base {
			virtual void invoke(const TParams&...) = 0;

			ref<node> next{};
			node* previous{};
		};
		template<typename FN>
		struct node_impl final : node {
			~node_impl() noexcept = default;
			void invoke(const TParams&... params) override { m_fn(params...); }
			[[no_unique_address]] FN m_fn;
			explicit node_impl(FN&& fncb) : m_fn(std::move(fncb)) {}
		};

	public:
		using traits_type = TTraits;
		using handle = signal_handle;

		signal() = default;
		~signal();
		signal(const signal&) = delete;
		//NOLINTNEXTLINE(performance-noexcept-move-constructor)
		signal(signal&&);
		signal& operator=(const signal&) = delete;
		//NOLINTNEXTLINE(performance-noexcept-move-constructor)
		signal& operator=(signal&&);

		[[nodiscard]] size_t size() const noexcept;
		[[nodiscard]] bool empty() const noexcept { return size() == 0; }

		template<typename FN>
		handle append(FN&& fncb);
		template<typename FN>
		handle prepend(FN&& fncb);

		bool remove(const handle& hdl);
		[[nodiscard]] bool owns_handle(const handle& hdl) const;

		size_t operator()(const TParams&... params) const;

		template<typename FN>
		handle operator+=(FN&& fncb) {
			return append(std::forward<decltype(fncb)>(fncb));
		}

		void operator-=(const handle& hdl) { remove(hdl); }

	private:
		mutable typename TTraits::mutex_type m_mutex{};
		mutable ref<node> m_head{};
		mutable node* m_tail{};
		std::atomic<size_t> m_current_counter{1};

		size_t get_next_counter() {
			const auto result = m_current_counter.fetch_add(1, std::memory_order::seq_cst);
			if (result == 0) { // overflow, let's reset all nodes' counters.
				{
					std::lock_guard lck(m_mutex);
					auto node = m_head;
					while (node) {
						node->counter = 1;
						node = node->next;
					}
				}
				return m_current_counter.fetch_add(1, std::memory_order::seq_cst);
			}
			return result;
		}

		void free_node(ref<node>& node) const noexcept {
			if (node->next) { node->next->previous = node->previous; }
			if (node->previous) { node->previous->next = node->next; }
			node->counter = detail::signal_removed_counter;
			if (m_head == node) m_head = node->next;
			if (m_tail == node) m_tail = node->previous;
		}
	};

	template<typename T>
	using signal_st = signal<T, signal_traits_st>;
	template<typename T>
	using signal_mt = signal<T, signal_traits_mt>;

	template<typename, typename, typename = signal_traits_mt>
	class signal_manager;

	template<typename TEventType, typename... TParams, typename TTraits>
	class signal_manager<TEventType, void(TParams...), TTraits> {
	public:
		using event_type = TEventType;
		using signal_type = signal<void(TParams...), TTraits>;
		using traits_type = TTraits;
		using handle = typename signal_type::handle;

		signal_manager() = default;
		//NOLINTNEXTLINE(performance-noexcept-move-constructor)
		signal_manager(signal_manager&&);
		//NOLINTNEXTLINE(performance-noexcept-move-constructor)
		signal_manager& operator=(signal_manager&&);
		signal_manager(const signal_manager&) = delete;
		signal_manager& operator=(const signal_manager&) = delete;

		template<typename FN>
		handle append(event_type event, FN&& fncb) {
			auto iter = find_or_create_slot(event);
			assert(iter != m_mapping.end());
			return iter->second.append(std::forward<decltype(fncb)>(fncb));
		}
		template<typename FN>
		handle prepend(event_type event, FN&& fncb) {
			auto iter = find_or_create_slot(event);
			assert(iter != m_mapping.end());
			return iter->second.prepend(std::forward<decltype(fncb)>(fncb));
		}

		bool remove(event_type event, const handle& hdl) {
			std::shared_lock lck{m_mutex};
			auto iter = m_mapping.find(event);
			return iter != m_mapping.end() && iter->second.remove(hdl);
		}

		bool owns_handle(event_type event, const handle& hdl) const {
			std::shared_lock lck{m_mutex};
			auto iter = m_mapping.find(event);
			return iter != m_mapping.end() && iter->second.owns_handle(hdl);
		}

		size_t invoke(event_type event, const TParams&... params) const {
			std::shared_lock lck{m_mutex};
			auto iter = m_mapping.find(event);
			if (iter == m_mapping.end()) return 0;
			return iter->second(params...);
		}

		size_t operator()(event_type event, const TParams&... params) const { return invoke(event, params...); }

		size_t shrink_to_fit() {
			std::unique_lock lck{m_mutex};
			size_t res = 0;
			for (auto it = m_mapping.begin(); it != m_mapping.end();) {
				if (it->second.empty()) {
					res++;
					it = m_mapping.erase(it);
				} else
					it++;
			}
			return res;
		}

	private:
		mutable std::shared_mutex m_mutex{};
		std::unordered_map<event_type, signal_type> m_mapping;

		auto find_or_create_slot(event_type evt) {
			std::shared_lock lck{m_mutex};
			auto iter = m_mapping.find(evt);
			if (iter == m_mapping.end()) {
				lck.unlock();
				std::unique_lock unique{m_mutex};
				iter = m_mapping.find(evt);
				if (iter != m_mapping.end()) return iter;
				iter = m_mapping.emplace(evt, signal_type{}).first;
			}
			return iter;
		}
	};

	template<typename... TParams, typename TTraits>
	//NOLINTNEXTLINE(performance-noexcept-move-constructor)
	inline signal<void(TParams...), TTraits>::signal(signal&& other) {
		std::scoped_lock lck{m_mutex, other.m_mutex};
		m_head = std::exchange(other.m_head, nullptr);
		m_tail = std::exchange(other.m_tail, nullptr);
		m_current_counter.store(other.m_current_counter.exchange(1));
	}

	template<typename... TParams, typename TTraits>
	//NOLINTNEXTLINE(performance-noexcept-move-constructor)
	inline signal<void(TParams...), TTraits>& signal<void(TParams...), TTraits>::operator=(signal&& other) {
		std::scoped_lock lck{m_mutex, other.m_mutex};
		auto node = m_head;
		m_head.reset();
		while (node) {
			auto next = node->next;
			node->previous = nullptr;
			node->next.reset();
			node = next;
		}
		assert(!node);
		m_head = std::exchange(other.m_head, nullptr);
		m_tail = std::exchange(other.m_tail, nullptr);
		m_current_counter.store(other.m_current_counter.exchange(1));
	}

	template<typename... TParams, typename TTraits>
	inline signal<void(TParams...), TTraits>::~signal() {
		auto node = m_head;
		m_head.reset();
		while (node) {
			auto next = node->next;
			node->previous = nullptr;
			node->next.reset();
			node = next;
		}
		assert(!node);
	}

	template<typename... TParams, typename TTraits>
	inline size_t signal<void(TParams...), TTraits>::size() const noexcept {
		std::lock_guard lck{m_mutex};
		auto node = m_head;
		size_t res = 0;
		while (node) {
			if (node->counter != detail::signal_removed_counter) res++;
			node = node->next;
		}
		return res;
	}

	template<typename... TParams, typename TTraits>
	template<typename FN>
	inline typename signal<void(TParams...), TTraits>::handle signal<void(TParams...), TTraits>::append(FN&& fncb) {
		ref<node> new_node(new node_impl<FN>(std::forward<decltype(fncb)>(fncb)));
		new_node->counter = get_next_counter();
		if (std::lock_guard lck{m_mutex}; m_head) {
			new_node->previous = m_tail;
			m_tail->next = new_node;
			m_tail = new_node.get();
		} else {
			m_head = new_node;
			m_tail = new_node.get();
		}
		return handle(static_ref_cast<detail::signal_node_base>(new_node));
	}

	template<typename... TParams, typename TTraits>
	template<typename FN>
	inline typename signal<void(TParams...), TTraits>::handle signal<void(TParams...), TTraits>::prepend(FN&& fncb) {
		ref<node> new_node(new node_impl<FN>(std::forward<decltype(fncb)>(fncb)));
		new_node->counter = get_next_counter();
		if (std::lock_guard lck{m_mutex}; m_head) {
			new_node->next = m_head;
			m_head->previous = new_node.get();
			m_head = new_node;
		} else {
			m_head = new_node;
			m_tail = new_node;
		}
		return handle(static_ref_cast<detail::signal_node_base>(new_node));
	}

	template<typename... TParams, typename TTraits>
	inline bool signal<void(TParams...), TTraits>::remove(const handle& hdl) {
		auto node = static_ref_cast<signal::node>(hdl.m_node);
		if (!node) return false;
		std::lock_guard lck{m_mutex};
		free_node(node);
		return true;
	}

	template<typename... TParams, typename TTraits>
	inline bool signal<void(TParams...), TTraits>::owns_handle(const handle& hdl) const {
		auto node = static_ref_cast<signal::node>(hdl.m_node);
		if (!node || node->counter == detail::signal_removed_counter) return false;
		std::lock_guard lck{m_mutex};
		auto handle = m_head;
		while (handle && handle != node) {
			handle = handle->next;
		}
		return node == handle;
	}

	template<typename... TParams, typename TTraits>
	inline size_t signal<void(TParams...), TTraits>::operator()(const TParams&... params) const {
		ref<node> node{};

		{
			std::lock_guard lck(m_mutex);
			node = m_head;
		}

		size_t ninvoked = 0;
		if (!node) return ninvoked;

		const auto counter = m_current_counter.load(std::memory_order_acquire);
		do {
			if (node->counter != detail::signal_removed_counter && counter >= node->counter) {
				node->invoke(params...);
				++ninvoked;
			}

			std::lock_guard lck(m_mutex);
			if (node->counter == detail::signal_removed_counter) { free_node(node); }
			node = node->next;
		} while (node);
		return ninvoked;
	}
} // namespace asyncpp

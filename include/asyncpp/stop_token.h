#pragma once
#ifndef ASYNCPP_FORCE_CUSTOM_STOP_TOKEN
#define ASYNCPP_FORCE_CUSTOM_STOP_TOKEN 0
#endif

#include <version>
#if defined(_LIBCPP_VERSION) || ASYNCPP_FORCE_CUSTOM_STOP_TOKEN
#include <atomic>
#include <thread>
#else
#include <stop_token>
#endif

namespace asyncpp {
#if defined(_LIBCPP_VERSION) || ASYNCPP_FORCE_CUSTOM_STOP_TOKEN
	struct nostopstate_t {
		explicit nostopstate_t() = default;
	};
	inline constexpr nostopstate_t nostopstate{};

	class stop_source;

	class stop_token {
	public:
		stop_token() noexcept = default;

		stop_token(const stop_token&) noexcept = default;
		stop_token(stop_token&&) noexcept = default;

		~stop_token() = default;

		stop_token& operator=(const stop_token&) noexcept = default;
		stop_token& operator=(stop_token&&) noexcept = default;

		[[nodiscard]] bool stop_possible() const noexcept {
			return static_cast<bool>(m_state) && m_state->stop_possible();
		}

		[[nodiscard]] bool stop_requested() const noexcept {
			return static_cast<bool>(m_state) && m_state->stop_requested();
		}

		void swap(stop_token& rhs) noexcept { m_state.swap(rhs.m_state); }

		[[nodiscard]] friend bool operator==(const stop_token& lhs, const stop_token& rhs) {
			return lhs.m_state == rhs.m_state;
		}

		friend void swap(stop_token& lhs, stop_token& rhs) noexcept { lhs.swap(rhs); }

	private:
		friend class stop_source;
		template<typename Callback>
		friend class stop_callback;

		static void yield() noexcept {
#if defined __i386__ || defined __x86_64__
			__builtin_ia32_pause();
#endif
			std::this_thread::yield();
		}

		struct binary_semaphore {
			explicit binary_semaphore(int initial) : m_counter(initial > 0 ? 1 : 0) {}

			void release() { m_counter.fetch_add(1, std::memory_order::release); }

			void acquire() {
				int old = 1;
				while (
					!m_counter.compare_exchange_weak(old, 0, std::memory_order::acquire, std::memory_order::relaxed)) {
					old = 1;
					yield();
				}
			}

			std::atomic<int> m_counter;
		};

		struct stop_cb_node_t {
			using cb_fn_t = void(stop_cb_node_t*) noexcept;
			cb_fn_t* m_callback;
			stop_cb_node_t* m_prev = nullptr;
			stop_cb_node_t* m_next = nullptr;
			bool* m_destroyed = nullptr;
			binary_semaphore m_done{0};

			explicit stop_cb_node_t(cb_fn_t* callback) : m_callback(callback) {}

			void run() noexcept { m_callback(this); }
		};

		class stop_state_t {
			using value_type = uint32_t;
			static constexpr value_type mask_stop_requested_bit = 1;
			static constexpr value_type mask_locked_bit = 2;
			static constexpr value_type mask_ssrc_counter_inc = 4;

			std::atomic<value_type> m_owners{1};
			std::atomic<value_type> m_value{mask_ssrc_counter_inc};
			stop_cb_node_t* m_head = nullptr;
			std::thread::id m_requester;

		public:
			stop_state_t() = default;

			bool stop_possible() noexcept { return (m_value.load(std::memory_order::acquire) & ~mask_locked_bit) != 0; }

			bool stop_requested() noexcept {
				return (m_value.load(std::memory_order::acquire) & mask_stop_requested_bit) != 0;
			}

			void add_owner() noexcept { m_owners.fetch_add(1, std::memory_order::relaxed); }

			void release_ownership() noexcept {
				if (m_owners.fetch_sub(1, std::memory_order::acq_rel) == 1) delete this;
			}

			void add_ssrc() noexcept { m_value.fetch_add(mask_ssrc_counter_inc, std::memory_order::relaxed); }

			void sub_ssrc() noexcept { m_value.fetch_sub(mask_ssrc_counter_inc, std::memory_order::release); }

			bool request_stop() noexcept {
				auto old = m_value.load(std::memory_order::acquire);
				do {
					if ((old & mask_stop_requested_bit) != 0) return false;
				} while (
					!try_lock(old, mask_stop_requested_bit, std::memory_order::acq_rel, std::memory_order::acquire));

				m_requester = std::this_thread::get_id();

				while (m_head != nullptr) {
					bool is_last_cb{true};
					stop_cb_node_t* node = m_head;
					m_head = m_head->m_next;
					if (m_head != nullptr) {
						m_head->m_prev = nullptr;
						is_last_cb = false;
					}

					unlock();

					bool is_destroyed = false;
					node->m_destroyed = &is_destroyed;

					node->run();

					if (!is_destroyed) {
						node->m_destroyed = nullptr;
						node->m_done.release();
					}

					if (is_last_cb) return true;

					lock();
				}

				unlock();
				return true;
			}

			bool register_callback(stop_cb_node_t* node) noexcept {
				auto old = m_value.load(std::memory_order::acquire);
				do {
					if ((old & mask_stop_requested_bit) != 0) {
						node->run();
						return false;
					}

					if (old < mask_ssrc_counter_inc) return false;
				} while (!try_lock(old, 0, std::memory_order::acquire, std::memory_order::acquire));

				node->m_next = m_head;
				if (m_head != nullptr) { m_head->m_prev = node; }
				m_head = node;
				unlock();
				return true;
			}

			void remove_callback(stop_cb_node_t* node) {
				lock();

				if (node == m_head) {
					m_head = m_head->m_next;
					if (m_head != nullptr) m_head->m_prev = nullptr;
					unlock();
					return;
				}
				if (node->m_prev != nullptr) {
					node->m_prev->m_next = node->m_next;
					if (node->m_next != nullptr) node->m_next->m_prev = node->m_prev;
					unlock();
					return;
				}

				unlock();

				if (!(m_requester == std::this_thread::get_id())) {
					node->m_done.acquire();
					return;
				}

				if (node->m_destroyed != nullptr) *node->m_destroyed = true;
			}

		private:
			void lock() noexcept {
				auto old = m_value.load(std::memory_order::relaxed);
				while (!try_lock(old, 0, std::memory_order::acquire, std::memory_order::relaxed)) {}
			}

			void unlock() noexcept { m_value.fetch_sub(mask_locked_bit, std::memory_order::release); }

			bool try_lock(value_type& curval, value_type newbits, std::memory_order success,
						  std::memory_order failure) noexcept {
				if ((curval & mask_locked_bit) != 0) {
					yield();
					curval = m_value.load(failure);
					return false;
				}
				newbits |= mask_locked_bit;
				return m_value.compare_exchange_weak(curval, curval | newbits, success, failure);
			}
		};

		struct stop_state_ref {
			stop_state_ref() = default;

			explicit stop_state_ref([[maybe_unused]] const stop_source& source) : m_ptr(new stop_state_t()) {}

			stop_state_ref(const stop_state_ref& other) noexcept : m_ptr(other.m_ptr) {
				if (m_ptr != nullptr) m_ptr->add_owner();
			}

			stop_state_ref(stop_state_ref&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

			stop_state_ref& operator=(const stop_state_ref& other) noexcept {
				if(&other == this) return *this;
				if (auto* ptr = other.m_ptr; ptr != m_ptr) {
					if (ptr != nullptr) ptr->add_owner();
					if (m_ptr != nullptr) m_ptr->release_ownership();
					m_ptr = ptr;
				}
				return *this;
			}

			stop_state_ref& operator=(stop_state_ref&& other) noexcept {
				stop_state_ref(std::move(other)).swap(*this);
				return *this;
			}

			~stop_state_ref() {
				if (m_ptr != nullptr) m_ptr->release_ownership();
			}

			void swap(stop_state_ref& other) noexcept { std::swap(m_ptr, other.m_ptr); }

			explicit operator bool() const noexcept { return m_ptr != nullptr; }

			stop_state_t* operator->() const noexcept { return m_ptr; }

#if __cpp_impl_three_way_comparison >= 201907L
			friend bool operator==(const stop_state_ref&, const stop_state_ref&) = default;
#else
			friend bool operator==(const stop_state_ref& lhs, const stop_state_ref& rhs) noexcept {
				return lhs.m_ptr == rhs.m_ptr;
			}

			friend bool operator!=(const stop_state_ref& lhs, const stop_state_ref& rhs) noexcept {
				return lhs.m_ptr != rhs.m_ptr;
			}
#endif

		private:
			stop_state_t* m_ptr = nullptr;
		};

		stop_state_ref m_state;

		// NOLINTNEXTLINE(modernize-pass-by-value)
		explicit stop_token(const stop_state_ref& state) noexcept : m_state{state} {}
	};

	/// A type that allows a stop request to be made.
	class stop_source {
	public:
		stop_source() : m_state(*this) {}

		explicit stop_source([[maybe_unused]] nostopstate_t tag) noexcept {}

		stop_source(const stop_source& other) noexcept : m_state(other.m_state) {
			if (m_state) m_state->add_ssrc();
		}

		stop_source(stop_source&&) noexcept = default;

		stop_source& operator=(const stop_source& other) noexcept {
			if(&other == this) return *this;
			if (m_state != other.m_state) {
				stop_source sink(std::move(*this));
				m_state = other.m_state;
				if (m_state) m_state->add_ssrc();
			}
			return *this;
		}

		stop_source& operator=(stop_source&&) noexcept = default;

		~stop_source() {
			if (m_state) m_state->sub_ssrc();
		}

		[[nodiscard]] bool stop_possible() const noexcept { return static_cast<bool>(m_state); }

		[[nodiscard]] bool stop_requested() const noexcept {
			return static_cast<bool>(m_state) && m_state->stop_requested();
		}

		[[nodiscard]] bool request_stop() const noexcept {
			if (stop_possible()) return m_state->request_stop();
			return false;
		}

		[[nodiscard]] stop_token get_token() const noexcept { return stop_token{m_state}; }

		void swap(stop_source& other) noexcept { m_state.swap(other.m_state); }

		[[nodiscard]] friend bool operator==(const stop_source& lhs, const stop_source& rhs) noexcept {
			return lhs.m_state == rhs.m_state;
		}

		friend void swap(stop_source& lhs, stop_source& rhs) noexcept { lhs.swap(rhs); }

	private:
		stop_token::stop_state_ref m_state;
	};

	/// A wrapper for callbacks to be run when a stop request is made.
	template<typename Callback>
	class [[nodiscard]] stop_callback {
		static_assert(std::is_nothrow_destructible_v<Callback>);
		static_assert(std::is_invocable_v<Callback>);

	public:
		using callback_type = Callback;

		template<typename Cb>
			requires(std::is_constructible_v<Callback, Cb>)
		explicit stop_callback(const stop_token& token, Cb&& callback) noexcept(std::is_nothrow_constructible_v<Callback, Cb>)
			: m_cb(std::forward<Cb>(callback)) {
			if (auto state = token.m_state) {
				if (state->register_callback(&m_cb)) m_state.swap(state);
			}
		}

		template<typename Cb>
			requires(std::is_constructible_v<Callback, Cb>)
		explicit stop_callback(stop_token&& token, Cb&& callback) noexcept(std::is_nothrow_constructible_v<Callback, Cb>)
			: m_cb(std::forward<Cb>(callback)) {
			if (auto& state = token.m_state) {
				if (state->register_callback(&m_cb)) m_state.swap(state);
			}
		}

		~stop_callback() {
			if (m_state) { m_state->remove_callback(&m_cb); }
		}

		stop_callback(const stop_callback&) = delete;
		stop_callback& operator=(const stop_callback&) = delete;
		stop_callback(stop_callback&&) = delete;
		stop_callback& operator=(stop_callback&&) = delete;

	private:
		struct cb_impl : stop_token::stop_cb_node_t {
			template<typename Cb>
			// NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
			explicit cb_impl(Cb&& callback) : stop_cb_node_t(&execute), m_cb(std::forward<Cb>(callback)) {}

			Callback m_cb;

			static void execute(stop_cb_node_t* that) noexcept {
				Callback& callback = static_cast<cb_impl*>(that)->m_cb;
				std::forward<Callback>(callback)();
			}
		};

		cb_impl m_cb;
		stop_token::stop_state_ref m_state;
	};

	template<typename Callback>
	stop_callback(stop_token, Callback) -> stop_callback<Callback>;

#else
	using stop_source = std::stop_source;
	using stop_token = std::stop_token;
	template<typename Callback>
	using stop_callback = std::stop_callback<Callback>;
	using nostopstate_t = std::nostopstate_t;
	inline constexpr nostopstate_t nostopstate{};
#endif
} // namespace asyncpp

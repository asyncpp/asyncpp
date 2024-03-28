#pragma once
#include <asyncpp/detail/std_import.h>
#include <asyncpp/ref.h>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace asyncpp {

	template<typename TResult>
	class promise;

	template<typename T>
	struct is_promise : std::false_type {};
	template<typename T>
	struct is_promise<promise<T>> : std::true_type {};

	/**
     * \brief Promise type that allows waiting for a result in both synchronous and asynchronous code.
     * \tparam TResult Type of the result
     */
	template<typename TResult>
	class promise {
		struct state : intrusive_refcount<state> {
			std::mutex m_mtx{};
			std::condition_variable m_cv{};
			std::variant<std::monostate, TResult, std::exception_ptr> m_value{};
			std::vector<std::function<void(TResult*, std::exception_ptr)>> m_on_result{};
		};
		ref<state> m_state{};

		friend class promise<void>;

		struct awaiter {
			constexpr explicit awaiter(ref<state> state) : m_state(std::move(state)) {}
			constexpr bool await_ready() noexcept {
				assert(this->m_state);
				std::unique_lock lck{this->m_state->m_mtx};
				return !std::holds_alternative<std::monostate>(this->m_state->m_value);
			}
			bool await_suspend(coroutine_handle<void> h) noexcept {
				assert(this->m_state);
				assert(h);
				std::unique_lock lck{this->m_state->m_mtx};
				if (std::holds_alternative<std::monostate>(this->m_state->m_value)) {
					this->m_state->m_on_result.emplace_back([h](TResult*, std::exception_ptr) mutable { h.resume(); });
					return true;
				} else
					return false;
			}
			TResult& await_resume() {
				assert(this->m_state);
				std::unique_lock lck{this->m_state->m_mtx};
				assert(!std::holds_alternative<std::monostate>(this->m_state->m_value));
				if (std::holds_alternative<TResult>(this->m_state->m_value))
					return std::get<TResult>(this->m_state->m_value);
				else
					std::rethrow_exception(std::get<std::exception_ptr>(this->m_state->m_value));
			}

		private:
			ref<state> m_state;
		};

	public:
		using result_type = TResult;

		/// \brief Construct a new promise object in its pending state
		promise() : m_state{make_ref<state>()} {}
		/// \brief Copy constructor
		promise(const promise& other) : m_state{other.m_state} {}
		/// \brief Copy assignment
		promise& operator=(const promise& other) {
			m_state = other.m_state;
			return *this;
		}

		/**
         * \brief Check if the promise is pending
         * \note This is a temporary snapshot and should only be used for logging. Consider the returned value potentially invalid by the moment the call returns.
         */
		bool is_pending() const noexcept {
			std::unique_lock lck{m_state->m_mtx};
			return std::holds_alternative<std::monostate>(m_state->m_value);
		}

		/**
         * \brief Check if the promise is fulfilled
         * \note Unlike is_pending() this value is permanent.
         * \return true if the promise contains a value.
         */
		bool is_fulfilled() const noexcept {
			std::unique_lock lck{m_state->m_mtx};
			return std::holds_alternative<TResult>(m_state->m_value);
		}

		/**
         * \brief Check if the promise is rejected
         * \note Unlike is_pending() this value is permanent.
         * \return true if the promise contains an exception.
         */
		bool is_rejected() const noexcept {
			std::unique_lock lck{m_state->m_mtx};
			return std::holds_alternative<std::exception_ptr>(m_state->m_value);
		}

		/**
         * \brief Fulfill the promise with a value.
         * \throw std::logic_error if the promise is already fulfilled or rejected.
         * \param value The value to store inside the promise
         * \note Callbacks and waiting coroutines are resumed inside this call.
         */
		void fulfill(TResult&& value) {
			if (!try_fulfill(std::move(value))) throw std::logic_error("promise is not pending");
		}

		/**
         * \brief Try to fulfill the promise with a value.
         * \return True if the promise was fulfilled, false if the promise was not pending
         * \param value The value to store inside the promise
         * \note Callbacks and waiting coroutines are resumed inside this call.
         */
		bool try_fulfill(TResult&& value) {
			std::unique_lock lck{m_state->m_mtx};
			if (!std::holds_alternative<std::monostate>(m_state->m_value)) return false;
			m_state->m_value.template emplace<TResult>(std::move(value));
			m_state->m_cv.notify_all();
			auto callbacks = std::move(m_state->m_on_result);
			auto& res = std::get<TResult>(m_state->m_value);
			lck.unlock();
			for (auto& e : callbacks) {
				if (e) {
					try {
						e(&res, nullptr);
					} catch (...) { std::terminate(); }
				}
			}
			return true;
		}

		/**
         * \brief Reject the promise with an exception
         * \throw std::logic_error if the promise is already fulfilled or rejected
         * \param ex The exception to use for rejection
         * \note Callbacks and waiting coroutines are resumed inside this call
         */
		void reject(std::exception_ptr e) {
			if (!try_reject(e)) throw std::logic_error("promise is not pending");
		}

		/**
         * \brief Try to reject the promise with an exception
         * \return True if the promise was rejected, false if the promise was not pending
         * \param ex The exception to use for rejection
         * \note Callbacks and waiting coroutines are resumed inside this call
         */
		bool try_reject(std::exception_ptr e) {
			std::unique_lock lck{m_state->m_mtx};
			if (!std::holds_alternative<std::monostate>(m_state->m_value)) return false;
			m_state->m_value.template emplace<std::exception_ptr>(std::move(e));
			m_state->m_cv.notify_all();
			auto callbacks = std::move(m_state->m_on_result);
			auto ex = std::get<std::exception_ptr>(m_state->m_value);
			lck.unlock();
			for (auto& e : callbacks) {
				if (e) {
					try {
						e(nullptr, ex);
					} catch (...) { std::terminate(); }
				}
			}
			return true;
		}

		/**
         * \brief Reject the promise with an exception
         * \throw std::logic_error if the promise is already fulfilled or rejected
         * \tparam TException The exception type to use for rejection
         * \param args Arguments passed to the constructor of the exception type
         * \note Callbacks and waiting coroutines are resumed inside this call
         */
		template<typename TException, typename... Args>
		void reject(Args&&... args) {
			reject(std::make_exception_ptr(TException{args...}));
		}

		/**
         * \brief Try to reject the promise with an exception
         * \return True if the promise was rejected, false if the promise was not pending
         * \tparam TException The exception type to use for rejection
         * \param args Arguments passed to the constructor of the exception type
         * \note Callbacks and waiting coroutines are resumed inside this call
         */
		template<typename TException, typename... Args>
		bool try_reject(Args&&... args) {
			return try_reject(std::make_exception_ptr(TException{args...}));
		}

		/**
         * \brief Register a callback to be executed once a result is available. If the promise is not pending the callback is directly executed.
         * \param cb Callback to invoke as soon as a result is available.
         */
		void on_result(std::function<void(TResult*, std::exception_ptr)> cb) {
			if (!cb) return;
			state& s = *m_state;
			std::unique_lock lck{s.m_mtx};
			if (std::holds_alternative<std::monostate>(s.m_value)) {
				s.m_on_result.emplace_back(std::move(cb));
			} else if (std::holds_alternative<TResult>(s.m_value)) {
				lck.unlock();
				cb(&std::get<TResult>(s.m_value), nullptr);
			} else {
				lck.unlock();
				cb(nullptr, std::get<std::exception_ptr>(s.m_value));
			}
		}

		/**
         * \brief Synchronously get the result. If the promise is rejected the rejecting exception gets thrown.
         * \return TResult& Reference to the result value
         */
		TResult& get() const {
			state& s = *m_state;
			std::unique_lock lck{s.m_mtx};
			while (std::holds_alternative<std::monostate>(s.m_value)) {
				s.m_cv.wait(lck);
			}
			if (std::holds_alternative<TResult>(s.m_value))
				return std::get<TResult>(s.m_value);
			else
				std::rethrow_exception(std::get<std::exception_ptr>(s.m_value));
		}

		/**
         * \brief Synchronously get the result with a timeout. If the promise is rejected the rejecting exception gets thrown.
         * \return TResult* Pointer to the result value or nullptr on timeout
         */
		template<class Rep, class Period>
		TResult* get(std::chrono::duration<Rep, Period> timeout) const {
			state& s = *m_state;
			std::unique_lock lck{s.m_mtx};
			if (std::holds_alternative<std::monostate>(s.m_value)) s.m_cv.wait_for(lck, timeout);
			if (std::holds_alternative<std::monostate>(s.m_value)) return nullptr;
			if (std::holds_alternative<TResult>(s.m_value))
				return &std::get<TResult>(s.m_value);
			else
				std::rethrow_exception(std::get<std::exception_ptr>(s.m_value));
		}

		/**
         * \brief Synchronously try get the result. If the promise is rejected the rejecting exception gets thrown.
         * \return TResult* Pointer to the result value or nullptr on timeout
         */
		std::pair<TResult*, std::exception_ptr> try_get(std::nothrow_t) const noexcept {
			state& s = *m_state;
			std::unique_lock lck{s.m_mtx};
			if (std::holds_alternative<std::monostate>(s.m_value)) return {nullptr, nullptr};
			if (std::holds_alternative<TResult>(s.m_value))
				return {&std::get<TResult>(s.m_value), nullptr};
			else
				return {nullptr, std::get<std::exception_ptr>(s.m_value)};
		}

		/**
         * \brief Synchronously try get the result. If the promise is rejected the rejecting exception gets thrown.
         * \return TResult* Pointer to the result value or nullptr on timeout
         */
		TResult* try_get() const {
			auto res = try_get(std::nothrow);
			if (res.second) std::rethrow_exception(std::move(res.second));
			return res.first;
		}

		/**
         * \brief Asynchronously get the result. If the promise is rejected the rejecting exception gets thrown.
         * \return TResult& Pointer to the result value
         */
		auto operator co_await() const noexcept {
			assert(this->m_state);
			return awaiter{m_state};
		}

		/**
		* \brief Resolve this promise once the first of the provided promises is resolved.
		*		  This can be used to start multiple operations and continue once the
		*		  the first one is ready. The results of the remaining promises
		*		  are ignored.
		* \tparam T The type of the promises
		* \param args The promises to wait for
		*/
		template<typename... T>
		void first(promise<T>... args) {
			(args.on_result([p = *this](typename promise<T>::result_type* res, std::exception_ptr ex) mutable {
				if (res)
					p.try_fulfill(std::move(*res));
				else
					p.try_reject(ex);
			}),
			 ...);
		}

		/**
		* \brief Resolve this promise once the first promise is resolved without and exception.
		*		  This can be used to start multiple operations
		*		  and continue once the the first successful is ready. If none of the
		*         promises gets fulfilled the exception of the last failed promise is returned.
		*		  The results of the remaining promises are ignored.
		* \tparam T The type of the promises
		* \param args The promises to wait for
		*/
		template<typename... T>
		void first_successful(promise<T>... args) {
			auto finished = std::make_shared<size_t>(0);
			(args.on_result(
				 [p = *this, finished](typename promise<T>::result_type* res, std::exception_ptr ex) mutable {
					 std::unique_lock lck{p.m_state->m_mtx};
					 (*finished)++;
					 if (!std::holds_alternative<std::monostate>(p.m_state->m_value)) return;
					 if (res) {
						 p.m_state->m_value.template emplace<TResult>(std::move(*res));
						 p.m_state->m_cv.notify_all();
						 auto callbacks = std::move(p.m_state->m_on_result);
						 auto& res = std::get<TResult>(p.m_state->m_value);
						 lck.unlock();
						 for (auto& e : callbacks) {
							 if (e) {
								 try {
									 e(&res, nullptr);
								 } catch (...) { std::terminate(); }
							 }
						 }
					 } else if (*finished == sizeof...(args)) {
						 p.m_state->m_value.template emplace<std::exception_ptr>(std::move(ex));
						 p.m_state->m_cv.notify_all();
						 auto callbacks = std::move(p.m_state->m_on_result);
						 auto ex = std::get<std::exception_ptr>(p.m_state->m_value);
						 lck.unlock();
						 for (auto& e : callbacks) {
							 if (e) {
								 try {
									 e(nullptr, ex);
								 } catch (...) { std::terminate(); }
							 }
						 }
					 }
				 }),
			 ...);
		}

		/**
         * \brief Get a fufilled promise with the specified value
         * \param value Value for the fulfilled promise
         * \return A promise in its fulfilled state
         */
		static promise make_fulfilled(TResult&& value) {
			promise res;
			res.fulfill(std::move(value));
			return res;
		}

		/**
         * \brief Get a rejected promise with the specified exception
         * \param ex Exception to store in the rejected promise
         * \return A promise in its rejected state
         */
		static promise make_rejected(std::exception_ptr ex) {
			promise res;
			res.reject(ex);
			return res;
		}

		/**
         * \brief Get a rejected promise with the specified exception
         * \tparam TException The type of the exception to store
         * \param args Parameters to pass to the exception type constructor
         * \return A promise in its rejected state
         */
		template<typename TException, typename... Args>
		static promise make_rejected(Args&&... args) {
			promise res;
			res.reject<TException, Args...>(std::forward<Args>(args)...);
			return res;
		}

		/**
		* \brief Return a promise that is fulfilled/rejected once the first of
		*		  the specified promises is fulfilled or rejected. This can be
		*		  used to start multiple operations and continue once the
		*		  the first one is ready. The results of the remaining promises
		*		  are ignored.
		* \tparam T The type of the promises
		* \param args The promises to wait for
		* \return A promise that copies the state of the first finished argument.
		*/
		template<typename... T>
		static promise make_first(promise<T>... args) {
			promise p;
			p.first(args...);
			return p;
		}

		/**
		* \brief Return a promise that is fulfilled once the first of the specified
		*		  promises is fulfilled. This can be used to start multiple operations
		*		  and continue once the the first successful is ready. If none of the
		*         promises gets fulfilled the exception of the last failed promise is returned.
		*		  The results of the remaining promises are ignored.
		* \tparam T The type of the promises
		* \param args The promises to wait for
		* \return A promise that copies the state of the first successful argument.
		*/
		template<typename... T>
		static promise make_first_successful(promise<T>... args) {
			promise p;
			p.first_successful(args...);
			return p;
		}

		/**
		 * \brief Promise type to allow using promise<T> as the return value for a coroutine.
		 * \note Promise is fairly heavy weight and as a result should not be used as a general purpose
		 *		 return type for coroutines. Instead its intended purpose is for usage on the border to
		 *       synchronous code.
		 */
		class promise_type;
	};

	/**
     * \brief Promise type that allows waiting for a result in both synchronous and asynchronous code.
     * \tparam TResult Type of the result
     */
	template<>
	class promise<void> {
		struct void_result {};
		promise<void_result> m_inner;

	public:
		using result_type = void;

		promise() = default;
		promise(const promise& other) = default;
		promise& operator=(const promise& other) = default;
		bool is_pending() const noexcept { return m_inner.is_pending(); }
		bool is_fulfilled() const noexcept { return m_inner.is_fulfilled(); }
		bool is_rejected() const noexcept { return m_inner.is_rejected(); }
		void fulfill() { m_inner.fulfill({}); }
		bool try_fulfill() { return m_inner.try_fulfill({}); }
		void reject(std::exception_ptr e) { m_inner.reject(std::move(e)); }
		bool try_reject(std::exception_ptr e) { return m_inner.try_reject(std::move(e)); }
		template<typename TException, typename... Args>
		void reject(Args&&... args) {
			m_inner.reject<TException>(std::forward<Args>(args)...);
		}
		template<typename TException, typename... Args>
		bool try_reject(Args&&... args) {
			return m_inner.try_reject<TException>(std::forward<Args>(args)...);
		}
		void on_result(std::function<void(std::exception_ptr)> cb) {
			if (!cb) return;
			m_inner.on_result([cb = std::move(cb)](void_result*, std::exception_ptr ex) { cb(std::move(ex)); });
		}
		void on_result(std::function<void(result_type*, std::exception_ptr)> cb) {
			if (!cb) return;
			m_inner.on_result(
				[cb = std::move(cb)](void_result*, std::exception_ptr ex) { cb(nullptr, std::move(ex)); });
		}
		void get() const { m_inner.get(); }
		template<class Rep, class Period>
		bool get(std::chrono::duration<Rep, Period> timeout) const {
			return m_inner.get(timeout) != nullptr;
		}
		std::pair<bool, std::exception_ptr> try_get(std::nothrow_t) const noexcept {
			auto [f, s] = m_inner.try_get(std::nothrow);
			return {f != nullptr, std::move(s)};
		}
		bool try_get() const {
			auto res = try_get(std::nothrow);
			if (res.second) std::rethrow_exception(std::move(res.second));
			return res.first;
		}
		auto operator co_await() const noexcept {
			struct awaiter {
				explicit awaiter(ref<promise<void_result>::state> p) : m_inner(std::move(p)) {}
				bool await_ready() noexcept { return m_inner.await_ready(); }
				bool await_suspend(coroutine_handle<void> h) noexcept { return m_inner.await_suspend(std::move(h)); }
				void await_resume() { m_inner.await_resume(); }

			private:
				promise<void_result>::awaiter m_inner;
			};
			assert(this->m_inner.m_state);
			return awaiter{this->m_inner.m_state};
		}
		template<typename... T>
		void first(promise<T>... args) {
			return m_inner.first(args...);
		}
		template<typename... T>
		void first_successful(promise<T>... args) {
			return m_inner.first_successful(args...);
		}
		template<typename... T>
		void all(promise<T>... args) {
			struct state {
				state(promise p) noexcept : result(std::move(p)) {}
				promise result;
				std::atomic<size_t> count{};
			};
			auto s = std::make_shared<state>(*this);
			(args.on_result([s](typename promise<T>::result_type* res, std::exception_ptr ex) mutable {
				auto curid = s->count.fetch_add(1);
				if (curid + 1 == sizeof...(args)) { s->result.fulfill(); }
			}),
			 ...);
		}

		static promise make_fulfilled() {
			promise res;
			res.fulfill();
			return res;
		}
		static promise make_rejected(std::exception_ptr ex) {
			promise res;
			res.reject(ex);
			return res;
		}
		template<typename TException, typename... Args>
		static promise make_rejected(Args&&... args) {
			promise res;
			res.reject<TException, Args...>(std::forward<Args>(args)...);
			return res;
		}

		template<typename... T>
		static promise make_first(promise<T>... args) {
			promise p;
			p.first(args...);
			return p;
		}
		template<typename... T>
		static promise make_first_successful(promise<T>... args) {
			promise p;
			p.first_successful(args...);
			return p;
		}
		template<typename... T>
		static promise make_all(promise<T>... args) {
			promise p;
			p.all(args...);
			return p;
		}

		/**
		 * \brief Promise type to allow using promise<T> as the return value for a coroutine.
		 * \note Promise is fairly heavy weight and as a result should not be used as a general purpose
		 *		 return type for coroutines. Instead its intended purpose is for usage on the border to
		 *       synchronous code.
		 */
		class promise_type;
	};

	template<typename T>
	class promise<T>::promise_type {
		promise m_promise;

	public:
		promise get_return_object() { return m_promise; }
		suspend_never initial_suspend() noexcept { return {}; }
		suspend_never final_suspend() noexcept { return {}; }
		template<class U>
		void return_value(U&& value)
			requires(std::is_convertible_v<U, T>)
		{
			this->m_promise.fulfill(std::forward<U>(value));
		}
		template<class U>
		void return_value(U const& value)
			requires(!std::is_reference_v<U>)
		{
			this->m_promise.fulfill(value);
		}
		void unhandled_exception() { this->m_promise.reject(std::current_exception()); }
	};

	class promise<void>::promise_type {
		promise m_promise;

	public:
		promise get_return_object() { return m_promise; }
		suspend_never initial_suspend() noexcept { return {}; }
		suspend_never final_suspend() noexcept { return {}; }
		void return_void() { this->m_promise.fulfill(); }
		void unhandled_exception() { this->m_promise.reject(std::current_exception()); }
	};
} // namespace asyncpp

#pragma once
#include <asyncpp/detail/std_import.h>
#include <asyncpp/ref.h>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <variant>
#include <vector>

namespace asyncpp {

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

	public:
		using result_type = TResult;

		/// \brief Construct a new promise object in its pending state
		promise() : m_state{make_ref<state>()} {}
		/// \brief Copy constructor
		promise(const promise& other) : m_state{other.m_state} {}
		/// \brief Copy assignment
		promise& operator=(const promise& other) {
			if (&other != this) m_state = other.m_state;
			return *this;
		}

		/**
         * \brief Check if the promise is pending
         * \note This is a temporary snapshot and should only be used for logging. Consider the returned value potentially invalid by the moment the call returns.
         */
		[[nodiscard]] bool is_pending() const noexcept {
			std::unique_lock lck{m_state->m_mtx};
			return std::holds_alternative<std::monostate>(m_state->m_value);
		}

		/**
         * \brief Check if the promise is fulfilled
         * \note Unlike is_pending() this value is permanent.
         * \return true if the promise contains a value.
         */
		[[nodiscard]] bool is_fulfilled() const noexcept {
			std::unique_lock lck{m_state->m_mtx};
			return std::holds_alternative<TResult>(m_state->m_value);
		}

		/**
         * \brief Check if the promise is rejected
         * \note Unlike is_pending() this value is permanent.
         * \return true if the promise contains an exception.
         */
		[[nodiscard]] bool is_rejected() const noexcept {
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
			auto& res = m_state->m_value.template emplace<TResult>(std::move(value));
			m_state->m_cv.notify_all();
			auto callbacks = std::move(m_state->m_on_result);
			lck.unlock();
			for (auto& callback : callbacks) {
				if (callback) {
					try {
						callback(&res, nullptr);
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
		void reject(std::exception_ptr exception) {
			if (!try_reject(exception)) throw std::logic_error("promise is not pending");
		}

		/**
         * \brief Try to reject the promise with an exception
         * \return True if the promise was rejected, false if the promise was not pending
         * \param ex The exception to use for rejection
         * \note Callbacks and waiting coroutines are resumed inside this call
         */
		bool try_reject(std::exception_ptr exception) {
			std::unique_lock lck{m_state->m_mtx};
			if (!std::holds_alternative<std::monostate>(m_state->m_value)) return false;
			m_state->m_value.template emplace<std::exception_ptr>(exception);
			m_state->m_cv.notify_all();
			auto callbacks = std::move(m_state->m_on_result);
			lck.unlock();
			for (auto& callback : callbacks) {
				if (callback) {
					try {
						callback(nullptr, exception);
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
		void on_result(std::function<void(TResult*, std::exception_ptr)> callback) {
			auto& state = *m_state;
			std::unique_lock lck{state.m_mtx};
			if (std::holds_alternative<std::monostate>(state.m_value)) {
				state.m_on_result.emplace_back(std::move(callback));
			} else if (std::holds_alternative<TResult>(state.m_value)) {
				lck.unlock();
				callback(&std::get<TResult>(state.m_value), nullptr);
			} else {
				lck.unlock();
				callback(nullptr, std::get<std::exception_ptr>(state.m_value));
			}
		}

		/**
         * \brief Synchronously get the result. If the promise is rejected the rejecting exception gets thrown.
         * \return TResult& Reference to the result value
         */
		TResult& get() const {
			auto& state = *m_state;
			std::unique_lock lck{state.m_mtx};
			while (std::holds_alternative<std::monostate>(state.m_value)) {
				state.m_cv.wait(lck);
			}
			if (std::holds_alternative<TResult>(state.m_value)) return std::get<TResult>(state.m_value);
			std::rethrow_exception(std::get<std::exception_ptr>(state.m_value));
		}

		/**
         * \brief Synchronously get the result with a timeout. If the promise is rejected the rejecting exception gets thrown.
         * \return TResult* Pointer to the result value or nullptr on timeout
         */
		template<class Rep, class Period>
		TResult* get(std::chrono::duration<Rep, Period> timeout) const {
			auto& state = *m_state;
			std::unique_lock lck{state.m_mtx};
			if (std::holds_alternative<std::monostate>(state.m_value)) state.m_cv.wait_for(lck, timeout);
			if (std::holds_alternative<std::monostate>(state.m_value)) return nullptr;
			if (std::holds_alternative<TResult>(state.m_value)) return &std::get<TResult>(state.m_value);
			std::rethrow_exception(std::get<std::exception_ptr>(state.m_value));
		}

		/**
         * \brief Synchronously try get the result. If the promise is rejected the rejecting exception gets thrown.
         * \return TResult* Pointer to the result value or nullptr on timeout
         */
		std::pair<TResult*, std::exception_ptr> try_get([[maybe_unused]] std::nothrow_t tag) const noexcept {
			auto& state = *m_state;
			std::unique_lock lck{state.m_mtx};
			if (std::holds_alternative<std::monostate>(state.m_value)) return {nullptr, nullptr};
			if (std::holds_alternative<TResult>(state.m_value)) return {&std::get<TResult>(state.m_value), nullptr};
			return {nullptr, std::get<std::exception_ptr>(state.m_value)};
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
			struct awaiter {
				constexpr explicit awaiter(ref<state> state) : m_state(std::move(state)) {}
				constexpr bool await_ready() noexcept {
					assert(this->m_state);
					std::unique_lock lck{this->m_state->m_mtx};
					return !std::holds_alternative<std::monostate>(this->m_state->m_value);
				}
				bool await_suspend(coroutine_handle<void> hdl) noexcept {
					assert(this->m_state);
					assert(hdl);
					std::unique_lock lck{this->m_state->m_mtx};
					if (std::holds_alternative<std::monostate>(this->m_state->m_value)) {
						this->m_state->m_on_result.emplace_back(
							// NOLINTNEXTLINE(performance-unnecessary-value-param)
							[hdl](TResult*, std::exception_ptr) mutable { hdl.resume(); });
						return true;
					}
					return false;
				}
				TResult& await_resume() {
					assert(this->m_state);
					std::unique_lock lck{this->m_state->m_mtx};
					assert(!std::holds_alternative<std::monostate>(this->m_state->m_value));
					if (std::holds_alternative<TResult>(this->m_state->m_value))
						return std::get<TResult>(this->m_state->m_value);
					std::rethrow_exception(std::get<std::exception_ptr>(this->m_state->m_value));
				}

			private:
				ref<state> m_state;
			};
			assert(this->m_state);
			return awaiter{m_state};
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
		static promise make_rejected(std::exception_ptr exception) {
			promise res;
			res.reject(exception);
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
		static promise first(promise<T>... args) {
			promise first;
			(args.on_result([first](typename promise<T>::result_type* res, std::exception_ptr exception) mutable {
				if (res)
					first.try_fulfill(std::move(*res));
				else
					first.try_reject(exception);
			}),
			 ...);
			return first;
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
		static promise first_successful(promise<T>... args) {
			constexpr size_t total = sizeof...(args);
			promise first;
			auto finished = std::make_shared<size_t>(0);
			(args.on_result(
				 [first, total, finished](typename promise<T>::result_type* res, std::exception_ptr exception) mutable {
					 std::unique_lock lck{first.m_state->m_mtx};
					 (*finished)++;
					 if (!std::holds_alternative<std::monostate>(first.m_state->m_value)) return;
					 if (res) {
						 first.m_state->m_value.template emplace<TResult>(std::move(*res));
						 first.m_state->m_cv.notify_all();
						 auto callbacks = std::move(first.m_state->m_on_result);
						 auto& res = std::get<TResult>(first.m_state->m_value);
						 lck.unlock();
						 for (auto& callback : callbacks) {
							 if (callback) {
								 try {
									 callback(&res, nullptr);
								 } catch (...) { std::terminate(); }
							 }
						 }
					 } else if (*finished == total) {
						 first.m_state->m_value.template emplace<std::exception_ptr>(exception);
						 first.m_state->m_cv.notify_all();
						 auto callbacks = std::move(first.m_state->m_on_result);
						 lck.unlock();
						 for (auto& callback : callbacks) {
							 if (callback) {
								 try {
									 callback(nullptr, exception);
								 } catch (...) { std::terminate(); }
							 }
						 }
					 }
				 }),
			 ...);
			return first;
		}

		/**
		* \brief Return a promise that is fulfilled once all the specified promises are fulfilled.
		*		  This can be used to start multiple operations in parallel and collect the results.
		* \tparam T The type of the promises
		* \param args The promises to wait for
		* \return A promise that gets fulfilled with a vector of all promises in
		* 			the order they are passed once all are finished.
		*/
		static promise<std::vector<promise<TResult>>> all(std::vector<promise<TResult>> args) {
			struct all_state {
				size_t total{};
				std::atomic<size_t> count{};
				std::vector<promise<TResult>> promises;
				promise<std::vector<promise<TResult>>> result;
			};
			auto state = std::make_shared<all_state>();
			state->total = args.size();
			state->promises = std::move(args);
			for (auto& promise : state->promises) {
				promise.on_result(
					// NOLINTNEXTLINE(performance-unnecessary-value-param)
					[state]([[maybe_unused]] TResult* res, [[maybe_unused]] std::exception_ptr exception) mutable {
						auto curid = state->count.fetch_add(1);
						if (curid + 1 == state->total) { state->result.fulfill(std::move(state->promises)); }
					});
			}
			return state->result;
		}

		/**
		* \brief Return a promise that is fulfilled with the value of the promises once all the specified
		*         promises are fulfilled. This can be used to start multiple operations in parallel and
		*		  collect the results. Unlike all, the function rejects if any of the promises reject.
		* \tparam T The type of the promises
		* \param args The promises to wait for
		* \return A promise that gets fulfilled with a vector of all values in the order of finishing.
		*/
		static promise<std::vector<TResult>> all_values(std::vector<promise<TResult>> args) {
			struct all_state {
				std::mutex mtx{};
				size_t total{};
				size_t count{};
				std::vector<TResult> results;
				promise<std::vector<TResult>> done;
			};
			auto state = std::make_shared<all_state>();
			state->total = args.size();
			state->results.reserve(args.size());
			for (auto& promise : args) {
				promise.on_result([state](TResult* res, std::exception_ptr exception) mutable {
					std::unique_lock lck{state->mtx};
					state->count++;
					if (!state->done.is_pending()) return;
					if (exception) {
						state->done.reject(exception);
					} else {
						state->results.push_back(*res);
						if (state->count == state->total) state->done.fulfill(std::move(state->results));
					}
				});
			}
			return state->done;
		}
	};
} // namespace asyncpp

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
						this->m_state->m_on_result.emplace_back(
							[h](TResult*, std::exception_ptr) mutable { h.resume(); });
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
		static promise first(promise<T>... args) {
			promise p;
			(args.on_result([p](typename promise<T>::result_type* res, std::exception_ptr ex) mutable {
				if (res)
					p.try_fulfill(std::move(*res));
				else
					p.try_reject(ex);
			}),
			 ...);
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
		static promise first_successful(promise<T>... args) {
			constexpr size_t total = sizeof...(args);
			promise p;
			auto finished = std::make_shared<size_t>(0);
			(args.on_result([p, total, finished](typename promise<T>::result_type* res, std::exception_ptr ex) mutable {
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
				} else if (*finished == total) {
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
			return p;
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
			struct state {
				size_t total{};
				std::atomic<size_t> count{};
				std::vector<promise<TResult>> promises;
				promise<std::vector<promise<TResult>>> result;
			};
			auto s = std::make_shared<state>();
			s->total = args.size();
			s->promises = std::move(args);
			for (auto& e : s->promises) {
				e.on_result([s](TResult* res, std::exception_ptr ex) mutable {
					auto curid = s->count.fetch_add(1);
					if (curid + 1 == s->total) { s->result.fulfill(std::move(s->promises)); }
				});
			}
			return s->result;
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
			struct state {
				std::mutex mtx{};
				size_t total{};
				size_t count{};
				std::vector<TResult> results;
				promise<std::vector<TResult>> done;
			};
			auto s = std::make_shared<state>();
			s->total = args.size();
			s->results.reserve(args.size());
			for (auto& e : args) {
				e.on_result([s](TResult* res, std::exception_ptr ex) mutable {
					std::unique_lock lck{s->mtx};
					s->count++;
					if (!s->done.is_pending()) return;
					if (ex) {
						s->done.reject(ex);
					} else {
						s->results.push_back(*res);
						if (s->count == s->total) s->done.fulfill(std::move(s->results));
					}
				});
			}
			return s->done;
		}
	};
} // namespace asyncpp

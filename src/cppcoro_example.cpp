// cxx-async/src/example.cpp

#include "cxx_async.h"
#include "cxx-async/src/main.rs.h"
#include "rust/cxx.h"
#include <cppcoro/schedule_on.hpp>
#include <cppcoro/static_thread_pool.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <cppcoro/when_all.hpp>
#include <experimental/coroutine>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#define SPLIT_LIMIT     32
#define ARRAY_SIZE      16384

void rust_resume_cxx_coroutine(uint8_t *coroutine_address) {
    std::experimental::coroutine_handle<void>::from_address(
        static_cast<void *>(coroutine_address)).resume();
}

void rust_destroy_cxx_coroutine(uint8_t *coroutine_address) {
    if (coroutine_address != nullptr) {
        std::experimental::coroutine_handle<void>::from_address(
            static_cast<void *>(coroutine_address)).destroy();
    }
}

class RustAsyncError : public std::exception {
    std::string m_what;

public:
    RustAsyncError(std::string &&what) : m_what(std::move(what)) {}

    const char *what() const noexcept {
        return m_what.c_str();
    }
};

// Given a channel type, fetches the receiver type.
template<typename Channel>
using RustOneshotReceiverFor =
    typename decltype(static_cast<Channel *>(nullptr)->receiver)::element_type;

// Given a channel type, fetches the sender type.
template<typename Channel>
using RustOneshotSenderFor =
    typename decltype(static_cast<Channel *>(nullptr)->sender)::element_type;

// Given a receiver type, fetches the channel type.
template<typename Receiver>
using RustOneshotChannelFor = decltype(static_cast<Receiver *>(nullptr)->channel());

// Given a channel type, fetches the result type.
//
// This extracts the type of the `value` parameter from the `send` method using the technique
// described here: https://stackoverflow.com/a/28033314
template<typename Fn>
struct RustOneshotGetResultTypeFromSendFn;
template<typename Sender, typename TheResult, typename TheError>
struct RustOneshotGetResultTypeFromSendFn<void (Sender::*)(const TheResult *, TheError) noexcept> {
    typedef TheResult Result;
};

template<typename Channel>
using RustOneshotResultFor = typename
    RustOneshotGetResultTypeFromSendFn<decltype(&RustOneshotSenderFor<Channel>::send)>::Result;

template<typename Channel>
struct RustOneshotChannelTraits {};

template<typename Channel>
class RustOneshotAwaiter {
    typedef RustOneshotReceiverFor<Channel> Receiver;
    typedef RustOneshotResultFor<Channel> Result;

    enum class RecvResult {
        Pending = 0,
        Ready = 1,
        Error = 2,
    };

    // Tries to receive a value. If a value is ready, this method places it in `m_result` and
    // returns true. If no value is ready, this method returns false.
    //
    // If `next` is supplied, this method ensures that it will be called when a value becomes
    // ready. If the value is available right now, then this method calls `next` immediately.
    RecvResult try_recv(std::optional<std::experimental::coroutine_handle<void>> next =
            std::optional<std::experimental::coroutine_handle<void>>()) noexcept {
        // Require destructor to be called manually.
        union MaybeResult {
            Result some;
        };

        uint8_t *coroutine_address = nullptr;
        if (next)
            coroutine_address = reinterpret_cast<uint8_t *>(next->address());

        MaybeResult maybe_result;
        std::string error;
        auto ready = static_cast<RecvResult>(m_receiver->recv(
            &maybe_result.some, error, coroutine_address));
        switch (ready) {
        case RecvResult::Ready:
            m_result = std::move(maybe_result.some);
            maybe_result.some.~Result();
            if (next)
                (*next)();
            break;
        case RecvResult::Error:
            m_error = RustAsyncError(std::move(error));
            if (next)
                (*next)();
            break;
        case RecvResult::Pending:
            break;
        }
        return ready;
    }

public:
    RustOneshotAwaiter(rust::Box<Receiver> &&receiver) :
        m_receiver(std::move(receiver)), m_result() {}

    bool await_ready() noexcept {
        if (m_result || m_error)
            return true;
        RecvResult ready = try_recv();
        return ready == RecvResult::Ready || ready == RecvResult::Error;
    }

    void await_suspend(std::experimental::coroutine_handle<void> next) {
        try_recv(std::move(next));
    }

    Result await_resume() {
        // One of these will be present if `await_ready` returned true.
        if (m_result)
            return *std::move(m_result);
        if (m_error)
            throw *std::move(m_error);

        switch (try_recv()) {
        case RecvResult::Ready:     return *std::move(m_result);
        case RecvResult::Error:     throw *std::move(m_error);
        case RecvResult::Pending:   break;
        }
        std::terminate();
    }

    rust::Box<Receiver> m_receiver;
    std::optional<Result> m_result;
    std::optional<RustAsyncError> m_error;
};

template<typename Receiver>
auto operator co_await(rust::Box<Receiver> &&receiver) noexcept {
    return RustOneshotAwaiter<RustOneshotChannelFor<Receiver>>(std::move(receiver));
}

template<typename Receiver>
struct cppcoro::awaitable_traits<rust::Box<Receiver> &&> {
    typedef RustOneshotResultFor<RustOneshotChannelFor<Receiver>> await_result_t;
};

template<typename Channel>
class RustOneshotPromise {
public:
    RustOneshotPromise() :
        m_channel(static_cast<RustOneshotReceiverFor<Channel> *>(nullptr)->channel()) {}

    rust::Box<RustOneshotReceiverFor<Channel>> get_return_object() noexcept {
        return std::move(m_channel.receiver);
    }

    std::experimental::suspend_never initial_suspend() const noexcept { return {}; }
    std::experimental::suspend_never final_suspend() const noexcept { return {}; }

    void return_value(const RustOneshotResultFor<Channel> &value) {
        m_channel.sender->send(&value, rust::Str());
    }

    void unhandled_exception() noexcept {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception &exception) {
            m_channel.sender->send(nullptr, rust::Str(exception.what()));
        } catch (...) {
            m_channel.sender->send(nullptr, rust::Str("Unhandled C++ exception"));
        }
    }

    Channel m_channel;
};

template<typename Receiver, typename... Args>
struct std::experimental::coroutine_traits<rust::Box<Receiver>, Args...> {
    typedef decltype(static_cast<Receiver *>(nullptr)->channel()) Channel;
    using promise_type = RustOneshotPromise<Channel>;
};

// Application code follows:

template<>
struct RustOneshotChannelTraits<RustOneshotChannelF64> {};

cppcoro::task<double> dot_product_inner(cppcoro::static_thread_pool &thread_pool,
                                        double a[],
                                        double b[],
                                        size_t count) {
    if (count > SPLIT_LIMIT) {
        size_t half_count = count / 2;
        auto [first, second] = co_await cppcoro::when_all(
            cppcoro::schedule_on(thread_pool, dot_product_inner(thread_pool, a, b, half_count)),
            dot_product_inner(thread_pool, a + half_count, b + half_count, count - half_count));
        co_return first + second;
    }

    double sum = 0.0;
    for (size_t i = 0; i < count; i++)
        sum += a[i] * b[i];
    co_return sum;
}

cppcoro::task<double> dot_product_on(cppcoro::static_thread_pool &thread_pool) {
    std::uniform_real_distribution<double> distribution(-10000.0, 10000.0);
    std::default_random_engine random_engine;

    std::vector<double> array_a, array_b;
    for (size_t i = 0; i < ARRAY_SIZE; i++) {
        array_a.push_back(distribution(random_engine));
        array_b.push_back(distribution(random_engine));
    }

    co_return co_await dot_product_inner(thread_pool, &array_a[0], &array_b[0], array_a.size());
}

rust::Box<RustOneshotReceiverF64> dot_product() {
    static cppcoro::static_thread_pool thread_pool;
    co_return co_await cppcoro::schedule_on(thread_pool, dot_product_on(thread_pool));
}

void call_rust_dot_product() {
    rust::Box<RustOneshotReceiverF64> oneshot_receiver = rust_dot_product();
    double result = cppcoro::sync_wait(std::move(oneshot_receiver));
    std::cout << result << std::endl;
}

rust::Box<RustOneshotReceiverF64> not_product() {
    if (true)
        throw std::runtime_error("kaboom");
    co_return 1.0;
}

void call_rust_not_product() {
    try {
        rust::Box<RustOneshotReceiverF64> oneshot_receiver = rust_not_product();
        double result = cppcoro::sync_wait(std::move(oneshot_receiver));
        std::cout << result << std::endl;
    } catch (const std::exception &error) {
        std::cout << error.what() << std::endl;
    }
}

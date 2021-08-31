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
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
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
template<typename Sender, typename TheResult>
struct RustOneshotGetResultTypeFromSendFn<void (Sender::*)(TheResult) noexcept> {
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

    // Tries to receive a value. If a value is ready, this method places it in `m_result` and
    // returns true. If no value is ready, this method returns false.
    //
    // If `next` is supplied, this method ensures that it will be called when a value becomes
    // ready. If the value is available right now, then this method calls `next` immediately.
    bool try_recv(std::optional<std::experimental::coroutine_handle<void>> next =
            std::optional<std::experimental::coroutine_handle<void>>()) noexcept {
        // Require destructor to be called manually.
        union MaybeResult {
            Result some;
        };

        uint8_t *coroutine_address = nullptr;
        if (next)
            coroutine_address = reinterpret_cast<uint8_t *>(next->address());

        MaybeResult maybe_result;
        bool ready = m_receiver->recv(&maybe_result.some, coroutine_address);
        if (!ready)
            return false;

        m_result = std::move(maybe_result.some);
        maybe_result.some.~Result();
        if (next)
            (*next)();
        return true;
    }

public:
    RustOneshotAwaiter(rust::Box<Receiver> &&receiver) :
        m_receiver(std::move(receiver)), m_result() {}

    bool await_ready() noexcept {
        return m_result || try_recv();
    }

    void await_suspend(std::experimental::coroutine_handle<void> next) {
        try_recv(std::move(next));
    }

    Result await_resume() {
        bool ready = try_recv();
        assert(ready);
        return *std::move(m_result);
    }

    rust::Box<Receiver> m_receiver;
    std::optional<Result> m_result;
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

    auto get_return_object() noexcept {
        return std::move(m_channel.receiver);
    }

    std::experimental::suspend_never initial_suspend() const noexcept { return {}; }
    std::experimental::suspend_never final_suspend() const noexcept { return {}; }

    void return_value(const RustOneshotResultFor<Channel> &value) {
        m_channel.sender->send(value);
    }

    void unhandled_exception() noexcept { /* TODO */ }

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

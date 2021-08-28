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

// FIXME(pcwalton): This should use a traits struct.
template<typename Oneshot, typename Result>
struct RustOneshotPromise {
    // Require destructor to be called manually.
    union MaybeResult {
        Result some;
    };

public:
    RustOneshotPromise() : m_oneshot(static_cast<Oneshot *>(nullptr)->make()) {}

    rust::Box<Oneshot> get_return_object() noexcept {
        return m_oneshot->clone_box();
    }

    std::experimental::suspend_never initial_suspend() const noexcept { return {}; }
    std::experimental::suspend_never final_suspend() const noexcept { return {}; }

    void return_value(const Result &value) { m_oneshot->send(value); }
    void unhandled_exception() noexcept { /* TODO */ }

    struct Awaiter {
    public:
        Awaiter(rust::Box<Oneshot> &&oneshot) : m_oneshot(std::move(oneshot)), m_result() {}

        bool await_ready() noexcept {
            // Already have the result?
            if (m_result)
                return true;

            // Try to receive the result.
            MaybeResult maybe_result;
            bool ready = m_oneshot->try_recv(&maybe_result.some);
            if (!ready)
                return false;
            m_result = std::move(maybe_result.some);
            maybe_result.some.~Result();
            return true;
        }

        void await_suspend(std::experimental::coroutine_handle<void> next) {
            MaybeResult maybe_result;
            bool ready = m_oneshot->poll_with_coroutine_handle(
                &maybe_result.some,
                reinterpret_cast<uint8_t *>(next.address()));
            if (!ready) {
                std::cout << "going to sleep!" << std::endl;
                return;
            }
            m_result = std::move(maybe_result.some);
            maybe_result.some.~Result();
            next();
        }

        Result await_resume() {
            // Kinda hacky...
            await_ready();
            return *m_result;
        }

    private:
        rust::Box<Oneshot> m_oneshot;
        std::optional<Result> m_result;
    };

private:
    rust::Box<Oneshot> m_oneshot;
};

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

RustOneshotPromise<RustOneshotF64, double>::Awaiter operator co_await(
        rust::Box<RustOneshotF64> &&oneshot) noexcept {
    return RustOneshotPromise<RustOneshotF64, double>::Awaiter(std::move(oneshot));
}

template<typename... Args>
struct std::experimental::coroutine_traits<rust::Box<RustOneshotI32>, Args...> {
    using promise_type = RustOneshotPromise<RustOneshotI32, int32_t>;
};

template<typename... Args>
struct std::experimental::coroutine_traits<rust::Box<RustOneshotF64>, Args...> {
    using promise_type = RustOneshotPromise<RustOneshotF64, double>;
};

template<>
struct cppcoro::awaitable_traits<rust::Box<RustOneshotF64> &&> {
//struct cppcoro::awaitable_traits<RustOneshotPromise<RustOneshotF64, double>::Awaiter &> {
    typedef double await_result_t;
};

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

rust::Box<RustOneshotF64> dot_product() {
    static cppcoro::static_thread_pool thread_pool;
    co_return co_await cppcoro::schedule_on(thread_pool, dot_product_on(thread_pool));
}

void call_rust_dot_product() {
    rust::Box<RustOneshotF64> oneshot = rust_dot_product();
    double result = cppcoro::sync_wait(std::move(oneshot));
    std::cout << result << std::endl;
}

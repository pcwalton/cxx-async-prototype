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
#include <random>
#include <thread>
#include <type_traits>
#include <vector>

#define SPLIT_LIMIT     32
#define ARRAY_SIZE      16384

template<typename Oneshot, typename Result>
struct RustOneshotPromise {
public:
    RustOneshotPromise() : m_oneshot(static_cast<Oneshot *>(nullptr)->make()) {}

    rust::Box<Oneshot> get_return_object() noexcept {
        return m_oneshot->clone_box();
    }

    std::experimental::suspend_never initial_suspend() const noexcept { return {}; }
    std::experimental::suspend_never final_suspend() const noexcept { return {}; }

    void return_value(const Result &value) {
        m_oneshot->set_value(value);
    }

    void unhandled_exception() noexcept {
        // TODO
    }

private:
    rust::Box<Oneshot> m_oneshot;
};

template<typename... Args>
struct std::experimental::coroutine_traits<rust::Box<RustOneshotI32>, Args...> {
    using promise_type = RustOneshotPromise<RustOneshotI32, int32_t>;
};

template<typename... Args>
struct std::experimental::coroutine_traits<rust::Box<RustOneshotF64>, Args...> {
    using promise_type = RustOneshotPromise<RustOneshotF64, double>;
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

#ifdef MINITASK
// Task implementation
template<typename T>
struct task {
    struct promise_type {
        T m_result;
        std::experimental::coroutine_handle<> m_prev;

        task get_return_object() noexcept {
            return {std::experimental::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::experimental::suspend_never initial_suspend() const noexcept { return {}; }

        auto final_suspend() const noexcept {
            struct awaiter {
                bool await_ready() const noexcept { return false; }
                void await_resume() const noexcept {}
                std::experimental::coroutine_handle<> await_suspend(
                        std::experimental::coroutine_handle<promise_type> coro) noexcept {
                    auto prev = coro.promise().m_prev;
                    if (prev)
                        return prev;
                    return std::experimental::noop_coroutine();
                }
            };
            return awaiter {};
        }

        void return_value(T value) noexcept { m_result = std::move(value); }

        void unhandled_exception() noexcept {
            // TODO
        }
    };

    std::experimental::coroutine_handle<promise_type> m_handle;

    bool await_ready() const noexcept { return m_handle.done(); }
    T await_resume() const noexcept { return std::move(m_handle.promise().m_result); }
    void await_suspend(std::experimental::coroutine_handle<> prev) const noexcept {
        m_handle.promise().m_prev = prev;
    }
};
#endif

#if 0
struct RustReceiverAwaiter {
private:
    rust::Box<RustReceiverI32> m_receiver;
public:
    RustReceiverAwaiter(rust::Box<RustReceiverI32> &&receiver) : m_receiver(std::move(receiver)) {}
    bool await_ready() const noexcept {
        return false;
    }
    void await_suspend(std::experimental::coroutine_handle<> continuation) const {
        // TODO
    }
    int32_t await_resume() {
        // TODO
    }
};
#endif

#if 0
// Allow co_await'ing std::future<T> and std::future<void>
// by naively spawning a new thread for each co_await.
template <typename T>
auto operator co_await(std::future<T> future) noexcept
requires(!std::is_reference_v<T>) {
  struct awaiter : std::future<T> {
    bool await_ready() const noexcept {
      using namespace std::chrono_literals;
      return this->wait_for(0s) != std::future_status::timeout;
    }
    void await_suspend(std::experimental::coroutine_handle<> cont) const {
      std::thread([this, cont] {
        std::experimental::coroutine_handle<> my_cont = std::move(cont);
        this->wait();
        my_cont();
      }).detach();
    }
    T await_resume() { return this->get(); }
  };
  return awaiter{std::move(future)};
}
#endif

cppcoro::task<int32_t> return_six() noexcept { co_return 6; }
cppcoro::task<int32_t> return_seven() noexcept { co_return 7; }

rust::Box<RustOneshotI32> my_async_operation() {
    int a = co_await return_six();
    int b = co_await return_seven();
    co_return a * b;
}

#if 0
RustReceiverAwaiter operator co_await(rust::Box<RustReceiverI32> &&receiver) noexcept {
    return RustReceiverAwaiter(std::move(receiver));
}

rust::Box<RustReceiverI32> just_return(int32_t value) {
    RustChannelI32 channel = make_channel();
    channel.sender->set_value(value);
    return std::move(channel.receiver);
}
#endif

/*
void sleeper(rust::Box<RustSenderI32> sender) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "waking up 0" << std::endl;
    sender->set_value(1);
}

rust::Box<RustReceiverI32> my_async_operation() {
    RustChannelI32 channel = make_channel();
    std::thread thread(sleeper, std::move(channel.sender));
    thread.detach();
    rust::Box<RustReceiverI32> future = std::move(channel.receiver);
    int32_t result = co_await future;
    co_return result;
}
*/

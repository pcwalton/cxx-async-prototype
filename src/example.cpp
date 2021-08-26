// cxx-async/src/example.cpp

#include "cxx_async.h"
#include "cxx-async/src/main.rs.h"
#include "rust/cxx.h"
#include <experimental/coroutine>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <type_traits>

template<typename... Args>
struct std::experimental::coroutine_traits<rust::Box<RustOneshotI32>, Args...> {
    struct promise_type : std::promise<int32_t> {
        promise_type() : m_oneshot(make_oneshot_i32()) {}

        rust::Box<RustOneshotI32> get_return_object() noexcept {
            return m_oneshot->clone_box();
        }

        suspend_never initial_suspend() const noexcept { return {}; }
        suspend_never final_suspend() const noexcept { return {}; }

        void return_value(const int32_t &value) {
            m_oneshot->set_value(value);
        }

        void unhandled_exception() noexcept {
            // TODO
        }

    private:
        rust::Box<RustOneshotI32> m_oneshot;
    };
};

rust::Box<RustOneshotI32> my_async_operation() {
    int a = co_await std::async([] { return 6; });
    int b = co_await std::async([] { return 7; });
    co_return a * b;
}

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

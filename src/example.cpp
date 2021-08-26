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
struct std::experimental::coroutine_traits<rust::Box<RustReceiverI32>, Args...> {
    struct promise_type : std::promise<int32_t> {
        rust::Box<RustReceiverI32> get_return_object() noexcept {
            // TODO
        }

        suspend_never initial_suspend() const noexcept { return {}; }
        suspend_never final_suspend() const noexcept { return {}; }

        void return_value(const int32_t &value) {
            // TODO
        }

        void unhandled_exception() noexcept {
            // TODO
        }
    };
};

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

RustReceiverAwaiter operator co_await(rust::Box<RustReceiverI32> &&receiver) noexcept {
    return RustReceiverAwaiter(std::move(receiver));
}

rust::Box<RustReceiverI32> just_return(int32_t value) {
    RustChannelI32 channel = make_channel();
    channel.sender->set_value(value);
    return std::move(channel.receiver);
}

rust::Box<RustReceiverI32> my_async_operation() {
    int a = co_await just_return(6);
    int b = co_await just_return(7);
    co_return a * b;
}

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

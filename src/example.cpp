// cxx-async/src/example.cpp

#include "cxx_async.h"
#include "cxx-async/src/main.rs.h"
#include "rust/cxx.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

#if 0
void sleeper(std::shared_ptr<CxxFutureImpl<int32_t>> future) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "waking up" << std::endl;
    future->set_value(1);
}

CxxFutureI32 my_async_operation() {
    CxxFutureI32 future { .m_impl = std::make_shared<CxxFutureImplI32>(CxxFutureImpl<int32_t>()) };
    std::thread thread(sleeper, future.m_impl);
    thread.detach();
    return future;
}
#endif

void sleeper(rust::Box<RustSenderI32> sender) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "waking up" << std::endl;
    sender->set_value(1);
}

rust::Box<RustReceiverI32> my_async_operation() {
    RustChannelI32 channel = make_channel();
    std::thread thread(sleeper, std::move(channel.sender));
    thread.detach();
    return std::move(channel.receiver);
}

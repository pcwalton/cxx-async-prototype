// cxx-async/src/example.cpp

#include "cxx-async/src/main.rs.h"
#include "cxx_async.h"
#include "cxx_async_cppcoro.h"
#include "example_common.h"
#include "rust/cxx.h"
#include <cassert>
#include <chrono>
#include <cppcoro/schedule_on.hpp>
#include <cppcoro/static_thread_pool.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <cppcoro/when_all.hpp>
#include <cstdint>
#include <exception>
#include <experimental/coroutine>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

template <> struct RustOneshotChannelTraits<RustOneshotChannelF64> {};

static cppcoro::task<double>
dot_product_inner(cppcoro::static_thread_pool &thread_pool, double a[],
                  double b[], size_t count) {
  if (count > EXAMPLE_SPLIT_LIMIT) {
    size_t half_count = count / 2;
    auto [first, second] = co_await cppcoro::when_all(
        cppcoro::schedule_on(thread_pool,
                             dot_product_inner(thread_pool, a, b, half_count)),
        dot_product_inner(thread_pool, a + half_count, b + half_count,
                          count - half_count));
    co_return first + second;
  }

  double sum = 0.0;
  for (size_t i = 0; i < count; i++)
    sum += a[i] * b[i];
  co_return sum;
}

static cppcoro::task<double>
dot_product_on(cppcoro::static_thread_pool &thread_pool) {
  Xorshift rand;
  std::vector<double> array_a, array_b;
  for (size_t i = 0; i < EXAMPLE_ARRAY_SIZE; i++) {
    array_a.push_back((double)rand.next());
    array_b.push_back((double)rand.next());
  }

  co_return co_await dot_product_inner(thread_pool, &array_a[0], &array_b[0],
                                       array_a.size());
}

rust::Box<RustOneshotReceiverF64> cppcoro_dot_product() {
  static cppcoro::static_thread_pool thread_pool;
  co_return co_await cppcoro::schedule_on(thread_pool,
                                          dot_product_on(thread_pool));
}

void cppcoro_call_rust_dot_product() {
  rust::Box<RustOneshotReceiverF64> oneshot_receiver = rust_dot_product();
  double result = cppcoro::sync_wait(std::move(oneshot_receiver));
  std::cout << result << std::endl;
}

rust::Box<RustOneshotReceiverF64> cppcoro_not_product() {
  if (true)
    throw std::runtime_error("kaboom");
  co_return 1.0; // just to make this function a coroutine
}

void cppcoro_call_rust_not_product() {
  try {
    rust::Box<RustOneshotReceiverF64> oneshot_receiver = rust_not_product();
    double result = cppcoro::sync_wait(std::move(oneshot_receiver));
    std::cout << result << std::endl;
  } catch (const std::exception &error) {
    std::cout << error.what() << std::endl;
  }
}

rust::Box<RustOneshotReceiverString>
cppcoro_ping_pong(int i) {
  std::string string(co_await rust_cppcoro_ping_pong(i + 1));
  string += "pong ";
  co_return std::move(string);
}

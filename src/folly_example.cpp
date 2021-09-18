// cxx-async/src/folly_example.cpp

#define FOLLY_HAS_COROUTINES 1

#include "cxx-async/src/main.rs.h"
#include "cxx_async.h"
#include "example_common.h"
#include "rust/cxx.h"
#include <iostream>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Task.h>
#include <folly/tracing/AsyncStack.h>

// FIXME(pcwalton): This definition is only needed to make the Folly Homebrew
// package link, I think. Detect that.
namespace folly {

FOLLY_NOINLINE void
resumeCoroutineWithNewAsyncStackRoot(coro::coroutine_handle<> h,
                                     folly::AsyncStackFrame &frame) noexcept {
  detail::ScopedAsyncStackRoot root;
  root.activateFrame(frame);
  h.resume();
}

} // namespace folly

static folly::coro::Task<double> dot_product_inner(
    folly::Executor::KeepAlive<folly::CPUThreadPoolExecutor> &thread_pool,
    double a[], double b[], size_t count) {
  if (count > EXAMPLE_SPLIT_LIMIT) {
    size_t half_count = count / 2;
    folly::Future<double> taskA =
        dot_product_inner(thread_pool, a, b, half_count)
            .semi()
            .via(thread_pool);
    folly::Future<double> taskB =
        dot_product_inner(thread_pool, a + half_count, b + half_count,
                          count - half_count)
            .semi()
            .via(thread_pool);
    auto [first, second] =
        co_await folly::collectAll(std::move(taskA), std::move(taskB));
    co_return *first + *second;
  }

  double sum = 0.0;
  for (size_t i = 0; i < count; i++)
    sum += a[i] * b[i];
  co_return sum;
}

static folly::coro::Task<double> dot_product_on(
    folly::Executor::KeepAlive<folly::CPUThreadPoolExecutor> &thread_pool) {
  Xorshift rand;
  std::vector<double> array_a, array_b;
  for (size_t i = 0; i < EXAMPLE_ARRAY_SIZE; i++) {
    array_a.push_back((double)rand.next());
    array_b.push_back((double)rand.next());
  }

  co_return co_await dot_product_inner(thread_pool, &array_a[0], &array_b[0],
                                       array_a.size());
}

rust::Box<RustOneshotReceiverF64> folly_dot_product() {
  static folly::Executor::KeepAlive<folly::CPUThreadPoolExecutor> thread_pool(
      new folly::CPUThreadPoolExecutor(8));
  co_return co_await dot_product_on(thread_pool).semi().via(thread_pool);
}

void folly_call_rust_dot_product() {
  rust::Box<RustOneshotReceiverF64> oneshot_receiver = rust_dot_product();
  double result = folly::coro::blockingWait(std::move(oneshot_receiver));
  std::cout << result << std::endl;
}

rust::Box<RustOneshotReceiverF64> folly_not_product() {
  if (true)
    throw std::runtime_error("kaboom");
  co_return 1.0; // just to make this function a coroutine
}

void folly_call_rust_not_product() {
  try {
    rust::Box<RustOneshotReceiverF64> oneshot_receiver = rust_not_product();
    double result = folly::coro::blockingWait(std::move(oneshot_receiver));
    std::cout << result << std::endl;
  } catch (const std::exception &error) {
    std::cout << error.what() << std::endl;
  }
}

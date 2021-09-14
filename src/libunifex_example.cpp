// cxx-async/src/libunifex_example.cpp

#include "cxx-async/src/main.rs.h"
#include "cxx_async.h"
#include "example_common.h"
#include "rust/cxx.h"
#include <unifex/config.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/static_thread_pool.hpp>
#include <unifex/task.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>

using UnifexThreadPoolScheduler =
    decltype(unifex::static_thread_pool().get_scheduler());

static unifex::task<double>
dot_product_inner(UnifexThreadPoolScheduler &scheduler, double a[], double b[],
                  size_t count) {
  if (count > EXAMPLE_SPLIT_LIMIT) {
    size_t half_count = count / 2;
    auto taskA = [&]() -> unifex::task<double> {
      co_await unifex::schedule(scheduler);
      co_return co_await dot_product_inner(scheduler, a, b, half_count);
    };
    auto taskB = [&]() -> unifex::task<double> {
      co_return co_await dot_product_inner(scheduler, a + half_count,
                                           b + half_count, count - half_count);
    };
    auto results = co_await unifex::when_all(taskA(), taskB());
    double a = std::get<0>(std::get<0>(std::get<0>(results)));
    double b = std::get<0>(std::get<0>(std::get<1>(results)));
    co_return a + b;
  }

  double sum = 0.0;
  for (size_t i = 0; i < count; i++)
    sum += a[i] * b[i];
  co_return sum;
}

static unifex::task<double>
dot_product_on(UnifexThreadPoolScheduler &scheduler) {
  Xorshift rand;
  std::vector<double> array_a, array_b;
  for (size_t i = 0; i < EXAMPLE_ARRAY_SIZE; i++) {
    array_a.push_back((double)rand.next());
    array_b.push_back((double)rand.next());
  }

  co_return co_await dot_product_inner(scheduler, &array_a[0], &array_b[0],
                                       array_a.size());
}

rust::Box<RustOneshotReceiverF64> libunifex_dot_product() {
  static unifex::static_thread_pool thread_pool;
  UnifexThreadPoolScheduler scheduler = thread_pool.get_scheduler();
  co_await unifex::schedule(scheduler);
  co_return co_await dot_product_on(scheduler);
}

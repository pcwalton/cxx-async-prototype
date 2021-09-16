// cxx-async/src/libunifex_example.cpp

#include "cxx-async/src/main.rs.h"
#include "cxx_async.h"
#include "example_common.h"
#include "rust/cxx.h"
#include <functional>
#include <iostream>
#include <unifex/config.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/static_thread_pool.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>

template <typename Channel, typename UnifexReceiver> class RustOperation {
  typedef RustOneshotReceiverFor<Channel> RustReceiver;

  class Continuation {
  public:
    Continuation(RustOperation &operation) : m_operation(operation) {}

    void *address() const noexcept { return &m_operation; }
    void operator()() {
      m_operation.callback();
    }

  private:
    RustOperation &m_operation;
  };

  friend class Continuation;

  void callback() {
    unifex::set_value(*std::move(m_unifex_receiver), std::move(m_awaiter.await_resume()));
    m_unifex_receiver.reset();
  }

public:
  void start() noexcept {
    try {
      m_awaiter.try_recv(std::optional(Continuation(*this)));
    } catch (...) {
      // TODO
    }
  }

  RustOperation(rust::Box<RustReceiver> &&rust_receiver,
                UnifexReceiver &&unifex_receiver)
      : m_awaiter(RustOneshotAwaiter<Channel>(std::move(rust_receiver))),
        m_unifex_receiver(std::move(unifex_receiver)) {}

private:
  RustOperation(const RustOperation &) = delete;
  void operator=(const RustOperation &) = delete;

  RustOneshotAwaiter<Channel> m_awaiter;
  std::optional<UnifexReceiver> m_unifex_receiver;
};

template <typename Receiver, typename UnifexReceiver>
RustOperation<RustOneshotChannelFor<Receiver>, UnifexReceiver>
tag_invoke(unifex::tag_t<unifex::connect>, rust::Box<Receiver> &&rust_receiver,
           UnifexReceiver &&unifex_receiver) {
  return RustOperation<RustOneshotChannelFor<Receiver>, UnifexReceiver>(
      std::move(rust_receiver), std::move(unifex_receiver));
}

// Rust Receivers are actually unifex senders. Unfortunate naming!
template <typename Receiver> struct unifex::sender_traits<rust::Box<Receiver>> {
  template <template <typename...> class Variant,
            template <typename...> class Tuple>
  using value_types =
      Variant<Tuple<RustOneshotResultFor<RustOneshotChannelFor<Receiver>>>>;
  // FIXME(pcwalton): Is this right?
  template <template <typename...> class Variant> using error_types = Variant<>;
  // TODO(pcwalton): Actually fulfill this contract by calling `set_done`.
  static constexpr bool sends_done = true;
};

#if 0
template<typename RustReceiver, typename UnifexReceiver>
void operator()(rust::Box<RustReceiver> &&rust_receiver, UnifexReceiver &&unifex_receiver) {
  // TODO
}
#endif

#if 0
class Blah {
public:
  Blah(rust::Box<RustOneshotReceiverF64> other) {}
  template<typename Receiver>
  void connect(Receiver &&r) {}
};

template<typename F, typename R>
void std::invoke(F &&f, rust::Box<RustOneshotReceiverF64> r, R r2) noexcept {

}
#endif

// Application code follows:

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

// rust::Box<RustOneshotReceiverF64> libunifex_dot_product() {
rust::Box<RustOneshotReceiverF64> libunifex_dot_product() {
  static unifex::static_thread_pool thread_pool;
  UnifexThreadPoolScheduler scheduler = thread_pool.get_scheduler();
  co_await unifex::schedule(scheduler);
  co_return co_await dot_product_on(scheduler);
}

void libunifex_call_rust_dot_product() {
  // static unifex::static_thread_pool thread_pool;
  // UnifexThreadPoolScheduler scheduler = thread_pool.get_scheduler();
  // auto oneshot_receiver = dot_product_on(scheduler);
  // RustReceiver<RustOneshotChannelF64> oneshot_receiver = rust_dot_product();
  rust::Box<RustOneshotReceiverF64> oneshot_receiver = rust_dot_product();
  double result = *unifex::sync_wait(std::move(oneshot_receiver));
  std::cout << result << std::endl;
}

rust::Box<RustOneshotReceiverF64> libunifex_not_product() {
  if (true)
    throw std::runtime_error("kaboom");
  co_return 1.0; // just to make this function a coroutine
}

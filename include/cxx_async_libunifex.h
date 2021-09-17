// cxx-async/include/cxx_async_libunifex.h

#ifndef CXX_ASYNC_CXX_ASYNC_LIBUNIFEX_H
#define CXX_ASYNC_CXX_ASYNC_LIBUNIFEX_H

#include "cxx_async.h"

template <typename Channel, typename UnifexReceiver> class RustOperation {
  typedef RustOneshotReceiverFor<Channel> RustReceiver;
  typedef RustOneshotResultFor<Channel> Result;

  class Task {
  public:
    class promise_type {
    public:
      Task get_return_object() noexcept { return {}; }
      std::experimental::suspend_never initial_suspend() const noexcept {
        return {};
      }
      std::experimental::suspend_never final_suspend() const noexcept {
        return {};
      }
      void unhandled_exception() noexcept {
        // Should never be called, since we catch exceptions in `make_task()` below.
        std::terminate();
      }
      void return_void() noexcept {}
    };
  };

  Task make_task() noexcept {
    try {
      unifex::set_value(std::move(m_unifex_receiver),
                        co_await std::move(m_rust_receiver));
    } catch (...) {
      unifex::set_error(std::move(m_unifex_receiver), std::current_exception());
    }
    co_return;
  }

  RustOperation(const RustOperation &) = delete;
  void operator=(const RustOperation &) = delete;

  rust::Box<RustReceiver> m_rust_receiver;
  UnifexReceiver m_unifex_receiver;

public:
  void start() noexcept { make_task(); }

  RustOperation(rust::Box<RustReceiver> &&rust_receiver,
                UnifexReceiver &&unifex_receiver)
      : m_rust_receiver(std::move(rust_receiver)),
        m_unifex_receiver(std::move(unifex_receiver)) {}
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
  template <template <typename...> class Variant> using error_types = Variant<>;
  static constexpr bool sends_done = true;
};


#endif


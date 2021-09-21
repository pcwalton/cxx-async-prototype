// cxx-async/include/cxx_async.h

#ifndef CXX_ASYNC_CXX_ASYNC_H
#define CXX_ASYNC_CXX_ASYNC_H

#include "rust/cxx.h"
#include <cstdint>
#include <experimental/coroutine>
#include <optional>
#include <unifex/await_transform.hpp>

void rust_resume_cxx_coroutine(uint8_t *coroutine_address);
void rust_destroy_cxx_coroutine(uint8_t *coroutine_address);

// Forward declare a libunifex interoperability class so that we can friend it.
template <typename Channel, typename UnifexReceiver> class RustOperation;

class RustAsyncError : public std::exception {
  std::string m_what;

public:
  RustAsyncError(std::string &&what) : m_what(std::move(what)) {}

  const char *what() const noexcept { return m_what.c_str(); }
};

// Given a channel type, fetches the receiver type.
template <typename Channel>
using RustOneshotReceiverFor =
    typename decltype(static_cast<Channel *>(nullptr)->receiver)::element_type;

// Given a channel type, fetches the sender type.
template <typename Channel>
using RustOneshotSenderFor =
    typename decltype(static_cast<Channel *>(nullptr)->sender)::element_type;

// Given a receiver type, fetches the channel type.
template <typename Receiver>
using RustOneshotChannelFor =
    decltype(static_cast<Receiver *>(nullptr)->channel());

// Given a channel type, fetches the result type.
//
// This extracts the type of the `value` parameter from the `send` method using
// the technique described here: https://stackoverflow.com/a/28033314
template <typename Fn> struct RustOneshotGetResultTypeFromSendFn;
template <typename Sender, typename TheResult, typename TheError>
struct RustOneshotGetResultTypeFromSendFn<void (Sender::*)(const TheResult *,
                                                           TheError) noexcept> {
  typedef TheResult Result;
};

template <typename Channel>
using RustOneshotResultFor = typename RustOneshotGetResultTypeFromSendFn<
    decltype(&RustOneshotSenderFor<Channel>::send)>::Result;

template <typename Channel> struct RustOneshotChannelTraits {};

template<typename T>
union ManuallyDrop {
  T m_value;
  ManuallyDrop() {}
  ManuallyDrop(T &&value) : m_value(std::move(value)) {}
  ~ManuallyDrop() {}
};

template<typename T>
void forget(T &&value) {
  ManuallyDrop manually_drop(std::move(value));
}

template <typename Channel> class RustOneshotAwaiter {
  template <typename AnotherChannel, typename UnifexReceiver>
  friend class RustOperation;

  typedef RustOneshotReceiverFor<Channel> Receiver;
  typedef RustOneshotResultFor<Channel> Result;

  enum class RecvResult {
    Pending = 0,
    Ready = 1,
    Error = 2,
  };

  rust::Box<Receiver> m_receiver;
  std::optional<Result> m_result;
  std::optional<RustAsyncError> m_error;

  // Tries to receive a value. If a value is ready, this method places it in
  // `m_result` and returns true. If no value is ready, this method returns
  // false.
  //
  // If `next` is supplied, this method ensures that it will be called when a
  // value becomes ready. If the value is available right now, then this method
  // calls `next` immediately.
  RecvResult try_recv(
      std::optional<std::experimental::coroutine_handle<void>> next =
          std::optional<std::experimental::coroutine_handle<void>>()) noexcept {
    uint8_t *coroutine_address = nullptr;
    if (next)
      coroutine_address = reinterpret_cast<uint8_t *>(next->address());

    ManuallyDrop<Result> maybe_result;
    std::string error;
    auto ready = static_cast<RecvResult>(
        m_receiver->recv(&maybe_result.m_value, error, coroutine_address));
    switch (ready) {
    case RecvResult::Ready:
      m_result = std::move(maybe_result.m_value);
      maybe_result.m_value.~Result();
      if (next)
        (*next)();
      break;
    case RecvResult::Error:
      m_error = RustAsyncError(std::move(error));
      if (next)
        (*next)();
      break;
    case RecvResult::Pending:
      break;
    }
    return ready;
  }

public:
  RustOneshotAwaiter(rust::Box<Receiver> &&receiver)
      : m_receiver(std::move(receiver)), m_result() {}

  bool await_ready() noexcept {
    if (m_result || m_error)
      return true;
    RecvResult ready = try_recv();
    return ready == RecvResult::Ready || ready == RecvResult::Error;
  }

  void await_suspend(std::experimental::coroutine_handle<void> next) {
    try_recv(std::optional(std::move(next)));
  }

  Result await_resume() {
    // One of these will be present if `await_ready` returned true.
    if (m_result)
      return *std::move(m_result);
    if (m_error)
      throw *std::move(m_error);

    switch (try_recv()) {
    case RecvResult::Ready:
      return *std::move(m_result);
    case RecvResult::Error:
      throw *std::move(m_error);
    case RecvResult::Pending:
      break;
    }
    std::terminate();
  }
};

template <typename Receiver>
auto inline operator co_await(rust::Box<Receiver> &&receiver) noexcept {
  return RustOneshotAwaiter<RustOneshotChannelFor<Receiver>>(
      std::move(receiver));
}

template <typename Channel> class RustOneshotPromise {
  Channel m_channel;

public:
  RustOneshotPromise()
      : m_channel(static_cast<RustOneshotReceiverFor<Channel> *>(nullptr)
                      ->channel()) {}

  rust::Box<RustOneshotReceiverFor<Channel>> get_return_object() noexcept {
    return std::move(m_channel.receiver);
  }

  std::experimental::suspend_never initial_suspend() const noexcept {
    return {};
  }
  std::experimental::suspend_never final_suspend() const noexcept { return {}; }

  std::experimental::coroutine_handle<> unhandled_done() noexcept { return {}; }

  void return_value(RustOneshotResultFor<Channel> &&value) {
    m_channel.sender->send(&value, rust::Str());
    forget(std::move(value));
  }

  void unhandled_exception() noexcept {
    try {
      std::rethrow_exception(std::current_exception());
    } catch (const std::exception &exception) {
      m_channel.sender->send(nullptr, rust::Str(exception.what()));
    } catch (...) {
      m_channel.sender->send(nullptr, rust::Str("Unhandled C++ exception"));
    }
  }

  template <typename Value> auto await_transform(Value &&value) noexcept {
    return unifex::await_transform(*this, (Value &&) value);
  }
};

template <typename Receiver, typename... Args>
struct std::experimental::coroutine_traits<rust::Box<Receiver>, Args...> {
  typedef decltype(static_cast<Receiver *>(nullptr)->channel()) Channel;
  using promise_type = RustOneshotPromise<Channel>;
};

#endif

// cxx-async/include/cxx_async_cppcoro.h

#ifndef CXX_ASYNC_CXX_ASYNC_CPPCORO_H
#define CXX_ASYNC_CXX_ASYNC_CPPCORO_H

#include "cxx_async.h"
#include "rust/cxx.h"
#include <cppcoro/awaitable_traits.hpp>

// FIXME(pcwalton): Move this into a header.
template <typename Receiver>
struct cppcoro::awaitable_traits<rust::Box<Receiver> &&> {
  typedef RustOneshotResultFor<RustOneshotChannelFor<Receiver>> await_result_t;
};

#endif

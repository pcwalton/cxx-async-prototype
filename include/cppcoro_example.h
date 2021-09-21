// cxx-async/include/cppcoro_example.h

#ifndef CXX_ASYNC_CPPCORO_EXAMPLE_H
#define CXX_ASYNC_CPPCORO_EXAMPLE_H

#include "cxx_async.h"
#include "rust/cxx.h"

#define EXAMPLE_SPLIT_LIMIT     32
#define EXAMPLE_ARRAY_SIZE      16384

struct RustOneshotReceiverF64;
struct RustOneshotReceiverString;

rust::Box<RustOneshotReceiverF64> cppcoro_dot_product();
void cppcoro_call_rust_dot_product();
rust::Box<RustOneshotReceiverF64> cppcoro_not_product();
void cppcoro_call_rust_not_product();
rust::Box<RustOneshotReceiverString> cppcoro_ping_pong(int i);

#endif

// cxx-async/include/cppcoro_example.h

#ifndef CXX_ASYNC_CPPCORO_EXAMPLE_H
#define CXX_ASYNC_CPPCORO_EXAMPLE_H

#include "cxx_async.h"
#include "rust/cxx.h"

struct RustOneshotReceiverF64;

rust::Box<RustOneshotReceiverF64> dot_product();
void call_rust_dot_product();
rust::Box<RustOneshotReceiverF64> not_product();
void call_rust_not_product();
rust::String test_exception();

#endif

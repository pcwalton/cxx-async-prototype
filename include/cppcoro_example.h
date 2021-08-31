// cxx-async/include/example.h

#ifndef CXX_ASYNC_EXAMPLE_H
#define CXX_ASYNC_EXAMPLE_H

#include "cxx_async.h"
#include "rust/cxx.h"

struct RustOneshotReceiverF64;

rust::Box<RustOneshotReceiverF64> dot_product();
void call_rust_dot_product();

#endif
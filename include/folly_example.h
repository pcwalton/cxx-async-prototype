// cxx-async/include/folly_example.h

#ifndef CXX_ASYNC_FOLLY_EXAMPLE_H
#define CXX_ASYNC_FOLLY_EXAMPLE_H

#include "cxx_async.h"
#include "rust/cxx.h"

#define EXAMPLE_SPLIT_LIMIT     32
#define EXAMPLE_ARRAY_SIZE      16384

struct RustOneshotReceiverF64;

rust::Box<RustOneshotReceiverF64> folly_dot_product();
void folly_call_rust_dot_product();
rust::Box<RustOneshotReceiverF64> folly_not_product();
void folly_call_rust_not_product();

#endif

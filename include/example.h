// cxx-async/include/example.h

#ifndef CXX_ASYNC_EXAMPLE_H
#define CXX_ASYNC_EXAMPLE_H

#include "cxx_async.h"
#include "rust/cxx.h"

struct RustReceiverI32;

rust::Box<RustReceiverI32> my_async_operation();

#endif

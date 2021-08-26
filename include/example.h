// cxx-async/include/example.h

#ifndef CXX_ASYNC_EXAMPLE_H
#define CXX_ASYNC_EXAMPLE_H

#include "cxx_async.h"
#include "rust/cxx.h"

struct RustOneshotI32;

rust::Box<RustOneshotI32> my_async_operation();

#endif

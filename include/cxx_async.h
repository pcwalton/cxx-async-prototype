// cxx-async/include/cxx_async.h

#ifndef CXX_ASYNC_CXX_ASYNC_H
#define CXX_ASYNC_CXX_ASYNC_H

#include "rust/cxx.h"
#include <cstdint>

void rust_resume_cxx_coroutine(uint8_t *coroutine_address);
void rust_destroy_cxx_coroutine(uint8_t *coroutine_address);

#endif

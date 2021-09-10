#include "cxx_async.h"
#include <cstdint>

void rust_resume_cxx_coroutine(uint8_t *coroutine_address) {
    std::experimental::coroutine_handle<void>::from_address(
        static_cast<void *>(coroutine_address)).resume();
}

void rust_destroy_cxx_coroutine(uint8_t *coroutine_address) {
    if (coroutine_address != nullptr) {
        std::experimental::coroutine_handle<void>::from_address(
            static_cast<void *>(coroutine_address)).destroy();
    }
}

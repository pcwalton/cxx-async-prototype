// cxx-async/include/example_common.h

#ifndef CXX_ASYNC_EXAMPLE_COMMON_H
#define CXX_ASYNC_EXAMPLE_COMMON_H

#include <cstdint>

class Xorshift {
private:
    uint32_t m_state;
    Xorshift(Xorshift &) = delete;
    void operator=(Xorshift) = delete;
public:
    Xorshift() : m_state(0x243f6a88) {}
    uint32_t next();
};

#endif
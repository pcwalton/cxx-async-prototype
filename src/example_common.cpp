// cxx-async/src/example_common.cpp

#include "example_common.h"
#include <cstdint>

uint32_t Xorshift::next() {
    uint32_t x = m_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
    m_state = x;
	return x;
}

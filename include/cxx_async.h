// cxx-async/include/cxx_async.h

#ifndef CXX_ASYNC_CXX_ASYNC_H
#define CXX_ASYNC_CXX_ASYNC_H

#include "rust/cxx.h"
#include <cstdint>
#include <mutex>
#include <optional>

#if 0
template<typename R>
class CxxFutureImpl {
public:
    std::mutex m_lock;
    rust::Fn<void(int32_t)> m_callback;
    std::optional<R> m_result;
    bool m_resolved;

    CxxFutureImpl() : m_lock(), m_callback(nullptr), m_result(), m_callback(nullptr) {}
    CxxFutureImpl(CxxFutureImpl &&other) :
        m_lock(),
        m_callback(other.m_callback),
        m_result(other.m_state),
        m_resolved(other.m_resolved) {
        // FIXME: null out `other`?
    }

    // FIXME: Decide on a strategy for error handling.
    bool resolve(R value) noexcept;
    // FIXME: Is there a way to avoid the dependency on `rust/cxx.h` here, via a generic closure
    // parameter?
    bool then(rust::Fn<void(int32_t)> callback) noexcept;
private:
    CxxFutureImpl(const CxxFutureImpl &);
    CxxFutureImpl& operator=(const CxxFutureImpl &);
};

template<typename R>
bool
CxxFutureImpl<R>::resolve(R value) noexcept {
    m_lock.lock();

    // Mark resolved.
    if (m_resolved) {
        // FIXME: Throw exception?
        // FIXME: Caller should get `value` back, shouldn't they?
        m_lock.unlock();
        return false;
    }
    m_resolved = true;

    // Do we have a callback?
    if (m_callback != nullptr) {
        auto callback = std::move(m_callback);
        m_lock.unlock();
        m_callback(value);
        return true;
    }

    // No callback. Store result while we wait for one.
    m_result = std::move(value);
    m_lock.unlock();
    return true;
}

template<typename R>
bool
CxxFutureImpl<R>::then(rust::Fn<void(int32_t)> callback) noexcept {
    m_lock.lock();

    // If we already have a callback, fail.
    // FIXME: Throw exception?
    // FIXME: Caller should get `callback` back, shouldn't they?
    if (m_callback != nullptr) {
        m_lock.unlock();
        return false;
    }

    // Do we have a pending value?
    if (bool(m_result)) {
        R result = std::move(m_result);
        m_lock.unlock();
        (*callback)(std::move(result));
        return true;
    }

    // We're still pending.
    m_callback = std::move(callback);
    m_lock.unlock();
    return true;
}

// Explicit instantiation to work around lack of generics in `cxx`:
template class CxxFutureImpl<int32_t>;
typedef CxxFutureImpl<int32_t> CxxFutureImplI32;
#endif

#endif

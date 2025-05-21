#ifndef UTIL_LOCK_HPP
#define UTIL_LOCK_HPP
#include <util/class_field.hpp>
#include <util/backoff.hpp>
#include <atomic>
#include <algorithm>
#include "bsg_manycore_atomic.h"
namespace util
{
/**
 * @brief lock
 */
class lock
{
public:
    /**
     * @brief base constructor
     */
    lock() : locked_(0) {}

    /**
     * @brief acquire the lock
     */
    void acquire()
    {
        exponential_backoff<16>([this]() { return locked_.exchange(1, std::memory_order_acquire) == 1; });
    }

    /**
     * @brief try to acquire the lock
     * return true if the lock is acquired.
     */
    bool try_acquire()
    {
        int l = locked_.exchange(1, std::memory_order_acquire);
        return l == 0;
    }

    /**
     * @brief release the lock
     */
    void release()
    {
        locked_.store(0, std::memory_order_release);
    }

    /**
     * @brief locked
     */
    const std::atomic<int>& locked() const { return locked_; }

    /**
     * @brief locked
     */
    std::atomic<int>& locked() { return locked_; }

    std::atomic<int> locked_; //!< locked flag
};

/**
 * @brief tile lock, uses the 0/1 lock bit associated with manycore endpoints
 * this lock has no data as the bit is stored in a register
 */
class tile_lock
{
public:
    typedef tile_lock *lock_ptr;

    /**
     * @brief constructor
     */
    tile_lock() {}

    /**
     * @brief acquire the lock
     */
    void acquire() {
        int *lp = reinterpret_cast<int*>(this);
        exponential_backoff<16>([=]() { return bsg_amoswap(lp, 1) == 1; });
    }

    /**
     * @brief try to acquire the lock
     * return true if the lock is acquired
     */
    bool try_acquire() {
        int *lp = reinterpret_cast<int*>(this);
        int l = bsg_amoswap(lp, 1);
        return l == 0;
    }

    /**
     * @brief release the lock
     */
    void release() {
        int *lp = reinterpret_cast<int*>(this);
        bsg_amoswap_rl(lp, 0);
    }

    FIELD(lock_ptr, global_this); //!< global this
};

/**
 * @brief a lock guard, acquires lock on creation and releases on destruction
 */
template <typename Lock>
class lock_guard
{
public:
    /**
     * @brief constructor
     * @param l lock
     */
    lock_guard(Lock& l) : l_(l) {
        l_.acquire();
    }

    /**
     * @brief destructor
     */
    ~lock_guard() {
        l_.release();
    }

    Lock &l_; //!< lock
};

/**
 * @brief a tile lock guard, acquires lock on creation and releases on destruction
 */
template <typename T, typename Lock>
class lockable
{
public:
    template <typename... Args>
    lockable(Args&&... args) : data_(std::forward<Args>(args)...) {}

    FIELD(T, data); //!< data

    Lock& lock() { return lock_; } //!< lock
    const Lock& lock() const { return lock_; } //!< lock
    Lock lock_; //!< lock
};

/**
 * @brief internals for lockable specialization
 */
#define UTIL_LOCKABLE_INTERNAL(data_type, lock_type)    \
    public:                                             \
    template <typename... Args>                         \
    lockable(Args&&... args) : data_(std::forward<Args>(args)...) {} \
    FIELD(data_type, data);                             \
    lock_type& lock() { return lock_; }                 \
    const lock_type& lock() const { return lock_; }     \
    lock_type lock_;

/**
 * @brief make delegate function for lockable
 */
#define UTIL_LOCKABLE_FUNCTION(data_type, lock_type, return_type, method) \
    template <typename ...Args>                                         \
    return_type method(Args&&... args) {                                \
        return_type r;                                                  \
        {                                                               \
            util::lock_guard<lock_type> guard(lock_);                   \
            r =  data_.method(std::forward<Args>(args)...);             \
        }                                                               \
        return r;                                                       \
    }

#define UTIL_LOCKABLE_FUNCTION_CAN_FAIL(data_type, lock_type, return_type, fail_value, method) \
    template <typename ...Args>                                         \
    return_type method(Args&&... args) {                                \
        return_type r = fail_value;                                     \
        if (lock_.try_acquire()) {                                      \
            r = data_.method(std::forward<Args>(args)...);              \
            lock_.release();                                            \
        }                                                               \
        return r;                                                       \
    }
/**
 * @brief make unsafe delegate function for lockable
 */
#define UTIL_LOCKABLE_FUNCTION_UNSAFE(data_type, lock_type, return_type, method) \
    template <typename ...Args>                                         \
    return_type method##_unsafe(Args&&... args) {                       \
        return data_.method(std::forward<Args>(args)...);               \
    }

/**
 * @brief make delegate methods for lockable
 */
#define UTIL_LOCKABLE_METHOD(data_type, lock_type, method)              \
    template <typename ...Args>                                         \
    void method(Args&&... args) {                                       \
        util::lock_guard<lock_type> guard(lock_);                       \
        data_.method(std::forward<Args>(args)...);                      \
    }

/**
 * @brief make delegate methods for lockable
 */
#define UTIL_LOCKABLE_METHOD_UNSAFE(data_type, lock_type, method)       \
    template <typename ...Args>                                         \
    void method##_unsafe(Args&&... args) {                              \
        data_.method(std::forward<Args>(args)...);                      \
    }

/**
 * @brief make delegate methods for lockable
 */
#define UTIL_LOCKABLE_FUNCTION_CONST(data_type, lock_type, return_type, method) \
    template <typename ...Args>                                         \
    return_type method(Args&&... args) const {                          \
        return data_.method(std::forward<Args>(args)...);               \
    }

/**
 * @brief make delegate methods for lockable
 */
#define UTIL_LOCKABLE_METHOD_CONST(data_type, lock_type, method)        \
    template <typename ...Args>                                         \
    void method(Args&&... args) const {                                 \
        data_.method(std::forward<Args>(args)...);                      \
    }
}

#endif
